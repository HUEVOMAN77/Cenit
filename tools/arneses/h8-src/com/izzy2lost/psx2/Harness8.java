package com.izzy2lost.psx2;

import android.content.Context;
import android.content.SharedPreferences;

import java.lang.reflect.Field;
import java.lang.reflect.Method;

/**
 * Arnes 0.6.31 (FASE 1 del prompt directo) — el SINTONIZADOR REAL, de punta a punta.
 *
 * Por que hace falta un arnes nuevo: h5 y h7 son del clasificador, h6 del regidor,
 * y LOS TRES sustituyen CpuVUTuner por un stub. O sea que hasta aqui ningun arnes
 * compilaba el CpuVUTuner.java del repo, y "compila" es la primera condicion de
 * aceptacion del prompt. Este compila CpuVUTuner y TunerProfileStore del repo junto
 * con AdaptiveProfile y el clasificador de verdad, contra un mock del nativo cuyo
 * INI tiene estado (global y por-juego, con la distincion EXISTE / NO EXISTE): sin
 * esa distincion es imposible probar el diario anti-cierre.
 *
 * Cubre las tres categorias que exige el prompt:
 *  - unitarias: modos y migracion, diccionario de estados, cada puerta de gates(),
 *    la escalera de candidatos con todos sus filtros, y CADA criterio de decision
 *    evaluado aparte (ganancia, P95, minimo, speed, audio, termico, evidencia
 *    incompleta).
 *  - integracion: ensayo A/B/A/B completo -> adopcion -> post-verificacion, con el
 *    clasificador real como dueno del veredicto, y la prueba de que las ventanas de
 *    ensayo NO mueven heldScale ni slowFloorTicks ni la calibracion.
 *  - cierre forzoso: diario huerfano (proceso muerto) -> recoverAtBoot restaura los
 *     valores previos EXACTOS, BORRA la clave que Cenit creo y no existia, no toca
 *    claves ajenas, suspende ESE candidato y el HUD lo cuenta como RECOVERED.
 *
 * Lo que este arnes NO puede dar, y el prompt lo exige aparte: la evidencia en
 * hardware real de un juego CPU-bound y otro VU-bound adoptados. Aqui las FPS las
 * fabrica el caso; en el telefono tienen que salir del core. Un verde aqui prueba
 * la maquina, no la ganancia.
 */
public class Harness8 {

    static int checks = 0, failures = 0;

    static final String URI  = "saf://games/SLUS_210.79 (Game).iso";
    static final String URI2 = "saf://games/SLPM_620.01 (Otro).iso";
    static final String SEC  = "EmuCore/Speedhacks";
    static final String[] KEYN = {"EECycleRate", "EECycleSkip", "vuThread"};

    static Context ctx;
    static SharedPreferences prefs;

    // ==================================================================
    // utilidades
    // ==================================================================
    static void check(boolean ok, String name) {
        checks++;
        if (!ok) { failures++; System.out.println("FALLO: " + name); }
    }

    static void eqi(int got, int want, String name) {
        checks++;
        if (got != want) { failures++; System.out.println("FALLO: " + name + " (esperado " + want + ", obtenido " + got + ")"); }
    }

    static void eqs(String got, String want, String name) {
        checks++;
        if (!want.equals(got)) { failures++; System.out.println("FALLO: " + name + " (esperado '" + want + "', obtenido '" + got + "')"); }
    }

    static void eqf(float got, float want, String name) {
        checks++;
        if (!(Math.abs(got - want) <= 0.05f)) { failures++; System.out.println("FALLO: " + name + " (esperado " + want + ", obtenido " + got + ")"); }
    }

    static Field fld(String name) {
        try {
            final Field f = CpuVUTuner.class.getDeclaredField(name);
            f.setAccessible(true);
            return f;
        } catch (Exception e) {
            throw new RuntimeException("el CpuVUTuner del repo ya no tiene el campo '" + name
                    + "': el arnes se quedo viejo y eso es un cambio de contrato", e);
        }
    }

    static Object get(Object o, String name) {
        try { return fld(name).get(o); } catch (IllegalAccessException e) { throw new RuntimeException(e); }
    }

    static void set(Object o, String name, Object v) {
        try { fld(name).set(o, v); } catch (IllegalAccessException e) { throw new RuntimeException(e); }
    }

    static int gi(Object o, String name) { return ((Number) get(o, name)).intValue(); }

    /** Constante private del tuner: el arnes la LEE en vez de repetirla a mano. */
    static int st(String name) {
        try { return fld(name).getInt(null); }
        catch (Exception e) { throw new RuntimeException("constante '" + name + "' fuera", e); }
    }
    static float gf(Object o, String name) { return ((Number) get(o, name)).floatValue(); }
    static int[] garr(Object o, String name) { return (int[]) get(o, name); }

    static void putF(Object o, String name, int idx, float v) { ((float[]) get(o, name))[idx] = v; }

    static boolean callB(Object o, String method) {
        try {
            final Method m = CpuVUTuner.class.getDeclaredMethod(method);
            m.setAccessible(true);
            return (Boolean) m.invoke(o);
        } catch (Exception e) { throw new RuntimeException(method + ": " + e, e); }
    }

    static void call(Object o, String method) {
        try {
            final Method m = CpuVUTuner.class.getDeclaredMethod(method);
            m.setAccessible(true);
            m.invoke(o);
        } catch (Exception e) {
            Throwable c = e.getCause() == null ? e : e.getCause();
            throw new RuntimeException("llamada a " + method + " fallo: " + c, c);
        }
    }

    /** Pref de Java (voluntad del usuario): Rate y MTVU tienen spinner; Skip no. */
    static void prefBool(String k, boolean v) { prefs.edit().putBoolean(k, v).apply(); }
    static void prefInt(String k, int v) { prefs.edit().putInt(k, v).apply(); }
    static void prefRemove(String k) { prefs.edit().remove(k).apply(); }

    /** Global del INI simulado; `absent` = la clave no esta (no es un 0). */
    static void glob(String key, Integer v) {
        if (v == null) NativeApp.global.remove(SEC + "|" + key);
        else NativeApp.global.put(SEC + "|" + key, v);
    }

    static int gk(String key) {
        final Integer v = NativeApp.global.get(SEC + "|" + key);
        return v == null ? -999 : v;
    }

    static int gkGame(String uri, String key) {
        final Integer v = NativeApp.perGame.get(uri + "|" + SEC + "|" + key);
        return v == null ? -999 : v;
    }

    static int gkOther(String section, String key) {
        final Integer v = NativeApp.global.get(section + "|" + key);
        return v == null ? -999 : v;
    }

    /** Contexto y mock limpios + sesion arrancada. Cada caso empieza de cero. */
    static void boot() {
        NativeApp.reset();
        ctx = new Context();
        prefs = ctx.getSharedPreferences("app_prefs", Context.MODE_PRIVATE);
        NativeApp.currentUri = URI;
        NativeApp.crcByUri.put(URI, "AB12CD34");
        NativeApp.crcByUri.put(URI2, "EE56FF78");
    }

    static CpuVUTuner fresh() {
        boot();
        final CpuVUTuner t = new CpuVUTuner(ctx, prefs);
        t.startSession();
        NativeApp.writes.clear();
        NativeApp.tunerPublished.clear();
        gainM = gainR = gainS = 1.0;
        ignoreCandidate = false;
        return t;
    }

    static CenitLoadSample sample() { return new CenitLoadSample(NativeApp.safeGetCenitSample()); }

    /** Veredicto del CLASIFICADOR REAL: el tuner no acepta opiniones del arnes. */
    static GameLoadClassifier.Verdict realVerdict(CenitLoadSample s) {
        return GameLoadClassifier.classify(s, LoadPolicy.defaults(), 0);
    }

    /** Banderas de entorno del caso: el regidor se las pasa al tuner en cada tick. */
    static boolean scenTurbo = false, scenSettling = false, scenThermal = false;

    static int tick(CpuVUTuner t, AdaptiveProfile p) {
        NativeApp.currentUri = URI;
        final CenitLoadSample s = sample();
        // superblockBusy viene del MOCK, no del caso: es la misma lectura que hace el
        // regidor (NativeApp.safeSuperblockValidating). Falsa aqui habria convertido el
        // caso del superbloque en un verde por accidente.
        return t.tick(p, URI, 1f, s, realVerdict(s), 0, true, true, true,
                scenTurbo, scenSettling, scenThermal, NativeApp.safeSuperblockValidating());
    }

    /** split(-1): el ultimo campo (identidad) puede venir vacio y aun es un campo. */
    static String[] pub(int i) { return NativeApp.tunerPublished.get(i).split(",", -1); }

    static int firstWrite(String prefix) {
        for (int i = 0; i < NativeApp.writes.size(); i++)
            if (NativeApp.writes.get(i).startsWith(prefix)) return i;
        return -1;
    }

    static boolean wroteSomething(String prefix) { return firstWrite(prefix) >= 0; }

    static int countWrites(String exact) {
        int n = 0;
        for (String w : NativeApp.writes) if (w.equals(exact)) n++;
        return n;
    }

    // ==================================================================
    // escenario de FPS: el candidato rinde segun SU ganancia medida
    // ==================================================================
    static double gainM = 1.0, gainR = 1.0, gainS = 1.0;
    static boolean ignoreCandidate = false;

    /**
     * Fija fps/speed segun la tripleta EFECTIVA que el mock lee de su propio INI.
     * El arnes NUNCA le dice al tuner en que ventana esta: el tuner lo deduce del
     * INI, y el INI es justamente lo que se esta probando. Si el ensayo se saltara
     * la aplicacion, la ventana B mediria la base y nada de esto cuadra.
     */
    static void autoMetrics(int[] base) {
        double k = 1.0;
        if (!ignoreCandidate && base != null) {
            if (NativeApp.safeGetEffectiveMTVU() != base[2]) k = Math.max(k, gainM);
            if (NativeApp.safeGetEffectiveEECycleRate() != base[0]) k = Math.max(k, gainR);
            if (NativeApp.safeGetEffectiveEECycleSkip() != base[1]) k = Math.max(k, gainS);
        }
        NativeApp.fps = (float) (40.0 * k);
        NativeApp.speedPct = (float) (80.0 * k);
    }

    static boolean runUntil(CpuVUTuner t, AdaptiveProfile p, int wantState, int maxTicks, int[] base) {
        for (int i = 0; i < maxTicks; i++) {
            autoMetrics(base);
            tick(t, p);
            if (t.hudState() == wantState) return true;
        }
        return false;
    }

    static boolean reachedState(CpuVUTuner t, AdaptiveProfile p, int wantState, int maxTicks, int[] base) {
        for (int i = 0; i < maxTicks; i++) {
            autoMetrics(base);
            tick(t, p);
            for (String s : NativeApp.tunerPublished)
                if (Integer.parseInt(s.split(",")[0]) == wantState) return true;
        }
        return false;
    }

