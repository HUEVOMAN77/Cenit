package com.izzy2lost.psx2;

import android.content.Context;
import android.content.SharedPreferences;
import java.util.ArrayList;
import java.util.Arrays;

/**
 * Sintonizador CPU/VU automatico — Fase 1 del prompt directo (Cenit 0.6.31).
 *
 * QUE CAMBIA RESPECTO A 0.6.27, y por que cada cambio es una exigencia del
 * prompt y no un gusto propio:
 *
 *  1) AUTO_SAFE ES EL MODO POR DEFECTO, y OFF / MANUAL / AUTO_SAFE son modos
 *     DISTINTOS (seccion 1 y 10). Antes era un booleano apagado: una pieza que
 *     existe pero que nadie usa. "Encendido por defecto" NO es "mover ajustes al
 *     arrancar": AUTO_SAFE solo aprende cuando todas las puertas se cumplen
 *     durante varios muestreos seguidos (GATE_STREAK), y el primer tick de una
 *     partida no mueve nada.
 *  2) Puertas leidas del CLASIFICADOR y del SNAPSHOT (CenitLoadSample), nunca de
 *     getters sueltos: la lista completa esta en gates() con el motivo de cada una
 *     escrito, y el motivo se loguea la primera vez que aparece (un "no" sin
 *     razon es indistinguible de un fallo).
 *  3) VENTANAS A/B/A/B ALTERNADAS (seccion 6) en vez de base-then-candidato
 *     secuenciales. Con A y B separadas por un cuarto de minuto, una mejora de B
 *     puede ser sencillamente "el telefono se enfrio"; alternando, el ruido
 *     termico y la deriva de la partida se reparten entre las dos mitades. Cada
 *     ventana tiene su asentamiento y su muestra, y de cada una se apunta mediana,
 *     minimo, P50, P95, speed%, escalon termico y FALLOS DE AUDIO. Los contadores
 *     de audio no existian antes de 0.6.31: pedir "audio estable" sin observador
 *     de audio era pedir una opinion.
 *  4) UMBRALES DE ADOPCION DOCUMENTADOS Y CONFIGURABLES (seccion 7): MTVU +5 %,
 *     Rate +8 %, Skip +10 %, y ADEMAS P95 no peor, minimo de FPS no peor, speed%
 *     no peor, sin cortes de audio, sin crash, sin regresion grafica conocida y
 *     sin subida anormal de temperatura. Cada criterio se evalua aparte y se loguea
 *     con el que fallo, porque "no gagne nada" y "gane pero con tirones" son cosas
 *     distintas que el usuario tiene derecho a leer.
 *  5) VERIFICACION POST-ADOPCION de 45 s (seccion 7, 30-60 s): si la ganancia
 *     desaparece o aparecen cortes de audio, se revierte y el juego pasa a
 *     SUSPENDED. Una adopcion que nobody confirmed es una aposta.
 *  6) DIARIO ANTES DE CADA ESCRITURA (seccion 9) con el valor previo EXACTO, si la
 *     clave EXISTIA, el candidato y el tipo; y lectura de verificacion del valor
 *     EFECTIVO antes de dar nada por adoptado. Al morir el proceso, recoverAtBoot()
 *     restaura el valor previo, BORRA la clave global si no existia (no escribe un
 *     cero que el usuario no puso), elimina solo las claves que Cenit creo, marca
 *     el perfil "recuperado" y SUSPENDE el candidato concreto. Nunca se restaura
 *     el INI entero a ciegas y nunca se toca una clave ajena.
 *  7) LA MEMORIA VIAJA POR IDENTIDAD COMPLETA (seccion 8, TunerProfileStore):
 *     CRC + serial + SoC + backend + driver + version del core + version del
 *     tuner. Un perfil de Snapdragon no se aplica en un Mali, y un veredicto del
 *     metodo viejo (TUNER_VERSION 1) no autoriza nada en el nuevo. Sin identidad
 *     completa no se adopta nada: es "no se donde estoy", no "estoy en un sitio
 *     raro".
 *  8) UN SOLO DUENNO POR INTERVALO (seccion 6): el regidor se congela mientras
 *     este sintonizador mide; el sintonizador no mide cuando hay un paso de
 *     resolucion en el aire, cuando el sistema recorta por calor, cuando el
 *     usuario toca los ajustes (cancelar y revertir) o cuando el motor de
 *     superbloques esta validando (superblockBusy).
 *  9) HUD (seccion 9): TUNER / STATE / PROFILE / GAIN / FPS antes->despues /
 *     FRAME P50-P95 / THERMAL via NativeApp.setCenitTunerStatus. Un estado que no
 *     se ve no se puede auditar, y sin auditoria no es "seguro", es "confiado".
 * 10) CANDIDATOS del prompt, sin mezclar nunca: 0 original | 1 MTVU |
 *     2 Rate -1 + Skip 0 | 3 Rate 0 + Skip 1. "No probar Skip si MTVU/Rate ya
 *     bastaron": si con lo puesto el juego dejo de ir atrasado, el ensayo se
 *     cierra con lo ganado. Un valor que el usuario ya fijo a mano no se ofrece, y
 *     una clave fijada por-juego es intocable (ensayarla seria escribir en una
 *     pared: el por-juego manda sobre el global).
 *
 * Sigue INTACTO de 0.6.27 porque el prompt no lo discute y la medicion lo avala:
 * la plomeria de aplicacion (seters del INI global + ApplySettings; capa
 * por-juego para lo adoptado), la metrica de FPS REALES y nunca el % de velocidad
 * (GetSpeed = fps/objetivo*100 y el objetivo NO se re-escala con cuotas: con una
 * cuota el % miente por diseno), y la regla de que los ensayos NO alimentan la
 * calibracion del clasificador ni heldScale/slowFloorTicks.
 *
 * Los tres modos automaticos se distinguen asi, y queda dicho en Ajustes para que
 * nadie elija a ciegas:
 *   AUTO_SAFE ............ MTVU, Rate -1 y Skip 1 (la escalera completa)
 *   AUTO_MTVU_ONLY ....... solo MTVU; si no gana, se para ahi
 *   AUTO_SAFE_sinRS ...... MTVU y Skip 1, pero Rate NUNCA (Rate cambia cuanto
 *                         emula la consola; Skip solo deja cuadros de dibujo)
 */
final class CpuVUTuner {

    /** Lo que el regidor hace tras este tick. */
    static final int IDLE = 0;      // nada en curso: reglas clasicas normales
    static final int CONTINUE = 1;  // ensayo en curso: congelar subida/bajada de escala
    static final int RETRY = 2;     // el estado cambio este tick: reentrar el evaluate

    // ------------------------------------------------------------------
    // Modos (seccion 10). OFF/MANUAL/AUTO_SAFE son modos distintos y el HUD los
    // muestra distintos: "no hace nada porque esta apagado" no es "no hace nada
    // porque todavia no le dieron las puertas".
    // ------------------------------------------------------------------
    static final int MODE_OFF = 0;
    static final int MODE_MANUAL = 1;
    static final int MODE_AUTO_SAFE = 2;
    static final int MODE_AUTO_MTVU_ONLY = 3;
    static final int MODE_AUTO_NO_RATE_SKIP = 4;
    static final int MODE_COUNT = 5;

    static final int DEFAULT_MODE = MODE_AUTO_SAFE;

    /** Preferencia INT de modo; sustituye al booleano "cpu_tuner" de 0.6.27. */
    static final String PREF_MODE = "cpu_tuner_mode";
    /** Booleana antigua: se lee una sola vez para migrar y se borra al elegir modo. */
    static final String PREF_KEY = "cpu_tuner";

    /**
     * Modo vigente. La migracion es de un solo sentido y respeta la voluntad del
     * usuario: si toco el interruptor viejo, su decision manda (apagado sigue
     * apagado); si nunca lo toco, entra el default nuevo. En cuanto elige aqui se
     * borra la booleana, para que no puedan discrepar dos claves sobre lo mismo —
     * que es el defecto de umbrales duplicados que costo el ciclo 0.6.4.
     */
    static int modeOf(SharedPreferences p) {
        if (p.contains(PREF_MODE)) return clampMode(p.getInt(PREF_MODE, DEFAULT_MODE));
        if (p.contains(PREF_KEY))
            return p.getBoolean(PREF_KEY, false) ? MODE_AUTO_SAFE : MODE_OFF;
        return DEFAULT_MODE;
    }

    static int clampMode(int m) {
        return (m < MODE_OFF || m >= MODE_COUNT) ? DEFAULT_MODE : m;
    }

    static String modeName(int m) {
        switch (m) {
            case MODE_OFF: return "OFF";
            case MODE_MANUAL: return "MANUAL";
            case MODE_AUTO_MTVU_ONLY: return "AUTO_MTVU";
            case MODE_AUTO_NO_RATE_SKIP: return "AUTO_SAFE_sinRS";
            default: return "AUTO_SAFE";
        }
    }

    static void setMode(SharedPreferences p, int mode) {
        mode = clampMode(mode);
        p.edit().putInt(PREF_MODE, mode).remove(PREF_KEY).apply();
        android.util.Log.i("CpuVUTuner", "CpuVUTuner modo: " + modeName(mode)
                + " (cpu_tuner_mode=" + mode + ", booleana vieja retirada)");
    }

