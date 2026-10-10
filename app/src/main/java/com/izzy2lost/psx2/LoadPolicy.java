package com.izzy2lost.psx2;

/**
 * Cenit 0.6.30 — Politica calibrable del clasificador de carga.
 *
 * Todos los umbrales numericos viven aqui, no regados por el codigo. Cada uno
 * con su porque medible. El clasificador los recibe como parametro y no conoce
 * ningun valor por defecto propio: si manana la calibracion cambia, se cambia
 * en un solo sitio.
 */
public final class LoadPolicy {

    // --- Umbrales principales ---

    /** GPU por encima de esto con el juego atrasado: esta ocupando casi todo el
     *  reloj, recortar pixeles recupera cuadros. 85 y no 100 porque un telefono
     *  con vsync/present nunca llega al 100: el hueco es el propio present. */
    public final double gpuHotPct;

    /** EE (hilo de emulacion) por encima de esto: la CPU emulada es el cuello. */
    public final double emuHotPct;

    /** GPU por debajo de esto con el juego atrasado: esta holgada la mayor parte
     *  del reloj, asi que el retraso no puede ser de pixeles. 35 deja un margen
     *  grande por debajo de "saturada" para no cambiar de opinion a cada muestra. */
    public final double gpuIdlePct;

    /** ms de espera MTVU por encima de esto: evidencia de bloqueo del EE al VU1. */
    public final double vuWaitMs;

    /** Llamadas de espera MTVU por encima de esto: evidencia de bloqueo sostenido. */
    public final double vuWaitCalls;

    /** Velocidad por encima de esto: el juego va a tiempo, no hay cuello. */
    public final double atTargetPct;

    /** La metrica de GPU "no existe todavia": driver que no reporta tiempo de GPU,
     *  o GS recien abierto. Con <= esto no se sabe nada, y eso va a UNKNOWN. */
    public final double gpuMetricMissingPct;

    // --- Margen de desempate ---

    /** Para "ambas calientes" exigir que una le saque este margen a la otra para
     *  declarar GPU_BOUND con confianza alta; si no, la decision se toma igual
     *  (la mas llena) pero con confianza baja.
     *  Un margen sin evidencia es criterio, y por eso se reporta la confianza,
     *  no solo el estado. */
    public final double dominanceMarginPct;

    // --- Umbrales de confianza ---

    /** Confianza minima para permitir ALLOW_RESOLUTION_DROP. */
    public final int confAllowDrop;

    /** Confianza minima para permitir BLOCK_RESOLUTION_DROP. */
    public final int confAllowRaise;

    /** Confianza minima para entrar en modo aprendizaje (calibracion). */
    public final int confLearn;

    /** Si true, la politica no tiene evidencia suficiente para calibrar. */
    public final boolean provisional;

    private LoadPolicy(Builder b) {
        this.gpuHotPct = b.gpuHotPct;
        this.emuHotPct = b.emuHotPct;
        this.gpuIdlePct = b.gpuIdlePct;
        this.vuWaitMs = b.vuWaitMs;
        this.vuWaitCalls = b.vuWaitCalls;
        this.atTargetPct = b.atTargetPct;
        this.gpuMetricMissingPct = b.gpuMetricMissingPct;
        this.dominanceMarginPct = b.dominanceMarginPct;
        this.confAllowDrop = b.confAllowDrop;
        this.confAllowRaise = b.confAllowRaise;
        this.confLearn = b.confLearn;
        this.provisional = b.provisional;
    }

    /** Politica inicial por defecto, con los valores calibrados de 0.6.29/0.6.30. */
    public static LoadPolicy defaults() {
        return new Builder().build();
    }

    /**
     * 0.6.30: el metodo idleFloorPct(boolean) que vivia aqui esta BORRADO. Nadie lo
     * llamaba y guardaba la MISMA regla (x1,2) que DynamicResolutionGovernor aplica
     * con withGpuIdlePct: dos copias de un numero de aprendizaje son la receta para
     * que manana alguien cambie una y la otra se quede vieja. La regla vive ahora
     * en un solo sitio, junto a quien la aplica.
     */