    // ==================================================================
    // 1. modos y migracion (secciones 1 y 10)
    // ==================================================================
    static void testModes() {
        boot();
        // SIN ninguna preferencia: AUTO_SAFE por defecto. Eso NO es mover ajustes.
        eqi(CpuVUTuner.modeOf(prefs), CpuVUTuner.MODE_AUTO_SAFE, "el modo por defecto es AUTO_SAFE");
        eqi(CpuVUTuner.DEFAULT_MODE, CpuVUTuner.MODE_AUTO_SAFE, "DEFAULT_MODE sigue AUTO_SAFE");

        final SharedPreferences p2 = new SharedPreferences();
        p2.edit().putBoolean(CpuVUTuner.PREF_KEY, false).apply();
        eqi(CpuVUTuner.modeOf(p2), CpuVUTuner.MODE_OFF, "interruptor viejo apagado sigue apagado");
        p2.edit().putBoolean(CpuVUTuner.PREF_KEY, true).apply();
        eqi(CpuVUTuner.modeOf(p2), CpuVUTuner.MODE_AUTO_SAFE, "interruptor viejo encendido = AUTO_SAFE");
        p2.edit().putInt(CpuVUTuner.PREF_MODE, CpuVUTuner.MODE_AUTO_MTVU_ONLY).apply();
        eqi(CpuVUTuner.modeOf(p2), CpuVUTuner.MODE_AUTO_MTVU_ONLY, "el int gana a la booleana");
        CpuVUTuner.setMode(p2, CpuVUTuner.MODE_AUTO_NO_RATE_SKIP);
        eqi(CpuVUTuner.modeOf(p2), CpuVUTuner.MODE_AUTO_NO_RATE_SKIP, "setMode escribe el int");
        check(!p2.contains(CpuVUTuner.PREF_KEY), "setMode retira la booleana (un solo dueno de la clave)");
        eqs(CpuVUTuner.PREF_MODE, "cpu_tuner_mode", "clave fijada (la lee PlayabilityBundle como int)");

        eqi(CpuVUTuner.clampMode(-5), CpuVUTuner.MODE_AUTO_SAFE, "clampMode(-5) = default");
        eqi(CpuVUTuner.clampMode(99), CpuVUTuner.MODE_AUTO_SAFE, "clampMode(99) = default");
        eqi(CpuVUTuner.clampMode(0), CpuVUTuner.MODE_OFF, "clampMode(0) = OFF");
        eqi(CpuVUTuner.modeOf(p2), 4, "AUTO_NO_RATE_SKIP es el ordinal 4 (arrays.xml)");
        eqs(CpuVUTuner.modeName(CpuVUTuner.MODE_AUTO_SAFE), "AUTO_SAFE", "nombre AUTO_SAFE");
        eqs(CpuVUTuner.modeName(CpuVUTuner.MODE_AUTO_MTVU_ONLY), "AUTO_MTVU", "nombre AUTO_MTVU");
        eqs(CpuVUTuner.modeName(CpuVUTuner.MODE_AUTO_NO_RATE_SKIP), "AUTO_SAFE_sinRS", "nombre sinRS");
        eqs(CpuVUTuner.modeName(CpuVUTuner.MODE_MANUAL), "MANUAL", "nombre MANUAL");
        eqs(CpuVUTuner.modeName(CpuVUTuner.MODE_OFF), "OFF", "nombre OFF");
        eqi(CpuVUTuner.MODE_COUNT, 5, "5 modos = 5 entradas del spinner");

        // OFF y MANUAL no ensayan NUNCA, ni con todas las puertas abiertas.
        for (int mode : new int[]{CpuVUTuner.MODE_OFF, CpuVUTuner.MODE_MANUAL}) {
            final CpuVUTuner t = fresh();
            CpuVUTuner.setMode(prefs, mode);
            NativeApp.mtvuDefault = 1;
            glob("vuThread", 0);
            prefBool("mtvu", false);
            gainM = 1.30;
            final AdaptiveProfile p = new AdaptiveProfile(ctx, URI);
            int action = -1;
            for (int i = 0; i < 60; i++) action = tick(t, p);
            eqi(action, CpuVUTuner.IDLE, "modo " + CpuVUTuner.modeName(mode) + " responde IDLE");
            eqi(t.hudState(), mode == CpuVUTuner.MODE_OFF ? CpuVUTuner.ST_OFF : CpuVUTuner.ST_MANUAL,
                    "modo " + CpuVUTuner.modeName(mode) + " pinta SU propio estado, no IDLE");
            check(NativeApp.writes.isEmpty(),
                    "modo " + CpuVUTuner.modeName(mode) + " no escribe nada en el INI");
        }

        // AUTO_SAFE por defecto: abrir la app no mueve ni una preferencia.
        boot();
        final CpuVUTuner t0 = new CpuVUTuner(ctx, prefs);
        t0.startSession();
        check(NativeApp.writes.isEmpty(), "startSession en AUTO_SAFE no escribe ninguna cuota");
        check(!prefs.contains(CpuVUTuner.PREF_MODE), "startSession no planta cpu_tuner_mode");
        eqi(pub(0).length, 9, "la publicacion del HUD lleva los 9 campos del contrato");
        eqi(Integer.parseInt(pub(0)[1]), CpuVUTuner.MODE_AUTO_SAFE, "el HUD publica el modo vigente");
        eqi(Integer.parseInt(pub(0)[3]), -1, "sin medicion el FPS 'antes' es -1, no 0");
        eqi(Integer.parseInt(pub(0)[4]), -1, "sin medicion el FPS 'despues' es -1, no 0");
        eqf(Float.parseFloat(pub(0)[5]), -1f, "sin medicion el P50 es -1, no 0.0");
    }

    // ==================================================================
    // 2. diccionario de estados (contrato Java <-> ImGuiOverlays.cpp)
    // ==================================================================
    static void testStates() {
        final String[] want = {"OFF", "MANUAL", "IDLE", "LEARNING", "MEASURING_BASELINE",
                "MEASURING_CANDIDATE", "VERIFYING_ADOPTION", "ADOPTED", "REJECTED",
                "SUSPENDED", "POST_VERIFY", "RECOVERED"};
        eqi(CpuVUTuner.STATE_COUNT, want.length, "STATE_COUNT == longitudes parejas (kTunerStates=12)");
        for (int i = 0; i < want.length; i++) {
            eqs(CpuVUTuner.STATE_NAMES[i], want[i], "STATE_NAMES[" + i + "]");
            eqs(CpuVUTuner.stateName(i), want[i], "stateName(" + i + ")");
        }
        eqs(CpuVUTuner.stateName(99), "STATE_??", "estado fuera de rango NO se inventa");
        eqs(CpuVUTuner.stateName(-1), "STATE_??", "estado negativo NO se inventa");
        eqi(CpuVUTuner.ST_OFF, 0, "ST_OFF=0"); eqi(CpuVUTuner.ST_MANUAL, 1, "ST_MANUAL=1");
        eqi(CpuVUTuner.ST_IDLE, 2, "ST_IDLE=2"); eqi(CpuVUTuner.ST_LEARNING, 3, "ST_LEARNING=3");
        eqi(CpuVUTuner.ST_MEASURING_BASELINE, 4, "ST_MEASURING_BASELINE=4");
        eqi(CpuVUTuner.ST_MEASURING_CANDIDATE, 5, "ST_MEASURING_CANDIDATE=5");
        eqi(CpuVUTuner.ST_VERIFYING_ADOPTION, 6, "ST_VERIFYING_ADOPTION=6");
        eqi(CpuVUTuner.ST_ADOPTED, 7, "ST_ADOPTED=7"); eqi(CpuVUTuner.ST_REJECTED, 8, "ST_REJECTED=8");
        eqi(CpuVUTuner.ST_SUSPENDED, 9, "ST_SUSPENDED=9"); eqi(CpuVUTuner.ST_POST_VERIFY, 10, "ST_POST_VERIFY=10");
        eqi(CpuVUTuner.ST_RECOVERED, 11, "ST_RECOVERED=11");
        eqs(CpuVUTuner.thresholdSummary(),
                "MTVU +5.0% Rate +8.0% Skip +10.0% | P95<=1.02x | min>=0.88x | speed-1.0%"
                        + " | audio=0 | termico<=1 paso | post 45s",
                "resumen de umbrales (el que sale en el reporte de evidencia)");
        eqi(CpuVUTuner.WINDOW_SETTLE, 3, "asentamiento 3 s por ventana");
        eqi(CpuVUTuner.WINDOW_MEASURE, 8, "medicion 8 s por ventana");
        eqi(CpuVUTuner.GATE_STREAK, 8, "ocho muestras seguidas antes de aprender");
        eqi(CpuVUTuner.POST_VERIFY_TICKS, 45, "post-verificacion 45 s (prompt: 30-60)");
    }

