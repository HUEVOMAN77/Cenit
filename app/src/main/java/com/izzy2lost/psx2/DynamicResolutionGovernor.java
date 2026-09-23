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
 *  - Los pasos usan la misma escala que la interfaz: 1, 1.25, 1.5, 2, 2.5, 3, 4.
 *  - Si el usuario mueve la escala, si cambia el techo, o si hay mando de
 *    velocidad (fast-forward), toda la memoria del regidor se olvida.
 */
final class DynamicResolutionGovernor {

    private static final long TICK_MS = 1000L;
    private static final float DROP_BELOW_PCT = 95f;   // por debajo: el juego va atrasado
    private static final float HOLDS_ABOVE_PCT = 99.5f; // por encima: va sobrado
    private static final int SLOW_TICKS_TO_DROP = 2;
    private static final int FAST_TICKS_TO_RAISE = 8;
    private static final float[] STEPS = {1f, 1.25f, 1.5f, 2f, 2.5f, 3f, 4f};

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

    private final Runnable tick = new Runnable() {
        @Override public void run() {
            if (!active) return;
            evaluate();
            main.postDelayed(this, TICK_MS);
        }
    };

    DynamicResolutionGovernor(Context context, Host host) {
        this.prefs = context.getApplicationContext()
                .getSharedPreferences("app_prefs", Context.MODE_PRIVATE);
        this.host = host;
    }

    /** Se llama cuando el juego arranca. Idempotente. */
    void start() {
        if (active) return;
        active = true;
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
            fastTicks = 0;
            slowTicks++;
            if (slowTicks >= SLOW_TICKS_TO_DROP) {
                slowTicks = 0;
                final float next = nextStepDown(applied, ceiling);
                if (next < applied - 0.001f) {
                    applied = next;
                    host.applyUpscale(next);
                    cooldownTicks = 2;
                } else {
                    cooldownTicks = 5; // ya está en 1x, no hay marcha atrás
                }
            }
        } else if (speed > HOLDS_ABOVE_PCT && applied < ceiling - 0.001f) {
            slowTicks = 0;
            fastTicks++;
            if (fastTicks >= FAST_TICKS_TO_RAISE) {
                fastTicks = 0;
                applied = nextStepUp(applied, ceiling);
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
