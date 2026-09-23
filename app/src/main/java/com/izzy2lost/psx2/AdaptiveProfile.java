package com.izzy2lost.psx2;

import android.content.Context;
import android.content.SharedPreferences;

/**
 * Memoria de rendimiento POR JUEGO de Cenit (0.6.5, invento nuestro).
 *
 * Ningún emulador de sobremesa hace esto: después de cada partida, Cenit
 * recuerda qué escala realmente sostuvo el juego, si el cuello de botella era
 * CPU (bajar resolución no sirve), si el juego iba tan clavado que necesitará
 * cuotas de frame, y si el turbo ayudó. En la próxima sesión el regidor ya no
 * parte de cero ni adivina: empieza con lo que este teléfono ya demostró con
 * ESTE juego.
 *
 * Se guarda en "app_prefs" bajo "adapt:<uri>" con un formato clave=valor;
 * deliberadamente legable a mano por si hay que diagnosticar sin adb:
 *   floor=<escala que sostuvo> ;cpu=0/1 ;slow=<ticks lentos en 1x> ;
 *   turbo=<segundos de carga detectada>
 *
 * Las CUOTAS (EECycleSkip) no viven aquí: su casa es el INI por-juego del
 * core, que ya es la fuente de verdad. Este perfil solo guarda la EVIDENCIA
 * (slow) para que la UI ofrezca el modo cuotas con datos.
 *
 * Es por URI (no por serial) a propósito: el usuario elige el archivo, el
 * serial puede faltar, y las escrituras de rendimiento no viajan al INI del
 * juego (que es config compartida con el core) sino a las prefs de la app.
 */
final class AdaptiveProfile {

    private static final String PREFIX = "adapt:";

    /** Escala más baja con la que el juego llegó a sostener el techo. 0 = sin dato. */
    float heldScale = 0f;
    /** true = visto como CPU-bound estable: bajar píxeles no le va a ayudar. */
    boolean cpuBound = false;
    /** Ticks en 1x yendo lento: evidencia para el modo de cuotas (frame skip). */
    int slowFloorTicks = 0;
    /** Segundos totales con carga detectada + turbo: para elegir confiar en él. */
    int turboSeconds = 0;

    private final SharedPreferences prefs;
    private final String uri;

    /** context null = objeto vacío (memoria apagada): nunca carga ni guarda. */
    AdaptiveProfile(Context context, String gameUri) {
        this.prefs = context == null ? null
                : context.getApplicationContext()
                        .getSharedPreferences("app_prefs", Context.MODE_PRIVATE);
        this.uri = gameUri == null ? "" : gameUri;
        load();
    }

    boolean known() {
        return prefs != null && !uri.isEmpty();
    }

    private void load() {
        if (!known()) return;
        final String raw = prefs.getString(PREFIX + uri, null);
        if (raw == null) return;
        for (String part : raw.split(";")) {
            final int eq = part.indexOf('=');
            if (eq <= 0) continue;
            final String k = part.substring(0, eq).trim();
            final String v = part.substring(eq + 1).trim();
            try {
                switch (k) {
                    case "floor": heldScale = Float.parseFloat(v); break;
                    case "cpu": cpuBound = "1".equals(v); break;
                    case "slow": slowFloorTicks = Integer.parseInt(v); break;
                    case "turbo": turboSeconds = Integer.parseInt(v); break;
                    default: break;
                }
            } catch (NumberFormatException ignored) {
                // prefs tocadas a mano o de una versión anterior: descartar ese campo
            }
        }
    }

    void save() {
        if (!known()) return;
        prefs.edit().putString(PREFIX + uri,
                "floor=" + heldScale
                        + ";cpu=" + (cpuBound ? 1 : 0)
                        + ";slow=" + slowFloorTicks
                        + ";turbo=" + turboSeconds).apply();
    }

    /** Borrar el aprendizaje de un juego (desde sus Ajustes por juego). */
    static void forget(Context context, String gameUri) {
        if (gameUri == null || gameUri.isEmpty()) return;
        context.getApplicationContext().getSharedPreferences("app_prefs", Context.MODE_PRIVATE)
                .edit().remove(PREFIX + gameUri).apply();
    }
}