    // ==================================================================
    // 3. puertas de gates() (secciones 3, 5 y 6)
    // ==================================================================
    static void testGates() {
        // (a) con todas las puertas: GATE_STREAK muestras seguidas antes de tocar nada
        CpuVUTuner t = fresh();
        AdaptiveProfile p = new AdaptiveProfile(ctx, URI);
        for (int i = 0; i < CpuVUTuner.GATE_STREAK - 1; i++) tick(t, p);
        eqi(t.hudState(), CpuVUTuner.ST_LEARNING, "puertas abiertas: el estado es LEARNING");
        check(NativeApp.writes.isEmpty(), "antes del streak no hay ni una escritura");
        tick(t, p);
        eqi(t.hudState(), CpuVUTuner.ST_MEASURING_BASELINE,
                "la muestra GATE_STREAK arranca el ensayo (ni una menos)");
        eqi(garr(t, "mBase").length, 3, "leida la tripleta base");
        eqi(garr(t, "mUser").length, 3, "leida la tripleta del usuario (rollback exacto)");

        // prenda de las demas pruebas: el escenario base SI es CPU_BOUND con BLOCK
        final CenitLoadSample good = sample();
        final GameLoadClassifier.Verdict cpu = realVerdict(good);
        eqi(cpu.state.ordinalForHud(), CenitTelemetry.STATE_CPU_BOUND, "escenario base CPU_BOUND");
        eqi(cpu.action.codeForHud(), 2, "escenario base BLOCK_RESOLUTION_DROP");
        check(new AdaptiveProfile(ctx, URI).known(), "el perfil por-juego existe");

        // (b) cada puerta por separado
        t = fresh();
        tickWith(t, "", cpu, good, true, true, true);
        eqi(t.hudState(), CpuVUTuner.ST_IDLE, "puerta: sin juego delante no ensaya");

        t = fresh();
        tickWithNullProfile(t, cpu, good, true, true, true);
        eqi(t.hudState(), CpuVUTuner.ST_IDLE, "puerta: sin memoria por juego no ensaya");

        t = fresh();
        tickWith(t, URI, cpu, good, false, true, true);
        eqi(t.hudState(), CpuVUTuner.ST_IDLE, "puerta: a tiempo no hay nada que recuperar");

        t = fresh();
        tickWith(t, URI, cpu, good, true, false, true);
        eqi(t.hudState(), CpuVUTuner.ST_IDLE, "puerta: fuera del piso es trabajo del regidor");

        for (int k = 0; k < 4; k++) {
            t = fresh();
            NativeApp.currentUri = URI;
            final String name = new String[]{"turbo", "paso de resolucion", "termica", "superblock"}[k];
            t.tick(new AdaptiveProfile(ctx, URI), URI, 1f, good, cpu, 0, true, true, true,
                    k == 0, k == 1, k == 2, k == 3);
            eqi(t.hudState(), CpuVUTuner.ST_IDLE, "puerta: " + name + " no ensaya");
        }

        t = fresh();
        tickWith(t, URI, cpu, CenitLoadSample.invalid("sin muestra"), true, true, false);
        eqi(t.hudState(), CpuVUTuner.ST_IDLE, "puerta: muestra invalida no ensaya");

        final double[] noEmu = NativeApp.safeGetCenitSample();
        noEmu[CenitLoadSample.IDX_FLAGS] = CenitLoadSample.FLAG_WINDOW_COMPLETE | CenitLoadSample.FLAG_SAMPLE_FRESH;
        t = fresh();
        tickWith(t, URI, cpu, new CenitLoadSample(noEmu), true, true, true);
        eqi(t.hudState(), CpuVUTuner.ST_IDLE, "puerta: metrica de emulacion invalida (Regla 3)");

        final double[] stale = NativeApp.safeGetCenitSample();
        stale[CenitLoadSample.IDX_FLAGS] = CenitLoadSample.FLAG_EMU_METRIC_VALID | CenitLoadSample.FLAG_GPU_METRIC_VALID;
        t = fresh();
        tickWith(t, URI, cpu, new CenitLoadSample(stale), true, true, true);
        eqi(t.hudState(), CpuVUTuner.ST_IDLE, "puerta: ventana incompleta o muestra repetida");

        t = fresh();
        NativeApp.currentUri = URI;
        t.tick(new AdaptiveProfile(ctx, URI), URI, 1f, good, null, 0, true, true, true,
                false, false, false, false);
        eqi(t.hudState(), CpuVUTuner.ST_IDLE, "puerta: sin veredicto del clasificador no ensaya");

        t = fresh();
        tickWith(t, URI, new GameLoadClassifier.Verdict(GameLoadClassifier.LoadState.GPU_BOUND, 80,
                "gpu", GameLoadClassifier.Action.BLOCK_RESOLUTION_DROP), good, true, true, true);
        eqi(t.hudState(), CpuVUTuner.ST_IDLE, "puerta: GPU_BOUND no es cuello de emulacion");

        t = fresh();
        tickWith(t, URI, new GameLoadClassifier.Verdict(GameLoadClassifier.LoadState.UNKNOWN_LOAD, 25,
                "mezcla", GameLoadClassifier.Action.HOLD), good, true, true, true);
        eqi(t.hudState(), CpuVUTuner.ST_IDLE, "puerta: UNKNOWN_LOAD no ensaya");

        t = fresh();
        tickWith(t, URI, new GameLoadClassifier.Verdict(GameLoadClassifier.LoadState.CPU_BOUND, 30,
                "flojo", GameLoadClassifier.Action.WAIT_FOR_EVIDENCE), good, true, true, true);
        eqi(t.hudState(), CpuVUTuner.ST_IDLE, "puerta: CPU_BOUND con accion degradada no ensaya (Regla 5)");

        // VU_BOUND con BLOCK SI ensaya (el prompt pide CPU-bound O VU-bound)
        t = fresh();
        NativeApp.mtvuDefault = 1;
        glob("vuThread", 1);
        prefBool("mtvu", true);
        NativeApp.mtvuWaitMs = 20f;
        NativeApp.mtvuWaitCalls = 40L;
        tickWith(t, URI, new GameLoadClassifier.Verdict(GameLoadClassifier.LoadState.VU_BOUND, 85,
                "vu", GameLoadClassifier.Action.BLOCK_RESOLUTION_DROP), sample(), true, true, true);
        eqi(t.hudState(), CpuVUTuner.ST_LEARNING, "puerta: VU_BOUND con BLOCK si ensaya");
        eqi(gi(t, "mWaitHits"), 1, "observeVUWait conto la espera EE->VU1");

        // carga de texturas en curso
        t = fresh();
        NativeApp.textureCount = 10L;
        tickWith(t, URI, cpu, good, true, true, true);
        NativeApp.textureCount = 13L;
        tickWith(t, URI, cpu, good, true, true, true);
        eqi(t.hudState(), CpuVUTuner.ST_IDLE, "puerta: texturas cambiando no ensaya");
        check(gi(t, "mGateStreak") == 0, "la puerta de texturas corta el streak");

        // lectura de texturas ausente (-1) NO es "carga terminada": no bloquea
        t = fresh();
        NativeApp.textureCount = -1L;
        for (int i = 0; i < CpuVUTuner.GATE_STREAK; i++) tickWith(t, URI, cpu, good, true, true, true);
        eqi(t.hudState(), CpuVUTuner.ST_MEASURING_BASELINE, "sin senal de texturas se puede aprender");

        // cambio de escena (salto del P50 > 25 %)
        t = fresh();
        tickWith(t, URI, cpu, good, true, true, true);
        NativeApp.p50Ms = 30.0;
        tickWith(t, URI, cpu, sample(), true, true, true);
        eqi(t.hudState(), CpuVUTuner.ST_IDLE, "puerta: escena cambiada no ensaya");

        // (c) la memoria de identidad manda
        final TunerProfileStore.Identity id = TunerProfileStore.identityOf(URI, ctx);
        check(TunerProfileStore.identityComplete(id), "identidad completa en el arnes");
        TunerProfileStore st = new TunerProfileStore(ctx, URI, id);
        TunerProfileStore.Record r = st.load();
        r.id = id.id; r.uri = URI; r.at = System.currentTimeMillis();

        r.verdict = TunerProfileStore.V_ADOPTED; r.adopted = "m=1"; st.save(r);
        t = fresh();
        st = new TunerProfileStore(ctx, URI, TunerProfileStore.identityOf(URI, ctx));
        r = st.load(); r.id = st.load().id; r.uri = URI; r.verdict = TunerProfileStore.V_ADOPTED;
        r.adopted = "m=1"; r.at = System.currentTimeMillis(); st.save(r);
        tickWith(t, URI, cpu, good, true, true, true);
        eqi(t.hudState(), CpuVUTuner.ST_IDLE, "adopcion en vigor: no se vuelve a ensayar");

        r.verdict = TunerProfileStore.V_SUSPENDED; r.adopted = ""; r.suspensions = 1;
        r.at = System.currentTimeMillis(); st.save(r);
        t = fresh();
        st = new TunerProfileStore(ctx, URI, TunerProfileStore.identityOf(URI, ctx));
        r = st.load(); r.id = TunerProfileStore.identityOf(URI, ctx).id; r.uri = URI;
        r.verdict = TunerProfileStore.V_SUSPENDED; r.at = System.currentTimeMillis(); st.save(r);
        tickWith(t, URI, cpu, good, true, true, true);
        eqi(t.hudState(), CpuVUTuner.ST_SUSPENDED, "suspendido reciente pinta SUSPENDED");

        r.verdict = TunerProfileStore.V_REJECTED; r.at = System.currentTimeMillis(); st.save(r);
        t = fresh();
        st = new TunerProfileStore(ctx, URI, TunerProfileStore.identityOf(URI, ctx));
        r = st.load(); r.id = TunerProfileStore.identityOf(URI, ctx).id; r.uri = URI;
        r.verdict = TunerProfileStore.V_REJECTED; r.at = System.currentTimeMillis(); st.save(r);
        tickWith(t, URI, cpu, good, true, true, true);
        eqi(t.hudState(), CpuVUTuner.ST_REJECTED, "rechazo reciente pinta REJECTED");

        r.at = System.currentTimeMillis() - 8L * 24 * 3600 * 1000; st.save(r);
        t = fresh();
        st = new TunerProfileStore(ctx, URI, TunerProfileStore.identityOf(URI, ctx));
        r = st.load(); r.id = TunerProfileStore.identityOf(URI, ctx).id; r.uri = URI;
        r.verdict = TunerProfileStore.V_REJECTED; r.at = System.currentTimeMillis() - 8L * 24 * 3600 * 1000;
        st.save(r);
        tickWith(t, URI, cpu, good, true, true, true);
        eqi(t.hudState(), CpuVUTuner.ST_LEARNING, "rechazo caducado (7 dias) vuelve a permitir aprender");

        // un registro de OTRA identidad no manda aqui
        final TunerProfileStore.Identity foreign = new TunerProfileStore.Identity(
                "otro|id", "SLUS-21079", "ZZZZZZZZ", "otro-soc", "0", "-", "0.6.31",
                TunerProfileStore.TUNER_VERSION);
        t = fresh();
        final TunerProfileStore fst = new TunerProfileStore(ctx, URI, foreign);
        final TunerProfileStore.Record fr = fst.load();
        fr.id = foreign.id; fr.uri = URI; fr.verdict = TunerProfileStore.V_ADOPTED;
        fr.adopted = "m=1"; fr.at = System.currentTimeMillis();
        fst.save(fr);
        tickWith(t, URI, cpu, good, true, true, true);
        eqi(t.hudState(), CpuVUTuner.ST_LEARNING, "la memoria viaja por identidad, no por juego");
    }

    static void tickWith(CpuVUTuner t, String uri, GameLoadClassifier.Verdict v, CenitLoadSample s,
                         boolean behind, boolean atFloor, boolean metrics) {
        NativeApp.currentUri = uri;
        t.tick(new AdaptiveProfile(ctx, uri.isEmpty() ? URI : uri), uri, 1f, s, v, 0,
                behind, atFloor, metrics, false, false, false, false);
    }

    static void tickWithNullProfile(CpuVUTuner t, GameLoadClassifier.Verdict v, CenitLoadSample s,
                                    boolean behind, boolean atFloor, boolean metrics) {
        NativeApp.currentUri = URI;
        t.tick(null, URI, 1f, s, v, 0, behind, atFloor, metrics, false, false, false, false);
    }

