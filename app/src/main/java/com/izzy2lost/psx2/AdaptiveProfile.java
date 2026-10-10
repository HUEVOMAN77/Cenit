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
 *   floor=<escala que sostuvo> ;cpu=0/1 ;slow=<ticks lentos en el suelo> ;
 *   turbo=<segundos de carga detectada> ;tver=<veredicto del sintonizador> ;
 *   tad=<perfil adoptado "rate,skip,mtvu"> ;tap=0/1 ;tat=<epoch ms> ;tnow=<fps>
 *
 * Las CUOTAS (EECycleSkip) y el resto de speedhacks no viven aquí: su casa es
 * el INI por-juego del core, que ya es la fuente de verdad. Este perfil guarda
 * la EVIDENCIA (slow) y el VEREDICTO del sintonizador CPU/VU (0.6.27): qué se
 * adoptó, cuándo, y qué FPS dio — para no volver a benchar nunca un juego cuyo
 * resultado ya se conoce, y para poder deshacerlo sin dejar claves huérfanas.
 *
 * Los campos nuevos se leen con nombre, no por posición, así que un perfil
 * escrito por 0.6.26 (sin ellos) carga igual: lo que falte se queda por defecto.
 * Lo mismo al revés: una versión antigua que no conozca "tver" simplemente lo
 * descarta en su bucle switch.
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
    /** Ticks en el escalón más bajo yendo lento: evidencia para cuotas / sintonizador. */
    int slowFloorTicks = 0;
    /** Segundos totales con carga detectada + turbo: para elegir confiar en él. */
    int turboSeconds = 0;

    // ------------------------------------------------------------------
    // 0.6.27 (governor v4): memoria del sintonizador CPU/VU
    // ------------------------------------------------------------------
    /**
     * Veredicto legible del último benchmark ("MTVU ON", "Rate -1", "ninguno",
     * ""). No es estado operativo: el operativo es {@link #tunerAdoptado}, que
     * coincide con lo que hay escrito en el INI por-juego. Sirve para (a) no
     * re-benchar un juego ya resuelto y (b) contarle a la UI qué pasó.
     */
    String tunerVerdict = "";
    /** Perfil adoptado como "rate,skip,mtvu" ("" = ninguno). Clave para deshacer. */
    String tunerAdoptado = "";
    /** true = las tres claves están escritas por el sintonizador en el INI del juego. */
    boolean tunerAplicado = false;
    /** Momento del veredicto, para caducarlo (una semana) y volver a mirar. */
    long tunerAt = 0L;
    /** FPS medios con los que terminó el último benchmark (0 = sin dato). */
    float tunerNow = 0f;

    // ------------------------------------------------------------------
    // 0.6.30: tally del clasificador NUEVO (los siete estados del contrato)
    // ------------------------------------------------------------------
    /**
     * Cuantos ticks de esta partida vio el clasificador cada estado en ESTE
     * juego. No decide nada: es la evidencia que falta para saber si los umbrales
     * de LoadPolicy (gpu>=85 / gpu<=35 / ee>=80) estan bien puestos en un
     * Snapdragon 6 o 7. Sin este numero, la respuesta es mi criterio; con el es
     * una curva medida en telefonos ajenos, que es como se han corregido ya tres
     * cosas de este regidor.
     *
     * Se cuenta por JUEGO y no global porque un umbral que sirva para un juego
     * GPU-bound a 60 Hz puede no servir para un entrelazado.
     *
     * La casilla INVALID_SAMPLE es la que no existia antes y la que importa para
     * la Regla 3 del prompt: "GPU al 3%" y "GPU sin metrica" son cosas distintas,
     * y un perfil que no las separe no puede contarlo. En 0.6.29 la muestra mala
     * se sumaba a "sin veredicto" y el FÁBRICA no podia decir si el regidor se
     * paso la partida dudando o la partida sin lecturas.
     */
    int[] loadMix = new int[CenitTelemetry.STATE_COUNT]; // unk,nog,gpu,cpu,vu,therm,inv

    /**
     * El tally de 0.6.29 tenia CUATRO casillas con claves que ya no significan
     * lo mismo: su orden era gpu,emu,vu,sin y el nuevo es unk,nog,gpu,cpu,vu,
     * therm,inv. Leer el viejo con el formato nuevo llenaria "desconocido" con
     * recuentos de GPU y "sin cuello" con los de emulacion — justo los numeros
     * con los que alguien calibraria umbrales. Si viene con 4 cifras se traduce
     * con el orden antiguo; el save() lo reescribe ya con las siete.
     */
    private static void loadLegacyMix(String v, int[] into) {
        // Orden de 0.6.29: gpu, emu, vu, sin. Se traduce al orden de estados del
        // contrato; la casilla NO_BOTTLENECK queda en 0 porque aquel tally solo
        // contaba ticks CON retraso (un juego a tiempo no se registraba).
        final String[] p = v.split(",");
        into[CenitTelemetry.STATE_GPU_BOUND] = Integer.parseInt(p[0].trim());
        into[CenitTelemetry.STATE_CPU_BOUND] = Integer.parseInt(p[1].trim());
        into[CenitTelemetry.STATE_VU_BOUND] = Integer.parseInt(p[2].trim());
        into[CenitTelemetry.STATE_UNKNOWN_LOAD] = Integer.parseInt(p[3].trim());
    }

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

    String uri() {
        return uri;
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
                    // 0.6.27: el veredicto del sintonizador va URL-ish (sin '=' ni
                    // ';'), así que basta con guardar la cadena tal cual.
                    case "tver": tunerVerdict = java.net.URLDecoder.decode(v, "UTF-8"); break;
                    case "tad": tunerAdoptado = v; break;
                    case "tap": tunerAplicado = "1".equals(v); break;
                    case "tat": tunerAt = Long.parseLong(v); break;
                    case "tnow": tunerNow = Float.parseFloat(v); break;
                    // 0.6.30: el tally cambio de formato (4 casillas -> 7, con el
                    // orden de estados del contrato). Se aceptan los dos: un
                    // perfil de 0.6.29 se traduce, uno roto a mano se descarta
                    // entero antes de que un numero se cuele en la casilla
                    // equivocada.
                    case "load": {
                        final String[] parts = v.split(",");
                        if (parts.length == loadMix.length) {
                            for (int i = 0; i < parts.length; i++)
                                loadMix[i] = Integer.parseInt(parts[i].trim());
                        } else if (parts.length == 4) {
                            loadLegacyMix(v, loadMix);
                        }
                        break;
                    }
                    default: break;
                }
            } catch (NumberFormatException ignored) {
                // prefs tocadas a mano o de una versión anterior: descartar ese campo
            } catch (java.io.UnsupportedEncodingException ignored) {
                // UTF-8 siempre existe; esto es inalcanzable en Android.
            }
        }
    }

    void save() {
        if (!known()) return;
        String enc = tunerVerdict;
        try {
            enc = java.net.URLEncoder.encode(tunerVerdict, "UTF-8");
        } catch (java.io.UnsupportedEncodingException ignored) {
            enc = "";
        }
        prefs.edit().putString(PREFIX + uri,
                "floor=" + heldScale
                        + ";cpu=" + (cpuBound ? 1 : 0)
                        + ";slow=" + slowFloorTicks
                        + ";turbo=" + turboSeconds
                        + ";tver=" + enc
                        + ";tad=" + tunerAdoptado
                        + ";tap=" + (tunerAplicado ? 1 : 0)
                        + ";tat=" + tunerAt
                        + ";tnow=" + tunerNow
                        + ";load=" + loadMix[0] + "," + loadMix[1] + ","
                                + loadMix[2] + "," + loadMix[3] + ","
                                + loadMix[4] + "," + loadMix[5] + "," + loadMix[6]).apply();
    }

    /** Borrar el aprendizaje de un juego (desde sus Ajustes por juego). */
    static void forget(Context context, String gameUri) {
        if (gameUri == null || gameUri.isEmpty()) return;
        context.getApplicationContext().getSharedPreferences("app_prefs", Context.MODE_PRIVATE)
                .edit().remove(PREFIX + gameUri).apply();
    }
}
