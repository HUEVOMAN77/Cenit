package com.izzy2lost.psx2;

import java.util.ArrayList;
import java.util.List;

/**
 * Mock del nativo SOLO con las lecturas que consume el regidor. Los numeros se
 * ponen a mano por caso, que es justamente la gracia: se puede fabricar el
 * escenario que en un telefono real tardaria minutos (GPU saturada, carga,
 * metrica ausente) y ver que hace el regidor con el.
 *
 * 0.6.30: el regidor ya no lee getters sueltos, lee el SNAPSHOT. Para no reescribir
 * los 45 casos del arnes, las cifras siguen poniendose en los campos viejos y
 * makeSample() las arma al double[24] en el orden del contrato. Lo que SI cambia
 * es que ahora hay un solo punto donde se fabrica la muestra, y ahi se pueden
 * probar los estados que antes eran imposibles de simular: ventana recortada,
 * muestra repetida, driver sin tiempo de GPU y NaN por campo.
 */
public class NativeApp {

    public static boolean hasNoNativeBinary = false;
    public static boolean paused = false;

    // Lecturas del motor (todas en la unidad REAL que el core publica).
    static float speed = 100f;
    static float gpuPct = 50f;      // % del reloj real
    static float gpuMs = 8f;
    static float eePct = 50f;       // % del reloj real
    static float verticalHz = 59.94f;
    static float mtvuWaitMs = 0f;
    static long mtvuWaitCalls = 0L;
    static int effectiveRate = 0;
    static float effectiveUpscale = 0f; // 0 = el nativo no lo sabe

    // Control del SNAPSHOT (lo que antes no se podia fabricar).
    /** false = el nativo no contesta: safeGetCenitSample devuelve null. */
    static boolean sampleAvailable = true;
    /** Tamano del array devuelto: 23 proboca la rama de longitud equivocada. */
    static int sampleSize = 24;
    /** bits de FLAGS del contrato. 0xF = todo valido (el caso comun). */
    static int flags = CenitLoadSample.FLAG_GPU_METRIC_VALID
            | CenitLoadSample.FLAG_EMU_METRIC_VALID
            | CenitLoadSample.FLAG_WINDOW_COMPLETE
            | CenitLoadSample.FLAG_SAMPLE_FRESH;
    static double presents = 30;
    static double windowSeconds = 0.5;
    /** true = poner NaN en el campo indicado de la muestra (prueba de Regla 3). */
    static int nanField = -1;
    /** true = sin anillo todavia: P50/P95 a NaN, como el nativo con n < 20. */
    static boolean noRing = false;

    // Lo que el regidor mando aplicar y lo que el limitador recibio.
    static final List<String> actions = new ArrayList<>();
    /**
     * Cada veredicto publicado al HUD, como "estado,conf,accion,gpu,ee,termico".
     * El arnes lo mira porque la publicacion es parte del contrato: si el regidor
     * calcula el veredicto y no lo publica, el usuario no tiene como verificar.
     */
    static final List<String> published = new ArrayList<>();

    /**
     * Cuantas veces el regidor pidio borrar el cuadro del HUD. Se cuenta en una
     * variable aparte en vez de meter la marca dentro de published: las sentencias
     * del arnes miran posiciones de esa lista, y un "RESET" ahi cambiaria de indice
     * todo lo de abajo sin aportar nada que el contador no diga igual.
     */
    static int hudResets = 0;

    /** Cenit 0.6.31 (Fase 1): iguales, para el cuadro del sintonizador. */
    static final List<String> tunerPublished = new ArrayList<>();
    static int tunerHudResets = 0;

    static void reset() {
        paused = false;              // se filtraba de caso a caso y congelaba todo
        hasNoNativeBinary = false;
        speed = 100f; gpuPct = 50f; gpuMs = 8f; eePct = 50f; verticalHz = 59.94f;
        mtvuWaitMs = 0f; mtvuWaitCalls = 0L; effectiveRate = 0; effectiveUpscale = 0f;
        superblockValidating = false;
        actions.clear();
        published.clear();
        hudResets = 0;
        tunerPublished.clear();
        tunerHudResets = 0;
        sampleAvailable = true;
        sampleSize = 24;
        flags = CenitLoadSample.FLAG_GPU_METRIC_VALID | CenitLoadSample.FLAG_EMU_METRIC_VALID
                | CenitLoadSample.FLAG_WINDOW_COMPLETE | CenitLoadSample.FLAG_SAMPLE_FRESH;
        presents = 30; windowSeconds = 0.5;
        nanField = -1; noRing = false;
    }