    // ==================================================================
    // 4. la ventana: Regla 3 aplicada a la muestra
    // ==================================================================
    static void testWindowMath() {
        final CpuVUTuner t = fresh();
        set(t, "mWindow", 0);
        set(t, "mBase", new int[]{0, 0, 0});
        set(t, "mCand", new int[]{0, 0, 1});
        final float[] fps = (float[]) get(t, "mFps");

        for (int i = 0; i < CpuVUTuner.WINDOW_MEASURE; i++) fps[i] = 40f;
        set(t, "mWindowPos", CpuVUTuner.WINDOW_MEASURE - 1);
        check(!callB(t, "windowValid"), "ventana a medias NO es valida");
        set(t, "mWindowPos", CpuVUTuner.WINDOW_MEASURE);
        check(callB(t, "windowValid"), "ventana llena SI es valida");
        fps[3] = 0f;
        check(!callB(t, "windowValid"), "un 0 (sin presents) anula la ventana: no es '0 fps'");
        fps[3] = 40f;
        check(callB(t, "windowValid"), "sin el 0 la ventana vuelve a valer");
        fps[5] = Float.NaN;
        check(!callB(t, "windowValid"), "un NaN anula la ventana");

        // minimo y mediana sobre el conjunto FILTRADO
        for (int i = 0; i < 8; i++) fps[i] = 0f;
        fps[0] = 30f; fps[1] = 32f; fps[2] = 34f; fps[3] = 36f;
        fps[4] = 38f; fps[5] = 40f; fps[6] = 42f; fps[7] = 44f;
        set(t, "mWindowPos", 8);
        call(t, "closeWindow");
        eqf(((float[]) get(t, "mBaseMin"))[0], 30f, "minimo = 30 (los ceros no cuentan como tiron)");
        eqf(((float[]) get(t, "mBaseMed"))[0], 38f, "mediana del conjunto filtrado (fps[n/2])");
        eqf(gf(t, "mHudP50"), 38f, "el P50 del HUD sale del mismo conjunto filtrado");
        check(((boolean[]) get(t, "mWindowFilled"))[0], "la ventana A1 quedo registrada como llena");

        // una ventana B alimenta al candidato, no a la base
        set(t, "mWindow", 1);
        set(t, "mWindowPos", 8);
        call(t, "closeWindow");
        eqf(((float[]) get(t, "mCandMed"))[1], 38f, "B1 alimenta al candidato");
        eqf(((float[]) get(t, "mBaseMed"))[1], 0f, "B1 no ensucia la base");

        // audio: solo DELTAS desde la marca (no el historial del proceso)
        set(t, "mAudioMark", null);
        NativeApp.audioFaults[0] = 5L; // cortes viejos, de antes del ensayo
        call(t, "closeWindow");        // marca sin sumar
        eqi((int) ((long[]) get(t, "mCandAudio"))[0], 0, "la primera lectura de audio es referencia, no falta");
        NativeApp.audioFaults[0] = 6L;
        set(t, "mWindow", 3);
        call(t, "closeWindow");
        eqi((int) ((long[]) get(t, "mCandAudio"))[0], 1, "despues de la marca solo se suma el delta (1 corte)");

        // coherencia con el entorno: si el INI se movio solo, la ventana se aborta
        set(t, "mWindow", 0);
        check(!callB(t, "environmentDrifted"), "con el INI coherente no hay deriva");
        NativeApp.global.put(SEC + "|EECycleRate", 3);
        check(callB(t, "environmentDrifted"), "si la cuota se movio sola, hay deriva");
    }

    // ==================================================================
    // 5. escalera de candidatos y sus filtros (secciones 4 y 10)
    // ==================================================================
    static void testQueue() {
        // AUTO_SAFE: se prueban los tres y, si ninguno llega a su umbral, se rechaza.
        CpuVUTuner t = fresh();
        NativeApp.mtvuDefault = 1;
        glob("vuThread", 0);
        prefBool("mtvu", false);
        AdaptiveProfile p = new AdaptiveProfile(ctx, URI);
        gainM = 1.02; gainR = 1.02; gainS = 1.02;   // nada llega a su umbral
        check(reachedState(t, p, CpuVUTuner.ST_REJECTED, 6000, new int[]{0, 0, 0}),
                "escalera completa: tras probar los tres se rechaza");
        check(wroteSomething("set-global:vuThread=1"), "se probo MTVU ON");
        check(wroteSomething("set-global:EECycleRate=-1"), "se probo Rate -1");
        check(wroteSomething("set-global:EECycleSkip=1"), "se probo Skip 1 (ultimo recurso)");
        check(!wroteSomething("set-game"), "nada adoptado => nada escrito por-juego");
        check(!wroteSomething("del-global"), "sin adopcion no se borra ninguna clave global");

        // AUTO_MTVU_ONLY: jamas toca Rate ni Skip
        t = fresh();
        CpuVUTuner.setMode(prefs, CpuVUTuner.MODE_AUTO_MTVU_ONLY);
        NativeApp.mtvuDefault = 1;
        glob("vuThread", 0);
        prefBool("mtvu", false);
        gainM = 1.02;
        check(reachedState(t, new AdaptiveProfile(ctx, URI), CpuVUTuner.ST_REJECTED, 3000, new int[]{0, 0, 0}),
                "AUTO_MTVU rechaza si MTVU no llega al 5 %");
        check(!wroteSomething("set-global:EECycleRate=-1"), "AUTO_MTVU no prueba Rate");
        check(!wroteSomething("set-global:EECycleSkip=1"), "AUTO_MTVU no prueba Skip");

        // AUTO_SAFE_sinRS: jamas prueba Rate, aunque ahi estuviera la ganancia
        t = fresh();
        CpuVUTuner.setMode(prefs, CpuVUTuner.MODE_AUTO_NO_RATE_SKIP);
        NativeApp.mtvuDefault = 1;
        glob("vuThread", 0);
        prefBool("mtvu", false);
        gainM = 1.02; gainR = 1.25; gainS = 1.02;
        check(reachedState(t, new AdaptiveProfile(ctx, URI), CpuVUTuner.ST_REJECTED, 4000, new int[]{0, 0, 0}),
                "AUTO_SAFE_sinRS rechaza");
        check(!wroteSomething("set-global:EECycleRate=-1"), "AUTO_SAFE_sinRS no prueba Rate NUNCA");
        check(wroteSomething("set-global:EECycleSkip=1"), "AUTO_SAFE_sinRS si prueba Skip");
        check(!wroteSomething("set-global:EECycleSkip=2"), "y no prueba nada mas alla de Skip 1");

        // "no probar Skip si MTVU/Rate ya bastaron": al ganar MTVU la cola no avanza
        t = fresh();
        NativeApp.mtvuDefault = 1;
        glob("vuThread", 0);
        prefBool("mtvu", false);
        gainM = 1.10; gainR = 1.30; gainS = 1.30;
        check(reachedState(t, new AdaptiveProfile(ctx, URI), CpuVUTuner.ST_VERIFYING_ADOPTION,
                3000, new int[]{0, 0, 0}), "MTVU gana y cierra el ensayo");
        check(!wroteSomething("set-global:EECycleRate=-1"), "no se ensaya Rate si MTVU basta");
        check(!wroteSomething("set-global:EECycleSkip=1"), "no se ensaya Skip si MTVU basta");

        // valor que el usuario ya fijo a mano: no se ofrece ese candidato
        t = fresh();
        NativeApp.mtvuDefault = 1;
        glob("vuThread", 0);
        glob("EECycleRate", 1);
        prefBool("mtvu", false);
        prefInt("ee_cycle_rate", 1);
        gainM = 1.0; gainR = 1.25;
        check(reachedState(t, new AdaptiveProfile(ctx, URI), CpuVUTuner.ST_REJECTED, 4000, new int[]{1, 0, 0}),
                "con Rate fijado por el usuario se rechaza");
        check(!wroteSomething("set-global:EECycleRate=-1"), "no se ofrece el valor que el usuario fijo");
        eqi(gk("EECycleRate"), 1, "el Rate del usuario sigue siendo 1 al terminar");

        // clave fijada POR-JUEGO: intocable (el por-juego manda sobre el global)
        t = fresh();
        NativeApp.mtvuDefault = 1;
        glob("vuThread", 0);
        prefBool("mtvu", false);
        NativeApp.perGame.put(URI + "|" + SEC + "|EECycleRate", 0);
        NativeApp.perGame.put(URI + "|" + SEC + "|EECycleSkip", 0);
        gainM = 1.10;
        check(reachedState(t, new AdaptiveProfile(ctx, URI), CpuVUTuner.ST_ADOPTED, 3000, new int[]{0, 0, 0}),
                "con las otras dos fijadas por-juego gana MTVU");
        eqi(gkGame(URI, "EECycleRate"), 0, "la clave por-juego del usuario sigue intacta (valor)");
        eqi(gkGame(URI, "EECycleSkip"), 0, "la otra clave por-juego tambien");
        check(!wroteSomething("del-game:EECycleRate"), "no borra claves por-juego ajenas");
        check(!wroteSomething("del-game:EECycleSkip"), "ni la otra");

        // pocos nucleos: no se ofrece MTVU
        t = fresh();
        NativeApp.mtvuDefault = 0;
        gainM = 1.30;
        check(reachedState(t, new AdaptiveProfile(ctx, URI), CpuVUTuner.ST_REJECTED, 4000, new int[]{0, 0, 0}),
                "sin nucleos disponibles se rechaza");
        check(!wroteSomething("set-global:vuThread=1"), "no ofrece MTVU si el core no lo permite");
        gainM = 1.0; gainR = 1.0; gainS = 1.0;

        // MTVU ON -> OFF solo con evidencia VU-bound sostenida (mWaitHits)
        t = fresh();
        NativeApp.mtvuDefault = 1;
        glob("vuThread", 1);
        prefBool("mtvu", true);
        gainM = 1.0;
        check(reachedState(t, new AdaptiveProfile(ctx, URI), CpuVUTuner.ST_REJECTED, 4000, new int[]{0, 0, 1}),
                "con MTVU ON y sin evidencia VU se rechaza");
        check(!wroteSomething("set-global:vuThread=0"), "sin evidencia VU no se ofrece MTVU OFF");
        // y CON evidencia si se ofrece
        t = fresh();
        NativeApp.mtvuDefault = 1;
        glob("vuThread", 1);
        prefBool("mtvu", true);
        NativeApp.mtvuWaitMs = 20f;
        NativeApp.mtvuWaitCalls = 40L;
        gainM = 1.10;
        check(reachedState(t, new AdaptiveProfile(ctx, URI), CpuVUTuner.ST_ADOPTED, 3000, new int[]{0, 0, 1}),
                "con espera EE->VU1 sostenida si se ofrece MTVU OFF");
        eqi(gkGame(URI, "vuThread"), 0, "adoptado: MTVU OFF en la capa del juego");
        gainM = 1.0;
    }

    // ==================================================================
    // 6. decision: CADA criterio de la seccion 7, evaluado aparte
    // ==================================================================
    static void testDecide() {
        // kinds: 0 = MTVU (umbral 5), 1 = Rate (umbral 8), 2 = Skip (umbral 10).
        // Se prueba a +-0.5 del umbral y no clavado: la ganancia se calcula en float
        // sobre la mediana, y exigir "+8 % exacto adopta" seria una sentencia sobre
        // un bit de redondeo, no sobre la regla.
        decideCase("MTVU +4.5 % (umbral 5)", 0, 4.5f, false);
        decideCase("MTVU +5.5 % (umbral 5)", 0, 5.5f, true);
        decideCase("MTVU +10 %", 0, 10f, true);
        decideCase("Rate +7.5 % (umbral 8)", 1, 7.5f, false);
        decideCase("Rate +8.5 % (umbral 8)", 1, 8.5f, true);
        decideCase("Skip +9.5 % (umbral 10)", 2, 9.5f, false);
        decideCase("Skip +10.5 % (umbral 10)", 2, 10.5f, true);
        // La MISMA ganancia cambia de veredicto segun QUE pata es: el umbral no es
        // un numero suelto, es por candidato (seccion 7 del prompt).
        decideCase("+8.5 % como Skip (umbral 10) no adopta", 2, 8.5f, false);
    }

