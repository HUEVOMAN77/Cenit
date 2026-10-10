package com.izzy2lost.psx2;

/**
 * Cenit 0.6.30 — Clasificador universal de carga de juego.
 *
 * Clasifica el cuello de botella de un juego en ejecucion comparando el % de
 * reloj real que tiene la GPU con el % que tiene el hilo EE. Universal: no
 * depende del nombre del disco ni de saber a cuantos cuadros juega.
 *
 * Por que esto puede ser universal sin tabla de nombres: la metrica del core
 * NO es "cuanto tarda un cuadro", es cuanto tiempo REAL del reloj esta la GPU
 * ocupada. La del hilo EE se calcula con el MISMO denominador de ventana, asi
 * que las dos son fracciones del mismo reloj y se pueden comparar entre si.
 *
 * Lo que NO usamos: comparar los ms por cuadro contra un "presupuesto" 1000/fps.
 * GetVerticalFrequency() publica la frecuencia VERTICAL del modo de video, NUNCA
 * 30, y PerformanceMetrics::Update se llama campo a campo en entrelazado. "Cuadro"
 * en los ms de GPU y "cuadro" en 1000/fps no son la misma unidad para un juego
 * que diseña a 30. El presupuesto se sigue IMPRIMIENDO en la evidencia, pero
 * ninguna decision depende de el.
 *
 * Ninguna decision de esta clase escribe ajustes ni toca el motor: CLASIFICA.
 * Quien actua es el regidor (pixeles) y el sintonizador CPU/VU (emulacion).
 */
public final class GameLoadClassifier {

    private GameLoadClassifier() {}

    /** Estados de carga del clasificador (Fase 1.2 del contrato). */
    public enum LoadState {
        /** Sin evidencia suficiente para clasificar. */
        UNKNOWN_LOAD,
        /** El juego va a tiempo, no hay cuello. */
        NO_BOTTLENECK,
        /** GPU saturada: bajar resolucion AYUDA. */
        GPU_BOUND,
        /** EE/GS trabajandose el cuadro con GPU holgada: bajar resolucion NO ayuda. */
        CPU_BOUND,
        /** EE bloqueado esperando al VU1 (MTVU puesto). */
        VU_BOUND,
        /** Throttle termico moderado o superior. */
        THERMAL_BOUND,
        /** Muestra invalida (null, longitud incorrecta, sin validez). */
        INVALID_SAMPLE;

        /**
         * Codigo para el HUD nativo, escrito A MANO y no con ordinal().
         * Debe coincidir con el diccionario del contrato:
         * {0=UNKNOWN,1=NO_BOTTLENECK,2=GPU_BOUND,3=CPU_BOUND,4=VU_BOUND,
         *  5=THERMAL_BOUND,6=INVALID_SAMPLE}
         *
         * Por que no ordinal(): el numero cruza la frontera JNI y lo traduce un
         * switch en C++ que esta en OTRO archivo y en OTRO lenguaje. Si manana
         * alguien inserta un estado en el medio de esta enum, ordinal() cambia en
         * silencio el significado de los numeros viejos y el HUD empieza a pintar
         * "GPU_BOUND" donde el clasificador dijo THERMAL_BOUND: un numero equivale
         * a una decision de recorte. Con el mapeo explicito, reordenar la enum no
         * mueve ningun codigo, y agregar un estado sin agregarlo aqui se nota.
         * Es el mismo motivo por el que AdaptiveProfile tampoco usa indices
         * propios en su tally: ahi el numero vive dentro de las preferencias del
         * telefono, entre versiones.
         */
        public int ordinalForHud() {
            switch (this) {
                case UNKNOWN_LOAD:   return 0;
                case NO_BOTTLENECK:  return 1;
                case GPU_BOUND:      return 2;
                case CPU_BOUND:      return 3;
                case VU_BOUND:       return 4;
                case THERMAL_BOUND:  return 5;
                case INVALID_SAMPLE: return 6;
                default: return 0;
            }
        }
    }