    // ------------------------------------------------------------------
    // Diccionario de estados para el HUD. Los numeros son contrato: se publican
    // como int y el C++ los traduce. Todo estado nuevo se agrega AQUI, en el
    // kTunerStates[] de ImGuiOverlays.cpp y en el contrato, en la misma version
    // (la misma regla que CenitTelemetry impone para los estados de carga).
    // ------------------------------------------------------------------
    static final int ST_OFF = 0;
    static final int ST_MANUAL = 1;
    static final int ST_IDLE = 2;
    static final int ST_LEARNING = 3;
    static final int ST_MEASURING_BASELINE = 4;
    static final int ST_MEASURING_CANDIDATE = 5;
    static final int ST_VERIFYING_ADOPTION = 6;
    static final int ST_ADOPTED = 7;
    static final int ST_REJECTED = 8;
    static final int ST_SUSPENDED = 9;
    static final int ST_POST_VERIFY = 10;
    static final int ST_RECOVERED = 11;
    static final int STATE_COUNT = 12;

    static final String[] STATE_NAMES = {
            "OFF", "MANUAL", "IDLE", "LEARNING", "MEASURING_BASELINE",
            "MEASURING_CANDIDATE", "VERIFYING_ADOPTION", "ADOPTED", "REJECTED",
            "SUSPENDED", "POST_VERIFY", "RECOVERED"};

    static String stateName(int s) {
        return (s >= 0 && s < STATE_NAMES.length) ? STATE_NAMES[s] : "STATE_??";
    }

    // ------------------------------------------------------------------
    // Ventanas. 8 muestras de 1 s por ventana con FPS del snapshot; asentamiento
    // de 3 s ANTES de cada ventana (tambien antes de las B: medir el segundo en que
    // se escribe la cuota es medir el transitorio del cambio, no el candidato).
    // Cuatro ventanas por candidato (A1 B1 A2 B2) = 4*(3+8) = 44 s, mas 2 s de
    // aterrizaje por cada una de las tres veces que se cambia el global = 50 s.
    // Techo duro 420 s (7 min) para cubrir los tres candidatos con margen.
    // ------------------------------------------------------------------
    static final int WINDOW_SETTLE = 3;
    static final int WINDOW_MEASURE = 8;
    private static final int MAX_BENCH_TICKS = 420;
    private static final int PAUSE_LIMIT = 60;
    /** Muestras seguidas con todas las puertas antes de aprender (seccion 3). */
    static final int GATE_STREAK = 8;
    private static final int RETRY_WINDOW = 2;
    /** Ticks de espera de que el valor ensayado llegue al emulador. */
    private static final int APPLY_VERIFY_WAIT = 2;
    /** Ventanilla de verificacion post-adopcion (seccion 7: 30-60 s). */
    static final int POST_VERIFY_TICKS = 45;

    // Umbrales de ganancia (seccion 7). Son los unicos numeros del archivo que
    // deciden una adopcion; viven aqui, con nombre, para que se puedan discutir
    // con una medicion delante en vez de dentro de un if.
    static final float GAIN_MTVU_PCT = 5f;
    static final float GAIN_RATE_PCT = 8f;
    static final float GAIN_SKIP_PCT = 10f;
    /** El P95 del candidato no puede pasar de base * esta razon (2 % de ruido). */
    static final float P95_TOLERANCE = 1.02f;
    /** Un minimo de FPS mas de un 12 % por debajo del de la base no es estable. */
    static final float WORST_RATIO_MIN = 0.88f;
    /** El speed% del candidato no puede bajar de la base mas que esto. */
    static final float SPEED_DROP_TOLERANCE_PCT = 1f;
    /** Escalones de PowerManager que se toleran antes de anular una adopcion. */
    static final int THERMAL_STEP_LIMIT = 1;
    /** Sin base medible no hay juicio (un juego a 2 fps no es comparable). */
    static final float BASE_FPS_MIN = 5f;
    /** La mitad del umbral: es lo que tiene que seguir viendose al verificar. */
    static final float POST_VERIFY_MIN_RATIO = 0.5f;
    /** Salto brusco del P50 = cambio de escena: las fps de aqui no son comparables. */
    private static final float SCENE_P50_JUMP = 0.25f;
    /** Evidencia VU-bound: espera EE->VU1 (ms, llamadas) en ventanas consecutivas. */
    private static final float VU_WAIT_MS = 10f;
    private static final int VU_WAIT_CALLS_MIN = 30;
    private static final int VU_WAIT_TICKS_MIN = 2;

    static final String JOURNAL_PREFIX = "cpu_tuner_pending:";

    // Indices de la tripleta {rate, skip, mtvu}, en el orden de KEYS/SHORT.
    private static final int IDX_RATE = 0, IDX_SKIP = 1, IDX_MTVU = 2;
    private static final String SECTION_NAME = "EmuCore/Speedhacks";
    private static final String[] KEYS = {"EECycleRate", "EECycleSkip", "vuThread"};
    private static final String[] SHORT = {"r", "s", "m"};

    // Fases de la maquina. PH_NONE cubre "sin ensayo" (incluido contando puertas).
    private static final int PH_NONE = 0, PH_SETTLE = 1, PH_MEASURE = 2,
            PH_APPLY_WAIT = 3, PH_DECIDE = 4, PH_ADOPT_VERIFY = 5, PH_POST_VERIFY = 6;
    /** Ventanas del candidato: 0=A1(base) 1=B1(cand) 2=A2(base) 3=B2(cand). */
    private static final int W_COUNT = 4;

    private final Context appContext;
    private final SharedPreferences prefs;

    // ---------------- estado del ensayo: solo el hilo del regidor ----------------
    private int mPhase = PH_NONE;
    private int mWindow = 0;
    private int mTicks;
    private int mBenchTicks, mFrozenTicks, mRetries;
    private int mGateStreak;
    private int mWaitHits;
    private int mPublishState = ST_IDLE;

    private int[] mBase;                  // tripleta EFECTIVA al empezar
    private int[] mUser;                  // INI global capturado (rollback exacto)
    private boolean mFixedR, mFixedS, mFixedM;
    private int[][] mQueue;
    private int mCandIdx;
    private int[] mCand;

    // Muestras de la ventana en curso.
    private final float[] mFps = new float[WINDOW_MEASURE];
    private final float[] mP95 = new float[WINDOW_MEASURE];
    private final float[] mSpeed = new float[WINDOW_MEASURE];
    private int mWindowPos;

    // Acumulados por lado: 2 ventanas de base (A1,A2) y 2 de candidato (B1,B2).
    private final float[] mBaseMed = new float[W_COUNT];
    private final float[] mBaseMin = new float[W_COUNT];
    private final float[] mBaseP95 = new float[W_COUNT];
    private final float[] mCandMed = new float[W_COUNT];
    private final float[] mCandMin = new float[W_COUNT];
    private final float[] mCandP95 = new float[W_COUNT];
    private final float[] mSpeedSum = new float[W_COUNT];
    private final int[] mSpeedN = new int[W_COUNT];
    private int mBaseThermalMax = -1, mCandThermalMax = -1;
    private final long[] mBaseAudio = new long[3];
    private final long[] mCandAudio = new long[3];
    private long[] mAudioMark;
    private boolean[] mWindowFilled = new boolean[W_COUNT];

    // Resumen de la ultima decision, para el HUD y para el registro.
    private float mBaseAgg, mCandAgg, mGainPct;
    private float mHudP50 = Float.NaN, mHudP95 = Float.NaN;

    private float mStartApplied = -1f;    // la escala debe permanecer intacta
    private float mLastP50 = Float.NaN;   // detector de cambio de escena
    private long mTexCount = -1L;         // detector de carga de texturas
    private String mJournalUri = "";
    private TunerProfileStore.Identity mIdentity;
    private String mIdentityUri = null;
    private AdaptiveProfile mProfile;
    private boolean mPerGameTouched;

    // Veredicto del ultimo ensayo TERMINADO (onGameStop lo re-anota en el perfil).
    private String mLastVerdict = null;
    private String mLastAdopted = "";
    private float mLastFps = 0f;

    // Post-verificacion.
    private float mPostSum, mPostP95;
    private int mPostN;
    private float mPostNeedPct;

    private String mHoldReason = "";

    CpuVUTuner(Context app, SharedPreferences prefs) {
        this.appContext = app.getApplicationContext();
        this.prefs = prefs;
    }

    /** true = hay ensayo en curso: el governor no debe congelarse por su propio rate. */
    boolean isActive() {
        return mPhase != PH_NONE;
    }

    /** Estado del diccionario, para Ajustes y para el reporte de evidencia. */
    int hudState() { return mPublishState; }

    void startSession() {
        // El diario va primero: puede dejar claves por-juego que aun no nos pertenecen.
        final int recovered = recoverAtBoot(appContext, prefs);
        if (mPhase != PH_NONE) abort("estado sobrante de una sesion anterior");
        mProfile = null;
        mGateStreak = 0;
        mLastVerdict = null;
        mLastAdopted = "";
        mLastFps = 0f;
        mIdentity = null;
        mIdentityUri = null;
        mHoldReason = "";
        // RECOVERED es un estado con productor real: lo marca un diario consumido,
        // no una intuicion. Dura hasta que otra puerta lo pise, y el log de
        // recoverAtBoot ya dijo cual era el candidato suspendido.
        mPublishState = recovered > 0 ? ST_RECOVERED : stateForMode(modeOf(prefs));
        publish();
    }

