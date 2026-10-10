package com.izzy2lost.psx2;

/**
 * Arnés 0.6.30 — clasificador universal de carga con snapshot unificado.
 *
 * Ejercita la nueva API: CenitLoadSample, LoadPolicy, LoadTrend, GameLoadClassifier.
 * Cubre los 7 estados, umbrales por ambos lados, el bug histórico, NaN, muestras
 * inválidas, VU_WAIT sostenido, confianza, LoadTrend (mediana, descarte, EWMA,
 * calibración), y regresión de unidades.
 */
public class Harness5 {

    static int checks = 0, failures = 0;

    static void check(boolean ok, String name) {
        checks++;
        if (!ok) { failures++; System.out.println("FALLO: " + name); }
    }

    static void eqState(GameLoadClassifier.LoadState got,
                        GameLoadClassifier.LoadState want, String name) {
        checks++;
        if (got != want) {
            failures++;
            System.out.println("FALLO: " + name + " (esperado " + want + ", obtenido " + got + ")");
        }
    }

    static void eqAction(GameLoadClassifier.Action got,
                         GameLoadClassifier.Action want, String name) {
        checks++;
        if (got != want) {
            failures++;
            System.out.println("FALLO: " + name + " (esperado " + want + ", obtenido " + got + ")");
        }
    }

    static void eqf(double got, double want, String name) {
        checks++;
        if (Math.abs(got - want) > 0.01) {
            failures++;
            System.out.println("FALLO: " + name + " (esperado " + want + ", obtenido " + got + ")");
        }
    }

    /** Construye una muestra válida con flags completos (todos los bits a 1). */
    static CenitLoadSample sample(double fps, double speed, double gpu, double ee,
                                   double waitMs, double waitCalls) {
        double[] d = new double[24];
        d[CenitLoadSample.IDX_FPS] = fps;
        d[CenitLoadSample.IDX_SPEED_PCT] = speed;
        d[CenitLoadSample.IDX_GPU_PCT] = gpu;
        d[CenitLoadSample.IDX_EE_PCT] = ee;
        d[CenitLoadSample.IDX_MTVU_WAIT_MS] = waitMs;
        d[CenitLoadSample.IDX_MTVU_WAIT_CALLS] = waitCalls;
        d[CenitLoadSample.IDX_PRESENTS] = 25; // ventana completa
        d[CenitLoadSample.IDX_WINDOW_S] = 0.5;
        d[CenitLoadSample.IDX_FLAGS] = 15; // todos los bits de validez
        return new CenitLoadSample(d);
    }

    /** Clasifica con la política por defecto y thermal=0. */
    static GameLoadClassifier.Verdict classify(CenitLoadSample s) {
        return GameLoadClassifier.classify(s, LoadPolicy.defaults(), 0);
    }

