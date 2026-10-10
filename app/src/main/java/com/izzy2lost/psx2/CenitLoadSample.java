package com.izzy2lost.psx2;

/**
 * Cenit 0.6.30 — Muestra unificada de carga del motor.
 *
 * Por que existe: antes de esta clase, el governor leia doce getters sueltos
 * (getGPUUsage, getEEThreadUsage, getMtvuWaitMs, ...) y cada cruce JNI podia
 * caer a un lado distinto del borde de la ventana de 0,5 s del core. Con dos
 * lecturas de ventanas distintas, la comparacion GPU vs EE ya no es la del
 * mismo reloj ni del mismo intervalo: eso era literalmente posible con el
 * patron de 0.6.29 y producia veredictos inconsistentes entre ticks consecutivos.
 *
 * Un solo double[] de 24 elementos es un SNAPSHOT de un mismo instante de
 * PerformanceMetrics::Update. Los campos 0..15 y 17..18 salen del mismo
 * Update() (ventana de 0,5 s); los campos 5 y 6 (P50/P95) vienen del anillo
 * de 150 presents (~2,5-5 s) y se usan solo como TENDENCIA, nunca como
 * criterio instantaneo mezclado con la carga de la ventana.
 *
 * Tabla de unidades (todas en PORCENTAJE 0..100 donde aplique, NUNCA fraccion):
 *   0  FPS              cuadros/s
 *   1  SPEED_PCT        % (100 = a tiempo)
 *   2  FRAMETIME_AVG    ms
 *   3  FRAMETIME_MIN    ms
 *   4  FRAMETIME_MAX    ms
 *   5  FRAMETIME_P50    ms (anillo 150 presents, NO la ventana)
 *   6  FRAMETIME_P95    ms (anillo 150 presents, NO la ventana)
 *   7  GPU_PCT          % del reloj real (0..100, NO fraccion)
 *   8  GPU_AVG_MS       ms/cuadro, puede NaN
 *   9  EE_PCT           % del reloj real
 *  10  EE_AVG_MS        ms/cuadro
 *  11  GS_PCT           % del reloj real
 *  12  VU_THREAD_PCT    % del reloj real (0 si MTVU apagado)
 *  13  MTVU_WAIT_MS     ms acumulados en la ventana
 *  14  MTVU_WAIT_CALLS  llamadas
 *  15  MTVU_EXEC_MS     ms
 *  16  VERTICAL_HZ      Hz verticales del modo de video (NO fps del juego)
 *  17  PRESENTS         presents en la ventana
 *  18  WINDOW_S         segundos de la ventana medida
 *  19  UPSCALE          multiplicador efectivo
 *  20  EE_CYCLE_RATE    entero como double
 *  21  EE_CYCLE_SKIP    entero como double
 *  22  MTVU_ON          1.0 / 0.0
 *  23  FLAGS            bits de validez (bit0=GPU, bit1=EMU, bit2=ventana, bit3=fresca)
 */
public final class CenitLoadSample {

