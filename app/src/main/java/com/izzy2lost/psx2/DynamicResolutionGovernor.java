package com.izzy2lost.psx2;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Handler;
import android.os.Looper;

/**
 * Regidor de resolución dinámica de Cenit — v3 (0.6.5).
 *
 * No toca el bucle de dibujo del GS: parchearlo desde fuera deja targets
 * cacheados a escala distinta y rompe justo los juegos que más lo necesitan
 * (God of War reescribe sus buffers cada cuadro). En su lugar mide la velocidad
 * real de emulación —PerformanceMetrics::GetSpeed, el mismo número del HUD— una
 * vez por segundo, y cuando ve que el juego no llega a tiempo aplica UN paso de
 * resolución por la vía oficial: escribir "upscale_multiplier" en el INI y
 * ApplySettings, la ruta exacta que usaría el usuario a mano.
 *
 * Reglas base (desde 0.6.3/0.6.4):
 *  - "upscale_multiplier" del usuario es el TECHO; nunca se sube de ahí.
 *  - Baja rápido (2 s de tirones), sube lento (8 s holgados), enfriamiento 2 s.
 *  - Los pasos usan exactamente la escala del menú (desde 0.6.25: 0.5x ... 8x,
 *    los dos primeros escalones son sub-nativos).
 *  - Mando de velocidad, pausa o techo movido: congelar/olvidar medición.
 *  - Solo baja píxeles si el cuello ES la GPU (GPU-bound). GPU holgada + juego
 *    atrasado = CPU-bound: bajar resolución solo empeora la imagen gratis.
 *  - Con límite térmico del sistema (PowerManager API 29+, leído por reflexión):
 *    nunca sube, bajar sí.
 *
 * v3 (0.6.5), cuatro funciones que no existen en ningún otro port — todas del
 * lado Java, sobre métricas y la capa por-juego que ya construíamos:
 *
 *  1) MEMORIA por juego (AdaptiveProfile): lo que este teléfono sostuvo con
 *     este ISO se recuerda. El arranque parte de la escala aprendida en vez de
 *     pelear desde el techo, y un juego visto como CPU-bound estable no vuelve
 *     a recibir recortes que no le sirven.
 *  2) PRECORTE térmico: cuando la velocidad muestra una DERIVA hacia abajo a lo
 *     largo de ~20 s sin aviso formal del sistema (el throttling silencioso de
 *     muchas ROM), el regidor baja un paso ANTES de que el corte se sienta.
 *     Máximo dos por partida, para no convertir el pronóstico en espiral.
 *  3) TURBO DE CARGAS: pantalla de carga = GPU muerta + velocidad clavada en
 *     100%. Detectado eso, se engancha el limitador Turbo del núcleo (el mismo
 *     de fast-forward, con restauración) y se suelta al volver el juego real.
 *     Solo con "auto turbo" encendido y con el juego ya aprendido (turbo>0 en
 *     su perfil, o tras la primera detección de la sesión).
 *  4) EVIDENCIA de cuotas: si en el escalón más bajo de la lista (1x antes de
 *     0.6.25, hoy 0.5x) el juego sigue atrasado muchos segundos, se
 *     cuenta (slowFloorTicks en el perfil) y la pantalla de Ajustes ofrece el
 *     "modo cuotas" (EECycleSkip por-juego) con datos, no a ciegas.
 *
 * v5 (0.6.29) — el regidor deja de opinar sobre "de qué es el retraso": esa
 * pregunta se contesta en UN solo sitio, GameLoadClassifier, comparando el % de
 * reloj real que tiene la GPU con el % que tiene el hilo EE (las dos métricas del
 * core, misma ventana, misma unidad). Motivo, medido en el código del core:
 * getGPUUsage() devuelve PORCENTAJE (0..100) y aquí se comparaba contra 0,65/0,05
 * desde 0.6.4 — o sea que la rama "GPU holgada" casi nunca se declaraba y el
 * regidor recortaba imagen también en juegos donde recortar no recupera cuadros.
 * Un solo camino de código para cualquier disco del catálogo y para Snapdragon 6,
 * 7 u 8: lo único que cambia entre ellos es lo que miden.
 *
 * v6 (0.6.30, Fase 1 + Fase 4 del prompt maestro) — tres cambios de fondo, cada
 * uno con su porqué medible:
 *
 *  1) UNA lectura por tick. El regidor ya no hace ocho llamadas sueltas al motor:
 *     pide un snapshot (CenitLoadSample, 24 cifras del MISMO PerformanceMetrics::Update)
 *     y con eso se clasifica, se loguea y se alimenta al sintonizador. Antes, si un
 *     Update caía en medio de la ristra, el tick comparaba la GPU de una ventana con
 *     el EE de la anterior.
 *  2) La CONFIANZA manda la acción (Regla 5). El recorte exige
 *     ALLOW_RESOLUTION_DROP y el bloqueo exige BLOCK_RESOLUTION_DROP; una decisión
 *     floja llega degradada a WAIT_FOR_EVIDENCE y el regidor no aprende nada de
 *     ese tick. En v5 la confianza se imprimía pero no gobernaba nada.
 *  3) UN dueño por tick, con la matriz de la Fase 4: GPU_BOUND → se recorta y el
 *     sintonizador queda en espera; CPU_BOUND/VU_BOUND con permiso → se mantiene la
 *     escala y el sintonizador trabaja; THERMAL_BOUND → no se sube, no se benchea y
 *     NO se escribe cpuBound en el perfil; INVALID_SAMPLE → la métrica faltó (se
 *     busca la causa); UNKNOWN_LOAD → se espera. Los dos "no sé" estaban antes
 *     mezclados en la misma etiqueta y el tally no podía contarlos aparte.
 *
 * Además: la calibración de umbrales (LoadTrend) se acumula por clave juego+SoC+
 * backend y solo sustituye los números por defecto cuando pasa los mínimos del
 * prompt (3 juegos × 5 ventanas × 2 sesiones). Mientras no los pasa, la política
 * sigue provisional y el log lo dice.
 */
final class DynamicResolutionGovernor {

    private static final long TICK_MS = 1000L;
    private static final float DROP_BELOW_PCT = 95f;   // por debajo: el juego va atrasado
    private static final float HOLDS_ABOVE_PCT = 99.5f; // por encima: va sobrado
    private static final int SLOW_TICKS_TO_DROP = 2;
    private static final int FAST_TICKS_TO_RAISE = 8;
    // ------------------------------------------------------------------
    // Cenit 0.6.30 (Fase 1 del prompt maestro): los umbrales de carga ya NO viven
    // aqui. Viven en LoadPolicy, que es la unica dueña de los numeros, y el
    // regidor los recibe calibrados desde la evidencia acumulada (LoadTrend).
    //
    // Por que todavia hay constantes GPU_* aqui abajo: las reglas PROPIAS del
    // regidor (cuando enganchar el turbo de cargas) no son criterios de clasifi-
    // cacion y todavia no tienen evidencia para calibrarse. Se quedan en su sitio
    // y etiquetadas como tales; lo que se borro es la copia de umbrales del
    // clasificador, que en 0.6.4 ya demostro lo que pasa cuando dos archivos
    // tienen "su propio" numero para la misma pregunta.
    // ------------------------------------------------------------------