    /** Acciones permitidas segun la confianza. */
    public enum Action {
        /** No actuar, mantener estado actual. */
        HOLD,
        /** Permitir recorte de resolucion (GPU_BOUND con confianza suficiente). */
        ALLOW_RESOLUTION_DROP,
        /** Bloquear recorte de resolucion (CPU_BOUND, THERMAL_BOUND). */
        BLOCK_RESOLUTION_DROP,
        /** Esperar mas evidencia (INVALID_SAMPLE, UNKNOWN_LOAD). */
        WAIT_FOR_EVIDENCE;

        /**
         * Codigo para el HUD nativo, escrito A MANO y no con ordinal(). Debe
         * coincidir con el contrato:
         * {0=HOLD,1=ALLOW_RESOLUTION_DROP,2=BLOCK_RESOLUTION_DROP,
         *  3=WAIT_FOR_EVIDENCE}
         *
         * Lo mismo que LoadState.ordinalForHud(): este numero cruza la frontera
         * JNI y lo traduce un switch en C++. Reordenar la enum con ordinal()
         * cambiaria en silencio el significado de los numeros viejos.
         */
        public int codeForHud() {
            switch (this) {
                case HOLD: return 0;
                case ALLOW_RESOLUTION_DROP: return 1;
                case BLOCK_RESOLUTION_DROP: return 2;
                case WAIT_FOR_EVIDENCE: return 3;
                default: return 0;
            }
        }
    }

    /**
     * Veredicto del clasificador: estado, confianza, razon y accion permitida.
     * Inmutable, para que el governor o MainActivity lo lean sin riesgo de carrera.
     */
    public static final class Verdict {
        /** Estado de carga. */
        public final LoadState state;
        /** Confianza en el veredicto (0..100). */
        public final int confidencePct;
        /** Razon legible del veredicto. */
        public final String reason;
        /** Accion permitida segun la confianza. */
        public final Action action;

        public Verdict(LoadState state, int confidencePct, String reason, Action action) {
            this.state = state;
            this.confidencePct = Math.max(0, Math.min(100, confidencePct));
            this.reason = reason;
            this.action = action;
        }

        /** Codigo de estado para el HUD nativo. */
        public int stateOrdinal() { return state.ordinalForHud(); }

        /** Codigo de accion para el HUD nativo. */
        public int actionCode() { return action.codeForHud(); }
    }

    /**
     * Clasifica la muestra con la politica dada y el estado termico.
     * Puro: no lee prefs, no escribe nada, no llama al nativo.
     *
     * La escalera de decision, en este orden:
     * 1. Muestra invalida → INVALID_SAMPLE, WAIT_FOR_EVIDENCE
     * 2. Speed no real (NaN o <= 0) → INVALID_SAMPLE
     * 3. Thermal >= 2 con retraso → THERMAL_BOUND, BLOCK_RESOLUTION_DROP
     * 4. Speed >= 99,5 → NO_BOTTLENECK
     * 5. VU_WAIT sostenido → VU_BOUND
     * 6. GPU metrica ausente → CPU_BOUND (si EE caliente) o UNKNOWN_LOAD
     * 7. Ambas calientes → la mas llena gana
     * 8. Solo GPU caliente → GPU_BOUND; solo EE caliente → CPU_BOUND
     * 9. GPU <= piso de holgura → CPU_BOUND
     * 10. Zona intermedia → UNKNOWN_LOAD, HOLD
     *
     * @param s muestra del snapshot
     * @param p politica con umbrales
     * @param thermalStatus estado termico del sistema (0..4)
     * @return veredicto con estado, confianza, razon y accion
     */
    public static Verdict classify(CenitLoadSample s, LoadPolicy p, int thermalStatus) {
        return classify(s, p, thermalStatus, null);
    }

