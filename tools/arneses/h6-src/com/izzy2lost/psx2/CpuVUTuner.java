package com.izzy2lost.psx2;

import android.content.Context;
import android.content.SharedPreferences;

/**
 * El arnes NO prueba el sintonizador (tiene su propia maquina de estados de 800
 * lineas); lo que necesita es comprobar QUE el regidor le pasa el contexto
 * correcto. Aqui se registra cada tick y se responde lo que el caso pida.
 */
final class CpuVUTuner {
    static final int IDLE = 0;
    static final int CONTINUE = 1;
    static final int RETRY = 2;

    static final java.util.List<String> calls = new java.util.ArrayList<>();
    static int answer = IDLE;
    static boolean active = false;

    private final SharedPreferences prefs;
    CpuVUTuner(Context c, SharedPreferences p) { this.prefs = p; }

    boolean isActive() { return active; }
    /** Cenit 0.6.31: Ajustes pregunta el estado vivo para pintarlo en la nota. */
    int hudState() { return hudStateValue; }
    static int hudStateValue = -1;
    void startSession() { calls.add("startSession"); }
    void onFrozen() { calls.add("onFrozen"); }
    void halt(String why) { calls.add("halt:" + why); }
    void onGameStop(AdaptiveProfile p) { calls.add("onGameStop"); }

    int tick(AdaptiveProfile profile, String uri, float applied, CenitLoadSample sample,
             GameLoadClassifier.Verdict verdict, int thermalStatus, boolean behind,
             boolean atFloor, boolean metricsAlive, boolean turbo, boolean settling,
             boolean thermal, boolean superblockBusy) {
        calls.add("tick|applied=" + applied + "|behind=" + behind + "|atFloor=" + atFloor
                + "|verdict=" + (verdict == null ? "null" : verdict.state + "/" + verdict.action)
                + "|metrics=" + metricsAlive
                + "|turbo=" + turbo + "|settling=" + settling + "|thermal=" + thermal
                + "|sb=" + superblockBusy + "|ts=" + thermalStatus
                + "|sample=" + (sample == null ? "null" : "obj")
                + "|uri=" + uri + "|prof=" + (profile == null ? "null" : "obj"));
        return answer;
    }
}