    /** Umbral de "la metrica de GPU no existe todavia", compartido con el
     *  clasificador desde la MISMA politica (no dos constantes gemelas). */
    private static final float GPU_METRIC_MIN = (float) LoadPolicy.defaults().gpuMetricMissingPct;
    // Umbrales del turbo de cargas: ANTES valían 0,22 / 0,40 / 0,05 creyendo
    // que la métrica era fracción, así que "GPU muerta" pedía un 0,22% real
    // (imposible salvo en un menu) y "carga terminada" un 0,40% (se cumplía
    // siempre, liberando el turbo al tick siguiente). En porcentaje: una carga
    // real deja la GPU dibujando casi nada (< 22% de un cuadro que además no se
    // presenta), y el juego de verdad vuelve con la GPU trabajando (>= 40%).
    private static final float LOAD_GPU_MAX = 22f;
    private static final float LOAD_END_GPU = 40f;
    // "Hay métrica de GPU que leer" para la regla del turbo: el mismo 0,5 que
    // usa el clasificador, otra vez en la unidad real (antes era el 0,05
    // heredado del error de unidad, que se cumplía con casi cualquier cosa).
    private static final float LOAD_GPU_ALIVE = GPU_METRIC_MIN;
    // Escalones de PowerManager.THERMAL_STATUS_*: MODERATE y más arriba significan
    // que el SoC ya está recortando frecuencias por calor.
    private static final int THERMAL_BLOCK_RAISE = 2; // THERMAL_STATUS_MODERATE
    // v3 pre-corte: deriva de velocidad sostenida que huele a throttling mudo.
    private static final int TREND_WINDOW = 12;      // ticks de historia (12 s)
    private static final float TREND_DROP_PCT = 6f;  // media últimos 4 vs últimos 12
    private static final int PRECLIMITS_PER_GAME = 2;
    // v3 turbo de cargas: GPU casi ociosa + velocidad clavada, N segundos.
    // (Los umbrales de GPU de esta regla viven arriba, ya en %.)
    private static final int LOAD_TICKS_TO_TURBO = 4;
    private static final int TURBO_MAX_SECONDS = 45; // seguro: si la lectura miente, se suelta
    private static final int TURBO_REARM_TICKS = 6;
    // Los mismos escalones que ofrece el menú: una sola fuente de verdad. Si el
    // regidor tuviera lista propia, bajar desde un techo alto saltaría de 8x a 4x.
    private static final float[] STEPS = SettingsScreenController.SCALE_VALUES;

    interface Host {
        boolean isGameRunning();
        boolean isTimeScaled(); // fast-forward u otro mando de velocidad activo
        void applyUpscale(float value);
        /** URI del juego en curso para la memoria por-juego ("" = ninguno). */
        String currentGameUri();
    }

    private final Context appContext;
    private final SharedPreferences prefs;
    private final Host host;
    private final Handler main = new Handler(Looper.getMainLooper());

    private boolean active;
    private int slowTicks, fastTicks, cooldownTicks;
    // Escala realmente aplicada; 0 = ninguna todavía (se parte del techo).
    private float applied = 0f;
    // Techo que el regidor recuerda, para detectar cuando el usuario lo mueve.
    private float rememberedCeiling = 0f;
    // Si un paso aplicado no llega a la resolución efectiva (los ajustes por juego
    // pueden tener su propio upscale_multiplier, que manda sobre el INI global), el
    // regidor se rinde para esta sesión en vez de bajar escalones contra una pared.
    private float pendingApply = 0f;
    private int pendingTicks = 0;
    private boolean selfDisabled = false;
    // Regidor v2: estado térmico visto por última vez (-2 = sin señal, p. ej.
    // API < 29). -1 = normal.
    //
    // Las APIs térmicas se usan POR REFLEXIÓN a propósito: add/remove-
    // ThermalStatusChangedListener fallan de compilar contra algunos android.jar
    // del runner (métodos que sí existen en runtime, resueltos de forma
    // inconsistente según la plataforma instalada). Con reflexión el código
    // compila contra cualquier SDK y, si el método no está en runtime (API 26-28
    // o ROM recortada), el regidor trabaja sin señal térmica. Nunca revienta.
    private final android.os.PowerManager powerManager;
    private final java.lang.reflect.Method thermalAdd;
    private final java.lang.reflect.Method thermalRemove;
    private final java.lang.reflect.Method thermalGet;
    private final Class<?> thermalListenerClass;
    private Object thermalListener; // instancia del listener, vía Proxy
    private int lastThermalStatus = -2;
    // Para no repetir el aviso de "es CPU, no GPU" en cada tick.
    private boolean cpuBoundLogged = false;
    // 0.6.13: igual, pero para el aviso de "métrica de GPU ausente, no adivino".
    private boolean gpuMetricLogged = false;
    // 0.6.14: tercera etiqueta de estado, para que el log diga GPU_BOUND al
    // empezar a acumular ticks lentos y no solo cuando ya bajó la escala.
    private boolean gpuBoundLogged = false;
    // 0.6.29: cuarta etiqueta, UNKNOWN_LOAD — evidencia intermedia que no
    // autoriza a recortar. Sin este flag el log se repetiría en cada tick.
    private boolean unknownHoldLogged = false;
    // 0.6.30: quinta etiqueta, THERMAL_BOUND. Existe separada de las otras cuatro
    // porque es el unico estado donde NO se aprende nada: si se reutilizara el
    // flag de CPU_BOUND, un throttle a mitad de partida impediría imprimir el
    // CPU_BOUND real que venga despues de enfriar.
    private boolean thermalLogged = false;

    // ------------------------------------------------------------------
    // 0.6.30 (Fase 1): trend de la partida + política del clasificador
    // ------------------------------------------------------------------
    /**
     * Anillo de la partida en curso (medianas, P95 propio, rachas sostenidas,
     * descartes) y calibracion acumulada entre juegos. Dura lo que el objeto del
     * regidor; la calibracion se salva a prefs en stop() y se lee en start().
     */
    private final LoadTrend trend = new LoadTrend();
    /**
     * Politica activa, reconstruida cuando la evidencia alcanza los minimos del
     * prompt (3 juegos x 5 ventanas x 2 sesiones). Mientras no los alcance sigue
     * provisional, que es la unica manera honesta de decir "estos umbrales son mi
     * criterio, no una medicion".
     */
    private LoadPolicy mPolicy = LoadPolicy.defaults();

    // ------------------------------------------------------------------
    // v3: memoria, pre-corte térmico, turbo de cargas y evidencia de cuotas
    // ------------------------------------------------------------------
    private AdaptiveProfile profile;
    private boolean adaptiveOn;   // "recordar rendimiento" del usuario
    private boolean autoTurboOn;  // "turbo en cargas" del usuario
    // Pre-corte: anillo con las últimas velocidades medidas (0 = hueco).
    private final float[] speedRing = new float[TREND_WINDOW];
    private int speedRingPos = 0, speedRingFill = 0;
    private int preclimsUsed = 0;
    // Turbo de cargas.
    private int loadTicks = 0;        // ticks seguidos con pinta de carga
    private boolean turboOn = false;  // limitador Turbo enganchado por el regidor
    private int turboSeconds = 0;
    private int turboCooldown = 0;    // no re-entrar inmediatamente tras soltar
    // Contadores para el perfil.
    private int cpuBoundTicks = 0;
    // 0.6.27 (governor v4): el sintonizador CPU/VU por juego — la parte del
    // regidor que ACTÚA cuando el cuello no es la GPU (benchmark conservador de
    // EECycleRate/EECycleSkip/MTVU, rollback y memoria). Vive de este reloj de
    // 1 s: el regidor le pasa el contexto que ya midió y él responde si manda
    // (CONTINUE = mantener la escala quieta mientras ensaya) o no (IDLE).
    private final CpuVUTuner tuner;