    /**
     * Version con LoadTrend para VU_WAIT sostenido. Si trend es null, VU_WAIT
     * se decide con la muestra actual (sin sostenimiento).
     */
    public static Verdict classify(CenitLoadSample s, LoadPolicy p, int thermalStatus,
                                   LoadTrend trend) {
        return gate(classifyRaw(s, p, thermalStatus, trend), p);
    }

    /**
     * La escalera desnuda: decide estado, confianza y la ACCION QUE LA RAMA
     * PEDIRIA. No aplica el umbral de confianza: eso lo hace gate(), y lo hace
     * AQUI es la razon por la que existe gate() separado.
     */
    private static Verdict classifyRaw(CenitLoadSample s, LoadPolicy p, int thermalStatus,
                                       LoadTrend trend) {
        // 1. Muestra invalida
        if (s == null || !s.isValid()) {
            return new Verdict(LoadState.INVALID_SAMPLE, 0,
                    s == null ? "muestra nula" : s.whyInvalid(),
                    Action.WAIT_FOR_EVIDENCE);
        }
        if (!s.windowComplete()) {
            return new Verdict(LoadState.INVALID_SAMPLE, 0, "ventana recortada",
                    Action.WAIT_FOR_EVIDENCE);
        }
        if (!s.sampleFresh()) {
            return new Verdict(LoadState.INVALID_SAMPLE, 0, "muestra no fresca",
                    Action.WAIT_FOR_EVIDENCE);
        }
        if (!s.emuMetricValid() && !s.gpuMetricValid()) {
            return new Verdict(LoadState.INVALID_SAMPLE, 0, "sin metricas validas",
                    Action.WAIT_FOR_EVIDENCE);
        }

        // 2. Validar NaN en campos numéricos críticos
        // NaN no es cero; toda comparación con NaN es falsa. Si algún campo clave
        // es NaN, la muestra no es válida para clasificar.
        if (Double.isNaN(s.fps()) || Double.isNaN(s.speedPct()) ||
            Double.isNaN(s.gpuPct()) || Double.isNaN(s.eePct()) ||
            Double.isNaN(s.mtvuWaitMs()) || Double.isNaN(s.mtvuWaitCalls())) {
            return new Verdict(LoadState.INVALID_SAMPLE, 0, "campo numerico NaN",
                    Action.WAIT_FOR_EVIDENCE);
        }

        // 3. Speed no real (NaN captura con !(x > 0))
        if (!(s.speedPct() > 0)) {
            return new Verdict(LoadState.INVALID_SAMPLE, 0, "speed no real (NaN o cero)",
                    Action.WAIT_FOR_EVIDENCE);
        }

        final double speed = s.speedPct();
        final double gpu = s.gpuPct();
        final double ee = s.eePct();
        final boolean behind = speed < p.atTargetPct;

        // 3. Thermal >= 2 con retraso → THERMAL_BOUND
        // El calor no es un cuello de pixeles; recortar la resolucion durante un
        // throttle aprende cpuBound falso y no se confirma GPU_BOUND.
        if (thermalStatus >= 2 && behind) {
            // El termico es un hecho del sistema, no una inferencia entre dos
            // metricas: es la decision MEJOR fundada del clasificador, por eso su
            // fuerza es alta. Y su accion es BLOCK (no recortar), la barra baja.
            return new Verdict(LoadState.THERMAL_BOUND, 90,
                    "thermal=" + thermalStatus + " con retraso",
                    Action.BLOCK_RESOLUTION_DROP);
        }

        // 4. Speed >= 99,5 → NO_BOTTLENECK
        // "A tiempo" SI es una evidencia, no una ausencia.
        if (speed >= p.atTargetPct) {
            return new Verdict(LoadState.NO_BOTTLENECK, 95,
                    "velocidad " + fmt(speed) + "% >= " + fmt(p.atTargetPct) + "%",
                    Action.HOLD);
        }

        // 5. VU_WAIT sostenido
        // Requiere evidencia SOSTENIDA, no un pico. Si trend es null, se decide
        // con la muestra actual (waitMs >= 10 y waitCalls >= 30).
        final double waitMs = s.mtvuWaitMs();
        final double waitCalls = s.mtvuWaitCalls();
        final boolean vuWaitSustained;
        if (trend != null) {
            vuWaitSustained = trend.sustained(LoadTrend.COND_VU_WAIT, 3)
                    && waitMs >= p.vuWaitMs && waitCalls >= p.vuWaitCalls;
        } else {
            vuWaitSustained = waitMs >= p.vuWaitMs && waitCalls >= p.vuWaitCalls;
        }
        if (vuWaitSustained) {
            return new Verdict(LoadState.VU_BOUND, 85,
                    "VU_WAIT sostenido: " + fmt(waitMs) + "ms, " + (long)waitCalls + " llamadas",
                    Action.BLOCK_RESOLUTION_DROP);
        }

        // 6. GPU metrica ausente (bit0 = 0 o gpuPct <= 0,5)
        // Un driver sin metrica GPU no puede provocar recortes.
        final boolean gpuMetricMissing = !s.gpuMetricValid() || gpu <= p.gpuMetricMissingPct;
        if (gpuMetricMissing) {
            if (s.emuMetricValid() && ee >= p.emuHotPct) {
                return new Verdict(LoadState.CPU_BOUND, 55,
                        "GPU metrica ausente + EE caliente (" + fmt(ee) + "%)",
                        Action.BLOCK_RESOLUTION_DROP);
            }
            return new Verdict(LoadState.UNKNOWN_LOAD, 15,
                    "GPU metrica ausente, EE no caliente",
                    Action.HOLD);
        }

        // 7. Ambas calientes → la mas llena gana
        final boolean gpuHot = gpu >= p.gpuHotPct;
        final boolean eeHot = ee >= p.emuHotPct;
        if (gpuHot && eeHot) {
            final double margin = Math.abs(gpu - ee);
            // La fuerza de esta decision ES el margen, no una base mas un extra:
            // con GPU 96 / EE 78 (margen 18) la etapa dominante se distingue a
            // simple vista; con GPU 86 / EE 85.6 no hay nada que distinguir y una
            // decision de recorte a partir de ese numero es lanzar una moneda.
            // 40 + 3 por punto de margen, tope 85: el umbral de recorte
            // (confAllowDrop=55) queda entonces aproximadamente donde cae el
            // margen de dominancia (4 pp) mas una muestra decente. Asi el margen
            // y la confianza dicen lo mismo, y ninguno de los dos es decorativo.
            final int conf = (int) Math.min(85, 40 + margin * 3.0);

            if (gpu > ee) {
                return new Verdict(LoadState.GPU_BOUND, conf,
                        "GPU " + fmt(gpu) + "% > EE " + fmt(ee) + "% (margen " + fmt(margin) + "%)",
                        Action.ALLOW_RESOLUTION_DROP);
            } else {
                // Empate o EE > GPU: el empate va a emulacion (regla de 0.6.29)
                return new Verdict(LoadState.CPU_BOUND, conf,
                        "EE " + fmt(ee) + "% >= GPU " + fmt(gpu) + "% (empate a emulacion)",
                        Action.BLOCK_RESOLUTION_DROP);
            }
        }

        // 8. Solo GPU caliente → GPU_BOUND; solo EE caliente → CPU_BOUND
        if (gpuHot) {
            return new Verdict(LoadState.GPU_BOUND, 80,
                    "GPU " + fmt(gpu) + "% >= " + fmt(p.gpuHotPct) + "%",
                    Action.ALLOW_RESOLUTION_DROP);
        }
        if (eeHot) {
            return new Verdict(LoadState.CPU_BOUND, 80,
                    "EE " + fmt(ee) + "% >= " + fmt(p.emuHotPct) + "%",
                    Action.BLOCK_RESOLUTION_DROP);
        }

        // 9. GPU <= piso de holgura (medido, bit0=1) → CPU_BOUND
        if (gpu <= p.gpuIdlePct) {
            return new Verdict(LoadState.CPU_BOUND, 65,
                    "GPU " + fmt(gpu) + "% <= " + fmt(p.gpuIdlePct) + "% (holgada)",
                    Action.BLOCK_RESOLUTION_DROP);
        }

        // 10. Zona intermedia → UNKNOWN_LOAD, HOLD
        return new Verdict(LoadState.UNKNOWN_LOAD, 25,
                "METRICS_MIXED: GPU " + fmt(gpu) + "%, EE " + fmt(ee) + "%",
                Action.HOLD);
    }

