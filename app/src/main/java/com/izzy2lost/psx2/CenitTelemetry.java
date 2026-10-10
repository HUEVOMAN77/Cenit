package com.izzy2lost.psx2;

/**
 * Cenit 0.6.30 — Diccionario unico de estados de carga para todo lo que cruza
 * una frontera: el HUD nativo (JNI) y el tally que se guarda en las prefs del
 * perfil por-juego.
 *
 * Por que esta clase existe aparte de GameLoadClassifier.LoadState: en 0.6.29 el
 * numero de cada estado vivia en DOS sitios a la vez — el switch
 * ordinalForHud() de Java y el array kStates[] de ImGuiOverlays.cpp — y cada
 * caida del regidor se etiquetaba "UNKNOWN_LOAD" tanto si el clasificador no se
 * atrevia a decir nada (zonas intermedias) como si LA MUESTRA no era valida
 * (ventana recortada, muestra repetida, NaN). Los dos casos piden acciones
 * opuestas: el primero es "espera", el segundo es "la metrica fallo, busca la
 * causa". Con una sola casilla el tally del perfil no podia contarlo.
 *
 * Regla de mantenimiento: cualquier numero nuevo se agrega aqui, en el kStates[]
 * del HUD (app/src/main/cpp/pcsx2/ImGui/ImGuiOverlays.cpp) y en el contrato
 * (docs/CONTRATO_CENIT_LOAD_SAMPLE.md) en la MISMA version. El arnes del
 * contrato lo comprueba, y si alguien reordena sin avisar, el test falla antes
 * de que el numero equivoque una decision de recorte.
 */
public final class CenitTelemetry {

    private CenitTelemetry() {}

    public static final int STATE_UNKNOWN_LOAD = 0;
    public static final int STATE_NO_BOTTLENECK = 1;
    public static final int STATE_GPU_BOUND = 2;
    public static final int STATE_CPU_BOUND = 3;
    public static final int STATE_VU_BOUND = 4;
    public static final int STATE_THERMAL_BOUND = 5;
    public static final int STATE_INVALID_SAMPLE = 6;

    /** Casillas de la mezcla de carga que guarda AdaptiveProfile. */
    public static final int STATE_COUNT = 7;
}
