package com.izzy2lost.psx2;

/**
 * Cenit 0.6.29 — Clasificador universal de carga de juego (invento nuestro).
 *
 * El encargo fue claro: no quiero una lista de nombres. Quiero que CUALQUIER
 * disco del catálogo de PS2 se juegue lo mejor posible en Snapdragon 6/7/8, y
 * que la decisión salga de medir, no de lo que yo sepa sobre ese juego. Esta
 * clase es ese mecanismo: no hay ni un serial, ni un título, ni una tabla.
 *
 * **¿Qué problema real resuelve?** El regidor de resolución necesita saber si un
 * retraso es de PÍXELES (bajar resolución recupera cuadros) o de EMULACIÓN
 * (bajar resolución solo empeora la imagen gratis). Desde 0.6.4 esa decisión se
 * tomaba comparando getGPUUsage() contra 0,65 y 0,05 — y getGPUUsage() devuelve
 * PORCENTAJE (0..100), no fracción: con 60 Hz y GPU saturada, la fórmula del core
 * da 30 cuadros x 16,6 ms / (0,5 s x 10) = 99,6, y el propio HUD lo imprime con
 * "%". Las dos consecuencias, verificadas rama por rama en
 * DynamicResolutionGovernor.evaluate():
 *   - "GPU ociosa" (gpu < 0,65) solo se declaraba con la GPU por debajo del
 *     0,65% — o sea, parada. Un juego limitado por la CPU emulada con la GPU al
 *     8% (lo habitual en un menú o en un juego de escena simple) NO entraba ahí.
 *   - Como 8 >= 0,65, caía en la rama GPU_BOUND: slowTicks++ y BAJAR resolución.
 * Es decir, el regidor recortaba imagen en TODOS los casos con métrica viva,
 * incluidos aquellos en los que recortar no recupera ni un cuadro y solo estropea
 * la imagen. La etiqueta CPU_BOUND — la guarda que el diseño pedía — casi nunca
 * se emitía, y con ella tampoco la señal cpuBoundNow que el sintonizador CPU/VU de
 * 0.6.27 recibe ya calculada: por eso el benchmark por juego era tan raro de
 * disparar en un teléfono real. (0.6.13 creyó tocar este punto añadiendo
 * UNKNOWN_GPU para gpu <= 0,05; eso solo cubrió el valor exacto 0 — que es justo
 * el caso "driver sin tiempo de GPU" — y dejó el resto del rango igual de roto.)
 *
 * **Por qué esto puede ser universal sin tabla de nombres.** La métrica del core
 * NO es "cuánto tarda un cuadro", es cuánto tiempo REAL del reloj está la GPU
 * ocupada:
 *   s_gpu_usage = gpu_ms_acumulados_en_la_ventana / (ventana_s * 10)
 * o sea ms de GPU por cada 1000 ms de pared, en porcentaje. La del hilo EE se
 * calcula con el MISMO denominador (GetCPUTimeToPCTFactor sobre el delta de
 * tiempo de reloj de la ventana), así que las dos son fracciones del mismo reloj y
 * se pueden comparar entre sí. Eso es lo que hace la regla universal, y lo que
 * verificó el bug de 0.6.4: un "92%" significa "la GPU tiene lleno el 92% del
 * tiempo" en un juego a 60, en uno a 50 (PAL) y en un entrelazado que presenta
 * campo a campo — el número ya no depende de saber qué disco está delante ni de
 * adivinar su presupuesto. Con las dos etapas medidas, la que manda en una
 * canalización es la MÁS llena: si el cuadro llega tarde con la GPU al 95% y el EE
 * al 70%, el que no da más de sí es el sintetizador y recortar píxeles recupera
 * cuadros; al revés (EE 95%, GPU 70%) recortar solo estropea la imagen. Y si las
 * dos están por debajo de su llenado, ningún número de estos separa los casos:
 * UNKNOWN, que es la decisión barata de equivocar.
 *
 * **Lo que NO usamos, y por escrito para que no vuelva.** La tentación era
 * comparar los ms por cuadro contra un "presupuesto" 1000/fps. Se verificó contra
 * el core y hay dos trampas: (a) GetVerticalFrequency() publica la frecuencia
 * VERTICAL del modo de vídeo (59,94 / 59,83 progresivo / 50 / 49,76 / 60), NUNCA
 * 30, y PerformanceMetrics::Update se llama desde GSRenderer::VSync(field, ...) —
 * o sea campo a campo en entrelazado. "Cuadro" en los ms de GPU y "cuadro" en
 * 1000/fps no son la misma unidad para un juego que diseña a 30, así que la
 * comparación no está bien definida: se prestaría a declarar "píxeles" con un
 * número que no significa lo que parece; (b) el % de GPU y los ms de GPU salen del
 * MISMO acumulador (s_gpu_usage = acm/(ventana_s*10), s_average_gpu_time =
 * acm/presents), así que es matemáticamente imposible tener "% sin métrica"
 * (>0,5%) con ms altos: una rama que comparase ms "cuando falta el %" sería código
 * muerto que finge decidir. Y los ms además pueden llegar NaN — si en la ventana
 * de 0,5 s no hubo ni un present, el core divide acm/0 —, otra razón para no
 * gobernar con ellos.
 * El presupuesto se sigue IMPRIMIENDO en la evidencia, porque es un número que
 * ayuda a leer el log, pero ninguna decisión depende de él.
 *
 * Ninguna decisión de esta clase escribe ajustes ni toca el motor: CLASIFICA.
 * Quien actúa es el regidor (píxeles) y el sintonizador CPU/VU (emulación), con
 * sus propias guardas de reversibilidad de siempre.
 */
