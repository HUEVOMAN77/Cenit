package com.izzy2lost.psx2;

/**
 * El regidor solo pide la lista de escalones del menu (una sola fuente de
 * verdad). El arnes la EXTRAE del repo en cada corrida con
 * extra_scalervalues.py, para que el stub no pueda quedarse con una lista vieja
 * y aprobar un salto que el menu ya no ofrece.
 */
final class SettingsScreenController {
    static final float[] SCALE_VALUES = {0.5f, 0.75f, 1f, 1.25f, 1.5f, 2f, 2.5f, 3f, 4f, 5f, 6f, 8f};
}