    private static int stateForMode(int mode) {
        if (mode == MODE_OFF) return ST_OFF;
        if (mode == MODE_MANUAL) return ST_MANUAL;
        return ST_IDLE;
    }

    // ==================================================================
    // El tick del regidor. El governor ya midio y ya classific; aqui se Decide.
    // ==================================================================
    int tick(AdaptiveProfile profile, String uri, float applied, CenitLoadSample sample,
             GameLoadClassifier.Verdict verdict, int thermalStatus, boolean behind,
             boolean atFloor, boolean metricsAlive, boolean turbo, boolean settling,
             boolean thermal, boolean superblockBusy) {
        // Cenit 0.6.32 (Fase 2): la identidad juego+telefono se calcula aqui, en
        // cabecera, sea cual sea el modo — incluso con el sintonizador apagado o
        // en manual. publish() reenvia ese hash al motor de superbloques VU1, que
        // lo mete en la identidad de cada candidato (Machine::IdentityComplete).
        // Sin esto, un usuario con el tuner en OFF tendria candidatos con
        // identidad incompleta para siempre y la maquina jamas promocionaria nada:
        // vender como "no aplica" algo que nunca se dejo medir. identityFor esta
        // memoizado por URI, asi que el coste es una lectura por juego.
        identityFor(uri);

        final int mode = modeOf(prefs);
        if (mode == MODE_OFF || mode == MODE_MANUAL) {
            if (mPhase != PH_NONE) abort("el modo paso a " + modeName(mode));
            mGateStreak = 0;
            mPublishState = stateForMode(mode);
            publish();
            return IDLE;
        }

        observeVUWait(sample);

        if (mPhase == PH_NONE) {
            final int gate = gates(profile, uri, sample, verdict, behind, atFloor,
                    metricsAlive, turbo, settling, thermal, superblockBusy);
            if (gate == ST_LEARNING) {
                mGateStreak++;
                mPublishState = ST_LEARNING;
                if (mGateStreak >= GATE_STREAK) {
                    mGateStreak = 0;
                    begin(profile, uri, applied);
                    publish();
                    return isActive() ? RETRY : IDLE;
                }
            } else {
                mGateStreak = 0;
                mPublishState = gate;
            }
            publish();
            return IDLE;
        }

        // ------------------------- ensayo en curso -------------------------
        if (turbo || settling || thermal || superblockBusy || !metricsAlive) {
            mFrozenTicks++;
            if (mFrozenTicks > PAUSE_LIMIT) {
                abort("congelado demasiado (turbo/termica/sin metrica/superblock)");
                return RETRY;
            }
            mPublishState = ST_LEARNING;
            publish();
            return CONTINUE;
        }
        mFrozenTicks = 0;
        mBenchTicks++;
        if (mBenchTicks > MAX_BENCH_TICKS) {
            abort("techo duro del ensayo");
            return RETRY;
        }
        if (mStartApplied >= 0f && (applied <= 0f || Math.abs(applied - mStartApplied) > 0.001f)) {
            abort("la escala se movio a mitad: las FPS no serian comparables");
            return RETRY;
        }
        if (userMovedSettings()) {
            abort("el usuario cambio los ajustes durante el ensayo");
            return RETRY;
        }

        switch (mPhase) {
            case PH_SETTLE:
                if (--mTicks > 0) { publish(); return CONTINUE; }
                mWindowPos = 0;
                mPhase = PH_MEASURE;
                mTicks = WINDOW_MEASURE;
                publish();
                return CONTINUE;

            case PH_MEASURE: {
                collect(sample, thermalStatus);
                if (mPhase == PH_NONE) return RETRY; // collect aborto
                if (mWindowPos < WINDOW_MEASURE) { publish(); return CONTINUE; }
                if (!windowValid()) {
                    if (++mRetries > RETRY_WINDOW) {
                        abort("ninguna ventana util en " + windowLabel());
                        return RETRY;
                    }
                    mPhase = PH_SETTLE;
                    mTicks = WINDOW_SETTLE;
                    publish();
                    return CONTINUE;
                }
                closeWindow();
                nextWindow();
                publish();
                return isActive() ? CONTINUE : RETRY;
            }

            case PH_APPLY_WAIT: {
                // El apply viaja por un executor async: aqui se ESPERA a que
                // aterrice, no se aborta. Se aborta si al aterrizar la tripleta
                // efectiva NO es la que se pidio (entonces la ventana no seria
                // nuestra) o si el usuario movio algo mientras.
                if (--mTicks > 0) { publish(); return CONTINUE; }
                // Cada ventana tiene su asentamiento, tambien despues de escribir el
                // global: medir al segundo de aplicar una cuota es medir el transitorio
                // del cambio, no el candidato (punto 3 del encabezado y seccion 6).
                if (environmentDrifted()) {
                    abort("el valor aplicado no coincide con el que se pidio "
                            + "(esperada " + java.util.Arrays.toString(isCandidateWindow() ? mCand : mBase)
                            + ", efectiva " + java.util.Arrays.toString(effective()) + ")");
                    return RETRY;
                }
                mWindowPos = 0;
                mPhase = PH_SETTLE;
                mTicks = WINDOW_SETTLE;
                publish();
                return CONTINUE;
            }

            case PH_DECIDE:
                decide();
                publish();
                return isActive() ? CONTINUE : RETRY;

            case PH_ADOPT_VERIFY: {
                if (--mTicks > 0) { publish(); return CONTINUE; }
                if (!valuesInEffect(mCand)) {
                    android.util.Log.i("CpuVUTuner", "CpuVUTuner state: ADOPT_UNDONE — la "
                            + "adopcion nunca se vio en la lectura efectiva; se revierte");
                    deleteWrittenKeys(mJournalUri);
                    removeJournal();
                    applyGlobalsAsync(mUser);
                    mPerGameTouched = false;
                    finishRecord(TunerProfileStore.V_NONE, "", mBaseAgg, 0f);
                    mPublishState = ST_REJECTED;
                    publish();
                    return RETRY;
                }
                removeJournal();
                mPostSum = 0f; mPostN = 0; mPostP95 = 0f;
                mAudioMark = null;
                Arrays.fill(mCandAudio, 0L);
                mPhase = PH_POST_VERIFY;
                mTicks = POST_VERIFY_TICKS;
                mPublishState = ST_POST_VERIFY;
                android.util.Log.i("CpuVUTuner", "CpuVUTuner state: POST_VERIFY — 45 s "
                        + "midiendo con la adopcion puesta (necesario seguir viendo >= "
                        + mPostNeedPct + "%)");
                publish();
                return CONTINUE;
            }

            case PH_POST_VERIFY: {
                collectPost(sample);
                if (--mTicks > 0) { publish(); return CONTINUE; }
                finishPostVerify();
                publish();
                return RETRY;
            }
        }
        publish();
        return CONTINUE;
    }

    /** El usuario movio una clave del tablero: el ensayo ya no es suyo. */
    private boolean userMovedSettings() {
        if (mUser == null) return false;
        final int[] live = liveUserTriple();
        return live[IDX_RATE] != mUser[IDX_RATE] || live[IDX_MTVU] != mUser[IDX_MTVU];
    }

    // ==================================================================
    // Puertas (seccion 5). ST_LEARNING solo si TODAS se cumplen; si no, el estado
    // IDLE/SUSPENDED/REJECTED que toca, con el motivo escrito una sola vez.
    // ==================================================================
    private int gates(AdaptiveProfile profile, String uri, CenitLoadSample sample,
                      GameLoadClassifier.Verdict verdict, boolean behind, boolean atFloor,
                      boolean metricsAlive, boolean turbo, boolean settling, boolean thermal,
                      boolean superblockBusy) {
        if (uri == null || uri.isEmpty()) return hold("sin juego delante");
        if (profile == null || !profile.known())
            return hold("sin memoria por juego: el veredicto no tendria donde vivir");
        if (!behind) return hold("el juego va a tiempo: no hay nada que recuperar");
        if (!atFloor)
            return hold("la resolucion todavia puede bajar: ese es el trabajo del regidor");
        if (turbo) return hold("turbo de cargas enganchado");
        if (settling) return hold("hay un paso de resolucion en el aire");
        if (thermal) return hold("el sistema esta recortando por calor");
        if (superblockBusy) return hold("el motor de superbloques VU1 esta validando");
        if (!metricsAlive || sample == null || !sample.isValid()) return hold("metrica ausente");
        if (!sample.emuMetricValid()) return hold("metrica de emulacion invalida (Regla 3)");
        if (!sample.windowComplete() || !sample.sampleFresh())
            return hold("ventana incompleta o muestra repetida");
        if (verdict == null) return hold("sin veredicto del clasificador");
        // Regla 5: manda la ACCION, no la etiqueta suelta.
        if (verdict.action != GameLoadClassifier.Action.BLOCK_RESOLUTION_DROP)
            return hold("el clasificador no bloqueo el recorte con confianza");
        if (verdict.state != GameLoadClassifier.LoadState.CPU_BOUND
                && verdict.state != GameLoadClassifier.LoadState.VU_BOUND)
            return hold("el cuello no es la emulacion (" + verdict.state + ")");
        if (textureLoadActive()) return hold("cargando texturas o pipeline");
        if (sceneChanged(sample)) return hold("escena cambiada hace poco");

        final TunerProfileStore.Record rec = recordFor(uri);
        if (rec != null) {
            if (TunerProfileStore.V_ADOPTED.equals(rec.verdict))
                return hold("perfil adoptado en vigor (" + rec.adopted + ")");
            if (TunerProfileStore.V_SUSPENDED.equals(rec.verdict) && rec.blocksRetry(System.currentTimeMillis()))
                return ST_SUSPENDED;
            if ((TunerProfileStore.V_NONE.equals(rec.verdict) || TunerProfileStore.V_REJECTED.equals(rec.verdict))
                    && rec.fresh(System.currentTimeMillis()))
                return ST_REJECTED;
        }
        mProfile = profile;
        mJournalUri = uri;
        return ST_LEARNING;
    }