    /**
     * Regla 5 del prompt: ninguna accion automatica con confianza por debajo del
     * umbral definido PARA ESA ACCION. Sin esto, la confianza era un numero que
     * se imprimia pero no mandaba nada: el governor habria recortado igual con
     * un 20%. Umbral por accion y no uno global, porque las acciones no cuestan
     * lo mismo:
     *  - ALLOW_RESOLUTION_DROP (>= confAllowDrop): es la unica que DANIA la
     *    imagen y la que se aprende en el perfil. Exige la barra mas alta.
     *  - BLOCK_RESOLUTION_DROP (>= confLearn): no estropea nada, solo impide un
     *    recorte. Su riesgo es dejar el juego lento por pasarse de conservadora,
     *    asi que lleva la barra baja pero no cero: con 20% tampoco hay que
     *    convencer al governor de que ignore la GPU para siempre.
     *  - HOLD / WAIT_FOR_EVIDENCE: no actuan, no necesitan barra.
     * Si no alcanza la barra, el ESTADO se conserva (lo que vimos es lo que
     * vimos) y la accion se degrada a WAIT_FOR_EVIDENCE, que el governor lee como
     * "no aprender nada de este tick": sin eso, una CPU_BOUND de confianza 50
     * seguiria sumando cpuBoundTicks y convirtiendo una lectura floja en memoria
     * permanente del juego.
     */
    static Verdict gate(Verdict v, LoadPolicy p) {
        if (v == null) return v;
        final int need;
        switch (v.action) {
            case ALLOW_RESOLUTION_DROP: need = p.confAllowDrop; break;
            case BLOCK_RESOLUTION_DROP: need = p.confLearn; break;
            default: return v; // HOLD y WAIT_FOR_EVIDENCE no actuan por si solas
        }
        if (v.confidencePct >= need) return v;
        return new Verdict(v.state, v.confidencePct,
                v.reason + " | accion degradada: conf " + v.confidencePct + " < " + need,
                Action.WAIT_FOR_EVIDENCE);
    }