    /**
     * Un solo criterio por llamada: la ganancia se fija al % pedido y TODO lo demas
     * queda intacto; luego se rompen uno por uno el resto de los criterios.
     */
    static void decideCase(String name, int kind, float gainPct, boolean wantAdopt) {
        final int[] base = new int[]{0, 0, 0};
        final int[] cand = kind == 0 ? new int[]{0, 0, 1}
                : kind == 1 ? new int[]{-1, 0, 0} : new int[]{0, 1, 0};
        final CpuVUTuner t = fresh();
        plant(t, base, cand, 40f * (1f + gainPct / 100f), 20.1f, 38f, 80f * (1f + gainPct / 100f),
                0L, 0, -1);
        tick(t, null);
        eqi(t.hudState(), wantAdopt ? CpuVUTuner.ST_VERIFYING_ADOPTION : CpuVUTuner.ST_REJECTED,
                "decide: " + name);
        eqi(gi(t, "mPhase"), wantAdopt ? 5 : 0, "decide fase: " + name);
        eqi(wroteSomething("set-game") ? 1 : 0, wantAdopt ? 1 : 0, "decide escrituras: " + name);
        if (wantAdopt) {
            for (int i = 0; i < 3; i++) {
                final int wantV = cand[i] == base[i] ? -999 : cand[i];
                eqi(gkGame(URI, KEYN[i]), wantV, "adopcion escribe solo lo que gana: " + KEYN[i]);
            }
            check(firstWrite("journal-put") >= 0 && firstWrite("set-global") >= 0
                            && firstWrite("journal-put") < firstWrite("set-global"),
                    "diario ANTES de la primera escritura del INI: " + name);
            eqf(gf(t, "mGainPct"), gainPct, "ganancia anotada: " + name);
            eqi(Math.round(gf(t, "mGainPct") * 10f), Math.round(gainPct * 10f),
                    "ganancia viaja en milesimas de ratio: " + name);
        } else {
            final int[] u = garr(t, "mUser");
            eqi(gk("EECycleRate"), u[0], "rechazado: el global vuelve al valor del usuario (rate)");
            eqi(gk("EECycleSkip"), u[1], "rechazado: el global vuelve al valor del usuario (skip)");
            eqi(gk("vuThread"), u[2], "rechazado: el global vuelve al valor del usuario (mtvu)");
            check(!prefs.contains(CpuVUTuner.JOURNAL_PREFIX + URI), "rechazado: el diario se consumio");
        }
    }

    static void testDecideOtherCriteria() {
        // P95
        eqi(rejectCase("p95", 22.0f, 38f, 90f, 0L, 0), CpuVUTuner.ST_REJECTED,
                "P95 9 % peor que la base: no se adopta aunque la mediana gane 12 %");
        // minimo de FPS
        eqi(rejectCase("min", 20.1f, 33f, 90f, 0L, 0), CpuVUTuner.ST_REJECTED,
                "minimo 13 % por debajo: no se adopta (tirones)");
        // speed%
        eqi(rejectCase("speed", 20.1f, 38f, 78f, 0L, 0), CpuVUTuner.ST_REJECTED,
                "speed% mas bajo: no se adopta");
        // audio
        eqi(rejectCase("audio", 20.1f, 38f, 90f, 1L, 0), CpuVUTuner.ST_REJECTED,
                "un corte de audio del candidato: no se adopta");
        // termico
        eqi(rejectCase("thermal", 20.1f, 38f, 90f, 0L, 2), CpuVUTuner.ST_REJECTED,
                "dos escalones termicos mas: no se adopta");
        // ventanas faltantes
        final CpuVUTuner tm = fresh();
        plant(tm, new int[]{0, 0, 0}, new int[]{0, 0, 1}, 44.8f, 20.1f, 38f, 89.6f, 0L, 0, 3);
        tick(tm, null);
        eqi(tm.hudState(), CpuVUTuner.ST_REJECTED, "falta la ventana B2: evidencia incompleta => no adopta");
        // base no medible
        final CpuVUTuner tb = fresh();
        plant(tb, new int[]{0, 0, 0}, new int[]{0, 0, 1}, 12f, 20.1f, 11f, 40f, 0L, 0, -1);
        tb.getClass(); // (la base es 40 fps; se baja la base plantando mBaseMed)
        putF(tb, "mBaseMed", 0, 2f); putF(tb, "mBaseMed", 2, 2f);
        tick(tb, null);
        eqi(tb.hudState(), CpuVUTuner.ST_IDLE, "base bajo 5 fps: no hay juicio fiable (aborta, no adopta)");
        check(!wroteSomething("set-game"), "sin juicio fiable no se escribe nada");
        // termico tolerado (1 escalon) y P95 dentro de tolerancia: SI adopta
        eqi(rejectCase("ok", 20.4f, 36f, 89f, 0L, 1), CpuVUTuner.ST_VERIFYING_ADOPTION,
                "P95 +1.5 % (tolerancia 2 %), min -5 %, termico 1 paso: adopta");
        // el global NUNCA se queda con el valor de ensayo
        final CpuVUTuner t = fresh();
        plant(t, new int[]{0, 0, 0}, new int[]{0, 0, 1}, 44f, 20.1f, 38f, 88f, 0L, 0, -1);
        tick(t, null);
        eqi(gk("vuThread"), 0, "tras adoptar, el INI global vuelve al valor del usuario");
        eqi(gkGame(URI, "vuThread"), 1, "la ganancia vive en la capa por-juego, no en el global");
    }

    /** Adopta por ganancia y luego rompe UN criterio: devuelve el estado resultante. */
    static int rejectCase(String what, float candP95, float candMin, float candSpeed,
                          long candAudio, int candThermal) {
        final CpuVUTuner t = fresh();
        plant(t, new int[]{0, 0, 0}, new int[]{0, 0, 1}, 44.8f, candP95, candMin, candSpeed,
                candAudio, candThermal, -1);
        tick(t, null);
        return t.hudState();
    }

    /** Deja el ensayo plantado en PH_DECIDE con las cifras del caso. */
    static void plant(CpuVUTuner t, int[] base, int[] cand, float candMed, float candP95,
                      float candMin, float candSpeed, long candAudio, int candThermal,
                      int missingWindow) {
        set(t, "mBase", base);
        set(t, "mCand", cand);
        set(t, "mUser", base.clone());
        set(t, "mQueue", new int[][]{cand});
        set(t, "mCandIdx", 0);
        set(t, "mWindow", 3);
        set(t, "mWindowPos", 0);
        set(t, "mPhase", 4);            // PH_DECIDE
        set(t, "mStartApplied", 1f);
        set(t, "mJournalUri", URI);
        set(t, "mProfile", null);
        set(t, "mPerGameTouched", false);
        set(t, "mBenchTicks", 0);
        set(t, "mFrozenTicks", 0);
        set(t, "mBaseAgg", 0f);
        set(t, "mCandAgg", 0f);
        set(t, "mGainPct", 0f);
        set(t, "mAudioMark", null);
        putF(t, "mBaseMed", 0, 40f); putF(t, "mBaseMed", 2, 40f);
        putF(t, "mCandMed", 1, candMed); putF(t, "mCandMed", 3, candMed);
        putF(t, "mBaseMin", 0, 38f); putF(t, "mBaseMin", 2, 38f);
        putF(t, "mCandMin", 1, candMin); putF(t, "mCandMin", 3, candMin);
        putF(t, "mBaseP95", 0, 20.1f); putF(t, "mBaseP95", 2, 20.1f);
        putF(t, "mCandP95", 1, candP95); putF(t, "mCandP95", 3, candP95);
        putF(t, "mSpeedSum", 0, 80f); putF(t, "mSpeedSum", 2, 80f);
        putF(t, "mSpeedSum", 1, candSpeed); putF(t, "mSpeedSum", 3, candSpeed);
        set(t, "mSpeedN", new int[]{1, 1, 1, 1});
        final boolean[] filled = new boolean[]{true, true, true, true};
        if (missingWindow >= 0) filled[missingWindow] = false;
        set(t, "mWindowFilled", filled);
        set(t, "mBaseThermalMax", 0);
        set(t, "mCandThermalMax", candThermal);
        set(t, "mCandAudio", new long[]{candAudio, 0L, 0L});
        set(t, "mBaseAudio", new long[3]);
        glob("EECycleRate", base[0]);
        glob("EECycleSkip", base[1]);
        glob("vuThread", base[2]);
        prefInt("ee_cycle_rate", base[0]);
        prefBool("mtvu", base[2] == 1);
        NativeApp.fps = candMed;
        NativeApp.speedPct = candSpeed;
    }

