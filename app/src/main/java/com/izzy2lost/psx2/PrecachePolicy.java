package com.izzy2lost.psx2;

import android.content.SharedPreferences;
import android.util.Log;

/**
 * Cenit 0.6.28 — Cargar el juego en memoria (precache de disco).
 *
 * El núcleo ya tenía la casilla EmuCore/CdvdPrecache, pero en Android estaba
 * inutilizable: el arranque la invocaba con el ProgressCallback nulo que
 * devuelve este host (SIGSEGV seguro — ya desactivado en
 * VMManager::PrecacheCDVDFile) y ninguna pantalla la exponía. Aquí se pone la
 * pieza que faltaba para poder ofrecerla de verdad: una sonda que responde con
 * la MISMA cuenta que va a hacer el núcleo al abrir el disco
 * (ThreadedFileReader::CheckAvailableMemoryForPrecaching: los últimos 2 GB
 * disponibles no son para la imagen), y una decisión por arranque que se
 * escribe en la capa base.
 *
 * Dos niveles, en el orden de prioridad que ya usa el núcleo:
 *   - Preferencia "cdvd_precache" (apagada por defecto: en teléfonos chicos la
 *     cuenta casi siempre dice que no): modo automático de Cenit. En cada
 *     arranque se mide y se carga SOLO si cabe.
 *   - Clave EmuCore/CdvdPrecache del INI por-juego: manda por encima (la capa
 *     de juego va sobre la base), así que un Sí o un No fijado a mano gana
 *     siempre, y la primera escritura del arranque nunca pisa una orden
 *     explícita del usuario.
 *
 * No hace falta una red de gama aparte: la cuenta disponibles - 2 GB YA es la
 * red. En un teléfono de 4 GB el tope queda en unos cientos de megas y ningún
 * juego pasa de ahí; en uno de 12 GB entran hasta ~5 GB. Medición antes que
 * reglas, como siempre en Cenit.
 *
 * Regla 0.6.23 respetada: la sonda abre su propio descriptor sobre la URI y
 * solo pregunta el tamaño; jamás toca el lector del disco puesto ni
 * InputIsoFile, así que puede correr con el VM vivo sin matar una partida.
 */
public final class PrecachePolicy {

    private static final String TAG = "PrecachePolicy";

    /** Preferencia del modo automático global. */
    public static final String PREF_AUTO = "cdvd_precache";

    /** Prefijo bajo el que se anota el tamaño medido de cada juego. */
    public static final String SIZE_PREFIX = "iso_size:";

    /** Clave del núcleo, sección EmuCore: la orden por-juego. */
    public static final String INI_SECTION = "EmuCore";
    public static final String INI_KEY = "CdvdPrecache";

    // Códigos de veredicto que devuelve NativeApp.precacheProbe()[0].
    public static final int V_NO_URI = 0;
    public static final int V_UNSUPPORTED = 1;
    public static final int V_UNKNOWN = 2;
    public static final int V_FITS = 3;
    public static final int V_NO_FITS = 4;

    private PrecachePolicy() {}

    /** Modo automático global: apagado por defecto (en 384 MB por app la cuenta
     *  casi siempre dice que no; ver la línea de decisión en el registro). */
    public static boolean autoEnabled(SharedPreferences prefs) {
        return prefs.getBoolean(PREF_AUTO, false);
    }

    /**
     * Decisión para el arranque de UN juego concreto. Corre en el hilo de la
     * VM, justo antes de arrancarla: de ahí que la sonda pueda tardar lo que
     * tarde sin congelar la interfaz. El orden es lo importante: si el usuario
     * fijó Sí/No para este juego, Cenit no escribe nada en la capa base y la
     * capa por-juego manda sola; si no, la decisión automática va a la capa
     * base y el INI del juego (si alguien lo tocó desde fuera) pisa encima de
     * todos modos.
     *
     * @return true si hay que decirle al núcleo que precargue este juego.
     */
    public static boolean decideForBoot(SharedPreferences prefs, String gameUri) {
        return decideForBoot(prefs, gameUri, needsBootProbe(prefs) ? probeBounded(gameUri) : null);
    }

    /**
     * Misma decisión con la MEDIDA YA HECHA, para no sondear dos veces en el
     * arranque (precarga y ventana de streaming quieren el mismo tamaño).
     */
    public static boolean decideForBoot(SharedPreferences prefs, String gameUri, long[] probe) {
        if (!autoEnabled(prefs)) return false;
        if (gameUri == null || gameUri.isEmpty()) return false;
        // Manifiesto arcade: no es un disco; nada que cargar.
        if (gameUri.endsWith(".acgame") || gameUri.endsWith(".ini")) return false;
        return probeFits(probe);
    }