    /** Formateo de numero para razones. */
    private static String fmt(double v) {
        if (Double.isNaN(v)) return "NaN";
        if (Double.isInfinite(v)) return "Inf";
        long i = (long) v;
        if (Math.abs(v - i) < 0.05) return String.valueOf(i);
        return String.format(java.util.Locale.US, "%.1f", v);
    }

    // --- Puente legacy (Bottleneck / classifyLegacy / TALLY_SLOTS): ELIMINADO ---
    //
    // Estaba marcado como "transitorio para la 0.6.30" y en esta version se va.
    // Por que era peligroso dejarlo vivo: hacia la MISMA pregunta que classify()
    // pero con constantes propias (GPU_SATURATED_PCT, GPU_IDLE_PCT) y sin
    // confianza, sin FLAGS y sin termico. Un llamante nuevo que lo encontrara en
    // el autocompletado obtendria un veredicto que se salta la Regla 5 y puede
    // recortar imagen sin permiso. El governor ya no lo llama; AdaptiveProfile ya
    // usa los codigos del contrato (CenitTelemetry) y traduce el tally viejo de 4
    // casillas al leerlo, asi que aqui no queda nada que conservar.
    //
    // Las constantes que SI se quedan son frameBudgetMs/FALLBACK_TARGET_FPS: las
    // usa la evidencia nueva para imprimir el presupuesto (Regla 2: se muestra,
    // no decide).

