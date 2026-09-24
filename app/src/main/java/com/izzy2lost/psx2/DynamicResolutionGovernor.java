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
 *  - Los pasos usan exactamente la escala del menú (1x ... 8x).
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
 *  4) EVIDENCIA de cuotas: si en 1x el juego sigue atrasado muchos segundos, se
 *     cuenta (slowFloorTicks en el perfil) y la pantalla de Ajustes ofrece el
 *     "modo cuotas" (EECycleSkip por-juego) con datos, no a ciegas.
 */
final class DynamicResolutionGovernor {

    private static final long TICK_MS = 1000L;
    private static final float DROP_BELOW_PCT = 95f;   // por debajo: el juego va atrasado
    private static final float HOLDS_ABOVE_PCT = 99.5f; // por encima: va sobrado
    private static final int SLOW_TICKS_TO_DROP = 2;
    private static final int FAST_TICKS_TO_RAISE = 8;
    // Regidor v2: con la GPU por debajo de este uso, un retraso NO es de píxeles.
    // 0 = métrica todavía sin medir (GS recién abierto): no se filtra nada.
    private static final float GPU_BOUND_MIN = 0.65f;
    // Escalones de PowerManager.THERMAL_STATUS_*: MODERATE y más arriba significan
    // que el SoC ya está recortando frecuencias por calor.
    private static final int THERMAL_BLOCK_RAISE = 2; // THERMAL_STATUS_MODERATE
    // v3 pre-corte: deriva de velocidad sostenida que huele a throttling mudo.
    private static final int TREND_WINDOW = 12;      // ticks de historia (12 s)
    private static final float TREND_DROP_PCT = 6f;  // media últimos 4 vs últimos 12
    private static final int PRECLIMITS_PER_GAME = 2;
    // v3 turbo de cargas: GPU casi ociosa + velocidad clavada, N segundos.
    private static final float LOAD_GPU_MAX = 0.22f;
    private static final int LOAD_TICKS_TO_TURBO = 4;
    private static final float LOAD_END_GPU = 0.40f;
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

    private final Runnable tick = new Runnable() {
        @Override public void run() {
            if (!active) return;
            evaluate();
            main.postDelayed(this, TICK_MS);
        }
    };

    DynamicResolutionGovernor(Context context, Host host) {
        final Context app = context.getApplicationContext();
        this.appContext = app;
        this.prefs = app.getSharedPreferences("app_prefs", Context.MODE_PRIVATE);
        this.host = host;
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
        reset();
        main.postDelayed(tick, TICK_MS);
    }

    /** Al apagar el juego o cerrar la app: deja la escala del usuario aplicada. */
    void stop() {
        if (!active) return;
        active = false;
        main.removeCallbacks(tick);
        final float ceiling = ceiling();
        if (applied > 0f && applied < ceiling - 0.001f) host.applyUpscale(ceiling);
        applied = 0f;
        releaseTurbo("game stopped");
        detachThermalListener();
        // v3: el aprendizaje de esta partida se guarda con el juego.
        if (profile != null && profile.known()) profile.save();
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
        return Math.max(1f, prefs.getFloat("upscale_multiplier", 1f));
    }

    private static boolean isVmPaused() {
        if (NativeApp.hasNoNativeBinary) return true;
        try { return NativeApp.isPaused(); } catch (Throwable t) { return true; }
    }

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
        if (selfDisabled) return;
        // Con la CPU emulada a otra velocidad, medir "porcentaje de velocidad" deja
        // de significar "el teléfono no da abasto": el juego va lento porque el
        // usuario lo pidió. Congelar el regidor hasta que vuelva al 100%.
        // 0.6.12: se mira el valor EFECTIVO (capa por-juego incluida), no solo la
        // preferencia global — un -1 guardado en el INI de SOTC debe congelar igual,
        // y un 0 por-juego que anule un global viejo no puede dejar esto dormido.
        // La lectura nativa es barata (un entero); el fallback ante cualquier fallo
        // es la preferencia global de siempre.
        final int effectiveRate = NativeApp.safeGetEffectiveEECycleRate();
        if (effectiveRate != 0 || (NativeApp.hasNoNativeBinary
                && prefs.getInt("ee_cycle_rate", 0) != 0)) {
            if (applied > 0f) {
                host.applyUpscale(ceiling());
                applied = 0f;
            }
            slowTicks = 0;
            fastTicks = 0;
            return;
        }
        if (!prefs.getBoolean("dynamic_res", true)) {
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
            slowTicks = 0;
            fastTicks = 0;
            releaseTurbo("input/ pause froze governor");
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
            if (profile != null && profile.heldScale > 1.001f && profile.heldScale < ceiling - 0.001f)
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

        final float speed = NativeApp.safeGetEmulationSpeed();
        final float gpu = NativeApp.safeGetGPUUsage();

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
                if (applied <= 1.001f && speed < DROP_BELOW_PCT) {
                    profile.slowFloorTicks++;
                }
            }
        }

        // ---------------------------------------------------------------
        // v3: TURBO DE CARGAS. Carga = GPU muerta + velocidad clavada. Con
        // auto-turbo y el juego conocido (o tras la primera detección), se
        // engancha el limitador Turbo y se suelta al volver el juego real.
        // ---------------------------------------------------------------
        if (gpu > 0.05f && speed > 0f) {
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
        // Reglas clásicas (v2) de subida/bajada:
        // ---------------------------------------------------------------
        if (speed > 0f && speed < DROP_BELOW_PCT) {
            // ¿El retraso es de GPU? GPUUsage ~= (tiempo GPU)/(16.6 ms). Con la
            // GPU holgada, recortar resolución no recupera cuadros.
            // v3: además, si el perfil ya aprendió que ESTE juego es CPU-bound,
            // el filtro se aplica con un umbral más suave (0.8*GPU_BOUND_MIN)
            // porque la evidencia es de la partida anterior, no de este segundo.
            final float bound = (profile != null && profile.cpuBound)
                    ? GPU_BOUND_MIN * 0.8f : GPU_BOUND_MIN;
            if (gpu > 0.05f && gpu < bound) {
                slowTicks = 0;
                fastTicks = 0;
                cpuBoundTicks++;
                if (profile != null && profile.known() && cpuBoundTicks >= 15)
                    profile.cpuBound = true; // fue consistente: que conste en el perfil
                if (!cpuBoundLogged) {
                    cpuBoundLogged = true;
                    android.util.Log.i("DynRes", "behind at " + speed
                            + "% but GPU usage only " + gpu + ": CPU-bound, holding scale");
                }
                return;
            }
            cpuBoundLogged = false;
            cpuBoundTicks = 0; // el retraso este vez sí era de píxeles: cuenta nueva
            fastTicks = 0;
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
                    cooldownTicks = 5; // ya está en 1x, no hay marcha atrás
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

    /** Paso inmediatamente menor al actual, sin bajar de 1x ni subir del techo. */
    static float nextStepDown(float current, float ceiling) {
        float best = 1f;
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