final class GameLoadClassifier {

    private GameLoadClassifier() {}

    /** En qué cree el clasificador que está el cuello. */
    enum Bottleneck {
        /** GPU saturada: bajar resolución AYUDA. */
        GPU,
        /** EE trabajándose el cuadro con la GPU holgada: bajar resolución NO ayuda. */
        EMULATION,
        /** El EE bloqueado esperando al VU1 (MTVU puesto): hay que revisar el hilo. */
        VU_WAIT,
        /** Sin lecturas que autoricen a actuar, o contradictorias: no inventar. */
        UNKNOWN;

        /**
         * Casilla del tally que guarda AdaptiveProfile.loadMix. Se escribe a mano
         * (no ordinal()) porque el perfil persiste estos índices en sus prefs: si
         * mañana se reordena la enum, un ordinal silencioso mezclaría los
         * veredictos de partidas anteriores con los de ahora.
         */
        int tallySlot() {
            switch (this) {
                case GPU: return 0;
                case EMULATION: return 1;
                case VU_WAIT: return 2;
                default: return 3;
            }
        }
    }

    /** Casillas de {@link Bottleneck#tallySlot()}: una por veredicto posible. */
    static final int TALLY_SLOTS = 4;

    // ------------------------------------------------------------------
    // Umbrales, todos en PORCENTAJE (0..100) porque así llega la métrica.
    // Cada número lleva su porqué medible, no su gusto.
    // ------------------------------------------------------------------

    /** GPU por encima de esto con el juego atrasado: está ocupando casi todo el
     *  reloj, recortar píxeles recupera cuadros. 85 y no 100 porque un teléfono
     *  con vsync/present nunca llega al 100: el hueco es el propio present. */
    static final float GPU_SATURATED_PCT = 85f;

    /** GPU por debajo de esto con el juego atrasado: está holgada la mayor parte
     *  del reloj, así que el retraso no puede ser de píxeles. 35 deja un margen
     *  grande por debajo de "saturada" para no cambiar de opinión a cada muestra. */
    static final float GPU_IDLE_PCT = 35f;

    /** EE (hilo de emulación) por encima de esto: la CPU emulada es el cuello. */
    static final float EE_BUSY_PCT = 80f;

    /** La métrica de GPU "no existe todavía": driver que no reporta tiempo de GPU,
     *  o GS recién abierto. Antes valía 0,05 porque se creía fracción. No es lo
     *  mismo que "GPU ociosa": con 0 no se sabe nada, y eso va a UNKNOWN. */
    static final float GPU_METRIC_MISSING_PCT = 0.5f;

    // ------------------------------------------------------------------
    // Lo que sigue NO entra en ninguna decisión: son números para leer el log.
    // ------------------------------------------------------------------

    /** Objetivo de cuadros por defecto si el GS aún no publica uno: 50, el
     *  presupuesto MÁS holgado de los modos reales. Solo afecta al número que se
     *  IMPRIME en la evidencia, nunca a la decisión (ver el bloque "Lo que NO
     *  usamos" de arriba), así que no puede inclinar ningún veredicto. */
    static final float FALLBACK_TARGET_FPS = 50f;