    // --- Constantes de indice, exactas del contrato ---
    public static final int IDX_FPS = 0;
    public static final int IDX_SPEED_PCT = 1;
    public static final int IDX_FRAMETIME_AVG_MS = 2;
    public static final int IDX_FRAMETIME_MIN_MS = 3;
    public static final int IDX_FRAMETIME_MAX_MS = 4;
    /** Percentil P50 del anillo de 150 presents, NO de la ventana de 0,5 s. */
    public static final int IDX_FRAMETIME_P50_MS = 5;
    /** Percentil P95 del anillo de 150 presents, NO de la ventana de 0,5 s. */
    public static final int IDX_FRAMETIME_P95_MS = 6;
    /** Porcentaje del reloj real (0..100). GetGPUUsage() del core. NO es fraccion. */
    public static final int IDX_GPU_PCT = 7;
    /** ms/cuadro de GPU. Puede ser NaN si cero presents en la ventana. */
    public static final int IDX_GPU_AVG_MS = 8;
    /** Porcentaje del reloj real del hilo EE. GetCPUThreadUsage() del core. */
    public static final int IDX_EE_PCT = 9;
    /** ms/cuadro del hilo EE. */
    public static final int IDX_EE_AVG_MS = 10;
    /** Porcentaje del reloj real del hilo GS. */
    public static final int IDX_GS_PCT = 11;
    /** Porcentaje del reloj real del hilo VU. 0 si MTVU apagado. */
    public static final int IDX_VU_THREAD_PCT = 12;
    /** ms acumulados de espera MTVU en la ventana. */
    public static final int IDX_MTVU_WAIT_MS = 13;
    /** Llamadas de espera MTVU en la ventana. */
    public static final int IDX_MTVU_WAIT_CALLS = 14;
    /** ms de ejecucion MTVU en la ventana. */
    public static final int IDX_MTVU_EXEC_MS = 15;
    /** Hz verticales del modo de video. NO son fps del juego. */
    public static final int IDX_VERTICAL_HZ = 16;
    /** Presents en la ventana de 0,5 s. */
    public static final int IDX_PRESENTS = 17;
    /** Segundos de la ventana medida. */
    public static final int IDX_WINDOW_S = 18;
    /** Multiplicador de upscale efectivo. */
    public static final int IDX_UPSCALE = 19;
    /** EE Cycle Rate (entero como double). */
    public static final int IDX_EE_CYCLE_RATE = 20;
    /** EE Cycle Skip (entero como double). */
    public static final int IDX_EE_CYCLE_SKIP = 21;
    /** MTVU activo: 1.0 o 0.0. */
    public static final int IDX_MTVU_ON = 22;
    /** Bits de validez. bit0=GPU_METRIC_VALID, bit1=EMU_METRIC_VALID,
     *  bit2=WINDOW_COMPLETE, bit3=SAMPLE_FRESH. */
    public static final int IDX_FLAGS = 23;

    /** Longitud fija del snapshot. Si crece, se agrega AL FINAL. */
    public static final int SIZE = 24;

    // --- Bits de FLAGS ---
    /** bit 0 (valor 1): el driver reporto tiempo de GPU en esta ventana. */
    public static final int FLAG_GPU_METRIC_VALID = 1;
    /** bit 1 (valor 2): GetCPUThreadUsage() tuvo denominador de ventana valido. */
    public static final int FLAG_EMU_METRIC_VALID = 2;
    /** bit 2 (valor 4): la ventana duro >= 0.25 * UPDATE_INTERVAL. */
    public static final int FLAG_WINDOW_COMPLETE = 4;
    /** bit 3 (valor 8): el snapshot es posterior al ultimo ResetStatistics/Clear. */
    public static final int FLAG_SAMPLE_FRESH = 8;

    private final double[] data;
    private final String invalidReason;

    /**
     * Constructor principal. Acepta el double[] del nativo.
     * Si null o longitud != 24, la muestra es invalida (no excepcion: el governor
     * no puede caerse porque un driver viejo no la mande).
     */
    public CenitLoadSample(double[] raw) {
        if (raw == null || raw.length != SIZE) {
            this.data = null;
            this.invalidReason = (raw == null)
                    ? "muestra nula: el nativo no devolvio snapshot"
                    : "longitud " + (raw == null ? -1 : raw.length) + " esperada " + SIZE;
        } else {
            this.data = raw.clone(); // copia defensiva para inmutabilidad
            this.invalidReason = null;
        }
    }

    // Constructor privado para muestras invalidas con motivo.
    private CenitLoadSample(String why) {
        this.data = null;
        this.invalidReason = why;
    }

    /** Fabrica de muestras invalidas con motivo legible. */
    public static CenitLoadSample invalid(String why) {
        return new CenitLoadSample(why);
    }

