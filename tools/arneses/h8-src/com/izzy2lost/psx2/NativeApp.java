package com.izzy2lost.psx2;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Mock del nativo para h8. A diferencia del de h6 (que solo fabríca la muestra),
 * aqui el INI tiene estado de verdad: dos mapas (global y por-juego), la triplet
 * EFECTIVA derivada de ellos con la misma cascada del core (por-juego > global >
 * default) y un registro de todo lo que el sintonizador escribio o borro.
 *
 * Por que hace falta: las tres promesas duras del prompt (diario con el valor
 * previo EXACTO y si la clave EXISTIA, revertir borrando en vez de sembrar un 0,
 * y no tocar nunca una clave ajena) son imposibles de probar contra un mock que
 * no distingue "0 porque no esta" de "0 porque alguien lo escribio".
 */
public class NativeApp {

    public static boolean hasNoNativeBinary = false;

    // ---------------- INI global (s_settings_interface) ----------------
    /** clave "section|key" -> valor. La AUSENCIA es un estado real, no un 0. */
    public static final Map<String, Integer> global = new LinkedHashMap<>();
    /** clave "uri|section|key" -> valor (capa por-juego). */
    public static final Map<String, Integer> perGame = new LinkedHashMap<>();

    // ---------------- escenario ----------------
    /** valor por defecto de MTVU cuando ninguna capa lo fija (coresAllowMTVU). */
    public static int mtvuDefault = 0;
    /** false = VM parada: el por-juego no manda todavia (igual que el core). */
    public static boolean vmAlive = true;

    public static float fps = 40f;
    public static float speedPct = 80f;
    public static double p50Ms = 16.7;
    public static double p95Ms = 20.1;
    public static float gpuPct = 20f;
    public static float eePct = 95f;
    public static float mtvuWaitMs = 0f;
    public static long mtvuWaitCalls = 0L;
    public static float verticalHz = 59.94f;
    public static float effectiveUpscale = 1f;

    /** -1 = senal ausente (el caso honesto), >=0 = numero de reemplazos en cache. */
    public static long textureCount = -1L;

    /** Contadores ACUMULADOS de audio del proceso: {silencio, descartados, timestretch}. */
    public static final long[] audioFaults = new long[3];

    /** CRC por uri; getGameCrcSafe devuelve "" si no esta (igual que el nativo). */
    public static final Map<String, String> crcByUri = new HashMap<>();

    // ---------------- registros para las aserciones ----------------
    /** Cada publicacion al cuadro del tuner: "state,mode,gain,before,after,p50,p95,term,prof". */
    public static final List<String> tunerPublished = new ArrayList<>();
    public static int tunerHudResets = 0;
    /** Cada escritura/borrado del INI, en orden, para auditar el flujo A/B/A/B. */
    public static final List<String> writes = new ArrayList<>();

    static String g(String section, String key) { return section + "|" + key; }
    static String p(String uri, String section, String key) { return uri + "|" + section + "|" + key; }

    public static void reset() {
        hasNoNativeBinary = false;
        global.clear(); perGame.clear();
        mtvuDefault = 0; vmAlive = true;
        fps = 40f; speedPct = 80f; p50Ms = 16.7; p95Ms = 20.1;
        gpuPct = 20f; eePct = 95f; mtvuWaitMs = 0f; mtvuWaitCalls = 0L;
        verticalHz = 59.94f; effectiveUpscale = 1f;
        textureCount = -1L;
        audioFaults[0] = audioFaults[1] = audioFaults[2] = 0L;
        crcByUri.clear();
        tunerPublished.clear(); tunerHudResets = 0; writes.clear();
        superblockValidating = false;
    }

    // ---------------- cascada efectiva ----------------
    static int readLayer(String uri, String section, String key, int def) {
        if (vmAlive) {
            final Integer v = perGame.get(p(uri, section, key));
            if (v != null) return v;
        }
        final Integer g = global.get(g(section, key));
        if (g != null) return g;
        return def;
    }

    /** uri del juego en curso: la usa el mock para resolver la cascada. */
    public static String currentUri = "saf://games/SLUS_210.79%20(Game).iso";

    public static int safeGetEffectiveEECycleRate() {
        return readLayer(currentUri, "EmuCore/Speedhacks", "EECycleRate", 0);
    }
    public static int safeGetEffectiveEECycleSkip() {
        return readLayer(currentUri, "EmuCore/Speedhacks", "EECycleSkip", 0);
    }
    public static int safeGetEffectiveMTVU() {
        return readLayer(currentUri, "EmuCore/Speedhacks", "vuThread", mtvuDefault);
    }

    // ---------------- INI global ----------------
    public static int safeGetGlobalSettingInt(String section, String key, int fallback) {
        final Integer v = global.get(g(section, key));
        return v == null ? fallback : v;
    }
    public static boolean safeDeleteGlobalSettingKey(String section, String key) {
        writes.add("del-global:" + key);
        global.remove(g(section, key));
        return true;
    }

    // ---------------- INI por-juego ----------------
    public static int safeGetGameSettingInt(String uri, String section, String key, int fallback) {
        final Integer v = perGame.get(p(uri, section, key));
        return v == null ? fallback : v;
    }
    public static boolean safeSetGameSettingInt(String uri, String section, String key, int value) {
        writes.add("set-game:" + key + "=" + value);
        perGame.put(p(uri, section, key), value);
        return true;
    }
    public static boolean safeDeleteGameSettingKey(String uri, String section, String key) {
        writes.add("del-game:" + key);
        perGame.remove(p(uri, section, key));
        return true;
    }