    private int hold(String why) {
        if (!why.equals(mHoldReason)) {
            mHoldReason = why;
            android.util.Log.i("CpuVUTuner", "CpuVUTuner state: IDLE — no ensaya: " + why);
        }
        return ST_IDLE;
    }

    /**
     * Carga de texturas: delta del numero de reemplazos cargados en cache. Si la
     * lectura no esta disponible devuelve false, que es el valor honesto: "no hay
     * senal de carga", no "hay carga".
     */
    private boolean textureLoadActive() {
        final long now = NativeApp.safeGetLoadedTextureCount();
        if (now < 0) { mTexCount = -1L; return false; }
        if (mTexCount < 0) { mTexCount = now; return false; }
        final boolean moving = now != mTexCount;
        mTexCount = now;
        return moving;
    }

    private boolean sceneChanged(CenitLoadSample s) {
        final double p50 = s.frametimeP50Ms();
        if (Double.isNaN(p50) || p50 <= 0) return false;
        if (!(mLastP50 > 0f)) { mLastP50 = (float) p50; return false; }
        final float prev = mLastP50;
        mLastP50 = (float) p50;
        final float delta = Math.abs((float) p50 - prev) / Math.max(0.001f, prev);
        return delta > SCENE_P50_JUMP;
    }

    private TunerProfileStore.Record recordFor(String uri) {
        final TunerProfileStore.Identity id = identityFor(uri);
        if (id == null || id.id.isEmpty()) return null;
        final TunerProfileStore.Record r = new TunerProfileStore(appContext, uri, id).load();
        return r.known() ? r : null;
    }

    private TunerProfileStore.Identity identityFor(String uri) {
        final String u = uri == null ? "" : uri;
        if (mIdentity == null || !u.equals(mIdentityUri)) {
            try { mIdentity = TunerProfileStore.identityOf(u, appContext); }
            catch (Throwable t) { mIdentity = null; }
            mIdentityUri = u;
        }
        return mIdentity;
    }

    // ==================================================================
    // arranque del ensayo
    // ==================================================================
    private void begin(AdaptiveProfile profile, String uri, float applied) {
        final TunerProfileStore.Identity id = identityFor(uri);
        if (!TunerProfileStore.identityComplete(id)) {
            hold("identidad incompleta (CRC/serial/version del core no leibles): "
                    + "no se adopta nada automaticamente");
            return;
        }
        mJournalUri = uri;
        mProfile = profile;
        readBaseline(uri);
        buildQueue();
        if (mQueue.length == 0) {
            android.util.Log.i("CpuVUTuner", "CpuVUTuner state: REJECTED — no hay ningun "
                    + "candidato libre que ofrecer (todo fijado por-juego o ya en el valor "
                    + "que se probaria)");
            finishRecord(TunerProfileStore.V_NONE, "", 0f, 0f);
            mPublishState = ST_REJECTED;
            publish();
            return;
        }
        mCandIdx = -1;
        mBenchTicks = 0;
        mFrozenTicks = 0;
        mStartApplied = applied;
        mHoldReason = "";
        mPerGameTouched = false;
        resetStats();
        NativeApp.safeResetAudioFaultStats();
        startNextCandidate();
    }

    /** Tripletas: efectiva (lo que corre), fijada por-juego (intocable) y global. */
    private void readBaseline(String uri) {
        final int gR = NativeApp.safeGetGameSettingInt(uri, SECTION_NAME, KEYS[0], -99);
        final int gS = NativeApp.safeGetGameSettingInt(uri, SECTION_NAME, KEYS[1], -99);
        final int gM = NativeApp.safeGetGameSettingInt(uri, SECTION_NAME, KEYS[2], -99);
        mFixedR = gR != -99;
        mFixedS = gS != -99;
        mFixedM = gM != -99;
        final int[] e = effective();
        mBase = new int[]{
                mFixedR ? gR : e[IDX_RATE],
                mFixedS ? gS : e[IDX_SKIP],
                mFixedM ? gM : e[IDX_MTVU]};
        // El CycleSkip global no tiene preferencia Java (vive solo en el INI): se
        // lee del INI para poder restaurarlo EXACTO.
        mUser = new int[]{
                prefs.getInt("ee_cycle_rate", 0),
                NativeApp.safeGetGlobalSettingInt(SECTION_NAME, KEYS[1], 0),
                prefs.getBoolean("mtvu", NativeApp.defaultMTVU()) ? 1 : 0};
    }

    private static int clampRate(int v) { return Math.max(-3, Math.min(3, v)); }
    private static int clampSkip(int v) { return v < 0 ? 0 : Math.min(3, v); }
    private static int clampMtvu(int v) { return v < 0 ? 0 : (v == 0 ? 0 : 1); }

    /**
     * Tripleta efectiva saneada: los wrappers "safe" devuelven 0 (Rate) o -1
     * (Skip/MTVU) cuando la lectura cae. -1 no es valor licito de Skip ni MTVU, asi
     * que cae a 0 — el valor menos intrusivo y el unico honesto ante dato ausente.
     */
    private static int[] effective() {
        return new int[]{
                clampRate(NativeApp.safeGetEffectiveEECycleRate()),
                clampSkip(NativeApp.safeGetEffectiveEECycleSkip()),
                clampMtvu(NativeApp.safeGetEffectiveMTVU())};
    }

    private boolean valuesInEffect(int[] want) {
        if (want == null) return false;
        final int[] e = effective();
        for (int i = 0; i < 3; i++) {
            if (want[i] == mBase[i]) continue;
            if (e[i] != want[i]) return false;
        }
        return true;
    }

    /**
     * ¿La tripleta EFECTIVA es la que se supone? En las ventanas A es mBase (el
     * global restaurado, mas las claves fijadas por-juego que mandan sobre el) y en
     * las B es mCand. Cualquier otro valor significa que alguien —un spinner del
     * usuario, un reload tardio, el propio core— movio la emulacion bajo nuestros
     * pies: las FPS que se graban no son de ESTO. Se compara TODO (no solo las
     * claves que cambian), porque en la ventana A un valor que se mueva sin que
     * nadie lo ordeno invalida la base entera.
     */
    private boolean environmentDrifted() {
        final int[] expected = isCandidateWindow() ? mCand : mBase;
        if (expected == null) return false;
        final int[] e = effective();
        return e[IDX_RATE] != expected[IDX_RATE] || e[IDX_SKIP] != expected[IDX_SKIP]
                || e[IDX_MTVU] != expected[IDX_MTVU];
    }

    /**
     * Cola del prompt, sin mezclar nunca, filtrada por el modo y por lo que el
     * usuario o el juego ya fijaron. El orden es el del enunciado: 1 MTVU,
     * 2 Rate -1 + Skip 0, 3 Rate 0 + Skip 1.
     */
    private void buildQueue() {
        final int mode = modeOf(prefs);
        final boolean allowRate = mode == MODE_AUTO_SAFE;
        final boolean allowSkip = mode == MODE_AUTO_SAFE || mode == MODE_AUTO_NO_RATE_SKIP;
        final ArrayList<int[]> q = new ArrayList<>();
        // 1) MTVU invertido: cero coste visual. Hacia OFF solo con espera EE->VU1
        //    sostenida (eso es VU_BOUND: el EE parado esperando al VU1).
        if (!mFixedM && NativeApp.defaultMTVU()) {
            final int target = mBase[IDX_MTVU] == 0 ? 1 : 0;
            if (target != mBase[IDX_MTVU] && (target == 1 || mWaitHits >= VU_WAIT_TICKS_MIN)) {
                final int[] c = mBase.clone();
                c[IDX_MTVU] = target;
                q.add(c);
            }
        }
        // 2) Rate -1 con Skip 0 — un paso por debajo del normal, jamais turbo.
        if (allowRate && !mFixedR && !mFixedS && mBase[IDX_RATE] == 0 && mBase[IDX_SKIP] == 0) {
            final int[] c = mBase.clone();
            c[IDX_RATE] = -1;
            q.add(c);
        }
        // 3) Skip 1 con Rate 0 — ultimo recurso. Que no se pruebe "si MTVU o Rate ya
        //    bastaron" no se filtra aqui: se cumple solo porque al ganar un candidato
        //    la sesion se cierra con adoptCurrent() y la cola nunca avanza. O sea que
        //    Skip solo se ensaya si los anteriores NO ganaron, que es lo que pide el
        //    prompt, y la razon esta en el flujo, no en un if.
        if (allowSkip && !mFixedS && mBase[IDX_SKIP] == 0 && mBase[IDX_RATE] == 0) {
            final int[] c = mBase.clone();
            c[IDX_SKIP] = 1;
            q.add(c);
        }
        mQueue = q.toArray(new int[0][]);
        android.util.Log.i("CpuVUTuner", "CpuVUTuner state: LEARNING — cola " + mQueue.length
                + " candidato(s) modo " + modeName(mode)
                + (mWaitHits >= VU_WAIT_TICKS_MIN ? " (evidencia VU-bound)" : " (CPU-bound)")
                + " [fijados r=" + mFixedR + " s=" + mFixedS + " m=" + mFixedM + "]"
                + " base=" + mBase[IDX_RATE] + "," + mBase[IDX_SKIP] + "," + mBase[IDX_MTVU]
                + " global=" + mUser[IDX_RATE] + "," + mUser[IDX_SKIP] + "," + mUser[IDX_MTVU]);
    }