    // ==================================================================
    // 7. integracion: ensayo completo -> adopcion (con el clasificador real)
    // ==================================================================
    static void testAdoptIntegration() {
        final CpuVUTuner t = fresh();
        NativeApp.mtvuDefault = 1;
        glob("vuThread", 0);
        prefBool("mtvu", false);
        final AdaptiveProfile p = new AdaptiveProfile(ctx, URI);
        final float heldBefore = p.heldScale;
        final int slowBefore = p.slowFloorTicks;
        final int turboBefore = p.turboSeconds;
        gainM = 1.10;

        check(runUntil(t, p, CpuVUTuner.ST_ADOPTED, 3000, new int[]{0, 0, 0}),
                "el ensayo completo llega a ADOPTED");

        // orden duro: diario -> global del candidato -> revertido
        final int jPut = firstWrite("journal-put");
        final int gOn = firstWrite("set-global:vuThread=1");
        final int gOff = firstWrite("set-global:vuThread=0");
        check(jPut >= 0, "hubo diario");
        check(gOn > jPut, "el global del candidato se escribe DETRAS del diario");
        check(gOff > gOn, "al terminar la ventana B el global vuelve al valor del usuario");

        // A/B/A/B: dos ventanas por lado, y el candidato se pone DOS veces
        eqi(countWrites("set-global:vuThread=1"), 2, "las dos B ponen el candidato");
        eqi(countWrites("set-global:vuThread=0"), 2, "las dos A lo quitan");
        int a = 0, b = 0, post = 0, verify = 0;
        for (String s : NativeApp.tunerPublished) {
            final int st = Integer.parseInt(s.split(",")[0]);
            if (st == CpuVUTuner.ST_MEASURING_BASELINE) a++;
            if (st == CpuVUTuner.ST_MEASURING_CANDIDATE) b++;
            if (st == CpuVUTuner.ST_POST_VERIFY) post++;
            if (st == CpuVUTuner.ST_VERIFYING_ADOPTION) verify++;
        }
        check(a >= 2 && b >= 2, "el HUD mostro los dos lados por separado (A y B)");
        check(verify > 0 && post > 0, "el HUD mostro VERIFYING_ADOPTION y POST_VERIFY");

        // estado final
        eqi(gkGame(URI, "vuThread"), 1, "adoptado: MTVU en la capa del juego");
        eqi(gkGame(URI, "EECycleRate"), -999, "adoptado: nada mas escrito por-juego");
        eqi(gk("vuThread"), 0, "adoptado: el global NO queda con el valor de ensayo");
        eqi(gk("EECycleRate"), 0, "adoptado: el global de Rate intacto");
        eqi(gk("EECycleSkip"), 0, "adoptado: el global de Skip intacto");
        check(!prefs.contains(CpuVUTuner.JOURNAL_PREFIX + URI), "adoptado: el diario se consumio");

        // el registro de identidad y la memoria del regidor, los dos
        final TunerProfileStore.Record r =
                new TunerProfileStore(ctx, URI, TunerProfileStore.identityOf(URI, ctx)).load();
        eqs(r.verdict, TunerProfileStore.V_ADOPTED, "registro: adoptado");
        eqs(r.adopted, "m=1", "registro: mascara de lo adoptado");
        eqf((float) r.lastGainPct / 100f, 10f, "registro: ganancia en x100");
        check(r.blocksRetry(System.currentTimeMillis()), "registro adoptado bloquea re-ensayo (blocksRetry)");
        final AdaptiveProfile q = new AdaptiveProfile(ctx, URI);
        eqs(q.tunerVerdict, TunerProfileStore.V_ADOPTED, "perfil del regidor: veredicto");
        eqs(q.tunerAdoptado, "m=1", "perfil del regidor: mascara");
        check(q.tunerAplicado, "perfil del regidor: aplicado=true");
        eqf(q.tunerNow, 44f, "perfil del regidor: fps con la adopcion");

        // Regla del prompt: las ventanas de ensayo NO alimentan la calibracion
        eqf(q.heldScale, heldBefore, "ensayar no movio heldScale");
        eqi(q.slowFloorTicks, slowBefore, "ensayar no movio slowFloorTicks");
        eqi(q.turboSeconds, turboBefore, "ensayar no movio turboSeconds");

        // HUD: ganancia en milesimas, fps x100, identidad publicada
        final String[] last = pub(NativeApp.tunerPublished.size() - 1);
        eqi(Integer.parseInt(last[0]), CpuVUTuner.ST_ADOPTED, "HUD final: ADOPTED");
        eqi(Integer.parseInt(last[2]), 100, "HUD: +10 % = 100 milesimas de ratio");
        eqi(Integer.parseInt(last[3]), 4000, "HUD: fps antes 40.00 x100");
        eqi(Integer.parseInt(last[4]), 4400, "HUD: fps despues 44.00 x100");
        check(!last[8].isEmpty(), "HUD: identidad del perfil publicada");
        eqi(CpuVUTuner.modeOf(prefs), CpuVUTuner.MODE_AUTO_SAFE, "adoptar no cambia el modo");
        eqi(t.hudState(), CpuVUTuner.ST_ADOPTED, "hudState() dice ADOPTED");
        eqs(CpuVUTuner.stateName(t.hudState()), "ADOPTED", "nombre legible del estado final");

        // y con la adopcion en vigor no se vuelve a ensayar (misma sesion, misma memoria)
        gainM = 1.0;
        NativeApp.writes.clear();
        for (int i = 0; i < 60; i++) tick(t, new AdaptiveProfile(ctx, URI));
        check(NativeApp.writes.isEmpty(), "con adopcion en vigor no se escribe nada mas");
        eqi(t.hudState(), CpuVUTuner.ST_IDLE, "con adopcion en vigor el estado es IDLE (no LEARNING)");

        // la ventana de asentamiento NO se mide: la primera escritura llega despues
        final CpuVUTuner t2 = fresh();
        NativeApp.mtvuDefault = 1;
        glob("vuThread", 0);
        prefBool("mtvu", false);
        gainM = 1.10;
        runUntil(t2, new AdaptiveProfile(ctx, URI), CpuVUTuner.ST_MEASURING_CANDIDATE, 3000,
                new int[]{0, 0, 0});
        // Invariante del asentamiento: entre escribir el candidato en el global y la
        // primera muestra de la ventana B tienen que pasar el aterrizaje del valor
        // (APPLY_VERIFY_WAIT) y el asentamiento (WINDOW_SETTLE), sin medir en medio.
        // Medir el segundo en que se escribe la cuota es medir el transitorio del
        // cambio, no el candidato. La esperanza se LEE del tuner, no se repite a mano.
        final int wait = st("APPLY_VERIFY_WAIT"), settle = CpuVUTuner.WINDOW_SETTLE;
        eqi(gi(t2, "mPhase"), st("PH_APPLY_WAIT"), "recien programada B: primero se espera a que aterrice");
        int untilMeasure = 0;
        while (gi(t2, "mPhase") != st("PH_MEASURE") && untilMeasure < 30) {
            autoMetrics(new int[]{0, 0, 0});
            tick(t2, p);
            untilMeasure++;
        }
        eqi(untilMeasure, wait + settle,
                "entre escribir el candidato y medir pasan exactamente aterrizaje + asentamiento");
        eqi(gi(t2, "mWindowPos"), 0, "al empezar a medir, la ventana B esta vacia");
        eqi(gi(t2, "mTicks"), CpuVUTuner.WINDOW_MEASURE, "y la ventana B mide sus 8 muestras");
        autoMetrics(new int[]{0, 0, 0});
        tick(t2, p);
        eqi(gi(t2, "mWindowPos"), 1, "la primera muestra de B se toma despues del asentamiento");
        gainM = 1.0;
    }

    // ==================================================================
    // 8. integracion: post-verificacion que confirma y que revoca
    // ==================================================================
    static void testPostVerify() {
        // (a) la ganancia se sostiene: CONFIRMADO
        CpuVUTuner t = fresh();
        NativeApp.mtvuDefault = 1;
        glob("vuThread", 0);
        prefBool("mtvu", false);
        AdaptiveProfile p = new AdaptiveProfile(ctx, URI);
        gainM = 1.10;
        check(reachedState(t, p, CpuVUTuner.ST_POST_VERIFY, 3000, new int[]{0, 0, 0}),
                "post: entra a POST_VERIFY con la adopcion puesta");
        eqi(gkGame(URI, "vuThread"), 1, "POST_VERIFY: la adopcion ya esta escrita por-juego");
        check(!prefs.contains(CpuVUTuner.JOURNAL_PREFIX + URI), "POST_VERIFY: el diario ya se consumio");
        int ticks = 0;
        while (t.hudState() == CpuVUTuner.ST_POST_VERIFY && ticks < 200) {
            autoMetrics(new int[]{0, 0, 0});
            tick(t, p);
            ticks++;
        }
        eqi(t.hudState(), CpuVUTuner.ST_ADOPTED, "post: confirmar deja ADOPTED");
        eqi(CpuVUTuner.POST_VERIFY_TICKS, 45, "post: duracion 45 s (30-60 del prompt)");
        gainM = 1.0;

        // (b) la ganancia desaparece: REVOCADO -> SUSPENDED, y se BORRA lo escrito
        t = fresh();
        NativeApp.mtvuDefault = 1;
        glob("vuThread", 0);
        prefBool("mtvu", false);
        p = new AdaptiveProfile(ctx, URI);
        gainM = 1.10;
        check(reachedState(t, p, CpuVUTuner.ST_VERIFYING_ADOPTION, 3000, new int[]{0, 0, 0}),
                "revocar: llega a VERIFYING_ADOPTION");
        ignoreCandidate = true;   // el telefono se enfrio: el candidato deja de rendir
        check(runUntil(t, p, CpuVUTuner.ST_SUSPENDED, 3000, new int[]{0, 0, 0}),
                "revocar: la post-verificacion revoca y suspende");
        eqi(gkGame(URI, "vuThread"), -999, "revocado: la clave por-juego se BORRO (no se puso a 0)");
        eqi(gk("vuThread"), 0, "revocado: el global en el valor del usuario");
        check(!prefs.contains(CpuVUTuner.JOURNAL_PREFIX + URI), "revocado: sin diario sobrante");
        final TunerProfileStore.Record r =
                new TunerProfileStore(ctx, URI, TunerProfileStore.identityOf(URI, ctx)).load();
        eqs(r.verdict, TunerProfileStore.V_SUSPENDED, "revocado: registro suspendido");
        eqi(r.suspensions, 1, "revocado: contador de suspensiones");
        check(r.blocksRetry(System.currentTimeMillis()), "revocado: bloquea reintentar hoy");
        ignoreCandidate = false;

        // (c) una suspension dentro del cooldown no se reintenta
        t = fresh();
        NativeApp.mtvuDefault = 1;
        glob("vuThread", 0);
        prefBool("mtvu", false);
        gainM = 1.10;
        final TunerProfileStore st = new TunerProfileStore(ctx, URI, TunerProfileStore.identityOf(URI, ctx));
        final TunerProfileStore.Record rec = st.load();
        rec.id = TunerProfileStore.identityOf(URI, ctx).id;
        rec.uri = URI; rec.verdict = TunerProfileStore.V_SUSPENDED; rec.adopted = "";
        rec.at = System.currentTimeMillis(); rec.suspensions = 1;
        st.save(rec);
        NativeApp.writes.clear();
        for (int i = 0; i < 40; i++) tick(t, new AdaptiveProfile(ctx, URI));
        eqi(t.hudState(), CpuVUTuner.ST_SUSPENDED, "suspension: no reintenta dentro del cooldown");
        check(NativeApp.writes.isEmpty(), "suspension: ni una escritura durante el cooldown");

        // (d) audio: un corte con la adopcion puesta revoca (criterio medido, no asumido)
        t = fresh();
        NativeApp.mtvuDefault = 1;
        glob("vuThread", 0);
        prefBool("mtvu", false);
        p = new AdaptiveProfile(ctx, URI);
        gainM = 1.10;
        check(reachedState(t, p, CpuVUTuner.ST_MEASURING_CANDIDATE, 3000, new int[]{0, 0, 0}),
                "audio: ensayo en vuelo");
        boolean injected = false;
        for (int i = 0; i < 400 && !injected; i++) {
            autoMetrics(new int[]{0, 0, 0});
            if (gi(t, "mCandIdx") == 0 && gi(t, "mWindow") == 1 && gi(t, "mPhase") == 2) {
                NativeApp.audioFaults[0] += 1L;   // un corte, SOLO bajo el candidato 0
                injected = true;
            }
            tick(t, p);
        }
        check(injected, "audio: el caso logro inyectar el corte en la ventana B1");
        check(runUntil(t, p, CpuVUTuner.ST_REJECTED, 4000, new int[]{0, 0, 0}),
                "audio: un corte bajo el candidato basta para no adoptar (MTVU)");
        check(!wroteSomething("set-game"), "audio: nada adoptado, nada escrito por-juego");
        gainM = 1.0;
    }

