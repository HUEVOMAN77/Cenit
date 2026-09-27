package com.izzy2lost.psx2;

import android.content.Context;
import android.content.SharedPreferences;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Map;

/**
 * Sintonizador CPU/VU por juego — governor v4 (Cenit 0.6.27).
 *
 * El regidor de resolución (v1-v3) ya DISTINGUÍA el problema: con la GPU holgada
 * y el juego atrasado, el cuello es la CPU emulada (EE) o el VU1, y bajar
 * píxeles no recupera ni un cuadro. Lo detectaba, lo anunciaba en el log
 * ("DynRes state: CPU_BOUND") y se quedaba de brazos cruzados, porque recortar
 * imagen gratis es justo lo que este proyecto se negó a hacer. Esta clase le da
 * por fin manos, actuando SOLO por la plomería de aplicación en caliente ya
 * existente y probada: setters del INI + ApplySettings (Rate/Skip/MTVU se leen
 * en caliente — el recompilador ARM64 lee EECycleRate bloque a bloque y el bit 4
 * del bitset de Speedhacks en cada despacho, microVU-arm64.cpp; CheckForCPU-
 * ConfigChanges limpia las cachés del recompiler al diffear Speedhacks; MTVU no
 * reconstruye las CPUs, solo re-afina afinidades) y la capa por-juego con
 * ReloadGameSettings (WriteGameLayerInt) para la adopción permanente.
 * ENCONTRADO, NO TOCADO: la sincronización EE<->VU1/MTVU, los superbloques VU y
 * la semántica de EECycleRate/Skip. Aquí solo se escriben las mismas claves INI
 * que escribiría el usuario a mano, por la misma ruta que ya usa Ajustes.
 *
 * Clasificación (la parte "distinguir GPU-bound de CPU/VU-bound" del encargo):
 *  - GPU-bound  -> el regidor clásico baja resolución (sigue siendo su camino).
 *  - CPU_BOUND  -> GPU holgada sin espera EE->VU1: el EE trabaja solo. La oferta
 *                  sin coste visual es cederle el VU1 a un hilo (MTVU ON); si no
 *                  basta, un paso conservador de Rate/Skip.
 *  - VU_BOUND   -> MTVU puesto y el EE BLOQUEADO esperando al VU1: lo demuestra
 *                  la métrica wait_ms/wait_calls del núcleo (ventana de 0.5 s,
 *                  PerformanceMetrics::GetMtvuSyncStats, en ceros con MTVU
 *                  apagado). Entonces la oferta sensata es QUITARLE el hilo al
 *                  VU1 (MTVU OFF), no añadirle más carga al EE.
 *
 * Benchmark (máximo una vez por juego y veredicto):
 *  1) Línea base = la tripleta EFECTIVA al empezar (capa por-juego y GameDB
 *     incluidos). Las claves que el juego/usuario fijan por-juego NO se ofrecen
 *     a mover: el por-juego manda sobre el global, así que ensayarlas sería
 *     escribir en una pared. Cada ensayo escribe SOLO el INI global (lo que la
 *     capa por-juego no fija) y se verifica contra la lectura EFECTIVA antes de
 *     medir — si el valor no llegó, el ensayo se aborta, no se mide a ciegas.
 *  2) Métrica = FPS reales (PerformanceMetrics::GetFPS), nunca el % de velocidad:
 *     GetSpeed = fps/fps_objetivo*100 y el objetivo NO se re-escala con
 *     Rate/Skip — con una cuota el % miente por diseño (un -1 se reportaría
 *     ~75% aunque fuera puro). Solo gana quien saca MÁS cuadros reales.
 *  3) Ventanas: asentamiento + muestra (16 s base, 16 s por ensayo); decisión
 *     por MEDIANA (robusta a picos de carga) con piso de estabilidad (mínimo
 *     >= 0.5x de la mediana). Ganador: +6% para MTVU (no recorta nada) y +10%
 *     para Rate/Skip (que sí tocan la emulación).
 *  4) Rollback: cada ensayo no ganador restaura el global EXACTO capturado al
 *     empezar (mUser; el CycleSkip global no tiene preferencia Java, por eso se
 *     lee del INI con getGlobalSettingInt). Sin ganador, los globales quedan
 *     restaurados y no se adopta nada. Con ganador, la adopción se escribe en
 *     la capa POR-JUEGO (lo único que sobrevive a los arranques sin tocar las
 *     preferencias del usuario), solo para las claves que el ensayo ganó, y se
 *     confirma por lectura efectiva antes de registrarla; si la confirmación no
 *     llega en 4 s, se borra lo escrito y se restaura el global — nunca queda
 *     una adopción a medias ni un "0" huérfano pisando un valor anterior.
 *  5) Diario anti-cierre ANTES de cada escritura (global y por-juego) en
 *     "cpu_tuner_pending:<uri>" con la receta exacta ("r,s,m[;claves]"). Si el
 *     proceso muere a mitad — un juego incompatible con MTVU se cuelga de
 *     verdad —, recoverAtBoot() aplica la receta al arrancar la app (antes de
 *     que MainActivity re-aplique las preferencias) y otra vez en cada arranque
 *     de juego: borra las claves por-juego escritas, devuelve el global, deja
 *     el perfil como "recuperado" (y el juego podrá reintentarse otro día).
 *  6) Memoria (AdaptiveProfile, por URI): veredicto + máscara adoptada + cuándo
 *     + FPS. "adoptado" no se re-bencha nunca (su capa por-juego ya está en
 *     vigor); "ninguno" caduca a la semana (otro driver u otra versión pueden
 *     cambiar el resultado); "recuperado" deja reintentar. Mantener pulsado
 *     "Recordar rendimiento por juego" borra el perfil Y deshace la adopción
 *     en el INI del juego (forgetAdoption).
 *  7) Entorno: arranca tras 10 s seguidos de "atrasado + CPU-bound + ya en el
 *     escalón más bajo de resolución" (bajar píxeles ya no puede ayudar); se
 *     congela con turbo de cargas, límite térmico, métrica de GPU muerta, paso
 *     de resolución en el aire o pausa larga (>60 s), y aborta si la escala se
 *     mueve (las FPS dejarían de ser comparables). Todo el bloque cuelga de un
 *     interruptor EXPERIMENTAL (cpu_tuner) apagado por defecto.
 *
 * Interacción con el regidor (regla del congelamiento): mientras este sintonizador
 * tiene un ensayo en curso, EL es dueño de EECycleRate — el governor no debe
 * congelarse porque effectiveRate != 0 (pendiente 3 del diseño: distinguir
 * "rate movido por el governor" de "rate movido por el usuario"). Lo hace con
 * isActive(): el governor solo se congela por rate != 0 cuando este sintonizador
 * NO está midiendo. Y cuando un ensayo termina sin adoptar, los globales
 * restaurados dejan el rate donde estaba el usuario: el governor retoma solo.
 */