    // ==================================================================
    // ventanas A/B/A/B
    // ==================================================================

    private boolean isCandidateWindow() { return mWindow == 1 || mWindow == 3; }

    private void resetStats() {
        for (int i = 0; i < W_COUNT; i++) { mBaseMed[i] = 0f; mBaseMin[i] = 0f;
            mBaseP95[i] = 0f; mCandMed[i] = 0f; mCandMin[i] = 0f; mCandP95[i] = 0f;
            mSpeedSum[i] = 0f; mSpeedN[i] = 0; }
        mWindowFilled = new boolean[W_COUNT];
        mBaseThermalMax = -1; mCandThermalMax = -1;
        Arrays.fill(mBaseAudio, 0L); Arrays.fill(mCandAudio, 0L);
        mAudioMark = null;
        mBaseAgg = 0f; mCandAgg = 0f; mGainPct = 0f;
    }

    /** Candidato siguiente, o cierre sin adopcion. "Skip solo si nada antes basto". */
    private void startNextCandidate() {
        mCandIdx++;
        if (mCandIdx >= mQueue.length) {
            applyGlobalsAsync(mUser);
            finishWithoutAdoption();
            return;
        }
        mCand = mQueue[mCandIdx];
        mRetries = 0;
        resetStats();
        mWindow = 0;
        mPhase = PH_SETTLE;
        mTicks = WINDOW_SETTLE;
        mPublishState = ST_MEASURING_BASELINE;
        // El global ya esta en mUser (cada ensayo no ganador revierte), asi que la
        // ventana A es la linea base del usuario con el candidato NO aplicado.
        writeJournal(false);
        android.util.Log.i("CpuVUTuner", "CpuVUTuner state: MEASURING_BASELINE — A/B/A/B "
                + "para el candidato " + verdictFor(mCand) + " (" + (WINDOW_SETTLE + 1) + "+"
                + WINDOW_MEASURE + " s por ventana, " + W_COUNT + " ventanas)");
    }

    private void nextWindow() {
        mWindow++;
        mRetries = 0;
        if (mWindow >= W_COUNT) {
            mPhase = PH_DECIDE;
            mPublishState = ST_MEASURING_CANDIDATE;
            return;
        }
        if (isCandidateWindow()) {
            // Antes de escribir el global, el diario; y el diario ANTES de aplicar.
            applyGlobalsAsync(mCand);
            mPhase = PH_APPLY_WAIT;
            mTicks = APPLY_VERIFY_WAIT;
            mPublishState = ST_MEASURING_CANDIDATE;
            android.util.Log.i("CpuVUTuner", "CpuVUTuner state: MEASURING_CANDIDATE — "
                    + verdictFor(mCand) + " aplicado al global, esperando que aterrice");
        } else {
            applyGlobalsAsync(mUser);
            mPhase = PH_APPLY_WAIT;
            mTicks = APPLY_VERIFY_WAIT;
            mPublishState = ST_MEASURING_BASELINE;
        }
    }

    private String windowLabel() {
        switch (mWindow) {
            case 0: return "A1";
            case 1: return "B1";
            case 2: return "A2";
            case 3: return "B2";
            default: return "A?";
        }
    }

    private void collect(CenitLoadSample s, int thermalStatus) {
        if (mPhase != PH_MEASURE || mWindowPos >= WINDOW_MEASURE) return;
        if (environmentDrifted()) {
            abort("las cuotas se movieron solas a mitad de ventana " + windowLabel());
            return;
        }
        mFps[mWindowPos] = (float) s.fps();
        final double p95 = s.frametimeP95Ms();
        mP95[mWindowPos] = Double.isNaN(p95) || p95 <= 0 ? 0f : (float) p95;
        mSpeed[mWindowPos] = (float) s.speedPct();
        if (thermalStatus >= 0 && thermalStatus > mWindowThermal) mWindowThermal = thermalStatus;
        mWindowPos++;
    }

    /**
     * Ventana util = las WINDOW_MEASURE muestras tomadas y TODAS con FPS leida. Un 0
     * significa "el core no tuvo presents en esa ventana de 0,5 s" (pantalla de carga,
     * hitch largo, pausa), y eso no es un dato de rendimiento: mezclarlo con los buenos
     * bajaria la mediana del lado que sea y podria hacer GANAR un candidato por haber
     * tenido suerte con las cargas. Es la Regla 3 aplicada a la ventana: sin senal no es
     * "0 fps", es "sin muestra", y sin muestra la ventana se repite.
     */
    private boolean windowValid() {
        if (mWindowPos < WINDOW_MEASURE) return false;
        for (int i = 0; i < WINDOW_MEASURE; i++) {
            final float f = mFps[i];
            if (f <= 0f || Float.isNaN(f)) return false;
        }
        return true;
    }

    private void closeWindow() {
        // Solo las muestras con FPS leida: la mediana aguanta huecos, pero un MINIMO
        // calculado sobre un array con ceros diria "el candidato tuvo tirones" cuando
        // lo que tuvo fue una ventana del core sin presents. Lo mismo aplica al P50:
        // mediana y minimo salen del mismo conjunto filtrado, no del array crudo.
        final float[] fps = new float[WINDOW_MEASURE];
        int n = 0;
        for (int i = 0; i < WINDOW_MEASURE; i++) if (mFps[i] > 0f) fps[n++] = mFps[i];
        java.util.Arrays.sort(fps, 0, n);
        final float med = fps[n / 2];
        final float mn = fps[0];
        float p95max = 0f, spSum = 0f;
        int spN = 0;
        for (float f : mP95) if (f > p95max) p95max = f;
        for (float f : mSpeed) if (f > 0f) { spSum += f; spN++; }
        final float sp = spN > 0 ? spSum / spN : 0f;
        final int w = Math.min(mWindow, W_COUNT - 1);
        final boolean cand = isCandidateWindow();
        if (cand) { mCandMed[w] = med; mCandMin[w] = mn; mCandP95[w] = p95max; }
        else { mBaseMed[w] = med; mBaseMin[w] = mn; mBaseP95[w] = p95max; }
        mSpeedSum[w] += sp;
        mSpeedN[w]++;
        mWindowFilled[w] = true;
        if (cand) { if (mWindowThermal > mCandThermalMax) mCandThermalMax = mWindowThermal; }
        else { if (mWindowThermal > mBaseThermalMax) mBaseThermalMax = mWindowThermal; }
        mWindowThermal = -1;
        collectAudio(cand);
        mHudP50 = med; mHudP95 = p95max;
        android.util.Log.i("CpuVUTuner", "CpuVUTuner ventana " + windowLabel() + " ("
                + (cand ? "candidato " + verdictFor(mCand) : "base") + "): mediana " + med
                + " min " + mn + " P95 " + (p95max > 0f ? p95max : "s/anillo")
                + " speed " + sp + "% audio " + Arrays.toString(cand ? mCandAudio : mBaseAudio));
    }

    /** Termico maximo visto durante la ventana en curso (escalones PowerManager). */
    private int mWindowThermal = -1;

    /**
     * Audio: SOLO deltas entre cierres de ventana. Sin marca previa no se suma nada
     * (la primera lectura es el punto de referencia, no una falta).
     */
    private void collectAudio(boolean candidate) {
        final long[] now = NativeApp.safeGetAudioFaultStats();
        if (now == null || now.length < 3) return;
        if (mAudioMark == null) { mAudioMark = now.clone(); return; }
        final long[] into = candidate ? mCandAudio : mBaseAudio;
        for (int i = 0; i < 3; i++) {
            long d = now[i] - mAudioMark[i];
            if (d < 0) d = now[i]; // alguien reseteo los contadores: usar el absoluto
            into[i] += d;
        }
        mAudioMark = now.clone();
    }

