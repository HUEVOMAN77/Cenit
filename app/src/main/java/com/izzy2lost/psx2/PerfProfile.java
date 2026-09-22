package com.izzy2lost.psx2;

import android.content.Context;

/**
 * Perfil de rendimiento por hardware. La mayor parte se aplica en el núcleo
 * (ApplyHardwarePerformanceProfile en native-lib.cpp: speedhacks, spin de
 * readbacks y verbosidad de log ajustados al dispositivo). Esta clase expone
 * lo que necesita la capa Java: el escalado de resolución inicial sensato
 * según el nivel del equipo, para no empezar todos en 1x.
 */
public final class PerfProfile {

    private PerfProfile() {}

    /** Mali/otro desconocido: nativo. Los juegos exigentes se benefician igual del perfil nativo. */
    public static final int TIER_LOW = 0;
    /** Snapdragon fuera de la gama alta (Adreno 610-620): casi todos van a 1.5x. */
    public static final int TIER_MID = 1;
    /** Snapdragon 778G/8xx y superiores: 2x estable en la mayoría del catálogo. */
    public static final int TIER_HIGH = 2;

    private static volatile int sTier = -1;

    public static int tier(Context context) {
        int t = sTier;
        if (t < 0) {
            try {
                t = NativeApp.getDevicePerformanceTier();
            } catch (Throwable e) {
                t = TIER_LOW;
            }
            if (t != TIER_HIGH && t != TIER_MID) t = TIER_LOW;
            sTier = t;
        }
        return t;
    }

    /**
     * Escalado por defecto en el primer arranque (cuando todavía no hay nada
     * guardado en preferencias). El usuario lo cambia cuando quiere y queda fijo.
     */
    public static float defaultUpscale(Context context) {
        switch (tier(context)) {
            case TIER_HIGH: return 2.0f;
            case TIER_MID:  return 1.5f;
            default:        return 1.0f;
        }
    }
}