final class CpuVUTuner {

    /** Lo que el regidor hace tras este tick. */
    static final int IDLE = 0;      // nada en curso: reglas clásicas normales
    static final int CONTINUE = 1;  // ensayo en curso: congelar subida/bajada de escala
    static final int RETRY = 2;     // el estado cambió este tick: reentrar el evaluate

    private static final int BASE_SETTLE = 4;       // s ignoradas al (re)arrancar
    private static final int BASE_MEASURE = 12;     // muestras de FPS base
    private static final int TRIAL_VERIFY_WAIT = 2; // s hasta leer la verificación
    private static final int TRIAL_SETTLE = 4;
    private static final int TRIAL_MEASURE = 10;
    private static final int ADOPT_VERIFY_TICKS = 4; // rondas hasta confirmar adopción
    private static final int MAX_BENCH_TICKS = 300;  // techo duro ~5 min: abortar
    private static final int PAUSE_LIMIT = 60;       // s congelado antes de abortar
    private static final int START_STREAK = 10;      // s de evidencia para arrancar
    private static final float GAIN_UNDERCLOCK = 0.10f; // Rate/Skip: +10% FPS
    private static final float GAIN_MTVU = 0.06f;       // MTVU: +6% (no recorta nada)
    private static final float BASE_FPS_MIN = 5f;       // sin base medible, no hay juicio
    private static final float WORST_RATIO_MIN = 0.5f;  // estable: min >= 0.5*mediana
    private static final float VU_WAIT_MS = 10f;        // evidencia wait por ventana...
    private static final int VU_WAIT_CALLS_MIN = 30;    // ...y mínimo de llamadas
    private static final int VU_WAIT_TICKS_MIN = 2;     // vistas seguidas, no un latigazo
    private static final int RETRY_WINDOW = 2;          // ventanas inválidas por fase
    private static final long STALE_NONE_MS = 7L * 24 * 3600 * 1000; // "ninguno" caduca

    static final String JOURNAL_PREFIX = "cpu_tuner_pending:";
    static final String PREF_KEY = "cpu_tuner";

    // Índices de la tripleta {rate, skip, mtvu}, en el orden de KEYS/SHORT.
    private static final int IDX_RATE = 0, IDX_SKIP = 1, IDX_MTVU = 2;
    private static final String SECTION_NAME = "EmuCore/Speedhacks";
    private static final String[] KEYS = {"EECycleRate", "EECycleSkip", "vuThread"};
    private static final String[] SHORT = {"r", "s", "m"};

    private static final int PH_NONE = 0, PH_BASE_SETTLE = 1, PH_BASE_MEASURE = 2,
            PH_TRIAL_VERIFY = 3, PH_TRIAL_SETTLE = 4, PH_TRIAL_MEASURE = 5,
            PH_ADOPT_VERIFY = 6;

    private final Context appContext;
    private final SharedPreferences prefs;