    // ==================================================================
    // 9. un solo dueno por intervalo (seccion 6)
    // ==================================================================
    static void testOwnership() {
        // superblock validando a mitad del ensayo: congelar y NO medir
        final CpuVUTuner t = fresh();
        NativeApp.mtvuDefault = 1;
        glob("vuThread", 0);
        prefBool("mtvu", false);
        final AdaptiveProfile p = new AdaptiveProfile(ctx, URI);
        gainM = 1.10;
        check(runUntil(t, p, CpuVUTuner.ST_MEASURING_BASELINE, 3000, new int[]{0, 0, 0}),
                "dueno: ensayo en marcha");
        NativeApp.writes.clear();
        NativeApp.superblockValidating = true;
        for (int i = 0; i < 5; i++) { autoMetrics(new int[]{0, 0, 0}); tick(t, p); }
        check(NativeApp.writes.isEmpty(), "superblock validando: el tuner no escribe");
        eqi(gi(t, "mFrozenTicks"), 5, "superblock validando: cuenta como congelacion");
        NativeApp.superblockValidating = false;
        for (int i = 0; i < 20; i++) { autoMetrics(new int[]{0, 0, 0}); tick(t, p); }
        check(!NativeApp.writes.isEmpty(), "al liberar el superbloque el tuner retoma su ensayo");

        // pausa larga: cerrar con reversion total (no dejar un speedhack global puesto)
        for (int i = 0; i < 80; i++) t.onFrozen();
        eqi(t.hudState(), CpuVUTuner.ST_IDLE, "pausa larga aborta el ensayo");
        eqi(gk("vuThread"), 0, "aborto por pausa: el global vuelve al valor del usuario");
        eqi(gkGame(URI, "vuThread"), -999, "aborto por pausa: nada por-juego");
        check(!prefs.contains(CpuVUTuner.JOURNAL_PREFIX + URI), "aborto por pausa: sin diario sobrante");

        // el usuario mueve ajustes a mitad: cancelar, revertir y respetar SU valor
        final CpuVUTuner t2 = fresh();
        NativeApp.mtvuDefault = 1;
        glob("vuThread", 0);
        prefBool("mtvu", false);
        gainM = 1.10;
        check(runUntil(t2, new AdaptiveProfile(ctx, URI), CpuVUTuner.ST_MEASURING_CANDIDATE,
                3000, new int[]{0, 0, 0}), "ajustes: el candidato esta puesto en el global");
        prefInt("ee_cycle_rate", -2);           // el usuario elige Rate -2 en Ajustes
        tick(t2, new AdaptiveProfile(ctx, URI));
        eqi(t2.hudState(), CpuVUTuner.ST_IDLE, "ajustes: el ensayo se cancela");
        eqi(gk("EECycleRate"), -2, "ajustes: el tuner respeta el valor del usuario, no su fotografia");
        eqi(gk("vuThread"), 0, "ajustes: la cuota de ensayo se revierte");
        eqi(gkGame(URI, "vuThread"), -999, "ajustes: nada por-juego");
        gainM = 1.0;

        // halt() del regidor: cerrar ya, con reversion
        final CpuVUTuner t3 = fresh();
        NativeApp.mtvuDefault = 1;
        glob("vuThread", 0);
        prefBool("mtvu", false);
        gainM = 1.10;
        check(runUntil(t3, new AdaptiveProfile(ctx, URI), CpuVUTuner.ST_MEASURING_CANDIDATE,
                3000, new int[]{0, 0, 0}), "halt: el candidato esta puesto");
        t3.halt("el regidor se apago");
        eqi(t3.hudState(), CpuVUTuner.ST_IDLE, "halt deja IDLE");
        eqi(gk("vuThread"), 0, "halt revierte el global del ensayo");
        check(!prefs.contains(CpuVUTuner.JOURNAL_PREFIX + URI), "halt quita el diario");

        // onGameStop: parar el juego a mitad NO es un crash del candidato
        final CpuVUTuner t4 = fresh();
        NativeApp.mtvuDefault = 1;
        glob("vuThread", 0);
        prefBool("mtvu", false);
        gainM = 1.0;
        runUntil(t4, new AdaptiveProfile(ctx, URI), CpuVUTuner.ST_MEASURING_BASELINE, 3000,
                new int[]{0, 0, 0});
        final AdaptiveProfile gp = new AdaptiveProfile(ctx, URI);
        t4.onGameStop(gp);
        check(!prefs.contains(CpuVUTuner.JOURNAL_PREFIX + URI),
                "onGameStop: el diario se consume (si no, el proximo arranque lo leeria como crash)");
        eqi(gk("vuThread"), 0, "onGameStop: el global revertido");
        eqs(new TunerProfileStore(ctx, URI, TunerProfileStore.identityOf(URI, ctx)).load().verdict,
                "", "parar el juego a mitad NO deja veredicto: no es un crash del candidato");
        gainM = 1.0;
    }

    // ==================================================================
    // 10. identidad (seccion 8)
    // ==================================================================
    static void testIdentity() {
        boot();
        final TunerProfileStore.Identity id = TunerProfileStore.identityOf(URI, ctx);
        check(TunerProfileStore.identityComplete(id), "identidad completa con CRC+serial+version");
        check(id.id.contains("AB12CD34"), "la identidad lleva el CRC del disco");
        check(id.id.contains("SLUS-21079"), "la identidad lleva el serial");
        check(id.id.contains(android.os.Build.SOC_MODEL), "la identidad lleva el SoC");
        check(id.id.contains("|0.6.31|"), "la identidad lleva la version del core");
        check(id.id.endsWith("|" + TunerProfileStore.TUNER_VERSION), "la identidad cierra con la version del tuner");
        eqi(TunerProfileStore.TUNER_VERSION, 2, "TUNER_VERSION 2 = metodologia A/B/A/B");
        eqs(TunerProfileStore.PREF_PREFIX, "tuner2:", "el prefijo de prefs acompana a TUNER_VERSION");
        eqi(TunerProfileStore.identityOf(URI2, ctx).id.equals(id.id) ? 1 : 0, 0,
                "otro disco => otra identidad");

        prefs.edit().putInt("renderer", 1).apply();
        check(!TunerProfileStore.identityOf(URI, ctx).id.equals(id.id), "otro backend => otra identidad");
        prefs.edit().putString("custom_driver_id", "mesa-turnip-30").apply();
        check(!TunerProfileStore.identityOf(URI, ctx).id.equals(id.id), "otro driver => otra identidad");
        android.os.Build.SOC_MODEL = "mali-g57";
        check(TunerProfileStore.identityOf(URI, ctx).id.contains("mali-g57"), "otro SoC => otra identidad");
        android.os.Build.SOC_MODEL = "Qualcomm Snapdragon 778G";

        // sin CRC legible: identidad incompleta => no se adopta nada
        final CpuVUTuner t = fresh();
        NativeApp.crcByUri.remove(URI);
        NativeApp.mtvuDefault = 1;
        glob("vuThread", 0);
        prefBool("mtvu", false);
        gainM = 1.40;
        boolean adopto = false;
        for (int i = 0; i < 900; i++) {
            autoMetrics(new int[]{0, 0, 0});
            tick(t, new AdaptiveProfile(ctx, URI));
            if (t.hudState() == CpuVUTuner.ST_ADOPTED) { adopto = true; break; }
        }
        check(!adopto, "sin CRC no se adopta nada (identidad incompleta)");
        check(NativeApp.writes.isEmpty(), "identidad incompleta: ni una escritura en el INI");

        // version del core ilegible => incompleta
        NativeApp.crcByUri.put(URI, "AB12CD34");
        android.content.pm.PackageManager.throwOnQuery = true;
        check(!TunerProfileStore.identityComplete(TunerProfileStore.identityOf(URI, ctx)),
                "sin version del core la identidad es incompleta");
        android.content.pm.PackageManager.throwOnQuery = false;

        // caida a MANUFACTURER/MODEL (telefono sin SOC_MODEL)
        android.os.Build.SOC_MODEL = null;
        check(TunerProfileStore.identityOf(URI, ctx).id.contains("HUAWEI/NCO-LX3"),
                "sin SOC_MODEL se cae a MANUFACTURER/MODEL");
        android.os.Build.SOC_MODEL = "Qualcomm Snapdragon 778G";

        // un veredicto del metodo viejo (TUNER_VERSION 1) no autoriza nada
        final TunerProfileStore.Identity v1 = new TunerProfileStore.Identity(
                id.crc + "|x", id.serial, id.crc, id.soc, id.renderer, id.driver, id.core, 1);
        check(!v1.id.equals(id.id), "TUNER_VERSION 1 produce otra clave: no se reutiliza");

        // "borrar perfil aprendido"
        final TunerProfileStore st = new TunerProfileStore(ctx, URI, TunerProfileStore.identityOf(URI, ctx));
        final TunerProfileStore.Record r = st.load();
        r.id = TunerProfileStore.identityOf(URI, ctx).id;
        r.uri = URI; r.verdict = TunerProfileStore.V_ADOPTED; r.adopted = "m=1";
        r.at = System.currentTimeMillis();
        st.save(r);
        st.markActive();
        check(!TunerProfileStore.activeId(prefs).isEmpty(), "markActive dejo el id canonico");
        final AdaptiveProfile ap = new AdaptiveProfile(ctx, URI);
        ap.tunerAdoptado = "m=1"; ap.tunerAplicado = true; ap.tunerVerdict = TunerProfileStore.V_ADOPTED;
        ap.save();
        NativeApp.perGame.put(URI + "|" + SEC + "|vuThread", 1);
        final String msg = CpuVUTuner.forgetLearned(ctx, URI);
        check(msg.contains("borrado"), "forgetLearned informa cuanto se fue: " + msg);
        eqi(gkGame(URI, "vuThread"), -999, "forgetLearned deshace la adopcion (borra, no pone 0)");
        eqs(st.load().verdict, "", "forgetLearned borro el registro de identidad");
        eqs(new AdaptiveProfile(ctx, URI).tunerVerdict, "", "forgetLearned borro la memoria del regidor");
        eqs(CpuVUTuner.forgetLearned(ctx, ""), "no hay juego delante: no hay nada que borrar",
                "forgetLearned sin juego lo dice claro");
        gainM = 1.0;
    }

