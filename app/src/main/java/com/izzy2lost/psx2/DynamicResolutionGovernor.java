package com.izzy2lost.psx2;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Handler;
import android.os.Looper;

/**
 * Regidor de resolución dinámica de Cenit.
 *
 * No toca el bucle de dibujo del GS: parchearlo desde fuera deja targets
 * cacheados a escala distinta y rompe justo los juegos que más lo necesitan
 * (God of War reescribe sus buffers cada cuadro). En su lugar mide la velocidad
 * real de emulación —PerformanceMetrics::GetSpeed, el mismo número del HUD— una
 * vez por segundo, y cuando ve que el juego no llega a tiempo aplica UN paso de
 * resolución por la vía oficial: escribir "upscale_multiplier" en el INI y
 * ApplySettings, la ruta exacta que usaría el usuario a mano (y que el spinner
 * de escala de este menú ya usa en caliente desde 0.6.2).
 *
 * Reglas:
 *  - El valor de "upscale_multiplier" es el TECHO elegido; nunca se sube de ahí.
 *  - Baja rápido (2 segundos de tirones) y sube lento (8 segundos holgados),
 *    con enfriamiento de 2 s entre pasos: evita el aleteo.
 *  - Los pasos usan exactamente la escala que ofrece el menú (1x ... 8x), para
 *    que un escalón del regidor se pueda reproducir a mano.
 *  - Si el usuario mueve la escala, si cambia el techo, o si hay mando de
 *    velocidad (fast-forward), toda la memoria del regidor se olvida.
 *
 * Regidor v2 (Cenit 0.6.4, plan del inge §5 y §6), dos reglas nuevas:
 *  - Solo baja pixels cuando el cuello de botella ES la GPU. Si el juego va
 *    atrasado con la GPU holgada (uso bajo), lo que falta es CPU emulada y
 *    bajar la resolución solo empeora la imagen gratis: en ese caso el regidor
 *    no baja, espera (y registra el porqué una vez).
 *  - Con límite térmico activo (PowerManager, API 29+), no sube nunca y sí
 *    acepta bajar: enfriar es lo que corresponde cuando el SoC recorta fre-
 *    cuencias. El cambio térmico además despierta un tick al instante.
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
    // Los mismos escalones que ofrece el menú: una sola fuente de verdad. Si el
    // regidor tuviera lista propia, bajar desde un techo alto saltaría de 8x a 4x.
    private static final float[] STEPS = SettingsScreenController.SCALE_VALUES;

    interface Host {
        boolean isGameRunning();
        boolean isTimeScaled(); // fast-forward u otro mando de velocidad activo
        void applyUpscale(float value);
    }

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
    // Regidor v2: estado térmico visto por última vez (-2 = sin oyente, p. ej.
    // API < 29 o el sistema no expone PowerManager). -1 = normal.
    private final android.os.PowerManager powerManager;
    private Object thermalListener; // android.os.PowerManager.OnThermalStatusChangedListener
    private int lastThermalStatus = -2;
    // Para no repetir el aviso de "es CPU, no GPU" en cada tick.
    private boolean cpuBoundLogged = false;

    private final Runnable tick = new Runnable() {
        @Override public void run() {
            if (!active) return;
            evaluate();
            main.postDelayed(this, TICK_MS);
        }
    };

    DynamicResolutionGovernor(Context context, Host host) {
        final Context app = context.getApplicationContext();
        this.prefs = app.getSharedPreferences("app_prefs", Context.MODE_PRIVATE);
        this.host = host;
        android.os.PowerManager pm = null;
        try { pm = (android.os.PowerManager) app.getSystemService(Context.POWER_SERVICE); }
        catch (Throwable ignored) {}
        this.powerManager = pm;
    }

    /** API 29+: el sistema avisa cuando recorta frecuencias por calor. */
    @androidx.annotation.TargetApi(29)
    private void attachThermalListener(android.os.PowerManager pm) {
        if (thermalListener != null) return;
        final android.os.PowerManager.OnThermalStatusChangedListener listener = status -> {
            // El overload sin Executor entrega en el hilo principal; aún así,
            // PostDelayed aquí es barato y evita asumir el contrato.
            lastThermalStatus = status;
            if (active) {
                main.removeCallbacks(tick);
                main.postDelayed(tick, 200L);
            }
        };
        thermalListener = listener;
        pm.addThermalStatusChangedListener(listener);
        lastThermalStatus = pm.getCurrentThermalStatus();
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
        // En 26-28 no hay señal térmica accesible: el regidor trabaja sin ella.
        if (powerManager != null && android.os.Build.VERSION.SDK_INT >= 29) {
            try { attachThermalListener(powerManager); } catch (Throwable ignored) {}
        }
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
        // El regidor vive mientras viva la Activity; el oyente térmico también,
        // pero se quita al apagar para que una pantalla apagada no despierte ticks.
        if (thermalListener != null && powerManager != null
                && android.os.Build.VERSION.SDK_INT >= 29) {
            try {
                powerManager.removeThermalStatusChangedListener(
                        (android.os.PowerManager.OnThermalStatusChangedListener) thermalListener);
            } catch (Throwable ignored) {}
            // Sin nullear, el próximo start() creería que sigue enganchado.
            thermalListener = null;
        }
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
        if (prefs.getInt("ee_cycle_rate", 0) != 0) {
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
            // sentido: congelar.
            slowTicks = 0;
            fastTicks = 0;
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
            return;
        }
        if (applied <= 0f) {
            applied = ceiling;
            return;
        }

        final float speed = NativeApp.safeGetEmulationSpeed();
        // GetSpeed devuelve % respecto del objetivo (60 o 50 según juego).
        if (speed > 0f && speed < DROP_BELOW_PCT) {
            // Regidor v2: ¿el retraso es de GPU? GPUUsage ~= (tiempo GPU)/(16.6 ms).
            // Con la GPU holgada, recortar resolución no recupera cuadros: el que
            // no da más es el hilo de CPU emulada. Esperar en vez de bajar.
            // Uso 0 = métrica sin arrancar (GS recién abierto): no filtrar.
            final float gpu = NativeApp.safeGetGPUUsage();
            if (gpu > 0.05f && gpu < GPU_BOUND_MIN) {
                slowTicks = 0;
                fastTicks = 0;
                if (!cpuBoundLogged) {
                    cpuBoundLogged = true;
                    android.util.Log.i("DynRes", "behind at " + speed
                            + "% but GPU usage only " + gpu + ": CPU-bound, holding scale");
                }
                return;
            }
            cpuBoundLogged = false;
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
            // Regidor v2: con calor no se sube (haría justo lo que el límite
            // térmico intenta evitar). Bajar sigue permitido arriba.
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
        }
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