    public static void main(String[] args) {
        final GameLoadClassifier.LoadState UNK = GameLoadClassifier.LoadState.UNKNOWN_LOAD;
        final GameLoadClassifier.LoadState NO_BT = GameLoadClassifier.LoadState.NO_BOTTLENECK;
        final GameLoadClassifier.LoadState GPU = GameLoadClassifier.LoadState.GPU_BOUND;
        final GameLoadClassifier.LoadState CPU = GameLoadClassifier.LoadState.CPU_BOUND;
        final GameLoadClassifier.LoadState VU = GameLoadClassifier.LoadState.VU_BOUND;
        final GameLoadClassifier.LoadState TH = GameLoadClassifier.LoadState.THERMAL_BOUND;
        final GameLoadClassifier.LoadState INV = GameLoadClassifier.LoadState.INVALID_SAMPLE;

        final GameLoadClassifier.Action HOLD = GameLoadClassifier.Action.HOLD;
        final GameLoadClassifier.Action ALLOW = GameLoadClassifier.Action.ALLOW_RESOLUTION_DROP;
        final GameLoadClassifier.Action BLOCK = GameLoadClassifier.Action.BLOCK_RESOLUTION_DROP;
        final GameLoadClassifier.Action WAIT = GameLoadClassifier.Action.WAIT_FOR_EVIDENCE;

        // --------------------------------------------------------------
        // 1) MUESTRAS INVÁLIDAS → INVALID_SAMPLE, WAIT_FOR_EVIDENCE
        // --------------------------------------------------------------
        eqState(classify(null).state, INV, "muestra null = INVALID_SAMPLE");
        eqAction(classify(null).action, WAIT, "muestra null → WAIT_FOR_EVIDENCE");
        eqState(classify(CenitLoadSample.invalid("test")).state, INV, "muestra invalid() = INVALID_SAMPLE");
        eqState(classify(new CenitLoadSample(new double[10])).state, INV, "longitud incorrecta = INVALID_SAMPLE");

        // Sin ventana completa (bit2=0)
        double[] d = new double[24];
        d[CenitLoadSample.IDX_SPEED_PCT] = 70;
        d[CenitLoadSample.IDX_GPU_PCT] = 90;
        d[CenitLoadSample.IDX_FLAGS] = 11; // 1011: bit0=1, bit1=1, bit2=0, bit3=1
        eqState(classify(new CenitLoadSample(d)).state, INV, "sin windowComplete = INVALID_SAMPLE");

        // Sin sampleFresh
        d[CenitLoadSample.IDX_FLAGS] = 6; // bit3=0, sin SAMPLE_FRESH
        eqState(classify(new CenitLoadSample(d)).state, INV, "sin sampleFresh = INVALID_SAMPLE");
        // Sin métricas válidas
        d[CenitLoadSample.IDX_FLAGS] = 12; // bit0=0, bit1=0, bit2=1, bit3=1
        eqState(classify(new CenitLoadSample(d)).state, INV, "sin metricas validas = INVALID_SAMPLE");

        // --------------------------------------------------------------
        // 2) SPEED NO REAL (NaN o <= 0) → INVALID_SAMPLE
        // --------------------------------------------------------------
        eqState(classify(sample(60, Double.NaN, 90, 50, 0, 0)).state, INV, "speed NaN = INVALID_SAMPLE");
        eqState(classify(sample(60, 0, 90, 50, 0, 0)).state, INV, "speed 0 = INVALID_SAMPLE");
        eqState(classify(sample(60, -10, 90, 50, 0, 0)).state, INV, "speed negativo = INVALID_SAMPLE");

        // --------------------------------------------------------------
        // 3) THERMAL >= 2 CON RETRASO → THERMAL_BOUND, BLOCK_RESOLUTION_DROP
        // --------------------------------------------------------------
        GameLoadClassifier.Verdict v = GameLoadClassifier.classify(sample(60, 70, 90, 50, 0, 0),
                LoadPolicy.defaults(), 2);
        eqState(v.state, TH, "thermal=2 con retraso = THERMAL_BOUND");
        eqAction(v.action, BLOCK, "THERMAL_BOUND → BLOCK_RESOLUTION_DROP");
        v = GameLoadClassifier.classify(sample(60, 70, 90, 50, 0, 0), LoadPolicy.defaults(), 3);
        eqState(v.state, TH, "thermal=3 con retraso = THERMAL_BOUND");
        // Sin retraso no es THERMAL_BOUND
        v = GameLoadClassifier.classify(sample(60, 100, 90, 50, 0, 0), LoadPolicy.defaults(), 2);
        check(v.state != TH, "thermal=2 sin retraso NO es THERMAL_BOUND");

        // --------------------------------------------------------------
        // 4) SPEED >= 99,5 → NO_BOTTLENECK
        // --------------------------------------------------------------
        eqState(classify(sample(60, 99.5, 90, 50, 0, 0)).state, NO_BT, "speed 99,5 = NO_BOTTLENECK");
        eqState(classify(sample(60, 100, 90, 50, 0, 0)).state, NO_BT, "speed 100 = NO_BOTTLENECK");
        eqAction(classify(sample(60, 100, 90, 50, 0, 0)).action, HOLD, "NO_BOTTLENECK → HOLD");

        // --------------------------------------------------------------
        // 5) VU_WAIT SOSTENIDO → VU_BOUND
        // --------------------------------------------------------------
        // Sin LoadTrend, VU_WAIT se decide con la muestra actual
        v = classify(sample(60, 70, 90, 50, 15, 50));
        eqState(v.state, VU, "VU_WAIT sin trend = VU_BOUND");
        eqAction(v.action, BLOCK, "VU_BOUND → BLOCK_RESOLUTION_DROP");
        // Con LoadTrend, requiere sostenido (3 ventanas)
        LoadTrend trend = new LoadTrend();
        trend.add(sample(60, 70, 90, 50, 15, 50), false, false, false, 0);
        trend.add(sample(60, 70, 90, 50, 15, 50), false, false, false, 0);
        trend.add(sample(60, 70, 90, 50, 15, 50), false, false, false, 0);
        v = GameLoadClassifier.classify(sample(60, 70, 90, 50, 15, 50), LoadPolicy.defaults(), 0, trend);
        eqState(v.state, VU, "VU_WAIT sostenido 3 ventanas = VU_BOUND");
        // Una ventana sola NO lo dispara con trend
        trend = new LoadTrend();
        trend.add(sample(60, 70, 90, 50, 15, 50), false, false, false, 0);
        v = GameLoadClassifier.classify(sample(60, 70, 90, 50, 15, 50), LoadPolicy.defaults(), 0, trend);
        check(v.state != VU, "VU_WAIT una ventana sola con trend NO es VU_BOUND");

        // --------------------------------------------------------------
        // 6) GPU MÉTRICA AUSENTE → CPU_BOUND (si EE caliente) o UNKNOWN_LOAD
        // --------------------------------------------------------------
        // bit0=0 (GPU no válida)
        d = new double[24];
        d[CenitLoadSample.IDX_SPEED_PCT] = 70;
        d[CenitLoadSample.IDX_GPU_PCT] = 0;
        d[CenitLoadSample.IDX_EE_PCT] = 90;
        d[CenitLoadSample.IDX_PRESENTS] = 25;
        d[CenitLoadSample.IDX_WINDOW_S] = 0.5;
        d[CenitLoadSample.IDX_FLAGS] = 14; // bit0=0, bit1=1, bit2=1, bit3=1
        v = classify(new CenitLoadSample(d));
        eqState(v.state, CPU, "GPU ausente + EE caliente = CPU_BOUND");
        check(v.confidencePct == 55, "GPU ausente + EE caliente → confianza 55");
        // GPU ausente + EE frío
        d[CenitLoadSample.IDX_EE_PCT] = 40;
        v = classify(new CenitLoadSample(d));
        eqState(v.state, UNK, "GPU ausente + EE frío = UNKNOWN_LOAD");

        // --------------------------------------------------------------
        // 7) AMBAS CALIENTES → la más llena gana
        // --------------------------------------------------------------
        // 7b) REGLA 5 DEL PROMPT: la confianza GUBIERNA la accion, no es un adorno
        // --------------------------------------------------------------
        // Margen chico (GPU 88 / EE 85 => 3 pp): conf = 40+9 = 49 < 55 -> la
        // ACCION se degrada, el ESTADO se conserva. Si esto no pasara, el
        // governor recortaria la imagen con una lectura que no distingue las
        // dos etapas: es exactamente el error de 0.6.4 pero en miniatura.
        v = classify(sample(60, 70, 88, 85, 0, 0));
        eqState(v.state, GPU, "margen 3pp: el estado sigue siendo GPU_BOUND");
        eqAction(v.action, WAIT, "margen 3pp y conf < 55 -> accion degradada");
        check(v.reason.contains("accion degradada"), "la razon dice POR QUE se degradó");
        // Margen suficiente (GPU 95 / EE 88 => 7 pp): conf = 40+21 = 61 >= 55
        v = classify(sample(60, 70, 95, 88, 0, 0));
        eqAction(v.action, ALLOW, "margen 7pp y conf >= 55 -> permite recorte");
        // HOLD no se degrada nunca: no actua, no necesita barra
        v = classify(sample(60, 70, 60, 40, 0, 0));
        eqAction(v.action, HOLD, "UNKNOWN_LOAD + HOLD no se degrada (no actua)");
        // BLOCK con confianza baja TAMPOCO se degrada si pasa confLearn (40):
        // bloquear un recorte cuesta menos que hacerlo, y esa asimetria es la
        // razon de tener dos barras en vez de una.
        v = classify(sample(60, 70, 92, 92, 0, 0)); // empate -> conf 40
        eqAction(v.action, BLOCK, "empate con conf 40 = confLearn: bloquea igual");

        // --------------------------------------------------------------
        // GPU > EE + margen
        v = classify(sample(60, 70, 95, 82, 0, 0));
        eqState(v.state, GPU, "GPU 95 > EE 82 = GPU_BOUND");
        eqAction(v.action, ALLOW, "GPU_BOUND → ALLOW_RESOLUTION_DROP");
        check(v.confidencePct >= 70, "GPU > EE con margen → confianza alta");
        // EE > GPU
        v = classify(sample(60, 70, 88, 99, 0, 0));
        eqState(v.state, CPU, "EE 99 > GPU 88 = CPU_BOUND");
        eqAction(v.action, BLOCK, "CPU_BOUND → BLOCK_RESOLUTION_DROP");
        // Empate → CPU_BOUND (regla de 0.6.29)
        v = classify(sample(60, 70, 92, 92, 0, 0));
        eqState(v.state, CPU, "empate GPU=EE = CPU_BOUND (empate a emulación)");

        // --------------------------------------------------------------
        // 8) SOLO GPU CALIENTE → GPU_BOUND; SOLO EE CALIENTE → CPU_BOUND
        // --------------------------------------------------------------
        v = classify(sample(60, 70, 90, 50, 0, 0));
        eqState(v.state, GPU, "solo GPU caliente = GPU_BOUND");
        v = classify(sample(60, 70, 50, 90, 0, 0));
        eqState(v.state, CPU, "solo EE caliente = CPU_BOUND");

        // --------------------------------------------------------------
        // 9) GPU <= PISO DE HOLGURA → CPU_BOUND
        // --------------------------------------------------------------
        v = classify(sample(60, 70, 35, 50, 0, 0));
        eqState(v.state, CPU, "GPU 35 <= piso 35 = CPU_BOUND");
        v = classify(sample(60, 70, 20, 50, 0, 0));
        eqState(v.state, CPU, "GPU 20 <= piso 35 = CPU_BOUND");

        // --------------------------------------------------------------
        // 10) ZONA INTERMEDIA → UNKNOWN_LOAD, HOLD
        // --------------------------------------------------------------
        v = classify(sample(60, 70, 50, 40, 0, 0));
        eqState(v.state, UNK, "GPU 50 + EE 40 = UNKNOWN_LOAD");
        eqAction(v.action, HOLD, "UNKNOWN_LOAD → HOLD");
        check(v.reason.contains("METRICS_MIXED"), "UNKNOWN_LOAD reason contiene METRICS_MIXED");

        // --------------------------------------------------------------
        // 11) EL BUG HISTÓRICO: gpu=7%, ee=99% → CPU_BOUND, NUNCA recorte
        // --------------------------------------------------------------
        v = classify(sample(60, 70, 7, 99, 0, 0));
        eqState(v.state, CPU, "BUG: GPU 7% + EE 99% = CPU_BOUND (NUNCA recorte)");
        eqAction(v.action, BLOCK, "BUG: CPU_BOUND → BLOCK_RESOLUTION_DROP");
        // gpu=96%, ee=30% → GPU_BOUND
        v = classify(sample(60, 60, 96, 30, 0, 0));
        eqState(v.state, GPU, "GPU 96% + EE 30% = GPU_BOUND");
        eqAction(v.action, ALLOW, "GPU_BOUND → ALLOW_RESOLUTION_DROP");

        // --------------------------------------------------------------
        // 12) UMBRALES POR AMBOS LADOS
        // --------------------------------------------------------------
        // GPU hot: 84,9 vs 85
        v = classify(sample(60, 70, 84.9, 50, 0, 0));
        check(v.state != GPU, "GPU 84,9 < 85 NO es GPU_BOUND");
        v = classify(sample(60, 70, 85, 50, 0, 0));
        eqState(v.state, GPU, "GPU 85 >= 85 = GPU_BOUND");
        // EE hot: 79,9 vs 80
        v = classify(sample(60, 70, 50, 79.9, 0, 0));
        check(v.state != CPU || v.reason.contains("holgada"), "EE 79,9 < 80 NO es CPU_BOUND por eeHot");
        v = classify(sample(60, 70, 50, 80, 0, 0));
        eqState(v.state, CPU, "EE 80 >= 80 = CPU_BOUND");
        // GPU idle: 35 vs 35,1
        v = classify(sample(60, 70, 35, 50, 0, 0));
        eqState(v.state, CPU, "GPU 35 <= 35 = CPU_BOUND (holgada)");
        v = classify(sample(60, 70, 35.1, 50, 0, 0));
        eqState(v.state, UNK, "GPU 35,1 > 35 = UNKNOWN_LOAD (zona intermedia)");
        // Speed target: 99,4 vs 99,5
        v = classify(sample(60, 99.4, 90, 50, 0, 0));
        check(v.state != NO_BT, "speed 99,4 < 99,5 NO es NO_BOTTLENECK");
        v = classify(sample(60, 99.5, 90, 50, 0, 0));
        eqState(v.state, NO_BT, "speed 99,5 >= 99,5 = NO_BOTTLENECK");

        // --------------------------------------------------------------
        // 13) NaN EN CADA CAMPO NUMÉRICO → INVALID_SAMPLE, nunca GPU_BOUND
        // --------------------------------------------------------------
        final double nan = Double.NaN;
        eqState(classify(sample(nan, 70, 90, 50, 0, 0)).state, INV, "fps NaN = INVALID_SAMPLE");
        eqState(classify(sample(60, nan, 90, 50, 0, 0)).state, INV, "speed NaN = INVALID_SAMPLE");
        eqState(classify(sample(60, 70, nan, 50, 0, 0)).state, INV, "gpu NaN = INVALID_SAMPLE");
        eqState(classify(sample(60, 70, 90, nan, 0, 0)).state, INV, "ee NaN = INVALID_SAMPLE");
        eqState(classify(sample(60, 70, 90, 50, nan, 0)).state, INV, "waitMs NaN = INVALID_SAMPLE");
        eqState(classify(sample(60, 70, 90, 50, 0, nan)).state, INV, "waitCalls NaN = INVALID_SAMPLE");

        // --------------------------------------------------------------
        // 14) DESCRIBE Y EVIDENCIA CON FORMATO
        // --------------------------------------------------------------
        CenitLoadSample s = sample(60, 70, 90, 50, 0, 0);
        String desc = s.describe();
        check(desc.contains("gpu="), "describe contiene gpu=");
        check(desc.contains("ee="), "describe contiene ee=");
        check(desc.contains("speed="), "describe contiene speed=");
        check(desc.contains("presents="), "describe contiene presents=");
        check(desc.contains("flags="), "describe contiene flags=");
        v = classify(s);
        String ev = GameLoadClassifier.evidence(v, s);
        check(ev.contains("GPU_BOUND"), "evidence contiene el estado");
        check(ev.contains("conf="), "evidence contiene conf=");
        check(ev.contains("gpu="), "evidence contiene gpu=");
        check(ev.contains("ee="), "evidence contiene ee=");

        // --------------------------------------------------------------
        // 15) LOADTREND: MEDIANA, DESCARTE, EWMA
        // --------------------------------------------------------------
        trend = new LoadTrend();
        trend.add(sample(60, 70, 80, 50, 0, 0), false, false, false, 0);
        trend.add(sample(55, 75, 85, 55, 0, 0), false, false, false, 0);
        trend.add(sample(65, 65, 75, 45, 0, 0), false, false, false, 0);
        eqf(trend.medianFps(), 60, "mediana de FPS = 60");
        eqf(trend.medianGpuPct(), 80, "mediana de GPU = 80");
        eqf(trend.medianEePct(), 50, "mediana de EE = 50");
        // Descarte por pausa
        trend = new LoadTrend();
        trend.add(sample(60, 70, 80, 50, 0, 0), true, false, false, 0);
        check(trend.discardedCount() == 1, "pausa descarta 1 muestra");
        check((trend.discardMask() & LoadTrend.DISCARD_PAUSED) != 0, "descarte por pausa en mask");
        // Descarte por turbo
        trend = new LoadTrend();
        trend.add(sample(60, 70, 80, 50, 0, 0), false, true, false, 0);
        check((trend.discardMask() & LoadTrend.DISCARD_TURBO) != 0, "descarte por turbo en mask");
        // Descarte por NaN
        trend = new LoadTrend();
        trend.add(sample(60, nan, 80, 50, 0, 0), false, false, false, 0);
        check((trend.discardMask() & LoadTrend.DISCARD_NAN) != 0, "descarte por NaN en mask");
        // EWMA converge
        trend = new LoadTrend();
        for (int i = 0; i < 20; i++) {
            trend.add(sample(60, 70, 80, 50, 0, 0), false, false, false, 2);
        }
        check(Math.abs(trend.thermalEwma() - 2) < 0.5, "EWMA converge a 2 tras 20 ticks");

        // --------------------------------------------------------------
        // 16) CALIBRACIÓN: MÍNIMOS
        // --------------------------------------------------------------
        trend = new LoadTrend();
        // 2 juegos → provisional
        for (int i = 0; i < 10; i++) {
            trend.observe("game1", "sess1", sample(60, 70, 80, 50, 0, 0));
            trend.observe("game2", "sess1", sample(60, 70, 85, 55, 0, 0));
        }
        LoadPolicy pol = trend.suggestPolicy();
        check(pol.provisional, "2 juegos → politica provisional");
        // 3 juegos × 5 ventanas × 2 sesiones → sugiere
        trend = new LoadTrend();
        for (int g = 1; g <= 3; g++) {
            for (int sess = 1; sess <= 2; sess++) {
                for (int i = 0; i < 5; i++) {
                    trend.observe("game" + g, "sess" + sess, sample(60, 70, 80 + g, 50 + g, 0, 0));
                }
            }
        }
        pol = trend.suggestPolicy();
        check(!pol.provisional, "3 juegos × 5 ventanas × 2 sesiones → politica NO provisional");

        // --------------------------------------------------------------
        // 17) REGRESIÓN DE UNIDADES: verificar que el umbral correcto es 85
        // --------------------------------------------------------------
        // Este test verifica que el piso de "GPU saturada" es 85 POR CIENTO y no
        // 0.65 (bug histórico: se comparaba el porcentaje del core contra una
        // fraccion). Desde 0.6.30 el umbral vive en LoadPolicy, no en una
        // constante del clasificador, y hay UNA sola copia.
        eqf((float) LoadPolicy.defaults().gpuHotPct, 85f,
                "REGRESION: gpuHotPct debe ser 85, no 0.65");
        eqf((float) LoadPolicy.defaults().gpuIdlePct, 35f,
                "REGRESION: gpuIdlePct debe ser 35, no 0.35");
        
        // Verificar que con umbral correcto (85), GPU 7% + EE 99% da CPU_BOUND
        v = classify(sample(60, 70, 7, 99, 0, 0));
        eqState(v.state, CPU, "REGRESION: con umbral correcto 85, GPU 7% + EE 99% = CPU_BOUND");
        
        // Verificar que con umbral incorrecto (0.65), el resultado sería diferente
        // Con gpuHotPct=0.65, GPU 7% NO sería "hot" (7 >= 0.65 es true, pero 7 < 85)
        // Entonces caería en GPU_IDLE (7 <= 35) → CPU_BOUND igualmente
        // El bug real era comparar porcentajes (0-100) contra fracciones (0-1)
        LoadPolicy wrongPolicy = new LoadPolicy.Builder().gpuHotPct(0.65).build();
        v = GameLoadClassifier.classify(sample(60, 70, 7, 99, 0, 0), wrongPolicy, 0);
        // Con umbral 0.65, GPU 7% >= 0.65 es true, pero como 7 < 85 (valor real),
        // sigue siendo CPU_BOUND por la rama GPU_IDLE
        check(v.state == CPU, "REGRESION: incluso con umbral 0.65, GPU 7% + EE 99% = CPU_BOUND");

        // --------------------------------------------------------------
        // 18) EL PUENTE LEGACY YA NO EXISTE: los tres numeros que antes devolvia
        //     classifyLegacy se deciden ahora por la escalera de classify(), con
        //     FLAGS y con confianza. Se conservan las tres parejas de cifras del
        //     caso viejo para que la comparacion entre versiones sea literal.
        // --------------------------------------------------------------
        eqState(classify(sample(42, 70, 7, 99, 0, 0)).state, CPU,
                "7 / 99: el cuello es la emulacion (antes EMULATION)");
        eqAction(classify(sample(36, 70, 7, 99, 0, 0)).action, BLOCK,
                "y esa lectura PROHIBE recortar (antes no habia accion)");
        eqState(classify(sample(36, 60, 98, 45, 0, 0)).state, GPU,
                "GPU 98: pixeles (antes GPU)");
        eqAction(classify(sample(36, 60, 98, 45, 0, 0)).action, ALLOW,
                "y con evidencia suficiente si autoriza recortar");
        // VU: el case legacy usaba waitMs=40 / calls=120, por encima del umbral.
        // La diferencia es que ahora hace falta SOSTENEBERLO (trend) o que la
        // muestra sola alcance el umbral, y la accion es BLOCK, no "no se".
        v = classify(sample(36, 60, 95, 10, 40, 120));
        eqState(v.state, VU, "VU_WAIT 40ms/120 llamadas: VU_BOUND (antes VU_WAIT)");
        eqAction(v.action, BLOCK, "y bajar resolucion no esta autorizado");
        // Regla 5 sobre el puente viejo: las mismas cifras del EMULATION legado
        // con EE "caliente" por un pelo NO autorizan todavia. El legacy no tenia
        // manera de decir "todavia no lo sé"; este si.
        v = GameLoadClassifier.classify(sample(36, 60, 84.5, 80.5, 0, 0),
                LoadPolicy.defaults(), 0);
        check(v.action != ALLOW,
                "REGRA 5: margen 4pp no autoriza recortar aunque el legacy dijera GPU: "
                        + v.state + " conf=" + v.confidencePct + " " + v.action);

        System.out.println("Harness5: " + checks + " Comprobaciones, " + failures + " Fallos");
        System.exit(failures == 0 ? 0 : 1);
    }
}