    /**
     * Devuelve una copia inmutable con umbrales percentiles adaptativos cuando
     * hay evidencia suficiente. Si la politica ya es provisional (sin evidencia),
     * devuelve la misma.
     */
    public LoadPolicy withCalibration(double suggestedGpuHotPct,
                                       double suggestedEmuHotPct,
                                       double suggestedGpuIdlePct) {
        Builder b = new Builder();
        b.gpuHotPct = this.gpuHotPct;
        b.emuHotPct = this.emuHotPct;
        b.gpuIdlePct = this.gpuIdlePct;
        b.vuWaitMs = this.vuWaitMs;
        b.vuWaitCalls = this.vuWaitCalls;
        b.atTargetPct = this.atTargetPct;
        b.gpuMetricMissingPct = this.gpuMetricMissingPct;
        b.dominanceMarginPct = this.dominanceMarginPct;
        b.confAllowDrop = this.confAllowDrop;
        b.confAllowRaise = this.confAllowRaise;
        b.confLearn = this.confLearn;
        b.provisional = false; // si llegamos aqui, la evidencia fue suficiente
        // Solo aplicar los sugeridos si son razonables (en rango 0..100 y con sentido)
        if (suggestedGpuHotPct > 50 && suggestedGpuHotPct <= 100) b.gpuHotPct = suggestedGpuHotPct;
        if (suggestedEmuHotPct > 50 && suggestedEmuHotPct <= 100) b.emuHotPct = suggestedEmuHotPct;
        if (suggestedGpuIdlePct > 10 && suggestedGpuIdlePct < suggestedGpuHotPct) b.gpuIdlePct = suggestedGpuIdlePct;
        return b.build();
    }

    /**
     * Copia con otro piso de holgura de GPU. Existe porque la evidencia APRENDIDA
     * por juego (el perfil dice "este disco ya se vio que es de emulacion") debe
     * ensanchar ese umbral sin tocar ninguna otra cifra de la politica calibrada.
     * Copiar los doce campos a mano es feo pero correcto: una copia que comparta
     * el Builder correria el riesgo de arrastrar valores viejos si manana se
     * agrega un umbral nuevo, y el riesgo de esta clase es justamente que los
     * numeros sean uno solo.
     */
    public LoadPolicy withGpuIdlePct(double newIdlePct) {
        Builder b = new Builder();
        b.gpuHotPct = this.gpuHotPct;
        b.emuHotPct = this.emuHotPct;
        b.gpuIdlePct = newIdlePct;
        b.vuWaitMs = this.vuWaitMs;
        b.vuWaitCalls = this.vuWaitCalls;
        b.atTargetPct = this.atTargetPct;
        b.gpuMetricMissingPct = this.gpuMetricMissingPct;
        b.dominanceMarginPct = this.dominanceMarginPct;
        b.confAllowDrop = this.confAllowDrop;
        b.confAllowRaise = this.confAllowRaise;
        b.confLearn = this.confLearn;
        b.provisional = this.provisional;
        return b.build();
    }

    /** Builder para construir politicas personalizadas. */
    public static final class Builder {
        double gpuHotPct = 85;
        double emuHotPct = 80;
        double gpuIdlePct = 35;
        double vuWaitMs = 10;
        double vuWaitCalls = 30;
        double atTargetPct = 99.5;
        double gpuMetricMissingPct = 0.5;
        double dominanceMarginPct = 4;
        int confAllowDrop = 55;
        int confAllowRaise = 65;
        int confLearn = 40;
        boolean provisional = true; // por defecto, sin evidencia

        public Builder gpuHotPct(double v) { this.gpuHotPct = v; return this; }
        public Builder emuHotPct(double v) { this.emuHotPct = v; return this; }
        public Builder gpuIdlePct(double v) { this.gpuIdlePct = v; return this; }
        public Builder vuWaitMs(double v) { this.vuWaitMs = v; return this; }
        public Builder vuWaitCalls(double v) { this.vuWaitCalls = v; return this; }
        public Builder atTargetPct(double v) { this.atTargetPct = v; return this; }
        public Builder gpuMetricMissingPct(double v) { this.gpuMetricMissingPct = v; return this; }
        public Builder dominanceMarginPct(double v) { this.dominanceMarginPct = v; return this; }
        public Builder confAllowDrop(int v) { this.confAllowDrop = v; return this; }
        public Builder confAllowRaise(int v) { this.confAllowRaise = v; return this; }
        public Builder confLearn(int v) { this.confLearn = v; return this; }
        public Builder provisional(boolean v) { this.provisional = v; return this; }
        public LoadPolicy build() { return new LoadPolicy(this); }
    }
}