    /** True si la muestra tiene datos validos (constructor acepto el array). */
    public boolean isValid() {
        return data != null;
    }

    /** Motivo de invalidez, o null si la muestra es valida. */
    public String whyInvalid() {
        return invalidReason;
    }

    // --- Geters por campo, con unidad en el nombre y Javadoc de unidad ---

    /** Cuadros por segundo reales. Unidad: cuadros/s. */
    public double fps() { return field(IDX_FPS); }

    /** Velocidad de emulacion. Unidad: % (100 = a tiempo). */
    public double speedPct() { return field(IDX_SPEED_PCT); }

    /** Tiempo medio de cuadro. Unidad: ms. */
    public double frametimeAvgMs() { return field(IDX_FRAMETIME_AVG_MS); }

    /** Tiempo minimo de cuadro. Unidad: ms. */
    public double frametimeMinMs() { return field(IDX_FRAMETIME_MIN_MS); }

    /** Tiempo maximo de cuadro. Unidad: ms. */
    public double frametimeMaxMs() { return field(IDX_FRAMETIME_MAX_MS); }

    /** Percentil P50 del anillo de 150 presents. Unidad: ms.
     *  NO es de la ventana de 0,5 s; es tendencia de ~2,5-5 s. */
    public double frametimeP50Ms() { return field(IDX_FRAMETIME_P50_MS); }

    /** Percentil P95 del anillo de 150 presents. Unidad: ms.
     *  NO es de la ventana de 0,5 s; es tendencia de ~2,5-5 s. */
    public double frametimeP95Ms() { return field(IDX_FRAMETIME_P95_MS); }

    /** Uso de GPU como porcentaje del reloj real. Unidad: % (0..100).
     *  GetGPUUsage() del core. NO es fraccion. */
    public double gpuPct() { return field(IDX_GPU_PCT); }

    /** ms medios de GPU por cuadro. Unidad: ms. Puede ser NaN si cero presents. */
    public double gpuAvgMs() { return field(IDX_GPU_AVG_MS); }

    /** Uso del hilo EE como porcentaje del reloj real. Unidad: % (0..100). */
    public double eePct() { return field(IDX_EE_PCT); }

    /** ms medios del hilo EE por cuadro. Unidad: ms. */
    public double eeAvgMs() { return field(IDX_EE_AVG_MS); }

    /** Uso del hilo GS como porcentaje del reloj real. Unidad: % (0..100). */
    public double gsPct() { return field(IDX_GS_PCT); }

    /** Uso del hilo VU como porcentaje del reloj real. Unidad: % (0..100).
     *  0 si MTVU esta apagado. */
    public double vuThreadPct() { return field(IDX_VU_THREAD_PCT); }

    /** ms acumulados de espera MTVU en la ventana. Unidad: ms. */
    public double mtvuWaitMs() { return field(IDX_MTVU_WAIT_MS); }

    /** Llamadas de espera MTVU en la ventana. Unidad: llamadas. */
    public double mtvuWaitCalls() { return field(IDX_MTVU_WAIT_CALLS); }

    /** ms de ejecucion MTVU en la ventana. Unidad: ms. */
    public double mtvuExecMs() { return field(IDX_MTVU_EXEC_MS); }

    /** Frecuencia vertical del modo de video. Unidad: Hz.
     *  NO son los fps del juego: un juego a 30 en entrelazado publica ~59,94. */
    public double verticalHz() { return field(IDX_VERTICAL_HZ); }

    /** Presents en la ventana de 0,5 s. Unidad: presents. */
    public double presents() { return field(IDX_PRESENTS); }

    /** Segundos de la ventana medida. Unidad: s. */
    public double windowSeconds() { return field(IDX_WINDOW_S); }

    /** Multiplicador de upscale efectivo. Unidad: multiplicador (1.0, 2.0, ...). */
    public double upscale() { return field(IDX_UPSCALE); }