    /**
     * Clasifica con las lecturas del motor en la mano. Puro: no lee prefs, no
     * escribe nada, no llama al nativo. Así se puede ejercitar en el arnés con
     * números sintéticos de los casos que más importan (GPU-bound real, y
     * emulación-cuello con GPU holgada).
     *
     * La firma NO lleva ms de GPU ni el objetivo de cuadros del juego: con ellos
     * no se decide nada (ver el bloque "Lo que NO usamos" del encabezado) y poner
     * un parámetro que no manda es exactamente cómo se cuela una regla mal
     * definida. Esos dos números sí van a evidence(), que es donde aportan.
     *
     * @param speedPct     % de velocidad de emulación (100 = a tiempo).
     * @param gpuUsagePct  uso de GPU EN PORCENTAJE de reloj real (getGPUUsage).
     * @param eeUsagePct   uso del hilo EE en % (0 = sin métrica o EE parado).
     * @param mtvuWaitMs   ms que el EE quedó bloqueado esperando al VU1.
     * @param mtvuWaitCalls número de esas esperas en la ventana.
     */
    static Bottleneck classify(float speedPct, float gpuUsagePct, float eeUsagePct,
                               float mtvuWaitMs, long mtvuWaitCalls) {
        return classify(speedPct, gpuUsagePct, eeUsagePct, mtvuWaitMs, mtvuWaitCalls,
                GPU_IDLE_PCT);
    }

    /**
     * Versión con el umbral de "GPU holgada" ajustado por evidencia aprendida.
     * El regidor la usa cuando el perfil del juego ya demostró que el cuello es
     * de emulación: SUBIR el umbral (no bajarlo) hace más fácil seguir
     * declarándolo y por tanto seguir sosteniendo la escala, que es lo que esa
     * evidencia debe comprar. Cenit 0.6.29: el governor hacía lo contrario —
     * multiplicaba por 0,8, y así el aprendizaje de la partida anterior jugaba en
     * contra del juego, recortándole píxeles otra vez. Se corrige la dirección;
     * el factor (1,2) es el mismo que ya estaba escrito.
     *
     * Se expone separada para que el arnés pueda ejercitar las dos curvas (umbral
     * por defecto y umbral con evidencia) sobre los mismos números.
     */
    static Bottleneck classify(float speedPct, float gpuUsagePct, float eeUsagePct,
                               float mtvuWaitMs, long mtvuWaitCalls, float gpuIdlePct) {
        // Sin juego delante o con la consola arrancando no hay nada que clasificar.
        // Se escribe !(speedPct > 0f) y NO speedPct <= 0f a propósito: con NaN la
        // segunda forma es FALSA y el juego pasaría entero por delante de las
        // guardas, pudiendo acabar declarado "píxeles" sobre una lectura que no es
        // una lectura. !(x > 0) captura NaN, 0 y negativos de un solo golpe.
        if (!(speedPct > 0f)) return Bottleneck.UNKNOWN;
        // A tiempo: no hay cuello que buscar. Devolver UNKNOWN (no "GPU") para que
        // nadie recorte píxeles que no hacen falta.
        if (speedPct >= 99.5f) return Bottleneck.UNKNOWN;

        // El VU1 primero: si el EE está BLOQUEADO esperándolo, el resto de las
        // lecturas miente (el EE aparece poco ocupado porque no trabaja, espera;
        // y la GPU baja porque no le llegan cuadros). Prioridad sobre el %.
        if (mtvuWaitMs >= 10f && mtvuWaitCalls >= 30L) return Bottleneck.VU_WAIT;

        // ¿La métrica de GPU existe? Con <= 0,5% de reloj real no se puede
        // distinguir "parada" de "el driver no reporta" — y salen del mismo
        // acumulador, así que los ms tampoco aportan. Aquí manda el EE si trae
        // datos; si no, UNKNOWN: es exactamente la guarda que 0.6.13 pidió y no
        // llegó a cubrir, y evita aprender cpuBound a partir de la nada.
        if (gpuUsagePct <= GPU_METRIC_MISSING_PCT) {
            if (eeUsagePct >= EE_BUSY_PCT) return Bottleneck.EMULATION;
            return Bottleneck.UNKNOWN;
        }

        // Con métrica viva hay dos etapas medidas, cada una como fracción del MISMO
        // reloj real y en la MISMA ventana de 0,5 s: la GPU y el hilo EE. El cuadro
        // de PS2 se construye en el EE y se gasta en la GPU, y las dos etapas corren
        // a la vez, así que el rendimiento del conjunto lo manda la etapa MAS
        // OCUPADA — la otra todavía tiene holgura que nadie está usando. De ahí
        // sale el criterio de la zona donde las dos están calientes: se compara
        // cuál llena más el reloj y se le apunta a esa. No es una suma (sumar dos
        // porcentajes de recursos distintos no da nada: "GPU 60% + EE 40% = 100%"
        // sería decir que el teléfono está lleno cuando cada uno está a medias), es
        // un máximo, y un máximo sí es la definición de cuello en una canalización.
        final boolean gpuHot = gpuUsagePct >= GPU_SATURATED_PCT;
        final boolean eeHot = eeUsagePct >= EE_BUSY_PCT;
        if (gpuHot && eeHot) {
            // Las dos llenas: manda la que MAS llena el reloj. Con GPU 95 / EE 82
            // recortar píxeles libera la etapa que estaba por delante y recupera
            // cuadros; con GPU 88 / EE 99 la GPU vuelve a sobrar enseguida y el que
            // no da más de sí es el EE, así que recortar sería estropear la imagen
            // sin comprar cuadros. El EMPATE va a EMULATION (comparación estricta):
            // sin una etapa visiblemente por delante, recortar es la única acción
            // que daña algo, y esta clase no daña por no tener motivo.
            return gpuUsagePct > eeUsagePct ? Bottleneck.GPU : Bottleneck.EMULATION;
        }
        if (gpuHot) return Bottleneck.GPU;

        // EE lleno con la GPU por debajo de su umbral: la emulación manda.
        if (eeHot) return Bottleneck.EMULATION;

        // GPU visiblemente holgada (medida, no ausente) y el juego atrasado: el
        // retraso no puede ser de píxeles. Aquí no hace falta saber el nombre del
        // disco: una GPU que ocupa el 15% del reloj no está costando cuadros.
        if (gpuUsagePct <= gpuIdlePct) return Bottleneck.EMULATION;

        // Zona intermedia: ninguna etapa llena, la GPU visiblemente parada tampoco.
        // UNKNOWN a propósito, no por falta de ideas. GPU al 60% con EE al 40% es
        // la MISMA lectura tanto si manda la GPU como si manda el EE, como si hay
        // un throttle que aún no se declaró o una espera de disco: los números que
        // llegan aquí no separan esos casos y recortar imagen en el peor de ellos
        // no recupera ni un cuadro. Declarar EMULATION tampoco es neutro: tras 15
        // ticks el governor lo convierte en profile.cpuBound y esa memoria juega a
        // favor de no recortar aunque la evidencia no la sostuvo.
        //
        // El coste de este UNKNOWN es una mejora que a veces se queda sobre la
        // mesa. Consciente, y con una salida medida: la etiqueta nueva
        // "DynRes state: UNKNOWN_LOAD" imprime gpu%/ee%, así que si los Snapdragon
        // 6/7 resultan pasar muchos ticks aquí con la GPU alta, eso es LA curva que
        // falta para volver a mirar GPU_SATURATED_PCT — no una opinión mía.
        return Bottleneck.UNKNOWN;
    }