    // 0.6.27: el sintonizador CPU/VU pide reentrar YA cuando cambia de fase en
    // este mismo tick. Un retry corto sobre el MISMO reloj: nunca dos relojes.
    private boolean wantRetry = false;

    private final Runnable tick = new Runnable() {
        @Override public void run() {
            if (!active) return;
            evaluate();
            main.postDelayed(this, wantRetry ? 250L : TICK_MS);
            wantRetry = false;
        }
    };

    DynamicResolutionGovernor(Context context, Host host) {
        final Context app = context.getApplicationContext();
        this.appContext = app;
        this.prefs = app.getSharedPreferences("app_prefs", Context.MODE_PRIVATE);
        this.host = host;
        this.tuner = new CpuVUTuner(app, this.prefs);
        android.os.PowerManager pm = null;
        try { pm = (android.os.PowerManager) app.getSystemService(Context.POWER_SERVICE); }
        catch (Throwable ignored) {}
        this.powerManager = pm;
        java.lang.reflect.Method add = null, remove = null, get = null;
        Class<?> iface = null;
        if (pm != null && android.os.Build.VERSION.SDK_INT >= 29) {
            try {
                iface = Class.forName(
                        "android.os.PowerManager$OnThermalStatusChangedListener");
                add = android.os.PowerManager.class.getMethod(
                        "addThermalStatusChangedListener", iface);
                remove = android.os.PowerManager.class.getMethod(
                        "removeThermalStatusChangedListener", iface);
                get = android.os.PowerManager.class.getMethod("getCurrentThermalStatus");
            } catch (Throwable ignored) {
                add = remove = get = null;
                iface = null;
            }
        }
        thermalAdd = add;
        thermalRemove = remove;
        thermalGet = get;
        thermalListenerClass = iface;
    }

    /** API 29+: el sistema avisa cuando recorta frecuencias por calor. */
    private void attachThermalListener() {
        if (thermalListener != null || thermalAdd == null || thermalListenerClass == null)
            return;
        try {
            final Object listener = java.lang.reflect.Proxy.newProxyInstance(
                    thermalListenerClass.getClassLoader(),
                    new Class<?>[]{thermalListenerClass},
                    (proxy, method, args) -> {
                        if ("onThermalStatusChanged".equals(method.getName())
                                && args != null && args.length == 1) {
                            // Entrega en el hilo principal; re-programar el tick
                            // aquí es barato y evita asumir el contrato.
                            lastThermalStatus = ((Number) args[0]).intValue();
                            if (active) {
                                main.removeCallbacks(tick);
                                main.postDelayed(tick, 200L);
                            }
                        }
                        return null;
                    });
            thermalAdd.invoke(powerManager, listener);
            thermalListener = listener;
            final Object cur = thermalGet.invoke(powerManager);
            lastThermalStatus = (cur instanceof Number) ? ((Number) cur).intValue() : -1;
        } catch (Throwable ignored) {
            thermalListener = null;
        }
    }

    private void detachThermalListener() {
        if (thermalListener == null) return;
        if (thermalRemove != null) {
            try { thermalRemove.invoke(powerManager, thermalListener); } catch (Throwable ignored) {}
        }
        // Sin nullear, el próximo start() creería que sigue enganchado.
        thermalListener = null;
    }

    /** true = el SoC está recortando por temperatura: no subir nunca de escala. */
    private boolean thermalLimited() {
        return lastThermalStatus >= THERMAL_BLOCK_RAISE;
    }

    /** Se llama cuando el juego arranca. Idempotente. */
    void start() {
        if (active) return;
        active = true;
        sCurrent = this; // Ajustes necesita poder hablarle al regidor vivo
        // 0.6.30 (Fase 1.8): el cuadro de instrumentos se pone a cero ANTES del
        // primer tick. Sin esto, los ~600 ms que tardan el arranque y la primera
        // ventana en llenarse se veria el LOAD del disco anterior (o el veredicto
        // de hace dos partidas) pintado como si fuera de esta, y eso es justo el
        // numero viejo que hace confiar en una decision que no se tomo.
        NativeApp.safeResetCenitLoadVerdict();
        selfDisabled = false; // juego nuevo, capa por juego nueva: otra oportunidad
        attachThermalListener(); // sin-op si no hay señal térmica accesible
        adaptiveOn = prefs.getBoolean("adaptive_perf", true);
        autoTurboOn = prefs.getBoolean("auto_turbo", false);
        profile = adaptiveOn
                ? new AdaptiveProfile(appContext, hostCurrentUri())
                : new AdaptiveProfile(null, ""); // objeto vacío, sin guardar
        java.util.Arrays.fill(speedRing, 0f);
        speedRingPos = 0; speedRingFill = 0;
        preclimsUsed = 0;
        loadTicks = 0; turboOn = false; turboSeconds = 0; turboCooldown = 0;
        cpuBoundTicks = 0;
        unknownHoldLogged = false;
        thermalLogged = false;
        // 0.6.30 (Fase 1.3/1.7): juego nuevo, anillo nuevo; la CALIBRACION no,
        // porque vive indexada por clave y mezclar discos en el anillo de "sostenido"
        // seria inventar patrones. Se reanuda la acumulada en prefs y se reconstruye
        // la politica solo si la evidencia ya pasa los minimos del prompt.
        trend.clearRing();
        trend.deserialize(prefs.getString(CAL_PREF_KEY, ""));
        mPolicy = refreshPolicy();
        // Un reloj con la politica y el anillo nuevos: sin esto, el primer tick
        // clasifica con la politica del juego anterior.
        sessionStartMs = System.currentTimeMillis();
        // 0.6.27: procesar el diario de un benchmark que el cierre de la app
        // dejó a medias ANTES de tocar la escala (revertir speedhacks globales
        // colgados es lo único urgente; el benchmark nuevo, si viene, empieza
        // con su propia evidencia).
        tuner.startSession();
        reset();
        main.postDelayed(tick, TICK_MS);
    }

    /**
     * 0.6.30: reconstruye la politica a partir de la calibracion acumulada.
     * Devuelve la default (provisional=true) mientras no haya evidencia, y deja
     * constar en el log cuando si la hay: cambiar un umbral con medicion tiene que
     * quedar escrito, no quedarse dentro de un objeto.
     */
    private LoadPolicy refreshPolicy() {
        final LoadPolicy suggested = trend.suggestPolicy();
        if (suggested.provisional) return LoadPolicy.defaults();
        if (Math.abs(suggested.gpuHotPct - mPolicy.gpuHotPct) > 0.01
                || Math.abs(suggested.emuHotPct - mPolicy.emuHotPct) > 0.01
                || Math.abs(suggested.gpuIdlePct - mPolicy.gpuIdlePct) > 0.01) {
            android.util.Log.i("DynRes", "policy calibrated from evidence: gpu_hot "
                    + mPolicy.gpuHotPct + "->" + suggested.gpuHotPct
                    + " ee_hot " + mPolicy.emuHotPct + "->" + suggested.emuHotPct
                    + " gpu_idle " + mPolicy.gpuIdlePct + "->" + suggested.gpuIdlePct
                    + " (juegos=" + trend.calibrationKeyCount() + ")");
        }
        return suggested;
    }