    // ==================================================================
    // decision (seccion 7: todos los criterios, por separado y por escrito)
    // ==================================================================
    private void decide() {
        final float baseMed = aggregate(mBaseMed, true);
        final float candMed = aggregate(mCandMed, false);
        final float baseMin = worstMin(mBaseMin, true);
        final float candMin = worstMin(mCandMin, false);
        final float baseP95 = worstP95(mBaseP95, true);
        final float candP95 = worstP95(mCandP95, false);
        final float baseSpd = avgSpeed(true);
        final float candSpd = avgSpeed(false);
        mBaseAgg = baseMed; mCandAgg = candMed;
        if (baseMed < BASE_FPS_MIN) {
            abort("base por debajo de " + BASE_FPS_MIN + " fps: no hay juicio fiable");
            return;
        }
        final double gain = (candMed - baseMed) / Math.max(0.001f, baseMed) * 100.0;
        mGainPct = (float) gain;
        final int kind = candidateKind(mCand, mBase);
        final float need = kind == KIND_MTVU ? GAIN_MTVU_PCT
                : kind == KIND_RATE ? GAIN_RATE_PCT : GAIN_SKIP_PCT;
        mPostNeedPct = need;
        final long baseFaults = mBaseAudio[0] + mBaseAudio[1] + mBaseAudio[2];
        final long candFaults = mCandAudio[0] + mCandAudio[1] + mCandAudio[2];
        final boolean complete = mWindowFilled[0] && mWindowFilled[1]
                && mWindowFilled[2] && mWindowFilled[3];
        final String why = !complete ? "faltan ventanas A/B"
                : gain < need ? "ganancia " + Math.round(gain) + "% < " + need + "%"
                : (candP95 > 0f && baseP95 > 0f && candP95 > baseP95 * P95_TOLERANCE)
                    ? "P95 " + candP95 + " > " + baseP95 + "*" + P95_TOLERANCE
                : (baseMin > 0f && candMin < baseMin * WORST_RATIO_MIN)
                    ? "min FPS " + candMin + " < " + baseMin + "*" + WORST_RATIO_MIN
                : (baseSpd > 0f && candSpd > 0f
                    && candSpd < baseSpd - SPEED_DROP_TOLERANCE_PCT)
                    ? "speed% " + candSpd + " < " + baseSpd
                : candFaults > baseFaults
                    ? "cortes de audio " + candFaults + " > " + baseFaults
                : (mCandThermalMax >= 0 && mBaseThermalMax >= 0
                    && mCandThermalMax > mBaseThermalMax + THERMAL_STEP_LIMIT)
                    ? "termico " + mBaseThermalMax + "->" + mCandThermalMax
                : null;
        android.util.Log.i("CpuVUTuner", "CpuVUTuner state: MEASURING_CANDIDATE — resultado "
                + verdictFor(mCand) + ": base mediana " + baseMed + " min " + baseMin
                + " P95 " + baseP95 + " speed " + baseSpd + " | candidato mediana " + candMed
                + " min " + candMin + " P95 " + candP95 + " speed " + candSpd
                + " | ganancia " + Math.round(gain) + "% (umbral " + need + "%)"
                + (why == null ? " — TODOS los criterios pass" : " — NO SE ADOPTA: " + why));
        if (why == null) { adoptCurrent(); return; }
        // Sin ganador: el global vuelve a manos del usuario antes del siguiente.
        applyGlobalsAsync(mUser);
        startNextCandidate();
    }

    /** Mediana de las dos ventanas del lado (el centro no es un extremo: se promedia). */
    private float aggregate(float[] perWindow, boolean base) {
        final float a = perWindow[base ? 0 : 1], b = perWindow[base ? 2 : 3];
        if (a <= 0f) return b;
        if (b <= 0f) return a;
        return (a + b) / 2f;
    }

    /**
     * El PEOR de los dos lados, con la direccion correcta para cada cifra: un
     * minimo de FPS malo es el mas BAJO (los tirones no se promedian con el resto
     * de la ventana), un P95 malo es el mas ALTO. Mezclarlos — aplicar max() a los
     * dos — habria hecho que un candidato con un arranque malisimo pasara por
     * bueno porque su otra ventana tapo el numero.
     */
    private float worstMin(float[] perWindow, boolean base) {
        final float a = perWindow[base ? 0 : 1], b = perWindow[base ? 2 : 3];
        if (a <= 0f) return b;
        if (b <= 0f) return a;
        return Math.min(a, b);
    }

    private float worstP95(float[] perWindow, boolean base) {
        final float a = perWindow[base ? 0 : 1], b = perWindow[base ? 2 : 3];
        if (a <= 0f) return b;
        if (b <= 0f) return a;
        return Math.max(a, b);
    }

    private float avgSpeed(boolean base) {
        final int i0 = base ? 0 : 1, i1 = base ? 2 : 3;
        float s = mSpeedSum[i0] + mSpeedSum[i1];
        int n = mSpeedN[i0] + mSpeedN[i1];
        return n > 0 ? s / n : 0f;
    }

    private static final int KIND_MTVU = 0, KIND_RATE = 1, KIND_SKIP = 2;

    /** Que pata del candidato difiere de la base: es la que fija el umbral. */
    private static int candidateKind(int[] c, int[] b) {
        if (c == null || b == null) return KIND_MTVU;
        if (c[IDX_MTVU] != b[IDX_MTVU]) return KIND_MTVU;
        if (c[IDX_RATE] != b[IDX_RATE]) return KIND_RATE;
        return KIND_SKIP;
    }

    // ==================================================================
    // adopcion + verificacion post-adopcion (secciones 7 y 9)
    // ==================================================================
    private void adoptCurrent() {
        mPerGameTouched = true;
        writeJournal(true);
        applyPerGameChanged(true);
        applyGlobalsAsync(mUser);
        mPhase = PH_ADOPT_VERIFY;
        mTicks = APPLY_VERIFY_WAIT + WINDOW_SETTLE;
        mPublishState = ST_VERIFYING_ADOPTION;
        android.util.Log.i("CpuVUTuner", "CpuVUTuner state: VERIFYING_ADOPTION — "
                + verdictFor(mCand) + " escrito en la capa por-juego (ganancia "
                + Math.round(mGainPct) + "%, " + mBaseAgg + "->" + mCandAgg + " fps); "
                + "verificando lectura efectiva");
    }

    private void collectPost(CenitLoadSample s) {
        final float fps = (float) s.fps();
        if (fps > 0f) { mPostSum += fps; mPostN++; }
        final double p95 = s.frametimeP95Ms();
        if (!Double.isNaN(p95) && p95 > mPostP95) mPostP95 = (float) p95;
        mHudP95 = p95 > 0 ? (float) p95 : mHudP95;
        // El audio de la post-verificacion se mide, no se asume: finishPostVerify
        // exige "cortes == 0" y una promesa sin contador seria justo el criterio
        // inventado que el prompt prohibe. La marca se reinicio al entrar aqui, asi
        // que lo que se suma son SOLO los fallos con la adopcion puesta.
        collectAudio(true);
    }

    private void finishPostVerify() {
        final float mean = mPostN > 0 ? mPostSum / mPostN : 0f;
        final long faults = mCandAudio[0] + mCandAudio[1] + mCandAudio[2];
        final float still = mBaseAgg > 0f ? (mean - mBaseAgg) / mBaseAgg * 100f : 0f;
        final float need = mPostNeedPct * POST_VERIFY_MIN_RATIO;
        final boolean holds = mPostN >= POST_VERIFY_TICKS / 2 && still >= need && faults == 0;
        android.util.Log.i("CpuVUTuner", "CpuVUTuner state: POST_VERIFY — adoptado "
                + verdictFor(mCand) + ": " + mean + " fps (base " + mBaseAgg + ", ganancia "
                + Math.round(still) + "% sobre " + mGainPct + "% medida, necesario >= "
                + need + "%), P95 " + mPostP95 + ", fallos de audio " + faults
                + " -> " + (holds ? "CONFIRMADO" : "REVOCADO"));
        if (holds) {
            mPublishState = ST_ADOPTED;
            finishRecord(TunerProfileStore.V_ADOPTED, adoptedMask(), mean, mGainPct);
            return;
        }
        deleteWrittenKeys(mJournalUri);
        applyGlobalsAsync(mUser);
        mPerGameTouched = false;
        mPublishState = ST_SUSPENDED;
        finishRecord(TunerProfileStore.V_SUSPENDED, "", mean, still);
    }

    private void applyPerGameChanged(boolean write) {
        final String uri = mJournalUri;
        if (uri == null || uri.isEmpty() || mCand == null) return;
        for (int i = 0; i < 3; i++) {
            if (mCand[i] == mBase[i]) continue;
            if (write) NativeApp.safeSetGameSettingInt(uri, SECTION_NAME, KEYS[i], mCand[i]);
            else NativeApp.safeDeleteGameSettingKey(uri, SECTION_NAME, KEYS[i]);
        }
    }

    /** Borra del INI del juego SOLO lo que ESTE ensayo escribio (si lo escribio). */
    private void deleteWrittenKeys(String uri) {
        if (!mPerGameTouched || uri == null || uri.isEmpty() || mCand == null) return;
        for (int i = 0; i < 3; i++)
            if (mCand[i] != mBase[i])
                NativeApp.safeDeleteGameSettingKey(uri, SECTION_NAME, KEYS[i]);
        mPerGameTouched = false;
    }

    /** "r=-1,s=1,m=1" solo con las claves ganadas: la receta de undoAdoption. */
    private String adoptedMask() {
        final StringBuilder sb = new StringBuilder();
        for (int i = 0; i < 3; i++) {
            if (mCand != null && mBase != null && mCand[i] != mBase[i]) {
                if (sb.length() > 0) sb.append(',');
                sb.append(SHORT[i]).append('=').append(mCand[i]);
            }
        }
        return sb.toString();
    }