    /** Arma el double[24] del contrato con las cifras que el caso dejò puestas. */
    static double[] makeSample() {
        if (!sampleAvailable) return null;
        final double[] d = new double[sampleSize];
        if (sampleSize != CenitLoadSample.SIZE) return d; // relleno de ceros: longitud mala
        d[CenitLoadSample.IDX_FPS] = speed > 0 ? speed * 0.6 : 0;
        d[CenitLoadSample.IDX_SPEED_PCT] = speed;
        d[CenitLoadSample.IDX_FRAMETIME_AVG_MS] = noRing ? Double.NaN : 16.7;
        d[CenitLoadSample.IDX_FRAMETIME_MIN_MS] = noRing ? Double.NaN : 15.0;
        d[CenitLoadSample.IDX_FRAMETIME_MAX_MS] = noRing ? Double.NaN : 22.0;
        d[CenitLoadSample.IDX_FRAMETIME_P50_MS] = noRing ? Double.NaN : 16.7;
        d[CenitLoadSample.IDX_FRAMETIME_P95_MS] = noRing ? Double.NaN : 20.1;
        d[CenitLoadSample.IDX_GPU_PCT] = gpuPct;
        d[CenitLoadSample.IDX_GPU_AVG_MS] = gpuMs;
        d[CenitLoadSample.IDX_EE_PCT] = eePct;
        d[CenitLoadSample.IDX_EE_AVG_MS] = 12.0;
        d[CenitLoadSample.IDX_GS_PCT] = 10.0;
        d[CenitLoadSample.IDX_VU_THREAD_PCT] = mtvuWaitCalls > 0 ? 40.0 : 0.0;
        d[CenitLoadSample.IDX_MTVU_WAIT_MS] = mtvuWaitMs;
        d[CenitLoadSample.IDX_MTVU_WAIT_CALLS] = mtvuWaitCalls;
        d[CenitLoadSample.IDX_MTVU_EXEC_MS] = 5.0;
        d[CenitLoadSample.IDX_VERTICAL_HZ] = verticalHz;
        d[CenitLoadSample.IDX_PRESENTS] = presents;
        d[CenitLoadSample.IDX_WINDOW_S] = windowSeconds;
        d[CenitLoadSample.IDX_UPSCALE] = effectiveUpscale;
        d[CenitLoadSample.IDX_EE_CYCLE_RATE] = effectiveRate;
        d[CenitLoadSample.IDX_EE_CYCLE_SKIP] = 0;
        d[CenitLoadSample.IDX_MTVU_ON] = mtvuWaitCalls > 0 ? 1 : 0;
        d[CenitLoadSample.IDX_FLAGS] = flags;
        if (nanField >= 0 && nanField < d.length) d[nanField] = Double.NaN;
        return d;
    }

    public static boolean isPaused() { return paused; }
    public static float safeGetEmulationSpeed() { return speed; }
    public static float safeGetGPUUsage() { return gpuPct; }
    public static float safeGetGPUAverageTime() { return gpuMs; }
    public static float safeGetEEThreadUsage() { return eePct; }
    public static float safeGetFrameRateTarget() { return verticalHz; }
    public static float safeGetMtvuWaitMs() { return mtvuWaitMs; }
    public static long safeGetMtvuWaitCalls() { return mtvuWaitCalls; }
    public static int safeGetEffectiveEECycleRate() { return effectiveRate; }
    public static float safeGetEffectiveUpscale() { return effectiveUpscale; }
    public static void setLimiterModeAsync(int v) { actions.add("limiter=" + v); }

    // --- La unica lectura que hace el regidor desde 0.6.30 ---
    public static double[] safeGetCenitSample() { return makeSample(); }

    /** Publicacion del veredicto al HUD: se registra, no se pinta nada. */
    public static void safeSetCenitLoadVerdict(int stateOrdinal, int confidencePct,
                                               int actionCode, double gpuPct, double eePct,
                                               int thermalStatus) {
        published.add(stateOrdinal + "," + confidencePct + "," + actionCode + ","
                + gpuPct + "," + eePct + "," + thermalStatus);
    }

    /** Borrar el cuadro del HUD: se cuenta (ver hudResets). */
    public static void safeResetCenitLoadVerdict() { hudResets++; }

    /** Cenit 0.6.31 (Fase 1): idem para el cuadro del sintonizador. */
    public static void safeResetCenitTunerStatus() { tunerHudResets++; }

    /** Cenit 0.6.31: publicacion del estado del tuner al HUD (se registra). */
    public static void safeSetCenitTunerStatus(int state, int mode, int gainPermille,
                                               int fpsBeforeX100, int fpsAfterX100,
                                               float p50Ms, float p95Ms,
                                               int thermalStatus, String profileId) {
        tunerPublished.add(state + "," + mode + "," + gainPermille + "," + fpsBeforeX100
                + "," + fpsAfterX100 + "," + p50Ms + "," + p95Ms + "," + thermalStatus
                + "," + profileId);
    }

    /**
     * Cenit 0.6.31 (Fase 1): puerta "superblockBusy". Es un campo del escenario,
     * no una simulacion del motor: lo que se prueba es que el regidor la LEA y se
     * la pase al sintonizador en cada tick.
     */
    public static boolean superblockValidating = false;
    public static boolean safeSuperblockValidating() { return superblockValidating; }

    public static String getCurrentGameSerial() { return "SLUS-21079"; }
}