    // ==================================================================
    // 11. cierre forzoso: el diario (seccion 9)
    // ==================================================================
    static void testJournal() {
        // (a) el diario que escribe el ensayo: valor previo EXACTO + existencia
        CpuVUTuner t = fresh();
        NativeApp.mtvuDefault = 1;
        glob("EECycleRate", 0);
        glob("EECycleSkip", 0);
        glob("vuThread", 1);
        prefBool("mtvu", true);
        prefInt("ee_cycle_rate", 0);
        gainM = 1.0; gainR = 1.0;
        check(runUntil(t, new AdaptiveProfile(ctx, URI), CpuVUTuner.ST_MEASURING_CANDIDATE,
                3000, new int[]{0, 0, 1}), "diario: el ensayo llega a medir un candidato");
        final String jr = prefs.getString(CpuVUTuner.JOURNAL_PREFIX + URI, null);
        check(jr != null, "diario presente durante la ventana B");
        check(jr != null && jr.startsWith("v2|0,0,1|"), "diario: valores previos exactos (0,0,1) => " + jr);
        check(jr != null && jr.contains("|1:1:1|"), "diario: las tres claves EXISTIAN => " + jr);
        check(jr != null && jr.endsWith("|0"), "diario: indice del candidato en vuelo => " + jr);
        check(firstWrite("journal-put") >= 0 && firstWrite("journal-put") < firstWrite("set-global"),
                "diario escrito ANTES que ninguna cuota");

        // (b) clave que NO existia: flags 0 (para borrarla al revertir, no sembrar 0)
        t = fresh();
        NativeApp.mtvuDefault = 1;
        glob("EECycleRate", null);
        glob("EECycleSkip", null);
        glob("vuThread", 0);
        prefBool("mtvu", false);
        prefRemove("ee_cycle_rate");
        gainM = 1.0;
        check(runUntil(t, new AdaptiveProfile(ctx, URI), CpuVUTuner.ST_MEASURING_CANDIDATE,
                3000, new int[]{0, 0, 0}), "diario b: ensayo en vuelo con claves ausentes");
        final String jr2 = prefs.getString(CpuVUTuner.JOURNAL_PREFIX + URI, null);
        check(jr2 != null && jr2.contains("|0:0:1|"), "diario: rate/skip no existian (0:0:1) => " + jr2);
        // Revertir en sesion: el valor de ensayo NO puede quedar puesto. Escribir el
        // default del core (0) en vez de borrar la clave es la asimetria real con el
        // camino post-mortem, y queda dicha: la emulacion termina igual; despues de un
        // portazo la clave ademas desaparece (caso c).
        t.halt("fin del ensayo");
        eqi(gk("EECycleRate"), 0, "revertir en sesion: el Rate de ensayo no queda puesto");
        eqi(gk("EECycleSkip"), 0, "revertir en sesion: el Skip de ensayo no queda puesto");

        // (c) CIERRE FORZOSO: diario huerfano + INI con el ensayo puesto
        final CpuVUTuner t3 = fresh();
        NativeApp.mtvuDefault = 1;
        glob("EECycleRate", null);
        glob("EECycleSkip", null);
        glob("vuThread", 0);
        prefBool("mtvu", false);
        prefRemove("ee_cycle_rate");
        prefs.edit().putString(CpuVUTuner.JOURNAL_PREFIX + URI2, "v2|-1,0,0|0:0:1|r=-1|1").apply();
        NativeApp.global.put(SEC + "|EECycleRate", -1);   // el candidato que dejo el portazo
        NativeApp.global.put(SEC + "|EECycleSkip", 0);    // clave que Cenit creó
        NativeApp.perGame.put(URI2 + "|" + SEC + "|EECycleRate", -1); // escrita por Cenit
        NativeApp.perGame.put(URI2 + "|" + SEC + "|EECycleSkip", 3);  // ajena: no esta en la mask
        NativeApp.perGame.put(URI + "|" + SEC + "|EECycleRate", -2);  // ajena: otro juego
        NativeApp.global.put("GSopenmp|extra", 7);                     // ajena: otra seccion
        NativeApp.vmAlive = false;

        eqi(CpuVUTuner.recoverAtBoot(ctx, prefs), 1, "recoverAtBoot consumo un diario");
        check(!prefs.contains(CpuVUTuner.JOURNAL_PREFIX + URI2), "el diario se consumio");
        check(!NativeApp.global.containsKey(SEC + "|EECycleRate"),
                "recuperado: EECycleRate no existia => se BORRO, no se puso -1/0");
        check(!NativeApp.global.containsKey(SEC + "|EECycleSkip"),
                "recuperado: EECycleSkip no existia => se BORRO la clave que creo el ensayo");
        eqi(gk("vuThread"), 0, "recuperado: MTVU existia => restaurado su valor exacto");
        eqi(gkOther("GSopenmp", "extra"), 7, "recuperado: una clave de otra seccion intacta");
        eqi(gkGame(URI2, "EECycleRate"), -999, "recuperado: se borro la clave por-juego que escribio Cenit");
        eqi(gkGame(URI2, "EECycleSkip"), 3, "recuperado: una clave por-juego ajena NO se toca");
        eqi(gkGame(URI, "EECycleRate"), -2, "recuperado: el INI de OTRO juego intacto");

        final AdaptiveProfile p2 = new AdaptiveProfile(ctx, URI2);
        eqs(p2.tunerVerdict, TunerProfileStore.V_RECOVERED, "recuperado: la memoria del regidor lo dice");
        check(!p2.tunerAplicado, "recuperado: aplicado=false");
        check(p2.tunerAdoptado.isEmpty(), "recuperado: mascara vacia");
        final TunerProfileStore.Record r2 =
                new TunerProfileStore(ctx, URI2, TunerProfileStore.identityOf(URI2, ctx)).load();
        eqs(r2.verdict, TunerProfileStore.V_SUSPENDED, "recuperado: ESE candidato queda suspendido");
        eqi(r2.suspensions, 1, "recuperado: una suspension anotada");
        check(r2.detail.contains("candidato 1"),
                "recuperado: el registro dice CUAL candidato (no el juego entero): " + r2.detail);

        // (d) el diario huerfano da RECOVERED al HUD (estado con productor real)
        NativeApp.vmAlive = true;
        prefs.edit().putString(CpuVUTuner.JOURNAL_PREFIX + URI, "v2|0,0,0|1:1:1||0").apply();
        final CpuVUTuner t4 = new CpuVUTuner(ctx, prefs);
        t4.startSession();
        eqi(t4.hudState(), CpuVUTuner.ST_RECOVERED, "startSession con diario pendiente: RECOVERED");
        check(!prefs.contains(CpuVUTuner.JOURNAL_PREFIX + URI), "startSession consumo el diario");

        // (e) entrada ilegible: se descarta la entrada, NO se restaura a ciegas
        boot();
        prefs.edit().putString(CpuVUTuner.JOURNAL_PREFIX + URI, "v2|no,no|x").apply();
        NativeApp.global.put(SEC + "|EECycleRate", -3);
        // Ilegible: se quita la entrada y NO se toca el INI. No se cuenta como
        // recuperacion porque no hubo nada que recuperar: adivinar un valor previo es
        // justo lo que el diario existe para evitar. El caso deja de pintarse RECOVERED
        // en el HUD por eso (0 consumidos) y el log dice que la entrada era ilegible.
        eqi(CpuVUTuner.recoverAtBoot(ctx, prefs), 0, "ilegible: no cuenta como recuperacion");
        check(!prefs.contains(CpuVUTuner.JOURNAL_PREFIX + URI), "ilegible: se quito el diario");
        eqi(gk("EECycleRate"), -3, "ilegible: NO toco el INI (no adivina un valor)");

        // (f) diario v1 de 0.6.27: se acepta y restaura lo que dice el recibo viejo
        boot();
        prefs.edit().putString(CpuVUTuner.JOURNAL_PREFIX + URI, "-2,1,0;m=1").apply();
        NativeApp.global.put(SEC + "|EECycleRate", 0);
        NativeApp.global.put(SEC + "|EECycleSkip", 0);
        NativeApp.global.put(SEC + "|vuThread", 1);
        NativeApp.perGame.put(URI + "|" + SEC + "|vuThread", 1);
        NativeApp.crcByUri.put(URI, "AB12CD34");
        eqi(CpuVUTuner.recoverAtBoot(ctx, prefs), 1, "v1: se consumo");
        eqi(gk("EECycleRate"), -2, "v1: restaura el Rate del recibo");
        eqi(gk("EECycleSkip"), 1, "v1: restaura el Skip del recibo");
        eqi(gk("vuThread"), 0, "v1: restaura MTVU del recibo");
        eqi(gkGame(URI, "vuThread"), -999, "v1: borra la clave por-juego de la mascara");

        // (g) sin diario: recoverAtBoot no escribe absolutamente nada
        boot();
        NativeApp.global.put(SEC + "|EECycleRate", -1);
        NativeApp.writes.clear();
        eqi(CpuVUTuner.recoverAtBoot(ctx, prefs), 0, "sin diario: cero recuperaciones");
        check(NativeApp.writes.isEmpty(), "sin diario: ni una escritura en el INI");
        eqi(gk("EECycleRate"), -1, "sin diario: el INI queda como esta");
    }

    // ==================================================================
    // 12. undo de una adopcion concreta
    // ==================================================================
    static void testUndoAdoption() {
        boot();
        NativeApp.perGame.put(URI + "|" + SEC + "|EECycleRate", -1);
        NativeApp.perGame.put(URI + "|" + SEC + "|vuThread", 1);
        CpuVUTuner.deleteAdoptedKeys(URI, "r=-1,m=1");
        eqi(gkGame(URI, "EECycleRate"), -999, "undo: borra la clave adoptada");
        eqi(gkGame(URI, "vuThread"), -999, "undo: borra la otra");
        NativeApp.perGame.put(URI + "|" + SEC + "|EECycleRate", -3);
        CpuVUTuner.deleteAdoptedKeys(URI, "r=-1");
        eqi(gkGame(URI, "EECycleRate"), -3, "undo: NO borra un valor que ya no es el nuestro");
        CpuVUTuner.deleteAdoptedKeys(URI, "r=abc,z=1,q=2");
        check(gkGame(URI, "EECycleRate") == -3, "undo: mascara basura no rompe nada");
        CpuVUTuner.deleteAdoptedKeys("", "r=-1");
        CpuVUTuner.deleteAdoptedKeys(URI, "");
        check(true, "undo: uri o mascara vacias no hacen nada");
        NativeApp.perGame.put(URI + "|" + SEC + "|EECycleSkip", 2);
        CpuVUTuner.deleteAdoptedKeys(URI, "s=2");
        eqi(gkGame(URI, "EECycleSkip"), -999, "undo: Skip tambien es reversible");
    }

    // ==================================================================
    // 13. etiquetas compartidas por log, registro y UI
    // ==================================================================
    static void testLabels() {
        eqs(CpuVUTuner.verdictFor(null), "ninguno", "verdictFor(null)");
        eqs(CpuVUTuner.verdictFor(new int[]{0, 0, 0}), "MTVU OFF", "verdictFor tripleta original");
        eqs(CpuVUTuner.verdictFor(new int[]{-1, 0, 0}), "Rate -1 (75%)",
                "verdictFor Rate: la etiqueta dice SOLO lo que se toca");
        eqs(CpuVUTuner.verdictFor(new int[]{0, 1, 1}), "Skip 1 + MTVU ON", "verdictFor Skip + MTVU ON");
        eqs(CpuVUTuner.verdictFor(new int[]{0, 0, 1}), "MTVU ON", "verdictFor MTVU ON");
        eqs(CpuVUTuner.verdictFor(new int[]{-1, 1, 0}), "Rate -1 (75%) + Skip 1",
                "verdictFor no mezcla candidatos: solo etiqueta lo que hay");
        // el valor original del prompt es el candidato 0: la etiqueta lo dice
        eqs(CpuVUTuner.verdictFor(new int[]{0, 0, 0}), "MTVU OFF", "candidato 0 = original");
    }

    public static void main(String[] args) {
        testModes();
        testStates();
        testGates();
        testWindowMath();
        testQueue();
        testDecide();
        testDecideOtherCriteria();
        testAdoptIntegration();
        testPostVerify();
        testOwnership();
        testIdentity();
        testJournal();
        testUndoAdoption();
        testLabels();
        System.out.println("Harness8: " + checks + " Comprobaciones, " + failures + " Fallos");
        if (failures > 0) System.exit(1);
    }
}