    // --- estado del ensayo: solo el hilo principal, al reloj del regidor ---
    private int mPhase = PH_NONE;
    private int mTicks;                       // cuenta atrás de la fase
    private int mBenchTicks, mFrozenTicks;
    private int mWindowPos, mWindowNeed, mRetries;
    private int mStreak;                      // s seguidos de "CPU-bound en el suelo"
    private int mWaitHits;                    // s con espera EE->VU1 significativa
    private int[] mBase;                      // tripleta EFECTIVA al empezar {r,s,m}
    private int[] mUser;                      // INI global capturado (rollback exacto)
    private boolean mFixedR, mFixedS, mFixedM; // claves fijadas por-juego: intocables
    private int[][] mQueue;                   // candidatos filtrados, en orden
    private int mQueuePos;
    private int[] mCand;                      // ensayo en curso
    private final float[] mWindow = new float[BASE_MEASURE];
    private float mBaseMedian, mBaseMin, mTrialMedian, mTrialMin;
    private float mStartApplied = -1f;        // la escala debe permanecer intacta
    private String mJournalUri = "";
    // Perfil de la sesión en curso: finishRecord persiste el veredicto EN EL
    // INSTANTE (no al parar el juego) para que ninguna adopción quede nunca sin
    // rastro en la memoria si un segundo benchmark o un apagado del interruptor
    // llega antes del onGameStop.
    private AdaptiveProfile mProfile = null;
    // Adopción: qué claves se escribieron por-juego (máscara) y si el diario ya
    // las registró (controla qué hay que borrar al revertir).
    private boolean mPerGameTouched = false;
    // Veredicto del último ensayo TERMINADO, que onGameStop persiste en el perfil.
    private String mLastVerdict = null;
    private String mLastAdopted = "";
    private float mLastFps = 0f;

    CpuVUTuner(Context app, SharedPreferences prefs) {
        this.appContext = app.getApplicationContext();
        this.prefs = prefs;
    }

    /** true = hay un ensayo en curso: el governor no debe congelarse por su propio rate. */
    boolean isActive() {
        return mPhase != PH_NONE;
    }

    /** El juego arrancó: procesar el diario de un cierre brusco y limpiar restos. */
    void startSession() {
        recoverAtBoot(appContext, prefs);
        if (mPhase != PH_NONE) abort("state left over from a previous session");
        mProfile = null;
        mStreak = 0;
        mLastVerdict = null;
        mLastAdopted = "";
        mLastFps = 0f;
    }