    /** Al apagar el juego o cerrar la app: deja la escala del usuario aplicada. */
    void stop() {
        if (!active) return;
        active = false;
        if (sCurrent == this) sCurrent = null;
        main.removeCallbacks(tick);
        // El mismo cuadro se borra al cerrar: si el usuario vuelve al menu, el
        // overlay no debe seguir mostrando el veredicto de un juego que ya no esta
        // corriendo. Va aqui y no solo en start() porque el cierre puede ser lo
        // ultimo que ocurra antes de que el proceso muera (congelacion por el
        // sistema, deslizar arriba, matar la app): lo que se deja publicado es lo
        // que se vera al volver.
        NativeApp.safeResetCenitLoadVerdict();
        final float ceiling = ceiling();
        if (applied > 0f && applied < ceiling - 0.001f) host.applyUpscale(ceiling);
        applied = 0f;
        releaseTurbo("game stopped");
        detachThermalListener();
        // 0.6.27: un ensayo a medias se revierte y el veredicto del último se
        // anota en el perfil ANTES de salvarlo (mismo hilo, mismo instante).
        tuner.onGameStop(profile);
        // v3: el aprendizaje de esta partida se guarda con el juego.
        if (profile != null && profile.known()) profile.save();
        // 0.6.30 (Fase 1.7): la calibracion viaja por clave (juego + telefono +
        // backend), NO por URI de partida, y se salva aunque el usuario tenga
        // "recordar rendimiento" apagado: lo que se acumula aqui no es una decision
        // sobre SU juego, es evidencia de umbrales. Se guarda siempre y se puede
        // borrar entero desde Ajustes; nunca se usa para recortar sin pasar los
        // minimos del prompt.
        saveCalibration();
    }

    /** Clave de prefs donde vive la calibracion de umbrales (acumulada). */
    static final String CAL_PREF_KEY = "load_cal_v1";

    /** Identificador de sesion: un arranque de partida = una sesion. */
    private long sessionStartMs = 0L;

    /**
     * Id de sesion para LoadTrend.observe(). Se usa el momento del start(), no un
     * contador: el minimo del prompt son DOS sesiones separadas por juego, y un
     * numero que solo aumenta dentro de la misma partida no serviria para saber si
     * la segunda fue otro dia.
     */
    private String appSessionId() { return Long.toString(sessionStartMs); }

    /**
     * Clase del SoC, leida por REFLEXION a proposito. Build.SOC_MODEL es API 31 y
     * el SDK con el que compila CI no siempre lo resuelve igual (es exactamente el
     * motivo por el que las APIs termicas ya se leian asi desde 0.6.19). Si el
     * campo no esta, se cae a MANUFACTURER/MODEL, que existen desde API 1: la
     * clave de calibracion puede ser menos especifica en un telefono viejo, nunca
     * puede reventar la partida.
     */
    private static String socClass() {
        for (String field : new String[]{"SOC_MODEL", "SOC_MANUFACTURER"}) {
            try {
                final Object v = android.os.Build.class.getField(field).get(null);
                if (v instanceof String && !((String) v).isEmpty()) return (String) v;
            } catch (Throwable ignored) {
                // campo inexistente en este SDK/runtime: seguir bajando
            }
        }
        return android.os.Build.MANUFACTURER + "/" + android.os.Build.MODEL;
    }

    /**
     * Clave de calibracion de la Fase 1.7: serial del disco (no el nombre del
     * archivo: el mismo juego descargado dos veces es el mismo disco) + SoC +
     * backend Grafico elegido. Con esto, la evidencia de un Snapdragon 7 con
     * Turnip no le cambia los umbrales a un Exynos con otro driver, que es justo
     * lo que hay que poder separar cuando lleguen los demas telefonos.
     */
    private String calibrationKey() {
        final StringBuilder sb = new StringBuilder(48);
        String serial = "";
        try { serial = GameSerialUtils.serialFromUri(hostCurrentUri()); } catch (Throwable ignored) {}
        if (serial == null || serial.isEmpty()) {
            try { serial = NativeApp.getCurrentGameSerial(); } catch (Throwable ignored) {}
        }
        sb.append(serial == null || serial.isEmpty() ? "sin-serial" : serial).append('#');
        sb.append(socClass()).append('#').append(prefs.getInt("renderer", -1));
        return sb.toString();
    }

    /**
     * 0.6.30: el regidor en curso, para que Ajustes pueda hablarle sin pasar por
     * la actividad. Hay UN solo regidor por MainActivity y su vida la manda el
     * juego (start/stop), así que la referencia es simplemente "el que esta vivo
     * ahora"; se limpia en stop() y solo si es este, para no borrar por error el
     * del siguiente arranque.
     */
    private static volatile DynamicResolutionGovernor sCurrent;

    /**
     * Olvidar la calibracion de umbrales acumulada (Fase 1.7) en TODAS sus copias:
     * la de prefs, la del anillo en memoria y la politica que de ella se derivaba.
     *
     * Hacerlo a medias es peor que no hacerlo: si alguien (o esta clase) borrara
     * solo la preferencia, el regidor vivo resalvaria su copia en el proximo
     * stop() y el boton pareceria mentira. Y si solo se limpiara la memoria, la
     * preferencia vieja volveria en el proximo start(). Por eso este metodo hace
     * las tres cosas y se llama desde el unico sitio que tiene las tres a mano.
     *
     * @return true si habia evidencia que borrar (para que el aviso no mienta).
     */
    static boolean forgetCalibration(Context context) {
        final SharedPreferences p = context.getApplicationContext()
                .getSharedPreferences("app_prefs", Context.MODE_PRIVATE);
        final String saved = p.getString(CAL_PREF_KEY, "");
        final DynamicResolutionGovernor g = sCurrent;
        final int liveKeys = g != null ? g.trend.calibrationKeyCount() : 0;
        final boolean had = !saved.isEmpty() || liveKeys > 0;
        p.edit().remove(CAL_PREF_KEY).apply();
        if (g != null) g.forgetCalibrationLocked();
        android.util.Log.i("DynRes", "load-threshold calibration cleared by user"
                + " (claves en memoria=" + liveKeys + ")");
        return had;
    }

    /** Borra la copia viva; la politica vuelve a ser la provisional de siempre. */
    private void forgetCalibrationLocked() {
        trend.clearCalibration();
        mPolicy = refreshPolicy();
    }

    /** Salva la calibracion acumulada. Sin contexto de prefs, es sin-op. */
    private void saveCalibration() {
        final String data = trend.serialize();
        if (data.isEmpty() && prefs.getString(CAL_PREF_KEY, "").isEmpty()) return;
        prefs.edit().putString(CAL_PREF_KEY, data).apply();
    }