    private void finishWithoutAdoption() {
        removeJournal();
        mPublishState = ST_REJECTED;
        finishRecord(TunerProfileStore.V_REJECTED, "", mBaseAgg, mGainPct);
    }

    /**
     * Persiste el veredicto en las DOS memorias: el registro de identidad (lo que
     * autoriza o impide futuros ensayos) y el perfil por-juego del regidor (lo que
     * la UI lee). Hacerlo a medias dejaria una adopcion sin dueno.
     */
    private void finishRecord(String verdict, String adopted, float fps, float gainPct) {
        mLastVerdict = verdict;
        mLastAdopted = adopted == null ? "" : adopted;
        mLastFps = fps;
        final TunerProfileStore.Identity id = identityFor(mJournalUri);
        if (id != null && !id.id.isEmpty()) {
            final TunerProfileStore st = new TunerProfileStore(appContext, mJournalUri, id);
            final TunerProfileStore.Record r = st.load();
            r.id = id.id;
            r.uri = mJournalUri;
            r.verdict = verdict;
            r.adopted = adopted;
            r.at = System.currentTimeMillis();
            if (TunerProfileStore.V_SUSPENDED.equals(verdict)) r.suspensions++;
            r.lastFpsBase = Math.round(mBaseAgg * 1000f);
            r.lastFpsAdopted = Math.round(fps * 1000f);
            r.lastGainPct = Math.round(gainPct * 100f);
            r.detail = verdictFor(mCand);
            st.save(r);
            st.markActive();
        }
        if (mProfile != null && mProfile.known()) {
            mProfile.tunerVerdict = verdict;
            mProfile.tunerAdoptado = adopted;
            mProfile.tunerAplicado = !adopted.isEmpty();
            mProfile.tunerAt = System.currentTimeMillis();
            mProfile.tunerNow = fps;
            mProfile.save();
        }
        android.util.Log.i("CpuVUTuner", "CpuVUTuner state: " + verdict + " — "
                + (adopted.isEmpty() ? "nada adoptado" : "adoptado " + adopted)
                + " fps " + fps + " (base " + mBaseAgg + ", ganancia "
                + Math.round(gainPct) + "%) en perfil " + (id == null ? "?" : id.id));
        mPhase = PH_NONE;
        mCand = null;
        mQueue = null;
    }

    /** Cierre de emergencia con reversion total y SIN veredicto (reintentable). */
    private void abort(String why) {
        if (mPhase == PH_NONE) return;
        android.util.Log.i("CpuVUTuner", "CpuVUTuner state: ABORT — " + why);
        deleteWrittenKeys(mJournalUri);
        // Se revierte nuestra prueba, pero contra la voluntad del usuario ACTUAL:
        // liveUserTriple() coincide con mUser salvo que el usuario haya tocado Rate o
        // MTVU, caso en el que el abort llega precisamente de eso y re-escribir la
        // fotografia vieja habria borrado su eleccion recien hecha.
        if (mUser != null) applyGlobalsAsync(liveUserTriple());

        removeJournal();
        mPhase = PH_NONE;
        mCand = null;
        mQueue = null;
        mLastVerdict = null;
        mLastAdopted = "";
        mLastFps = 0f;
        mPublishState = ST_IDLE;
        publish();
    }

    // ==================================================================
    // diario (seccion 9) + recuperacion
    // ==================================================================

    /**
     * "cpu_tuner_pending:&lt;uri&gt;" = "v2|&lt;r&gt;,&lt;s&gt;,&lt;m&gt;|&lt;ex&gt;|&lt;mask&gt;|&lt;idx&gt;"
     *  - r,s,m: globales a los que hay que volver (valor previo EXACTO).
     *  - ex: "1:1:1" — si la clave global EXISTIA. 0 = hay que BORRARLA al revertir,
     *    no escribir un cero que el usuario no puso.
     *  - mask: claves por-juego que se llegaron a escribir ("" = ningun candidato).
     *  - idx: candidato en vuelo, para suspender ESE y no el juego entero.
     * Se escribe ANTES de cada aplicacion, y se consume al confirmar la adopcion o al
     * cerrar. Se acepta el formato v1 ("r,s,m[;mask]") de 0.6.27: la existencia no se
     * conocia entonces, asi que se restauran los valores leidos y se dice en el log.
     */
    private void writeJournal(boolean withAdoption) {
        if (mJournalUri == null || mJournalUri.isEmpty() || mUser == null) return;
        final String v = "v2|" + mUser[IDX_RATE] + ',' + mUser[IDX_SKIP] + ','
                + mUser[IDX_MTVU] + '|' + existsFlags() + '|'
                + (withAdoption ? adoptedMask() : "") + '|' + mCandIdx;
        prefs.edit().putString(JOURNAL_PREFIX + mJournalUri, v).apply();
    }

    private String existsFlags() {
        final StringBuilder sb = new StringBuilder();
        for (int i = 0; i < 3; i++) {
            if (sb.length() > 0) sb.append(':');
            sb.append(NativeApp.safeGetGlobalSettingInt(SECTION_NAME, KEYS[i], -999) == -999 ? 0 : 1);
        }
        return sb.toString();
    }

    private void removeJournal() {
        if (mJournalUri != null && !mJournalUri.isEmpty()
                && prefs.getString(JOURNAL_PREFIX + mJournalUri, null) != null) {
            prefs.edit().remove(JOURNAL_PREFIX + mJournalUri).apply();
        }
    }

    /**
     * Aplica TODOS los diarios pendientes, al arrancar la app y en cada arranque de
     * juego. Un cierre brusco pudo dejar el INI global con un ensayo puesto —el
     * CycleSkip global NO tiene preferencia que lo re-crie, asi que sin esto se
     * quedaria -1 para siempre— y pudo dejar claves por-juego escritas a medias.
     * Restaurar es exacto porque la receta esta en el diario, y el candidato que se
     * estaba ensayando queda SUSPENDIDO: un corte a mitad es evidencia negativa de
     * ESE candidato, no del juego entero.
     *
     * @return cuantos diarios se consumieron (0 = no habia nada que recuperar). Lo
     *     usa startSession() para pintar RECOVERED en el HUD: un estado del diccionario
     *     necesita un productor real, no una intuicion.
     */
    static int recoverAtBoot(Context app, SharedPreferences prefs) {
        final java.util.List<String> keys = new ArrayList<>();
        for (String k : prefs.getAll().keySet())
            if (k != null && k.startsWith(JOURNAL_PREFIX)) keys.add(k);
        int consumed = 0;
        for (String k : keys) {
            final String target = k.substring(JOURNAL_PREFIX.length());
            final String raw = prefs.getString(k, null);
            if (raw == null) { prefs.edit().remove(k).apply(); continue; }
            String head = raw, diff = "", ex = "";
            int candIdx = -1;
            if (raw.startsWith("v2|")) {
                final String[] p = raw.split("\\|");
                if (p.length >= 5) {
                    head = p[1]; ex = p[2]; diff = p[3];
                    try { candIdx = Integer.parseInt(p[4].trim()); } catch (NumberFormatException ignored) {}
                }
            } else {
                final int semi = raw.indexOf(';');
                if (semi < 0) head = raw;
                else { head = raw.substring(0, semi); diff = raw.substring(semi + 1); }
            }
            final String[] parts = head.split(",");
            if (parts.length != 3) { prefs.edit().remove(k).apply(); continue; }
            try {
                final int uR = Integer.parseInt(parts[0].trim());
                final int uS = Integer.parseInt(parts[1].trim());
                final int uM = Integer.parseInt(parts[2].trim());
                android.util.Log.i("CpuVUTuner", "CpuVUTuner state: RECOVER — ensayo cortado ("
                        + raw + ") en " + (target.isEmpty() ? "?" : target)
                        + "; candidato " + candIdx + " se suspende");
                if (!target.isEmpty()) {
                    for (int i = 0; i < 3; i++)
                        if (diff.contains(SHORT[i]))
                            NativeApp.safeDeleteGameSettingKey(target, SECTION_NAME, KEYS[i]);
                    suspendCrashedCandidate(app, target, candIdx);
                }
                // Con VM parada estos setters solo re-escriben el INI base, que es
                // exactamente el estado que el diario promete restaurar.
                restoreGlobal(0, uR, ex);
                restoreGlobal(1, uS, ex);
                restoreMtvu(uM, ex);
            } catch (Throwable t) {
                android.util.Log.e("CpuVUTuner", "entrada de diario ilegible: " + raw, t);
            }
            prefs.edit().remove(k).apply();
            consumed++;
        }
        return consumed;
    }

    /** Existia -> escribir el valor; no existia -> BORRAR la clave (no escribir 0). */
    private static void restoreGlobal(int i, int value, String ex) {
        final boolean existed = ex.isEmpty() || i >= ex.length() || ex.charAt(i * 2) == '1';
        if (!existed) { NativeApp.safeDeleteGlobalSettingKey(SECTION_NAME, KEYS[i]); return; }
        if (i == IDX_RATE) NativeApp.speedhackEecyclerate(value);
        else NativeApp.speedhackEecycleskip(value);
    }

    private static void restoreMtvu(int value, String ex) {
        final boolean existed = ex.isEmpty() || ex.length() < 5 || ex.charAt(4) == '1';
        if (!existed) { NativeApp.safeDeleteGlobalSettingKey(SECTION_NAME, KEYS[IDX_MTVU]); return; }
        NativeApp.setMTVU(value == 1);
    }