    // ==================================================================
    // El tick del regidor. El governor ya midió speed/gpu y trae el contexto
    // crudo; las etiquetas (CPU_BOUND / VU_BOUND) se deciden aquí, con la
    // métrica wait del núcleo como testigo entre las dos.
    // ==================================================================
    int tick(AdaptiveProfile profile, String uri, float applied,
             boolean behind, boolean atFloor, boolean cpuBoundNow, boolean metricsAlive,
             boolean turbo, boolean settling, boolean thermal) {
        if (!prefs.getBoolean(PREF_KEY, false)) {
            if (mPhase != PH_NONE) abort("switch turned off mid-benchmark");
            if (profile != null && profile.known() && profile.tunerAplicado) {
                undoAdoption(profile.uri(), profile.tunerAdoptado);
                clearTunerFields(profile);
                profile.save();
            }
            mStreak = 0;
            return IDLE;
        }
        if (mPhase == PH_NONE) {
            mStreak = (behind && atFloor && cpuBoundNow && !turbo && !settling && !thermal)
                    ? mStreak + 1 : 0;
            if (mStreak >= START_STREAK) {
                mStreak = 0;
                begin(profile, uri, applied);
                return isActive() ? RETRY : IDLE;
            }
            return IDLE;
        }

        // ------------------------------- ensayo en curso ------------------------------
        if (turbo || settling || thermal || !metricsAlive) {
            mFrozenTicks++;
            if (mFrozenTicks > PAUSE_LIMIT) {
                abort("paused too long mid-benchmark (turbo/thermal/no-metric)");
                return RETRY;
            }
            return CONTINUE;
        }
        mFrozenTicks = 0;
        mBenchTicks++;
        if (mBenchTicks > MAX_BENCH_TICKS) {
            abort("benchmark hit its hard time cap");
            return RETRY;
        }
        if (mStartApplied >= 0f && (applied <= 0f || Math.abs(applied - mStartApplied) > 0.001f)) {
            abort("resolution scale moved mid-benchmark (FPS would not be comparable)");
            return RETRY;
        }
        // Ventana en curso → la tripleta EFECTIVA debe ser la esperada (la base
        // en la ventana base, el candidato en la del ensayo). Si se movió sola
        // — un spinner del usuario en Ajustes o un recargado tardío — las FPS
        // que se están grabando ya no son de ESTE ensayo y la adopción se
        // calcularía contra una base fantasma: abortar con reversión (quedan
        // retryables: abort no deja veredicto). Las fases de verificación y
        // asentamiento NO se chequean: el apply asíncrono tarda 1-2 ticks en
        // aterrizar y ahí se espera a que aterrice, no se aborta.
        if ((mPhase == PH_BASE_MEASURE || mPhase == PH_TRIAL_MEASURE) && environmentDrifted()) {
            abort("speedhacks moved mid-window (user spinner or late reload): window is not ours");
            return RETRY;
        }

        switch (mPhase) {
            case PH_BASE_SETTLE:
                if (--mTicks > 0) return CONTINUE;
                mWindowPos = 0;
                mWindowNeed = BASE_MEASURE;
                mPhase = PH_BASE_MEASURE;
                mTicks = BASE_MEASURE;
                return CONTINUE;

            case PH_BASE_MEASURE: {
                collect();
                observeVUWait();
                if (--mTicks > 0) return CONTINUE;
                if (!windowValid()) {
                    if (++mRetries > RETRY_WINDOW) { abort("no usable FPS window for the baseline"); return RETRY; }
                    mPhase = PH_BASE_SETTLE;
                    mTicks = BASE_SETTLE;
                    return CONTINUE;
                }
                mBaseMedian = medianOfWindow();
                mBaseMin = minOfWindow();
                if (mBaseMedian < BASE_FPS_MIN) {
                    abort("baseline under " + BASE_FPS_MIN + " fps: no trustworthy comparison");
                    return RETRY;
                }
                if (baselineDrifted()) {
                    readBaseline(uri);
                    mPhase = PH_BASE_SETTLE;
                    mTicks = BASE_SETTLE;
                    android.util.Log.i("CpuVUTuner", "CpuVUTuner state: BASELINE_REREAD — effective values moved before the first trial");
                    return CONTINUE;
                }
                buildQueue();
                if (mQueue.length == 0) {
                    finishWithoutAdoption();
                    return RETRY;
                }
                startNextTrial();
                return isActive() ? CONTINUE : RETRY;
            }

            case PH_TRIAL_VERIFY: {
                if (--mTicks > 0) return CONTINUE;
                if (!trialInEffect()) {
                    abort("trial value never reached the emulator (user spinners? reload?)");
                    return RETRY;
                }
                mWindowPos = 0;
                mWindowNeed = TRIAL_MEASURE;
                mPhase = PH_TRIAL_SETTLE;
                mTicks = TRIAL_SETTLE;
                return CONTINUE;
            }

            case PH_TRIAL_SETTLE:
                if (--mTicks > 0) return CONTINUE;
                mWindowPos = 0;
                mPhase = PH_TRIAL_MEASURE;
                mTicks = TRIAL_MEASURE;
                return CONTINUE;

            case PH_TRIAL_MEASURE: {
                collect();
                if (--mTicks > 0) return CONTINUE;
                if (!windowValid()) {
                    if (++mRetries > RETRY_WINDOW) { abort("no usable FPS window on a trial"); return RETRY; }
                    mPhase = PH_TRIAL_SETTLE;
                    mTicks = TRIAL_SETTLE;
                    return CONTINUE;
                }
                mTrialMedian = medianOfWindow();
                mTrialMin = minOfWindow();
                evaluateTrialAndAdvance();
                return isActive() ? CONTINUE : RETRY;
            }

            case PH_ADOPT_VERIFY: {
                if (adoptionInEffect()) {
                    confirmAdoption();
                    return RETRY;
                }
                mTicks--;
                if (mTicks > 0) return CONTINUE;
                // La adopción no aterrizó: borrar las claves por-juego escritas y
                // cerrar SIN veredicto adoptado (el global ya volvió a mUser). El
                // diario también se borra: si no, el próximo arranque marcaría el
                // perfil como "recuperado" por una receta ya consumida.
                android.util.Log.i("CpuVUTuner", "CpuVUTuner state: ADOPT_UNDONE — adoption never visible in the effective read");
                deleteWrittenKeys(mJournalUri);
                removeJournal();
                finishRecord("ninguno", "");
                return RETRY;
            }
        }
        return CONTINUE;
    }

    /** El VM está en pausa (o el mando de velocidad activo): cuenta como congelación. */
    void onFrozen() {
        if (mPhase == PH_NONE) return;
        mFrozenTicks++;
        if (mFrozenTicks > PAUSE_LIMIT) abort("paused too long mid-benchmark");
    }

    /**
     * El regidor ya no va a ticking (se rindió o lo apagaron): un ensayo sin
     * reloj sería un speedhack global dejado puesto para siempre. Cerrar ya, con
     * reversión completa.
     */
    void halt(String why) {
        abort(why);
    }

    /** El juego para: un ensayo a medias se revierte; el veredicto se persiste. */
    void onGameStop(AdaptiveProfile profile) {
        if (mPhase != PH_NONE) abort("game stopped mid-benchmark");
        if (profile != null && profile.known() && mLastVerdict != null) {
            profile.tunerVerdict = mLastVerdict;
            profile.tunerAdoptado = mLastAdopted;
            profile.tunerAplicado = !mLastAdopted.isEmpty();
            profile.tunerAt = System.currentTimeMillis();
            profile.tunerNow = mLastFps;
        }
    }

    // ==================================================================
    // arranque del ensayo
    // ==================================================================