    // ---------------- seters del global (en el tuner viajan "async") ----------------
    public static void setEECycleRateAsync(int value) {
        writes.add("set-global:EECycleRate=" + value);
        global.put(g("EmuCore/Speedhacks", "EECycleRate"), value);
    }
    public static void setEECycleSkipAsync(int value) {
        writes.add("set-global:EECycleSkip=" + value);
        global.put(g("EmuCore/Speedhacks", "EECycleSkip"), value);
    }
    public static void setMTVUAsync(boolean enabled) {
        writes.add("set-global:vuThread=" + (enabled ? 1 : 0));
        global.put(g("EmuCore/Speedhacks", "vuThread"), enabled ? 1 : 0);
    }
    /** Sincronos: los usa recoverAtBoot con la VM parada. Mismo efecto visible. */
    public static void speedhackEecyclerate(int value) { setEECycleRateAsync(value); }
    public static void speedhackEecycleskip(int value) { setEECycleSkipAsync(value); }
    public static void setMTVU(boolean enabled) { setMTVUAsync(enabled); }

    public static boolean defaultMTVU() { return mtvuDefault == 1; }

    // ---------------- audio ----------------
    public static long[] safeGetAudioFaultStats() { return audioFaults.clone(); }
    public static void safeResetAudioFaultStats() {
        audioFaults[0] = audioFaults[1] = audioFaults[2] = 0L;
    }

    // ---------------- texturas / superbloques / CRC ----------------
    public static long safeGetLoadedTextureCount() { return textureCount; }
    public static boolean superblockValidating = false;
    public static boolean safeSuperblockValidating() { return superblockValidating; }
    public static String getGameCrcSafe(String uri) {
        final String c = crcByUri.get(uri);
        return c == null ? "" : c;
    }

    // ---------------- muestra del contrato ----------------
    public static double[] safeGetCenitSample() {
        final double[] d = new double[CenitLoadSample.SIZE];
        d[CenitLoadSample.IDX_FPS] = fps;
        d[CenitLoadSample.IDX_SPEED_PCT] = speedPct;
        d[CenitLoadSample.IDX_FRAMETIME_AVG_MS] = p50Ms;
        d[CenitLoadSample.IDX_FRAMETIME_MIN_MS] = p50Ms * 0.9;
        d[CenitLoadSample.IDX_FRAMETIME_MAX_MS] = p95Ms * 1.2;
        d[CenitLoadSample.IDX_FRAMETIME_P50_MS] = p50Ms;
        d[CenitLoadSample.IDX_FRAMETIME_P95_MS] = p95Ms;
        d[CenitLoadSample.IDX_GPU_PCT] = gpuPct;
        d[CenitLoadSample.IDX_GPU_AVG_MS] = 6.0;
        d[CenitLoadSample.IDX_EE_PCT] = eePct;
        d[CenitLoadSample.IDX_EE_AVG_MS] = 12.0;
        d[CenitLoadSample.IDX_GS_PCT] = 10.0;
        d[CenitLoadSample.IDX_VU_THREAD_PCT] = mtvuWaitCalls > 0 ? 40.0 : 0.0;
        d[CenitLoadSample.IDX_MTVU_WAIT_MS] = mtvuWaitMs;
        d[CenitLoadSample.IDX_MTVU_WAIT_CALLS] = mtvuWaitCalls;
        d[CenitLoadSample.IDX_MTVU_EXEC_MS] = 5.0;
        d[CenitLoadSample.IDX_VERTICAL_HZ] = verticalHz;
        d[CenitLoadSample.IDX_PRESENTS] = 30;
        d[CenitLoadSample.IDX_WINDOW_S] = 0.5;
        d[CenitLoadSample.IDX_UPSCALE] = effectiveUpscale;
        d[CenitLoadSample.IDX_EE_CYCLE_RATE] = safeGetEffectiveEECycleRate();
        d[CenitLoadSample.IDX_EE_CYCLE_SKIP] = safeGetEffectiveEECycleSkip();
        d[CenitLoadSample.IDX_MTVU_ON] = safeGetEffectiveMTVU();
        d[CenitLoadSample.IDX_FLAGS] = CenitLoadSample.FLAG_GPU_METRIC_VALID
                | CenitLoadSample.FLAG_EMU_METRIC_VALID
                | CenitLoadSample.FLAG_WINDOW_COMPLETE
                | CenitLoadSample.FLAG_SAMPLE_FRESH;
        return d;
    }

    public static CenitLoadSample sample() { return new CenitLoadSample(safeGetCenitSample()); }

    public static void safeSetCenitLoadVerdict(int stateOrdinal, int confidencePct,
                                               int actionCode, double gpu, double ee,
                                               int thermalStatus) { /* no es de este arnes */ }
    public static void safeResetCenitLoadVerdict() { }

    public static void safeSetCenitTunerStatus(int state, int mode, int gainPermille,
                                               int fpsBeforeX100, int fpsAfterX100,
                                               float p50Ms, float p95Ms,
                                               int thermalStatus, String profileId) {
        tunerPublished.add(state + "," + mode + "," + gainPermille + "," + fpsBeforeX100
                + "," + fpsAfterX100 + "," + p50Ms + "," + p95Ms + "," + thermalStatus
                + "," + profileId);
    }
    public static void safeResetCenitTunerStatus() { tunerHudResets++; }
}