    /** EE Cycle Rate efectivo. Unidad: entero como double. */
    public double eeCycleRate() { return field(IDX_EE_CYCLE_RATE); }

    /** EE Cycle Skip efectivo. Unidad: entero como double. */
    public double eeCycleSkip() { return field(IDX_EE_CYCLE_SKIP); }

    /** MTVU activo. Unidad: 1.0 (activo) o 0.0 (inactivo). */
    public double mtvuOn() { return field(IDX_MTVU_ON); }

    /** Bits de validez del snapshot. Ver FLAG_* para cada bit. */
    public int flags() {
        return data != null ? (int) data[IDX_FLAGS] : 0;
    }

    // --- Geters de validez derivadas de FLAGS ---
    // Regla 3 del prompt: 0% con bit=0 es "no sabemos", 0% con bit=1 es "holgada".
    // La diferencia decide si se recorta resolucion o no.

    /** bit0 = 1: el driver reporto tiempo de GPU.
     *  Regla 3: GPU al 3% con bit=1 es "GPU realmente holgada";
     *  GPU al 0% con bit=0 es "no sabemos". Distinguirlos es todo el punto. */
    public boolean gpuMetricValid() { return (flags() & FLAG_GPU_METRIC_VALID) != 0; }

    /** bit1 = 1: GetCPUThreadUsage() tuvo denominador de ventana valido. */
    public boolean emuMetricValid() { return (flags() & FLAG_EMU_METRIC_VALID) != 0; }

    /** bit2 = 1: la ventana duro >= 0.25 * UPDATE_INTERVAL.
     *  Un Update() cortisimo (primer tick, reset de stats) no es evidencia. */
    public boolean windowComplete() { return (flags() & FLAG_WINDOW_COMPLETE) != 0; }

    /** bit3 = 1: el snapshot es posterior al ultimo ResetStatistics/Clear. */
    public boolean sampleFresh() { return (flags() & FLAG_SAMPLE_FRESH) != 0; }

    // --- Helpers de NaN honestas ---
    // Usan la forma que captura NaN: !(x > 0) es cierto para NaN, 0 y negativos.
    // NUNCA x <= 0, porque NaN <= 0 es FALSO y el juego pasaria por delante.

    /** True si speedPct es un valor real positivo (no NaN, no cero, no negativo). */
    public boolean hasRealSpeed() { return speedPct() > 0; }

    /** True si gpuPct es un valor real positivo. */
    public boolean hasRealGpu() { return gpuPct() > 0; }

    /** True si eePct es un valor real positivo. */
    public boolean hasRealEe() { return eePct() > 0; }

    /** True si el frametime P95 del anillo es un valor real (no NaN).
     *  NaN significa que el anillo tenia menos de 20 muestras no nulas. */
    public boolean hasRealP95() { return !(frametimeP95Ms() != frametimeP95Ms()) ? true : false; }
    // Nota: la forma correcta es verificar que NO es NaN. x != x es cierto solo para NaN.

    /** Acceso directo al campo por indice. Devuelve NaN si la muestra es invalida. */
    public double field(int idx) {
        if (data == null || idx < 0 || idx >= SIZE) return Double.NaN;
        return data[idx];
    }

    /** Una linea corta para log/FABRICA con gpu/ee/speed/presents/flags. */
    public String describe() {
        if (!isValid()) return "INVALID(" + invalidReason + ")";
        return "gpu=" + fmt(gpuPct()) + "%"
                + " ee=" + fmt(eePct()) + "%"
                + " speed=" + fmt(speedPct()) + "%"
                + " presents=" + (long) presents()
                + " flags=0x" + Integer.toHexString(flags());
    }

    private static String fmt(double v) {
        if (Double.isNaN(v)) return "NaN";
        if (Double.isInfinite(v)) return "Inf";
        long i = (long) v;
        if (Math.abs(v - i) < 0.05) return String.valueOf(i);
        return String.format(java.util.Locale.US, "%.1f", v);
    }
}