    private void begin(AdaptiveProfile profile, String uri, float applied) {
        // Veredicto vigente: un perfil ya adoptado no se re-bencha (su capa
        // por-juego está en vigor); "ninguno" caduca a la semana; "recuperado"
        // o vacío dejan intentar.
        if (profile == null || !profile.known()) return;
        if (profile.tunerAplicado) return;
        if ("ninguno".equals(profile.tunerVerdict)
                && System.currentTimeMillis() - profile.tunerAt <= STALE_NONE_MS) return;
        mJournalUri = uri;
        mProfile = profile;
        readBaseline(uri);
        mStartApplied = applied;
        mBenchTicks = 0;
        mFrozenTicks = 0;
        mRetries = 0;
        mWaitHits = 0;
        mPerGameTouched = false;
        mPhase = PH_BASE_SETTLE;
        mTicks = BASE_SETTLE;
        android.util.Log.i("CpuVUTuner", "CpuVUTuner state: BENCHMARK_START — behind at the scale floor with the GPU idle; "
                + "effective baseline rate=" + mBase[IDX_RATE] + " skip=" + mBase[IDX_SKIP]
                + " mtvu=" + mBase[IDX_MTVU]
                + " (global before trials rate=" + mUser[IDX_RATE] + " skip=" + mUser[IDX_SKIP]
                + " mtvu=" + mUser[IDX_MTVU] + ")");
    }