    /**
     * ¿Hay que medir en el arranque? Solo si algún consumidor lo pide: el modo
     * automático, o la fábrica de ventana de streaming cuando el usuario no fijó
     * la suya Y todavía no tenemos el tamaño anotado. Con nada de esto que
     * decidir, el arranque no paga ni un stat.
     *
     * El tamaño se guarda por URI porque es lo único de la medida que no caduca:
     * el veredicto de "cabe o no cabe" depende de la RAM libre de ESTE momento,
     * así que ese siempre se vuelve a preguntar; los bytes del disco no cambian
     * solos. Con la caché, un ISO en un proveedor de nube se espera una vez en
     * la vida del juego y no en cada arranque.
     */
    public static boolean needsBootProbe(SharedPreferences prefs, long cachedSizeBytes) {
        if (autoEnabled(prefs)) return true;
        if (prefs.contains("iso_buffer_kb")) return false;   // el usuario ya eligió
        return cachedSizeBytes < 0L;   // -1 = nunca se midió; 0 = se intentó y no se pudo
    }

    public static boolean needsBootProbe(SharedPreferences prefs) {
        return needsBootProbe(prefs, -1L);
    }

    /**
     * Tamaño anotado de este juego: -1 si nunca se midió, 0 si se intentó y no
     * se pudo (archivo ilegible: no vale la pena volver a intentarlo en cada
     * arranque), o los bytes reales.
     */
    public static long cachedSize(SharedPreferences prefs, String gameUri) {
        if (prefs == null || gameUri == null || gameUri.isEmpty()) return -1L;
        return prefs.getLong(SIZE_PREFIX + gameUri, -1L);
    }

    /** Anota el tamaño medido (o el 0 de "no se pudo") para no repetirlo. */
    public static void noteSize(SharedPreferences prefs, String gameUri, long[] probe) {
        if (prefs == null || gameUri == null || gameUri.isEmpty()) return;
        if (probe == null) return; // sin medida ninguna: no anotar nada
        final long bytes = probe.length >= 2 ? probe[1] : 0L;
        final long logged = bytes > 0L ? bytes : 0L;
        if (prefs.getLong(SIZE_PREFIX + gameUri, -1L) == logged) return; // ya anotado
        prefs.edit().putLong(SIZE_PREFIX + gameUri, logged).apply();
    }

    /** Lo que se midió de este juego: tamaño en MB, o el motivo de no saberlo. */
    public static String sizeLabel(long[] probe, long cachedSizeBytes) {
        final long bytes = (probe != null && probe.length >= 2 && probe[1] > 0L)
                ? probe[1] : cachedSizeBytes;
        if (bytes > 0L) return mb(bytes) + "MB";
        if (probe != null && probe[0] == V_UNSUPPORTED) return "formato sin medida";
        return "sin medir";
    }

    /**
     * ¿Cabe la imagen? La cuenta es exactamente la que hará el núcleo al abrir
     * el disco; si la sonda no responde (sin nativo, URI ilegible) la respuesta
     * honesta es no.
     *
     * La medida va con cota de tiempo: el tamaño se pide por el proveedor de
     * documentos, y un proveedor de nube puede tardar lo que quiera. Esperarlo
     * retarde el arranque del juego no es aceptable: a los 1500 ms la respuesta
     * es "no precargar" y el juego arranca igual. En almacenamiento local, que
     * es el caso habitual de esta app, la cota nunca se toca.
     */
    public static boolean probeFits(String gameUri) {
        return probeFits(probeBounded(gameUri));
    }

    /** Veredicto sobre una medida ya tomada (null = no se midió = no cabe). */
    public static boolean probeFits(long[] probe) {
        if (probe == null) return false;
        final boolean fits = probe[0] == V_FITS;
        Log.i(TAG, "probe verdict=" + probe[0]
            + " need=" + mb(probe[1]) + "MB limit=" + mb(probe[2])
            + "MB avail=" + mb(probe[3]) + "MB -> " + (fits ? "cabe" : "no cabe"));
        return fits;
    }

    /**
     * Medida con cota, para llamar desde el hilo que sea (UI incluida): la
     * llamada real va a un worker daemon y nunca se espera más de
     * PROBE_TIMEOUT_MS. Devuelve null si no se pudo medir o si se agotó la cota.
     */
    public static long[] probeBounded(String gameUri) {
        if (gameUri == null || gameUri.isEmpty()) return null;
        java.util.concurrent.Future<long[]> fut =
            S_PROBE_EXEC.submit(() -> NativeApp.safePrecacheProbe(gameUri));
        try {
            return fut.get(PROBE_TIMEOUT_MS, java.util.concurrent.TimeUnit.MILLISECONDS);
        } catch (java.util.concurrent.TimeoutException t) {
            fut.cancel(true);
            Log.i(TAG, "probe timed out after " + PROBE_TIMEOUT_MS + "ms");
            return null;
        } catch (Throwable t) {
            Log.i(TAG, "probe failed: " + t);
            return null;
        }
    }

    /**
     * Orden por-juego del diálogo de ajustes: posición 0 = Auto (borrar la
     * clave: Cenit decide), 1 = Sí, 2 = No. Borrar es lo que deja la capa
     * limpia para que la decisión automática de la capa base vuelva a mandar.
     */
    public static boolean applyPerGame(String gameUri, int position) {
        if (gameUri == null || gameUri.isEmpty()) return false;
        switch (position) {
            case 1:
                return NativeApp.safeSetGameSettingInt(gameUri, INI_SECTION, INI_KEY, 1);
            case 2:
                return NativeApp.safeSetGameSettingInt(gameUri, INI_SECTION, INI_KEY, 0);
            default:
                return NativeApp.safeDeleteGameSettingKey(gameUri, INI_SECTION, INI_KEY);
        }
    }