    /**
     * Snapshot de la ventana, con el fallback que pide la Regla 5 del prompt.
     *
     * Si el nativo no contesta (binario ausente, JNI sin registrar, fallo en la
     * copia del array) se construye la muestra con CERO cruce JNI y el clasificador
     * respondera INVALID_SAMPLE: el regidor se queda quieto y el HUD lo dice. La
     * alternativa — leer los getters sueltos de 0.6.29 — estaria comparando metricas
     * de momentos distintos, que es el defecto que esta version viene a cerrar. No
     * hay nada que perder: sin snapshot tampoco hay clasificacion util.
     */
    private CenitLoadSample readSample() {
        final double[] raw = NativeApp.safeGetCenitSample();
        if (raw != null) return new CenitLoadSample(raw);
        return CenitLoadSample.invalid("el nativo no devolvio snapshot");
    }

    /** El usuario tocó algo que invalida la medición: olvidar y volver al techo. */
    void reset() {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            main.post(this::reset);
            return;
        }
        slowTicks = 0;
        fastTicks = 0;
        cooldownTicks = 0;
        pendingApply = 0f;
        pendingTicks = 0;
        rememberedCeiling = ceiling();
        if (applied > 0f) {
            host.applyUpscale(rememberedCeiling);
            applied = 0f;
        }
    }

    private String hostCurrentUri() {
        try { return host.currentGameUri(); } catch (Throwable t) { return ""; }
    }

    private float ceiling() {
        // Cenit 0.6.25: el piso del techo deja de ser 1x. Si el usuario eligió
        // 0.5x o 0.75x en el menú, ese es su techo: con Math.max(1f, ...) el
        // governor podía subirle la resolución por encima de lo que él puso.
        // STEPS[0] es el escalón más bajo de la lista compartida con Ajustes.
        return Math.max(STEPS[0], prefs.getFloat("upscale_multiplier", 1f));
    }

    private static boolean isVmPaused() {
        if (NativeApp.hasNoNativeBinary) return true;
        try { return NativeApp.isPaused(); } catch (Throwable t) { return true; }
    }

    /**
     * Cenit 0.6.29: esta función (boundThreshold) ya no existe. Hacía la misma
     * pregunta que ahora contesta GameLoadClassifier.classify —"¿este retraso es
     * de píxeles?"— pero con una constante absoluta en la unidad equivocada y con
     * el ajuste de evidencia al revés (×0.8, más difícil de declarar CPU-bound).
     * La evidencia aprendida sigue usándose, ahora en la dirección correcta:
     * ensancha el umbral de "GPU holgada" (policy.withGpuIdlePct, ver evaluate()).
     */
    private void evaluate() {
        // ¿El último paso llegó realmente a la resolución en uso? Si no, es que los
        // ajustes por juego fijan su propia escala y mandan sobre el INI global:
        // insistir solo haría bajar escalones sin efecto. Rendirse una vez, en seco.
        // Se esperan dos ticks: la aplicación viaja por un executor asíncrono y
        // juzgarla demasiado pronto cerraría el regidor por un falso positivo.
        if (pendingApply > 0f) {
            if (pendingTicks > 0) {
                pendingTicks--;
                return;
            }
            final float effective = NativeApp.safeGetEffectiveUpscale();
            final float want = pendingApply;
            pendingApply = 0f;
            if (effective > 0f && Math.abs(effective - want) > 0.01f) {
                selfDisabled = true;
                android.util.Log.i("DynRes", "effective upscale " + effective
                        + "x ignores requested " + want + "x (per-game settings); disabling");
                return;
            }
        }
        if (selfDisabled) {
            // 0.6.27: sin reloj no hay benchmark posible: abortar con reversión.
            tuner.halt("governor self-disabled");
            return;
        }
        // Con la CPU emulada a otra velocidad, medir "porcentaje de velocidad" deja
        // de significar "el teléfono no da abasto": el juego va lento porque el
        // usuario lo pidió. Congelar el regidor hasta que vuelva al 100%.
        // 0.6.12: se mira el valor EFECTIVO (capa por-juego incluida), no solo la
        // preferencia global — un -1 guardado en el INI de SOTC debe congelar igual,
        // y un 0 por-juego que anule un global viejo no puede dejar esto dormido.
        // La lectura nativa es barata (un entero); el fallback ante cualquier fallo
        // es la preferencia global de siempre.
        // 0.6.27 (governor v4): el congelamiento ahora distingue QUIÉN movió el
        // rate. Si el sintonizador CPU/VU está midiendo ahora mismo, un rate != 0
        // es SU ensayo: congelarse aquí congelaría su propio benchmark (y el
        // governor quedaría dormido justo cuando más se le necesita). Con el
        // sintonizador en vuelo se salta el congelamiento; el tick() de abajo
        // devuelve CONTINUE y las reglas clásicas no tocan la escala mientras se
        // mide. Cuando el ensayo termina pasa una de dos cosas: el global vuelve
        // a la línea base del usuario (rate 0: el regidor retoma solo), o el
        // perfil adoptado queda en el INI por-juego (rate != 0: el regidor se
        // congela por una decisión consciente — medir speed% con cuotas puestas
        // es mentira, como siempre se dijo).
        final boolean tunerActive = tuner.isActive();
        final int effectiveRate = NativeApp.safeGetEffectiveEECycleRate();
        if (!tunerActive && (effectiveRate != 0 || (NativeApp.hasNoNativeBinary
                && prefs.getInt("ee_cycle_rate", 0) != 0))) {
            if (applied > 0f) {
                host.applyUpscale(ceiling());
                applied = 0f;
            }
            slowTicks = 0;
            fastTicks = 0;
            return;
        }
        if (!prefs.getBoolean("dynamic_res", true)) {
            // 0.6.27: "dynamic_res" apagado manda sobre todo el regidor, y el
            // sintonizador corre en su reloj: cerrar cualquier ensayo con
            // reversión antes de quedarse en calma.
            tuner.halt("dynamic_res switched off");
            // Apagado a mitad de partida: devolver el techo y quedarse en calma,
            // listo para retomar si el interruptor vuelve.
            if (applied > 0f) {
                final float top = ceiling();
                if (applied < top - 0.001f) host.applyUpscale(top);
                applied = 0f;
            }
            slowTicks = 0;
            fastTicks = 0;
            return;
        }
        if (!host.isGameRunning() || host.isTimeScaled() || isVmPaused()) {
            // Con mando de velocidad o con el juego en pausa la medición pierde
            // sentido: congelar. Soltar el turbo regidor si estuviera enganchado:
            // el usuario (o la pausa) mandan sobre nuestra decisión.
            // 0.6.27: la pausa también congela el ensayo del sintonizador — y si
            // se alarga demasiado, él solo aborta y revierte (onFrozen).
            slowTicks = 0;
            fastTicks = 0;
            releaseTurbo("input/ pause froze governor");
            tuner.onFrozen();
            return;
        }
        final float ceiling = ceiling();
        if (Math.abs(ceiling - rememberedCeiling) > 0.001f) {
            rememberedCeiling = ceiling;
            reset();
            return;
        }
        if (cooldownTicks > 0) {
            cooldownTicks--;
            if (turboCooldown > 0) turboCooldown--;
            return;
        }
        if (applied <= 0f) {
            // v3 arranque con memoria: si este juego ya sostuvo una escala más
            // baja, se parte de ella (el usuario puede subir a mano y el reset
            // de techo manda). Sin perfil conocido, se parte del techo como antes.
            float startAt = ceiling;
            // 0.6.25: el umbral del perfil aprendido deja de ser 1x literal —
            // ahora es el escalón más bajo de la lista, para que un juego
            // aprendido en 0.75x también arranque directo en esa escala.
            if (profile != null && profile.heldScale > STEPS[0] + 0.001f && profile.heldScale < ceiling - 0.001f)
                startAt = profile.heldScale;
            applied = startAt;
            if (startAt < ceiling - 0.001f) {
                android.util.Log.i("DynRes", "starting learned scale " + startAt + "x for this game");
                pendingApply = startAt;
                pendingTicks = 2;
                host.applyUpscale(startAt);
                cooldownTicks = 2;
            }
            return;
        }

        // ==================================================================
        // Cenit 0.6.30 (Fase 1 del prompt maestro): UNA lectura por tick.
        //
        // Antes el regidor hacia ocho llamadas sueltas (speed, gpu, ee, gpuMs,
        // vertical, waitMs, waitCalls, upscale) y cada una caia en el instante
        // que le tocaba. PerformanceMetrics::Update renueva las metricas cada
        // 0,5 s y el tick es de 1 s, asi que la mayoria de los ticks leia todo de
        // la misma ventana — pero cuando un Update caia EN MEDIO de la ristra, el
        // tick mezclaba la GPU de una ventana con el EE de la anterior y el
        // clasificador comparaba dos etapas de dos intervalos distintos. El
        // prompt lo pide explicito: "Las metricas GPU y emulacion deben pertenecer
        // al mismo intervalo temporal". getCenitSample() devuelve el snapshot de
        // un mismo Update: ya no es una coincidencia temporal, es un hecho.
        // ==================================================================
        final CenitLoadSample sample = readSample();

        // Evidencia para el log, no decision: gpuMs puede venir NaN si la ventana
        // no tuvo presents y el "presupuesto" 1000/Hz no es comparable campo a
        // campo en entrelazado (GameLoadClassifier y el contrato explican por que).
        // Se declaran float para no tocar el tipo de todo lo de abajo (el anillo de
        // velocidad, el sintonizador y el turbo siguen en float): la diferencia de
        // precision no cambia una decision, y aqui lo que importa es la unidad.
        final float speed = (float) sample.speedPct();
        final float gpu = (float) sample.gpuPct();
        final float eePct = (float) sample.eePct();

        // La muestra entra al trend ANTES de clasificar: el clasificador pregunta
        // por rachas sostenidas, y la racha de este tick cuenta. add() responde
        // false cuando la tiro (pausa, turbo, cambio de escala, calor, sin
        // presents, NaN o ventana recortada) — en ese caso tampoco se suma a la
        // calibracion, que es justo lo que hay que no contaminar.
        //
        // Con un ensayo del sintonizador en vuelo NO se alimenta el trend: el
        // speed% de un ensayo esta bajo a PROPOSITO, y mezclarlo con la partida
        // real daria medianas y rachas de datos de prueba. Es la misma regla con la
        // que 0.6.27 ya protege heldScale y slowFloorTicks, ahora tambien para la
        // calibracion de umbrales.
        final boolean tunerMeasuring = tuner.isActive();
        final boolean counted = !tunerMeasuring && trend.add(sample, isVmPaused(), turboOn,
                pendingApply > 0f, lastThermalStatus);
        if (counted)
            trend.observe(calibrationKey(), appSessionId(), sample);

        // Politica: la calibrada por evidencia (si la hay) mas el ensanche que
        // aporta el perfil de ESTE juego. Con la evidencia aprendida de que el
        // disco es de emulacion, el piso de "GPU holgada" SUBE (x1,2), corregido
        // en 0.6.29: con *0.8 era mas dificil seguir declarandolo y el juego
        // volvia a recibir recortes que no le servian.
        LoadPolicy policy = mPolicy;
        if (profile != null && profile.cpuBound)
            policy = policy.withGpuIdlePct(policy.gpuIdlePct * 1.2);

        final GameLoadClassifier.Verdict verdict =
                GameLoadClassifier.classify(sample, policy, lastThermalStatus, trend);

        // Fase 1.8: el veredicto se publica SIEMPRE, antes de cualquier guarda que
        // haga retornar el tick. Si no, el HUD mostraria el veredicto de hace
        // minutos justo en los momentos donde el regidor no actua, que son los
        // que alguien tiene que poder verificar desde el sofa.
        NativeApp.safeSetCenitLoadVerdict(verdict.stateOrdinal(), verdict.confidencePct,
                verdict.actionCode(), gpu, eePct, lastThermalStatus);

        // ---------------------------------------------------------------
        // 0.6.27 (governor v4): el sintonizador CPU/VU se consulta AQUÍ, con el
        // contexto que el regidor ya midió y DELANTE de las contadoras de
        // aprendizaje: un ensayo con Ciclo EE -1 o cuota baja el speed% a ~75
        // POR DISEÑO (el objetivo de framerate no se re-escala), así que si
        // esas lecturas entraran en heldScale/slowFloorTicks/tendencia
        // contaminarían la memoria del juego con datos de prueba. Mientras el
        // ensayo dura (CONTINUE) el regidor no aprende, no sube y no baja.
        // ---------------------------------------------------------------
        final boolean behind = speed > 0f && speed < DROP_BELOW_PCT;
        final boolean atFloor = applied <= STEPS[0] + 0.001f;
        // Metrica viva = el clasificador tuvo muestra usable (valida y con
        // velocidad real). Es la definicion del contrato, no "gpu > 0,5": con
        // EE valido y GPU ausente si hay metrica que leer, y antes esto cerraba
        // benchmarks que iban bien.
        final boolean metricsAlive = verdict.state != GameLoadClassifier.LoadState.INVALID_SAMPLE
                && speed > 0f;
        // Cuello de emulacion (o VU esperando): lo que necesita el sintonizador.
        // Se lee de la ACCION, no del estado: Regla 5 del prompt — una CPU_BOUND
        // con confianza insuficiente ya no es "el cuello es la emulacion", es
        // "todavia no lo sabemos", y no debe encender un benchmark.
        final boolean cpuBoundNow = verdict.action == GameLoadClassifier.Action.BLOCK_RESOLUTION_DROP
                && (verdict.state == GameLoadClassifier.LoadState.CPU_BOUND
                    || verdict.state == GameLoadClassifier.LoadState.VU_BOUND);
        final int tunerAction = tuner.tick(profile, hostCurrentUri(), applied,
                behind, atFloor, cpuBoundNow, metricsAlive,
                turboOn, pendingApply > 0f, thermalLimited());
        if (tunerAction == CpuVUTuner.RETRY) wantRetry = true;
        if (tunerAction == CpuVUTuner.CONTINUE) {
            slowTicks = 0;
            fastTicks = 0;
            return;
        }

        // ---------------------------------------------------------------
        // v3: contadores de aprendizaje y tendencia (corren en todo tick
        // con métricas válidas, pase lo que pase con las reglas de subir/bajar)
        // ---------------------------------------------------------------
        if (speed > 0f) {
            pushSpeed(speed);
            if (profile != null && profile.known()) {
                if (speed > HOLDS_ABOVE_PCT) {
                    // Esta escala SÍ la sostuvo: memorizar la más baja sostenida.
                    if (profile.heldScale <= 0f || applied < profile.heldScale)
                        profile.heldScale = applied;
                }
                if (applied <= STEPS[0] + 0.001f && speed < DROP_BELOW_PCT) {
                    // "En el suelo de la lista y sigue atrasado": con los
                    // escalones sub-nativos el suelo ya no es 1x, pero la
                    // intentación (modo cuotas) sigue siendo la misma.
                    profile.slowFloorTicks++;
                }
            }
        }


        // ---------------------------------------------------------------
        // v3: TURBO DE CARGAS. Carga = GPU muerta + velocidad clavada. Con
        // auto-turbo y el juego conocido (o tras la primera detección), se
        // engancha el limitador Turbo y se suelta al volver el juego real.
        // ---------------------------------------------------------------
        if (gpu > LOAD_GPU_ALIVE && speed > 0f) {
            if (turboOn) {
                turboSeconds++;
                final boolean loadOver = gpu >= LOAD_END_GPU || speed < HOLDS_ABOVE_PCT;
                if (loadOver || turboSeconds >= TURBO_MAX_SECONDS) {
                    releaseTurbo(loadOver ? "load over" : "safety cap");
                    turboCooldown = TURBO_REARM_TICKS;
                }
            } else if (turboCooldown > 0) {
                turboCooldown--;
                loadTicks = 0;
            } else if (autoTurboOn
                    && gpu < LOAD_GPU_MAX
                    && speed >= HOLDS_ABOVE_PCT
                    && !thermalLimited()) {
                loadTicks++;
                // Con perfil aprendido se confía desde el segundo tick; si es la
                // primera vez que se ve esta carga, esperar un poco más.
                final int needed = (profile != null && profile.turboSeconds > 0) ? 2 : LOAD_TICKS_TO_TURBO;
                if (loadTicks >= needed) {
                    engageTurbo(); // el engagement mismo apunta el evento en el perfil
                }
            } else {
                loadTicks = 0;
            }
        }

        // ---------------------------------------------------------------
        // v3: PRE-CORTE térmico. Deriva de velocidad a la baja sin aviso
        // térmico formal => muchas ROM recortan frecuencia en silencio. Bajar
        // un paso ANTES del corte fuerte, máximo dos por partida.
        // ---------------------------------------------------------------
        if (speed > 0f && !thermalLimited() && preclimsUsed < PRECLIMITS_PER_GAME
                && speedRingFill >= TREND_WINDOW && applied >= ceiling - 0.001f) {
            final float recent = ringAverage(4);
            final float base = ringAverage(TREND_WINDOW);
            if (base > 0f && recent > 0f && (base - recent) >= TREND_DROP_PCT) {
                final float next = nextStepDown(applied, ceiling);
                if (next < applied - 0.001f) {
                    preclimsUsed++;
                    android.util.Log.i("DynRes", "silent throttling suspected (speed drift "
                            + base + "->" + recent + "); preemptive step down to " + next + "x");
                    applied = next;
                    pendingApply = next;
                    pendingTicks = 2;
                    host.applyUpscale(next);
                    cooldownTicks = 5; // vuelta larga: el pronóstico no debe aletear
                    java.util.Arrays.fill(speedRing, 0f);
                    speedRingFill = 0;
                    return;
                }
            }
        }

        // ---------------------------------------------------------------
        // Reglas clásicas (v2) de subida/bajada, ahora regidas por la MATRIZ de
        // la Fase 4 del prompt maestro: el dueno del tick es UN solo sistema y
        // cada veredicto autoriza un conjunto cerrado de acciones.
        //
        // Cenit 0.6.30 — lo que cambia respecto a 0.6.29:
        //  - La rama de recorte pide PERMISO, no evidencia: exige
        //    ALLOW_RESOLUTION_DROP. En 0.6.29 el regidor miraba el ESTADO y
        //    actuaba; la confianza se imprimia pero no mandaba nada (Regla 5).
        //  - Se separan los dos "no se" que estaban mezclados. INVALID_SAMPLE es
        //    "la metrica fallo" (hay que buscar la causa, y no se aprende nada);
        //    UNKNOWN_LOAD es "la metrica existe y no alcanza para concluir" (se
        //    espera). Antes los dos caian en la misma etiqueta y el FÁBRICA no
        //    podia distinguir un driver sin tiempo de GPU de un juego raro.
        //  - THERMAL_BOUND tiene rama propia. Es el unico caso donde bajar la
        //    resolucion NUNCA debe escribir cpuBound en el perfil: con el SoC
        //    recortando por calor, la emulacion SIEMPRE parece el cuello, y esa
        //    memoria se queda persiguiendo al juego cuando el telefono enfria.
        //
        // Las etiquetas "DynRes state: ..." siguen siendo grepeables y con las
        // mismas palabras donde el significado no cambio, para que los logs de
        // telefonos ajenos que ya tengo sigan siendo comparables con los nuevos.
        // ---------------------------------------------------------------
        if (speed > 0f && speed < DROP_BELOW_PCT) {
            // 0.6.29: tally del veredicto, UN tick por veredicto, y solo cuando hay
            // un retraso que diagnosticar. Se cuenta aqui y no arriba porque este
            // punto ya paso las dos guardas que invalidan la muestra: el CONTINUE
            // del sintonizador (un ensayo baja el speed% A PROPOSITO y sus lecturas
            // no hablan del juego) y las reglas de congelamiento. Contar un juego
            // que va a tiempo tampoco sirve: el clasificador devolveria UNKNOWN por
            // "no hay cuello", no por evidencia insuficiente, y eso llenaria la
            // casilla equivocada.
            //
            // 0.6.30: el indice es el codigo del CONTRATO (0..6, siete casillas),
            // no un "tallySlot" propio: el numero que se guarda en las prefs es el
            // mismo que pinta el HUD y el que traduce el C++, asi que no puede
            // discrepar entre los tres.
            if (profile != null && profile.known())
                profile.loadMix[verdict.stateOrdinal()]++;

            final boolean allowDrop = verdict.action == GameLoadClassifier.Action.ALLOW_RESOLUTION_DROP;
            final boolean blockDrop = verdict.action == GameLoadClassifier.Action.BLOCK_RESOLUTION_DROP;

            if (verdict.state == GameLoadClassifier.LoadState.THERMAL_BOUND) {
                // El SoC esta recortando por calor y el juego va atrasado: mantener
                // la escala, no empezar benchmark (el regidor ya se lo dijo al
                // sintonizador con thermal=true) y sobre todo NO aprender.
                slowTicks = 0;
                fastTicks = 0;
                cpuBoundTicks = 0; // un throttle ajeno no es evidencia de ESTE disco
                gpuBoundLogged = false;
                cpuBoundLogged = false;
                gpuMetricLogged = false;
                if (!thermalLogged) {
                    thermalLogged = true;
                    android.util.Log.i("DynRes", "DynRes state: THERMAL_BOUND — "
                            + GameLoadClassifier.evidence(verdict, sample)
                            + ": thermal limit, holding scale and not learning");
                }
            } else if (verdict.state == GameLoadClassifier.LoadState.INVALID_SAMPLE) {
                // La metrica no existe todavia (GS recien abierto, driver que no
                // reporta tiempo de GPU, ventana recortada o muestra repetida). No
                // fiarse para recortar: slowTicks en cero para que, cuando la
                // metrica despierte, se necesiten los ticks completos de retraso
                // real antes de mover nada.
                slowTicks = 0;
                fastTicks = 0;
                gpuBoundLogged = false;
                cpuBoundLogged = false;
                unknownHoldLogged = false;
                if (!gpuMetricLogged) {
                    gpuMetricLogged = true;
                    android.util.Log.i("DynRes", "DynRes state: INVALID_SAMPLE — "
                            + GameLoadClassifier.evidence(verdict, sample)
                            + ": metric missing, holding scale, not guessing");
                }
            } else if (verdict.state == GameLoadClassifier.LoadState.GPU_BOUND && allowDrop) {
                // Píxeles: recortar SÍ recupera cuadros, y la confianza del
                // clasificador alcanza para autorizarlo.
                cpuBoundLogged = false;
                gpuMetricLogged = false;
                unknownHoldLogged = false;
                thermalLogged = false;
                cpuBoundTicks = 0; // el retraso este vez sí era de píxeles: cuenta nueva
                fastTicks = 0;
                if (!gpuBoundLogged) {
                    gpuBoundLogged = true;
                    android.util.Log.i("DynRes", "DynRes state: GPU_BOUND — "
                            + GameLoadClassifier.evidence(verdict, sample)
                            + ": resolution can help");
                }
                slowTicks++;
                if (slowTicks >= SLOW_TICKS_TO_DROP) {
                    slowTicks = 0;
                    final float next = nextStepDown(applied, ceiling);
                    if (next < applied - 0.001f) {
                        applied = next;
                        pendingApply = next;
                        pendingTicks = 2;
                        host.applyUpscale(next);
                        cooldownTicks = 2;
                    } else {
                        cooldownTicks = 5; // ya está en el piso, no hay marcha atrás
                    }
                }
            } else if (blockDrop && (verdict.state == GameLoadClassifier.LoadState.CPU_BOUND
                        || verdict.state == GameLoadClassifier.LoadState.VU_BOUND)) {
                // El cuello es la emulación (EE/GS trabajándose el cuadro) o el EE
                // bloqueado esperando al VU1, con confianza suficiente para decirlo.
                // Bajar resolución en ambos casos solo estropea la imagen: mantener
                // la escala y dejar que el sintonizador CPU/VU, que ya recibió
                // cpuBoundNow, haga su parte.
                slowTicks = 0;
                fastTicks = 0;
                gpuBoundLogged = false;
                gpuMetricLogged = false;
                unknownHoldLogged = false;
                cpuBoundTicks++;
                if (profile != null && profile.known() && cpuBoundTicks >= 15)
                    profile.cpuBound = true; // fue consistente: que conste en el perfil
                if (!cpuBoundLogged) {
                    cpuBoundLogged = true;
                    android.util.Log.i("DynRes", "DynRes state: " + verdict.state + " — "
                            + GameLoadClassifier.evidence(verdict, sample)
                            + ": CPU-bound, holding scale");
                }
            } else {
                // UNKNOWN_LOAD, NO_BOTTLENECK retrasado, o una decision cuya
                // confianza NO llego al umbral de la accion (gate() la degrado a
                // WAIT_FOR_EVIDENCE). En todos los casos: no inventar. En el
                // catalogo entero, recortar de mas sale mas caro que dejar una
                // mejora sobre la mesa — y aqui es donde se ve, porque el log
                // imprime la razon del clasificador con la duda escrita.
                slowTicks = 0;
                fastTicks = 0;
                gpuBoundLogged = false;
                cpuBoundLogged = false;
                gpuMetricLogged = false;
                if (!unknownHoldLogged) {
                    unknownHoldLogged = true;
                    android.util.Log.i("DynRes", "DynRes state: UNKNOWN_LOAD — "
                            + GameLoadClassifier.evidence(verdict, sample)
                            + ": evidence insufficient, holding scale");
                }
            }
        } else if (speed > HOLDS_ABOVE_PCT && applied < ceiling - 0.001f) {
            // Con calor no se sube (haría justo lo que el límite térmico intenta
            // evitar). Bajar sigue permitido arriba.
            if (thermalLimited()) {
                slowTicks = 0;
                fastTicks = 0;
                return;
            }
            slowTicks = 0;
            fastTicks++;
            if (fastTicks >= FAST_TICKS_TO_RAISE) {
                fastTicks = 0;
                applied = nextStepUp(applied, ceiling);
                pendingApply = applied;
                pendingTicks = 2;
                host.applyUpscale(applied);
                cooldownTicks = 2;
            }
        } else {
            slowTicks = 0;
            fastTicks = 0;
            cpuBoundTicks = 0; // a tiempo: la racha de "CPU mandando" se corta
        }
    }

    // ------------------------------------------------------------------
    // v3 helpers
    // ------------------------------------------------------------------

    private void engageTurbo() {
        if (turboOn) return;
        turboOn = true;
        turboSeconds = 0;
        android.util.Log.i("DynRes", "loading screen detected: limiter -> Turbo");
        NativeApp.setLimiterModeAsync(1); // 1 = Turbo; restore lo devuelve a Nominal
        if (profile != null) profile.turboSeconds++;
    }

    private void releaseTurbo(String reason) {
        if (!turboOn) return;
        turboOn = false;
        android.util.Log.i("DynRes", "turbo released (" + reason + "): limiter -> Nominal");
        NativeApp.setLimiterModeAsync(0); // Nominal: si el usuario puso FF, su estado no pasa por aquí
    }

    private void pushSpeed(float s) {
        speedRing[speedRingPos] = s;
        speedRingPos = (speedRingPos + 1) % TREND_WINDOW;
        if (speedRingFill < TREND_WINDOW) speedRingFill++;
    }

    private float ringAverage(int lastN) {
        if (lastN > speedRingFill) lastN = speedRingFill;
        if (lastN <= 0) return 0f;
        float sum = 0f;
        int count = 0;
        for (int i = 1; i <= lastN; i++) {
            int idx = (speedRingPos - i + TREND_WINDOW * 2) % TREND_WINDOW;
            if (speedRing[idx] > 0f) {
                sum += speedRing[idx];
                count++;
            }
        }
        return count == 0 ? 0f : sum / count;
    }

    /**
     * Paso inmediatamente menor al actual, sin bajar del escalón más bajo de la
     * lista ni subir del techo. Cenit 0.6.25: el piso ya no es 1x sino STEPS[0]
     * (0.5x), así que el governor puede seguir recortando por debajo de nativo;
     * por eso existe la reconstrucción EASU, para que eso no sea solo un
     * estiramiento.
     */
    static float nextStepDown(float current, float ceiling) {
        float best = STEPS[0];
        for (float s : STEPS) {
            if (s < current - 0.001f && s <= ceiling + 0.001f && s > best) best = s;
        }
        return Math.min(best, current);
    }

    /** Paso inmediatamente mayor al actual, topado en el techo elegido. */
    static float nextStepUp(float current, float ceiling) {
        float best = ceiling;
        for (float s : STEPS) {
            if (s > current + 0.001f) {
                best = s;
                break;
            }
        }
        return Math.min(best, ceiling);
    }
}