    /**
     * Presupuesto de cuadro, para el log y para el bloque FÁBRICA del reporte.
     * OJO con lo que es: la frecuencia VERTICAL que publica el modo de vídeo, así
     * que en un juego entrelazado a 30 cuadros este número es la mitad del
     * presupuesto real por cuadro. Por eso es un dato para leer la evidencia, no
     * un criterio de decisión (ver el encabezado de la clase).
     */
    static float frameBudgetMs(float targetFps) {
        return 1000f / (targetFps > 5f ? targetFps : FALLBACK_TARGET_FPS);
    }

    /** Frase corta para el log y para FÁBRICA: qué se vio y por qué. */
    static String evidence(Bottleneck b, float speedPct, float gpuUsagePct, float gpuAvgMs,
                           float eeUsagePct, float targetFps) {
        final java.util.Locale us = java.util.Locale.US;
        // El ms de GPU puede llegar NaN: en la ventana de 0,5 s el core lo divide
        // entre el número de presents y con cero presents la división es 0/0. No
        // es un error que haya que esconder, pero "gpuMs=NaN" en un log que alguien
        // va a leer desde un teléfono se escribe mejor como "sin presents".
        final String ms = (Float.isNaN(gpuAvgMs) || Float.isInfinite(gpuAvgMs))
                ? "sin-presents" : String.format(us, "%.1f", gpuAvgMs);
        return b + " speed=" + String.format(us, "%.0f", speedPct)
                + "% gpu=" + String.format(us, "%.1f", gpuUsagePct)
                + "%/" + ms + "ms"
                + " ee=" + String.format(us, "%.0f", eeUsagePct)
                + "% vertical=" + String.format(us, "%.1f", targetFps > 5f ? targetFps : FALLBACK_TARGET_FPS)
                + "Hz presupuesto=" + String.format(us, "%.1f", frameBudgetMs(targetFps)) + "ms";
    }
}