    /** Objetivo de cuadros por defecto si el GS aun no publica uno. */
    public static final float FALLBACK_TARGET_FPS = 50f;

    /**
     * Presupuesto de cuadro, para el log. OJO: la frecuencia VERTICAL que publica
     * el modo de video, asi que en un juego entrelazado a 30 cuadros este numero
     * es la mitad del presupuesto real por cuadro. Por eso es un dato para leer
     * la evidencia, no un criterio de decision.
     */
    public static float frameBudgetMs(float targetFps) {
        return 1000f / (targetFps > 5f ? targetFps : FALLBACK_TARGET_FPS);
    }

    /**
     * Frase corta para el log con el veredicto nuevo (0.6.30). Lleva los numeros
     * con los que se puede interpretar la decision desde un log ajeno: el ms de
     * GPU por present (NaN visible como "sin-presents", porque un 0 mentiria), la
     * frecuencia VERTICAL con su presupuesto y el P50/P95 del anillo de 150
     * presents del core.
     *
     * Las dos ultimas lineas son la razon por la que este metodo existe aparte
     * del Verdict.reason: el prompt llama "dos intervalos distintos" a la ventana
     * de 0,5 s y al anillo de presents, y mezclarlos en una sola cifra es como
     * sumar velocidad y odometro. Por eso el P95 va etiquetado "(anillo)", nunca
     * se compara contra el presupuesto, y la frecuencia vertical se imprime junto
     * a la palabra "presupuesto" para que nadie la lea como fps del juego
     * (Regla 2).
     */
    public static String evidence(Verdict v, CenitLoadSample s) {
        if (s == null || !s.isValid()) {
            // Sin muestra no hay numeros que imprimir; la razon del veredicto ya
            // dice por que ("muestra nula", "ventana recortada", "campo NaN").
            return v.state + " conf=" + v.confidencePct + "% " + v.reason;
        }
        final java.util.Locale us = java.util.Locale.US;
        final double gpuMs = s.gpuAvgMs();
        final String ms = (Double.isNaN(gpuMs) || Double.isInfinite(gpuMs))
                ? "sin-presents" : String.format(us, "%.1f", gpuMs);
        final double vert = s.verticalHz();
        final String p95 = Double.isNaN(s.frametimeP95Ms())
                ? "sin-anillo" : String.format(us, "%.1f", s.frametimeP95Ms()) + "ms(anillo)";
        return v.state + " conf=" + v.confidencePct + "% acc=" + v.action
                + " speed=" + fmt(s.speedPct()) + "% fps=" + fmt(s.fps())
                + " gpu=" + fmt(s.gpuPct()) + "%/" + ms + "ms"
                + " ee=" + fmt(s.eePct()) + "%"
                + " vuwait=" + fmt(s.mtvuWaitMs()) + "ms/" + (long) s.mtvuWaitCalls()
                + " presents=" + (long) s.presents() + " ventana=" + fmt(s.windowSeconds()) + "s"
                + " flags=0x" + Integer.toHexString(s.flags())
                + " p50=" + (Double.isNaN(s.frametimeP50Ms()) ? "sin-anillo"
                        : String.format(us, "%.1f", s.frametimeP50Ms()) + "ms(anillo)")
                + " p95=" + p95
                + " vertical=" + String.format(us, "%.1f", vert) + "Hz"
                + " presupuesto=" + String.format(us, "%.1f", frameBudgetMs((float) vert)) + "ms"
                + " | " + v.reason;
    }
}