    /** Marca el perfil "recuperado" y suspende el candidato concreto del corte. */
    private static void suspendCrashedCandidate(Context app, String uri, int candIdx) {
        final AdaptiveProfile p = new AdaptiveProfile(app, uri);
        if (p.known()) {
            p.tunerAplicado = false;
            p.tunerAdoptado = "";
            p.tunerVerdict = TunerProfileStore.V_RECOVERED;
            p.tunerAt = System.currentTimeMillis();
            p.save();
        }
        final TunerProfileStore.Identity id;
        try { id = TunerProfileStore.identityOf(uri, app); } catch (Throwable t) { return; }
        if (id == null || id.id.isEmpty()) return;
        final TunerProfileStore st = new TunerProfileStore(app, uri, id);
        final TunerProfileStore.Record r = st.load();
        r.id = id.id;
        r.uri = uri;
        r.verdict = TunerProfileStore.V_SUSPENDED;
        r.at = System.currentTimeMillis();
        r.suspensions = r.suspensions + 1;
        r.detail = "candidato " + candIdx + " cortado de golpe (proceso muerto)";
        st.save(r);
    }

    /**
     * Borra del INI del juego EXACTAMENTE las claves que el sintonizador escribio, y
     * solo si siguen valiendo lo nuestro (si el usuario las movio despues, lo suyo
     * queda). Escribir 0 en su lugar NO vale: pisaria un valor anterior.
     */
    static void deleteAdoptedKeys(String uri, String mask) {
        if (uri == null || uri.isEmpty() || mask == null || mask.isEmpty()) return;
        for (String part : mask.split(",")) {
            final int eq = part.indexOf('=');
            if (eq <= 0) continue;
            final String sk = part.substring(0, eq).trim();
            int idx = -1;
            for (int i = 0; i < 3; i++) if (SHORT[i].equals(sk)) idx = i;
            if (idx < 0) continue;
            final int mine;
            try { mine = Integer.parseInt(part.substring(eq + 1).trim()); }
            catch (NumberFormatException e) { continue; }
            final int cur = NativeApp.safeGetGameSettingInt(uri, SECTION_NAME, KEYS[idx], -999);
            if (cur == mine) NativeApp.safeDeleteGameSettingKey(uri, SECTION_NAME, KEYS[idx]);
        }
    }

    /**
     * "Borrar perfil aprendido" (Ajustes, seccion 10). Deshace la adopcion, borra los
     * registros de identidad del juego y la memoria del regidor, y dice CUANTO se fue
     * de verdad para que el aviso no prometa un borrado que no ocurrio.
     */
    static String forgetLearned(Context app, String uri) {
        if (uri == null || uri.isEmpty()) return "no hay juego delante: no hay nada que borrar";
        final AdaptiveProfile old = new AdaptiveProfile(app, uri);
        deleteAdoptedKeys(uri, old.tunerAdoptado);
        final int ids = TunerProfileStore.forgetForUri(app, uri);
        AdaptiveProfile.forget(app, uri);
        android.util.Log.i("CpuVUTuner", "CpuVUTuner state: FORGET — " + ids
                + " registro(s) de identidad + la memoria del juego (" + uri + ")");
        return ids == 0
                ? "no habia perfil aprendido para este juego"
                : ids + " perfil(es) de identidad borrados y deshecha la adopcion";
    }

    /** El VM esta en pausa (o el mando de velocidad activo): cuenta como congelacion. */
    void onFrozen() {
        if (mPhase == PH_NONE) return;
        mFrozenTicks++;
        if (mFrozenTicks > PAUSE_LIMIT) abort("pausa larga a mitad del ensayo");
    }

    /** El regidor ya no va a ticking: un ensayo sin reloj seria un speedhack global
     *  dejado puesto para siempre. Cerrar ya, con reversion completa. */
    void halt(String why) { abort(why); }

    void onGameStop(AdaptiveProfile profile) {
        // Parar el juego con un ensayo en vuelo NO es un crash del candidato: si no
        // se borra el diario, el proximo arranque lo leeria como proceso muerto y
        // suspenderia un candidato que simplemente no termino de medirse.
        if (mPhase != PH_NONE) abort("el juego se paro a mitad del ensayo");
        if (profile != null && profile.known() && mLastVerdict != null) {
            profile.tunerVerdict = mLastVerdict;
            profile.tunerAdoptado = mLastAdopted;
            profile.tunerAplicado = !mLastAdopted.isEmpty();
            profile.tunerAt = System.currentTimeMillis();
            profile.tunerNow = mLastFps;
        }
    }

    // ==================================================================
    // aplicacion, evidencia y HUD
    // ==================================================================

    /**
     * Los tres viajan por el mismo executor serie-a-serie (orden garantizado).
     * Cenit 0.6.31 (arnes h8): durante el ensayo esto se llama con la fotografia de
     * mUser para revertir, y con la tripleta del usuario VIVA cuando el motivo del
     * cierre fue "el usuario cambio los ajustes". Usar la fotografia en ese caso
     * habria borrado del INI la eleccion recien hecha — "cancelar y revertir" no
     * puede significar "ganar el sintonizador".
     */
    private void applyGlobalsAsync(int[] triple) {
        if (triple == null) return;
        NativeApp.setEECycleRateAsync(triple[IDX_RATE]);
        NativeApp.setEECycleSkipAsync(triple[IDX_SKIP]);
        NativeApp.setMTVUAsync(triple[IDX_MTVU] == 1);
    }

    /**
     * Lo que el usuario quiere AHORA en las patas que tienen preferencia Java
     * (Rate y MTVU). CycleSkip no la tiene —vive solo en el INI global, sin
     * spinner—, asi que ahi se devuelve el valor fotografiado en readBaseline(),
     * que es el unico "previo exacto" que existe. Si el usuario lo movio a mano
     * en el INI, environmentDrifted() cortó el ensayo antes de llegar aqui.
     */
    private int[] liveUserTriple() {
        if (mUser == null) return null;
        return new int[]{
                prefs.getInt("ee_cycle_rate", 0),
                mUser[IDX_SKIP],
                prefs.getBoolean("mtvu", NativeApp.defaultMTVU()) ? 1 : 0};
    }

    void observeVUWait(CenitLoadSample s) {
        if (s == null || !s.isValid()) return;
        if (mBase != null && mBase[IDX_MTVU] != 1) return; // sin MTVU, wait es 0 por diseno
        if (s.mtvuWaitMs() >= VU_WAIT_MS && s.mtvuWaitCalls() >= VU_WAIT_CALLS_MIN) mWaitHits++;
        else mWaitHits = 0;
    }

    /**
     * Publica el estado del sintonizador al HUD. Los FPS "antes/despues" son los
     * ultimos medidos, no una promesa: sin las dos ventanas de cada lado se manda
     * el centinela -1 y el HUD lo pinta como "-" (un numero viejo pintado como
     * actual, o un 0,0 pintado como medida, es peor que un hueco).
     *
     * El 0 importa aqui: mBaseAgg arranca en 0, y publicar "0 fps de base" le daria
     * al HUD un numero inventado justo en los estados donde aun no se midio nada
     * (OFF, IDLE, LEARNING). Por eso se filtra <= 0 y se manda -1, igual que con los
     * frame times.
     */
    private void publish() {
        NativeApp.safeSetCenitTunerStatus(mPublishState, modeOf(prefs),
                Math.round(mGainPct * 10f),
                mBaseAgg > 0f ? Math.round(mBaseAgg * 100f) : -1,
                mCandAgg > 0f ? Math.round(mCandAgg * 100f) : -1,
                Float.isNaN(mHudP50) ? -1f : mHudP50,
                Float.isNaN(mHudP95) ? -1f : mHudP95,
                mCandThermalMax >= 0 ? mCandThermalMax : mBaseThermalMax,
                mIdentity == null ? "" : mIdentity.id);
    }

    /** Etiqueta legible compartida por logs, registro y UI. */
    static String verdictFor(int[] w) {
        if (w == null) return "ninguno";
        final StringBuilder sb = new StringBuilder();
        if (w[0] != 0) sb.append("Rate ").append(w[0]).append(" (75%)");
        if (w[1] != 0) { if (sb.length() > 0) sb.append(" + "); sb.append("Skip ").append(w[1]); }
        if (w[2] != 0 || (w[0] == 0 && w[1] == 0)) {
            if (sb.length() > 0) sb.append(" + ");
            sb.append(w[2] == 1 ? "MTVU ON" : "MTVU OFF");
        }
        return sb.length() == 0 ? "ninguno" : sb.toString();
    }

    /** Para el reporte de evidencia: cuanta ganancia hace falta para cada pata. */
    static String thresholdSummary() {
        return "MTVU +" + GAIN_MTVU_PCT + "% Rate +" + GAIN_RATE_PCT + "% Skip +"
                + GAIN_SKIP_PCT + "% | P95<=" + P95_TOLERANCE + "x | min>="
                + WORST_RATIO_MIN + "x | speed-" + SPEED_DROP_TOLERANCE_PCT
                + "% | audio=0 | termico<=" + THERMAL_STEP_LIMIT + " paso | post "
                + POST_VERIFY_TICKS + "s";
    }
}