    /** Posición inicial del spinner por-juego: 0 si la capa no tiene la clave. */
    public static int readPerGame(String gameUri) {
        if (gameUri == null || gameUri.isEmpty()) return 0;
        final int v = NativeApp.safeGetGameSettingInt(gameUri, INI_SECTION, INI_KEY, -1);
        if (v == 1) return 1;
        if (v == 0) return 2;
        return 0; // sin clave = Auto
    }

    /**
     * Texto honesto para la nota de la pantalla de Ajustes. No corre sondas a
     * propósito: se pinta en el hilo de la interfaz. Lo que mide cada juego se
     * ve en Ajustes del juego, con el archivo delante.
     */
    public static String describe(SharedPreferences prefs) {
        if (!autoEnabled(prefs)) {
            return "Apagado: ningún juego se carga entero en memoria. Cenit lo "
                + "mediría y lo cargaría solo si cabe en la RAM libre de este "
                + "teléfono (la reserva de seguridad del motor son 2 GB). Se "
                + "aplica al reiniciar cada juego; en Ajustes del juego puedes "
                + "ordenarlo a mano para uno solo. Apagado por defecto, y no por "
                + "cautela: medido en la partida de este reporte, el disco de "
                + "delante eran 4010 MB contra 2858 MB disponibles — la cuenta "
                + "dice que no cabe y el automático ni lo intenta.";
        }
        return "Automático: en cada arranque Cenit mide el juego y lo carga "
            + "entero en memoria SOLO si cabe en la RAM libre de este teléfono "
            + "(la reserva de seguridad del motor son 2 GB; en un teléfono de "
            + "4 GB casi nada cabe, en uno de 12 GB entran hasta unos 5 GB). "
            + "Se aplica al reiniciar cada juego. La decisión sale escrita en el "
            + "registro («Precache: base layer CdvdPrecache=0/1») y en el bloque "
            + "FÁBRICA del reporte con el tamaño medido, así que se puede "
            + "verificar qué hizo con tu disco en vez de confiar.";
    }

    /** Texto para la nota del diálogo por-juego, con la medida real delante. */
    public static String describeProbe(String gameUri, long[] probe) {
        if (gameUri == null || gameUri.isEmpty()) {
            return "Sin juego delante no hay nada que medir. Elige Auto/Sí/No "
                + "cuando abras un juego; se aplica al reiniciar el juego.";
        }
        if (probe == null) {
            return "No se pudo medir este archivo ahora mismo, así que Cenit no "
                + "promete nada para él. Con Sí, el núcleo decide al abrir el "
                + "disco. Se aplica al reiniciar el juego.";
        }
        switch ((int) probe[0]) {
            case V_UNSUPPORTED:
                return "Este formato de imagen no admite carga en memoria en el "
                    + "motor: con Sí ordenado a mano, el núcleo lo dirá en pantalla "
                    + "al arrancar y el juego entrará sin cargar. Se aplica al "
                    + "reiniciar el juego.";
            case V_UNKNOWN:
                return "No se pudo medir el tamaño de esta imagen. Con Sí, el "
                    + "núcleo decide al abrir el disco. Se aplica al reiniciar "
                    + "el juego.";
            case V_FITS:
                return "Este juego (" + mb(probe[1]) + " MB) CABE ahora mismo: "
                    + "se pueden reservar unos " + mb(probe[2]) + " MB de RAM "
                    + "libre. Se aplica al reiniciar el juego.";
            case V_NO_FITS:
                return "Este juego (" + mb(probe[1]) + " MB) NO cabe ahora mismo: "
                    + "solo se pueden reservar unos " + mb(probe[2]) + " MB. Con "
                    + "Sí ordenado a mano el núcleo lo intentará igualmente y, si "
                    + "no cabe, el juego arranca sin cargar (puede tardar más y "
                    + "el teléfono queda apurado). Se aplica al reiniciar el juego.";
            default:
                return "No hay imagen que medir para este juego. Se aplica al "
                    + "reiniciar el juego.";
        }
    }

    /** Cota de la medida: suficiente para un stat local, corta para un nube. */
    private static final long PROBE_TIMEOUT_MS = 1500L;

    // Workers perezosos y daemon: si un proveedor de documentos se cuelga, ese
    // worker queda ocupado y la siguiente cota pide otro — nunca se espera a
    // Dios en el hilo de la interfaz ni en el del arranque del juego.
    private static final java.util.concurrent.ExecutorService S_PROBE_EXEC =
        java.util.concurrent.Executors.newCachedThreadPool(r -> {
            Thread t = new Thread(r, "cenit-probe");
            t.setDaemon(true);
            return t;
        });

    private static long mb(long bytes) {
        return Math.max(0L, bytes) / (1024L * 1024L);
    }
}