    /**
     * Tripletas: la EFECTIVA (lo que corre, por-juego y GameDB incluidos), qué
     * está FIJADO por-juego (intocable) y el GLOBAL capturado (rollback exacto).
     * Sin VM, safeGetEffectiveEECycleRate lee el INI global y safeGetEffective-
     * MTVU también: la base es lo que el juego está ejecutando de verdad.
     */
    private void readBaseline(String uri) {
        final int gR = NativeApp.safeGetGameSettingInt(uri, SECTION_NAME, KEYS[0], -99);
        final int gS = NativeApp.safeGetGameSettingInt(uri, SECTION_NAME, KEYS[1], -99);
        final int gM = NativeApp.safeGetGameSettingInt(uri, SECTION_NAME, KEYS[2], -99);
        mFixedR = gR != -99;
        mFixedS = gS != -99;
        mFixedM = gM != -99;
        // Los wrappers "safe" devuelven 0/0/-1 al fallar (nunca -1 como valor
        // real de Skip/MTVU, que no lo usan); se sanea a rango sin inventar.
        final int[] e = effective();
        mBase = new int[]{
                mFixedR ? gR : e[IDX_RATE],
                mFixedS ? gS : e[IDX_SKIP],
                mFixedM ? gM : e[IDX_MTVU]};
        // El CycleSkip global no tiene preferencia Java (vive solo en el INI):
        // se lee del INI para poder restaurarlo EXACTO al terminar.
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
     * (Skip/MTVU) cuando la lectura cae. -1 no es un valor lícito de Skip ni
     * de MTVU, así que cae a 0 — el valor menos intrusivo y el único honesto
     * ante un dato ausente.
     */
    private static int[] effective() {
        return new int[]{
                clampRate(NativeApp.safeGetEffectiveEECycleRate()),
                clampSkip(NativeApp.safeGetEffectiveEECycleSkip()),
                clampMtvu(NativeApp.safeGetEffectiveMTVU())};
    }

    /** true si la tripleta efectiva se movió sola (reload tardío, spinner usuario). */
    private boolean baselineDrifted() {
        final int[] e = effective();
        return e[IDX_RATE] != mBase[IDX_RATE] || e[IDX_SKIP] != mBase[IDX_SKIP]
                || e[IDX_MTVU] != mBase[IDX_MTVU];
    }

    /**
     * ¿La ventana en curso sigue siendo la que se está midiendo? En la fase base
     * se espera la línea base; en la de ensayo, el candidato. Cualquier otro
     * valor efectivo significa que alguien (un spinner del usuario, un recargado
     * tardío, el propio core) movió la emulación bajo nuestros pies: las FPS que
     * se están tomando ya no corresponden a ESTO. Abortar es siempre lo seguro
     * (reversión completa y sin veredicto: el juego queda reintentable).
     */
    private boolean environmentDrifted() {
        final int[] expected = (mPhase == PH_TRIAL_MEASURE && mCand != null) ? mCand : mBase;
        if (expected == null) return false;
        final int[] e = effective();
        return e[IDX_RATE] != expected[IDX_RATE] || e[IDX_SKIP] != expected[IDX_SKIP]
                || e[IDX_MTVU] != expected[IDX_MTVU];
    }

    /**
     * Cola anti-intrusiva, filtrada por evidencia y por las claves fijadas:
     *   A) invertir MTVU — cero coste visual. Hacia ON siempre que el hardware
     *      permita hilar VU1 (3+ núcleos, la misma pregunta del perfil); hacia
     *      OFF SOLO con wait sostenido (eso es VU-bound: el EE parado esperando).
     *   B) Rate -1 con Skip 0 — un paso por debajo del normal, jamás turbo.
     *   C) Skip 1 con Rate 0 — cuota suave, último recurso.
     * B y C exigen Rate y Skip libres y en su valor normal (base 0/0): si el
     * usuario ya fijó un -1 por-juego, no se le hunde más. Las claves fijadas y
     * las que no se tocan viajan en el candidato con el valor de la base, así
     * que applyGlobals(mCand) SIEMPRE es una tripleta completa coherente.
     */
    private void buildQueue() {
        final ArrayList<int[]> q = new ArrayList<>();
        // A
        if (!mFixedM && !NativeApp.hasNoNativeBinary && NativeApp.defaultMTVU()) {
            final int target = mBase[IDX_MTVU] == 0 ? 1 : 0;
            if (target == 1 || mWaitHits >= VU_WAIT_TICKS_MIN) {
                final int[] c = mBase.clone();
                c[IDX_MTVU] = target;
                q.add(c);
            }
        }
        // B y C
        if (!mFixedR && !mFixedS && mBase[IDX_RATE] == 0 && mBase[IDX_SKIP] == 0) {
            final int[] b = mBase.clone();
            b[IDX_RATE] = -1;
            q.add(b);
            final int[] c = mBase.clone();
            c[IDX_SKIP] = 1;
            q.add(c);
        }
        mQueue = q.toArray(new int[0][]);
        mQueuePos = -1;
        android.util.Log.i("CpuVUTuner", "CpuVUTuner state: QUEUE " + mQueue.length + " candidate(s)"
                + (mWaitHits >= VU_WAIT_TICKS_MIN ? " — VU-bound evidence (EE waits on VU1)" : " — CPU-bound, no VU wait")
                + " [fixed r=" + mFixedR + " s=" + mFixedS + " m=" + mFixedM + "]");
    }

    private void startNextTrial() {
        mQueuePos++;
        mRetries = 0;
        if (mQueuePos >= mQueue.length) {
            finishWithoutAdoption();
            return;
        }
        mCand = mQueue[mQueuePos];
        writeJournal(false); // el diario ANTES de tocar el INI
        applyGlobalsAsync(mCand);
        mPhase = PH_TRIAL_VERIFY;
        mTicks = TRIAL_VERIFY_WAIT;
        android.util.Log.i("CpuVUTuner", "CpuVUTuner state: TRIAL " + verdictFor(mCand)
                + " applied globally; verifying");
    }

    private void evaluateTrialAndAdvance() {
        final double gain = (mTrialMedian - mBaseMedian) / Math.max(0.001f, mBaseMedian);
        final boolean underclocked = mCand[IDX_RATE] < 0 || mCand[IDX_SKIP] > 0;
        final float need = underclocked ? GAIN_UNDERCLOCK : GAIN_MTVU;
        final boolean stable = mTrialMin >= WORST_RATIO_MIN * mTrialMedian;
        android.util.Log.i("CpuVUTuner", "CpuVUTuner state: TRIAL_RESULT " + verdictFor(mCand)
                + " median " + mTrialMedian + " vs base " + mBaseMedian + " ("
                + Math.round(gain * 100) + "%, need " + Math.round(need * 100) + "%), min "
                + mTrialMin + (stable ? " stable" : " UNSTABLE"));
        if (gain >= need && stable) {
            adoptCurrent();
            return;
        }
        // Restaurar el global ANTES del siguiente ensayo: todos parten de la
        // misma línea base y un cierre a mitad deja la menor superficie posible.
        applyGlobalsAsync(mUser);
        startNextTrial();
    }

    // ==================================================================
    // adopción (capa por-juego: lo único que sobrevive a los arranques)
    // ==================================================================

    private void adoptCurrent() {
        mPerGameTouched = true;
        writeJournal(true); // receta completa (máscara por-juego) ANTES de escribir
        applyPerGameChanged(true);
        // El global vuelve a manos del usuario YA: la capa por-juego, que manda,
        // lleva el perfil adoptado, así el estado pendiente es mínimo.
        applyGlobalsAsync(mUser);
        mPhase = PH_ADOPT_VERIFY;
        mTicks = ADOPT_VERIFY_TICKS;
        android.util.Log.i("CpuVUTuner", "CpuVUTuner state: ADOPT " + verdictFor(mCand)
                + " written per-game, verifying through the effective read");
    }

    /** true si TODAS las claves que el ensayo mueve ya se ven en la lectura efectiva. */
    private boolean adoptionInEffect() {
        final int[] e = effective();
        for (int i = 0; i < 3; i++) {
            if (mCand[i] == mBase[i]) continue;
            if (e[i] != mCand[i]) return false;
        }
        return true;
    }

    private void confirmAdoption() {
        final String mask = adoptedMask();
        if (mask.isEmpty()) {
            deleteWrittenKeys(mJournalUri);
            finishRecord("ninguno", "");
            return;
        }
        removeJournal();
        finishRecord(verdictFor(mCand), mask);
    }

    private void applyPerGameChanged(boolean write) {
        final String uri = mJournalUri;
        if (uri == null || uri.isEmpty()) return;
        for (int i = 0; i < 3; i++) {
            if (mCand[i] == mBase[i]) continue;
            if (write) {
                NativeApp.safeSetGameSettingInt(uri, SECTION_NAME, KEYS[i], mCand[i]);
            } else {
                NativeApp.safeDeleteGameSettingKey(uri, SECTION_NAME, KEYS[i]);
            }
        }
    }

    /** Borra del INI del juego las claves que ESTE ensayo escribió (si las escribió). */
    private void deleteWrittenKeys(String uri) {
        if (!mPerGameTouched || uri == null || uri.isEmpty() || mCand == null) return;
        for (int i = 0; i < 3; i++) {
            if (mCand[i] != mBase[i])
                NativeApp.safeDeleteGameSettingKey(uri, SECTION_NAME, KEYS[i]);
        }
        mPerGameTouched = false;
    }

    /** "r=-1,s=1,m=1" solo con las claves ganadas: la receta de undoAdoption. */
    private String adoptedMask() {
        final StringBuilder sb = new StringBuilder();
        for (int i = 0; i < 3; i++) {
            if (mCand[i] != mBase[i]) {
                if (sb.length() > 0) sb.append(',');
                sb.append(SHORT[i]).append('=').append(mCand[i]);
            }
        }
        return sb.toString();
    }

    /** Cierre normal sin ganador: globales restaurados, veredicto "ninguno". */
    private void finishWithoutAdoption() {
        applyGlobalsAsync(mUser);
        removeJournal();
        finishRecord("ninguno", "");
    }

    private void finishRecord(String verdict, String adopted) {
        mLastVerdict = verdict;
        mLastAdopted = adopted == null ? "" : adopted;
        mLastFps = mLastAdopted.isEmpty() ? mBaseMedian : mTrialMedian;
        // Persistir YA, no esperar al onGameStop: con las claves adoptadas ya
        // escritas en el INI del juego, un veredicto sin salvar sería una
        // adopción huérfana (sin memoria que la deshaga ni que impida
        // re-bencharla encima). onGameStop vuelve a escribir los mismos valores:
        // es idempotente.
        if (mProfile != null && mProfile.known()) {
            mProfile.tunerVerdict = mLastVerdict;
            mProfile.tunerAdoptado = mLastAdopted;
            mProfile.tunerAplicado = !mLastAdopted.isEmpty();
            mProfile.tunerAt = System.currentTimeMillis();
            mProfile.tunerNow = mLastFps;
            mProfile.save();
        }
        mPhase = PH_NONE;
        mCand = null;
        mQueue = null;
        mPerGameTouched = false;
        android.util.Log.i("CpuVUTuner", "CpuVUTuner state: BENCHMARK_DONE — verdict " + verdict
                + ", fps " + mLastFps + " (base " + mBaseMedian + ")"
                + (mLastAdopted.isEmpty() ? "" : ", adopted " + mLastAdopted));
    }

    /** Cierre de emergencia con reversión total y SIN veredicto (reintentable). */
    private void abort(String why) {
        if (mPhase == PH_NONE) return;
        android.util.Log.i("CpuVUTuner", "CpuVUTuner state: ABORT — " + why);
        deleteWrittenKeys(mJournalUri);
        if (mUser != null) applyGlobalsAsync(mUser);
        removeJournal();
        mPhase = PH_NONE;
        mCand = null;
        mQueue = null;
        mLastVerdict = null;
        mLastAdopted = "";
        mLastFps = 0f;
    }

    // ==================================================================
    // diario + recuperación
    // ==================================================================

    /** "cpu_tuner_pending:<uri>" = "r,s,m[;mask]" — globales exactos + claves por-juego. */
    private void writeJournal(boolean withAdoption) {
        if (mJournalUri == null || mJournalUri.isEmpty() || mUser == null) return;
        final StringBuilder v = new StringBuilder()
                .append(mUser[0]).append(',').append(mUser[1]).append(',').append(mUser[2]);
        if (withAdoption) v.append(';').append(adoptedMask());
        prefs.edit().putString(JOURNAL_PREFIX + mJournalUri, v.toString()).apply();
    }

    private void removeJournal() {
        if (mJournalUri != null && !mJournalUri.isEmpty()
                && prefs.getString(JOURNAL_PREFIX + mJournalUri, null) != null) {
            prefs.edit().remove(JOURNAL_PREFIX + mJournalUri).apply();
        }
    }

    /**
     * Aplica TODOS los diarios pendientes. Se llama en applySavedSettings (en
     * cuanto la app levanta el binario, antes de que MainActivity re-aplique
     * las preferencias y antes de que ningún juego arranque) y en cada
     * startSession. Un cierre brusco pudo dejar el INI global con un ensayo
     * puesto — y el CycleSkip global NO tiene preferencia que lo re-crie, así
     * que sin esto se quedaría -1 para siempre — y pudo dejar claves por-juego
     * escritas a medias. Revertir es exacto porque la receta está en el diario.
     */
    static void recoverAtBoot(Context app, SharedPreferences prefs) {
        final Map<String, ?> all = prefs.getAll();
        final java.util.List<String> keys = new ArrayList<>();
        for (String k : all.keySet()) {
            if (k != null && k.startsWith(JOURNAL_PREFIX)) keys.add(k);
        }
        for (String k : keys) {
            final String target = k.substring(JOURNAL_PREFIX.length());
            final String raw = prefs.getString(k, null);
            if (raw == null) { prefs.edit().remove(k).apply(); continue; }
            final String head;
            final String diff;
            final int semi = raw.indexOf(';');
            if (semi < 0) { head = raw; diff = ""; } else { head = raw.substring(0, semi); diff = raw.substring(semi + 1); }
            final String[] parts = head.split(",");
            if (parts.length != 3) { prefs.edit().remove(k).apply(); continue; }
            try {
                final int uR = Integer.parseInt(parts[0].trim());
                final int uS = Integer.parseInt(parts[1].trim());
                final int uM = Integer.parseInt(parts[2].trim());
                android.util.Log.i("CpuVUTuner", "CpuVUTuner state: RECOVER — crashed benchmark ("
                        + head + (diff.isEmpty() ? "" : ";" + diff) + ") for "
                        + (target.isEmpty() ? "?" : target));
                if (!target.isEmpty()) {
                    for (int i = 0; i < 3; i++) {
                        if (diff.contains(SHORT[i]))
                            NativeApp.safeDeleteGameSettingKey(target, SECTION_NAME, KEYS[i]);
                    }
                    // El perfil de ese juego ya no puede afirmar una adopción viva:
                    // se marca "recuperado" (veredicto que permite reintentar).
                    final AdaptiveProfile p = new AdaptiveProfile(app, target);
                    // El diario implica un ensayo nuestro de este juego: el perfil
                    // pasa a "recuperado" aunque no hubiera adopción registrada.
                    if (p.known()) {
                        p.tunerAplicado = false;
                        p.tunerAdoptado = "";
                        p.tunerVerdict = "recuperado";
                        p.tunerAt = System.currentTimeMillis();
                        p.save();
                    }
                }
                // Con VM parada estos setters solo re-escriben el INI base, que es
                // exactamente el estado que el diario promete restaurar.
                NativeApp.speedhackEecyclerate(uR);
                NativeApp.speedhackEecycleskip(uS);
                NativeApp.setMTVU(uM == 1);
            } catch (Throwable t) {
                android.util.Log.e("CpuVUTuner", "recovery entry failed", t);
            }
            prefs.edit().remove(k).apply();
        }
    }

    /**
     * Deshace una adopción registrada en el perfil: borra del INI del juego
     * EXACTAMENTE las claves que el sintonizador escribió, y solo si siguen
     * valiendo lo nuestro (si el usuario las movió después, lo suyo queda).
     * Escribir 0 en su lugar NO vale: pisaría un valor anterior del usuario.
     */
    private void undoAdoption(String uri, String mask) {
        deleteAdoptedKeys(uri, mask);
    }

    /**
     * Público: el mismo borrado quirúrgico para el "olvidar" de Ajustes
     * (mantener pulsado "Recordar rendimiento por juego"), que elimina el
     * perfil entero — la adopción por-juego no debe sobrevivir a su memoria.
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
            if (cur == mine)
                NativeApp.safeDeleteGameSettingKey(uri, SECTION_NAME, KEYS[idx]);
        }
    }

    private static void clearTunerFields(AdaptiveProfile profile) {
        profile.tunerVerdict = "";
        profile.tunerAdoptado = "";
        profile.tunerAplicado = false;
        profile.tunerAt = 0L;
        profile.tunerNow = 0f;
    }

    // ==================================================================
    // globales, muestreo y clasificación
    // ==================================================================

    /** Los tres viajan por el mismo executor serie-a-serie (orden garantizado). */
    private void applyGlobalsAsync(int[] triple) {
        NativeApp.setEECycleRateAsync(triple[0]);
        NativeApp.setEECycleSkipAsync(triple[1]);
        NativeApp.setMTVUAsync(triple[2] == 1);
    }

    private boolean trialInEffect() {
        final int[] e = effective();
        for (int i = 0; i < 3; i++) {
            if (mCand[i] == mBase[i]) continue; // esta clave no se tocó
            if (e[i] != mCand[i]) return false;
        }
        return true;
    }

    private void collect() {
        final float fps = NativeApp.safeGetFPS();
        if (mWindowPos < mWindow.length) mWindow[mWindowPos] = fps;
        mWindowPos++;
    }

    private boolean windowValid() {
        if (mWindowPos < mWindowNeed) return false;
        final int n = Math.min(mWindowNeed, mWindow.length);
        for (int i = 0; i < n; i++) {
            final float f = mWindow[i];
            if (f <= 0f || Float.isNaN(f)) return false; // 0 = aún sin primer cuadro
        }
        return true;
    }

    private float medianOfWindow() {
        final float[] v = Arrays.copyOf(mWindow, Math.min(mWindowNeed, mWindow.length));
        Arrays.sort(v);
        return v[v.length / 2];
    }

    private float minOfWindow() {
        float m = Float.MAX_VALUE;
        final int n = Math.min(mWindowNeed, mWindow.length);
        for (int i = 0; i < n; i++) if (mWindow[i] < m) m = mWindow[i];
        return m;
    }

    /**
     * VU_BOUND: con MTVU en vigor, el EE bloqueado esperando al VU1. La ventana
     * del núcleo se renueva cada 0.5 s; dos lecturas fuertes seguidas cortan el
     * latigazo. Con MTVU apagado wait_ms es 0 por diseño: no hay nada que ver.
     */
    private void observeVUWait() {
        if (mBase == null || mBase[IDX_MTVU] != 1) return;
        final float ms = NativeApp.safeGetMtvuWaitMs();
        final long calls = NativeApp.safeGetMtvuWaitCalls();
        if (ms >= VU_WAIT_MS && calls >= VU_WAIT_CALLS_MIN) mWaitHits++;
        else mWaitHits = 0;
    }

    /** Etiqueta legible compartida por logs, perfil y UI. */
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
}
