package com.izzy2lost.psx2;

import android.content.SharedPreferences;
import android.util.Log;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Cenit 0.6.28 — El interruptor "máxima jugabilidad".
 *
 * El usuario de un teléfono de gama media no quiere elegir seis cosas: quiere
 * que el juego se mueva. Este archivo reúne en UN interruptor las palancas que
 * dan cuadros y dice en la misma pantalla lo que cuesta cada una. No es un "modo
 * turbo": es un paquete coherente, y la coherencia está también en lo que NO
 * toca (lo de abajo).
 *
 * Dos reglas hacen esto reversible de verdad:
 *
 *   1. Al encenderse se guarda una FOTOGRAFÍA del estado previo de todas las
 *      claves que el modo escribe, con tipo y con la marca de si estaban puestas
 *      de verdad. Al apagarse se restaura ESA fotografía, no unos "defaults"
 *      inventados: quitar el modo devuelve exactamente lo que había antes de
 *      ponerlo. Solo si la fotografía no existe (un cierre brusco entre encender
 *      y apagar, o la preferencia borrada a mano) se escriben los defaults
 *      documentados de cada clave, y se dice en el registro: quedarse callado
 *      sería dejar encendido algo que el usuario ya no pidió.
 *   2. No hay estado paralelo. El modo escribe las MISMAS preferencias que mueve
 *      cada interruptor de Ajustes, así que lo que el usuario toque después es
 *      suyo: el modo no lo reclama en cada arranque. "Máxima jugabilidad" es una
 *      forma cómoda de poner el tablero, no un dueño del tablero.
 *
 * La escala de resolución es la parte honesta del asunto. El modo escribe
 * upscale_multiplier con el techo que ESTE teléfono aguanta (gama baja 1x, media
 * 1.5x, alta 2x) pero nunca lo SUBE por encima de lo que eligió el usuario: si ya
 * estaba por debajo, se queda como está, porque bajar de ahí es precisamente lo
 * que el regidor dinámico hace solo. Y lo restaura al apagar.
 *
 * Lo que NO toca, y por qué:
 *   - ee_cycle_rate / cuotas: bajar la CPU emulada es la decisión MÁS visible
 *     que hay (el juego pasa a ir más despacio de reloj). Es terreno del
 *     sintonizador CPU/VU, que lo mide por juego y lo guarda por juego. Meterlo
 *     global desde un interruptor sería repetir el error de 0.6.11.
 *   - fast_cdvd: rompe juegos que leen sincronizado. No es una palanca de
 *     jugabilidad, es una trampa.
 *   - vu_superblock y la sonda de trazas VU: el motor por dentro. La puerta del
 *     plan exige cero divergencias medidas antes de habilitar, y un interruptor
 *     gregario no abre esa puerta. (No se toca el motor aquí.)
 *   - iso_buffer_kb: se BORRA la preferencia al encender, no se escribe un valor.
 *     Sin preferencia es la regla de fábrica de tamaño la que decide, medida en
 *     mano, para cada disco; un número fijo del modo apagaría esa ayuda.
 */
public final class PlayabilityBundle {

    private static final String TAG = "Playability";

    /** Preferencia del modo. */
    public static final String PREF_ON = "max_playability";
    /** Fotografía del estado previo, escrita ANTES de tocar nada. */
    static final String PREF_SNAPSHOT = "max_playability_prev";

    static final String KEY_UPSCALE = "upscale_multiplier";
    static final String KEY_ISO_BUFFER = "iso_buffer_kb";

    private PlayabilityBundle() {}

    /** Las booleanas del paquete. El orden es el de la explicación en pantalla. */
    static final String DYN_RES = "dynamic_res";
    static final String ADAPTIVE = "adaptive_perf";
    static final String EASU = "easu_reconstruct";
    static final String TUNER = CpuVUTuner.PREF_KEY;
    static final String ASYNC_SHADERS = "async_shader_compile";
    static final String PRECACHE = PrecachePolicy.PREF_AUTO;

    /**
     * Etiqueta de escala para el texto en pantalla: 1x, 1.5x, 2x. No uso "%g"
     * porque en Java deja los ceros del precision por defecto y salía
     * "Escala tope: 1.00000x", que es un texto de depuración, no de ajustes.
     */
    static String scaleLabel(float s) {
        return (s == (float) Math.floor(s)) ? String.valueOf((int) s) : String.valueOf(s);
    }

    /**
     * Escala que el modo propone por gama. Es el techo del regidor dinámico, no
     * una orden de dibujar siempre a esa escala: el regidor baja solo cuando el
     * juego no la sostiene. Gama baja se queda en 1x porque ahí más píxeles no
     * son nitidez, son cuadros perdidos.
     *
     * Coincide con PerfProfile.defaultUpscale a propósito: es el techo del
     * regidor Y la escala con la que Cenit arranca un teléfono de esa gama, así
     * que el modo no cambia la resolución de un teléfono que ya estaba en su
     * default. Si algún día se separan, hay que decirlo en describe().
     */
    public static float scaleFor(int tier) {
        switch (tier) {
            case PerfProfile.TIER_HIGH: return 2.0f;
            case PerfProfile.TIER_MID:  return 1.5f;
            default:                    return 1.0f;
        }
    }

    /**
     * La escala que el modo va a escribir: la propuesta por gama, pero NUNCA por
     * encima de la que el usuario eligió (si él ya está más abajo, está usando
     * una salida más conservadora que la nuestra y no se la subimos).
     */
    public static float scaleToWrite(SharedPreferences prefs, int tier) {
        final float want = scaleFor(tier);
        final float user = prefs.getFloat(KEY_UPSCALE, want);
        return Math.min(want, user);
    }

    /** Valor por defecto documentado de cada clave (el plan B sin fotografía). */
    static boolean defaultBooleanFor(String key) {
        switch (key) {
            case DYN_RES:
            case ADAPTIVE:
            case EASU:
                return true;
            default:
                return false; // TUNER, ASYNC_SHADERS, PRECACHE
        }
    }

    /** La booleana que el modo enciende al entrar. */
    static Map<String, Boolean> valuesFor(int tier) {
        final LinkedHashMap<String, Boolean> v = new LinkedHashMap<>();
        v.put(DYN_RES, true);
        v.put(ADAPTIVE, true);
        v.put(EASU, true);
        v.put(TUNER, true);
        v.put(ASYNC_SHADERS, true);
        // En gama baja la precarga queda fuera del paquete: con 3-4 GB de RAM,
        // copiar el disco entero desaloja la caché del propio juego, que es justo
        // lo que se quería evitar. La sonda ya protege, pero el modo no debe
        // prometer algo que en esa gama no ayuda.
        if (tier != PerfProfile.TIER_LOW) v.put(PRECACHE, true);
        return v;
    }

    /**
     * FOTOGRAFÍA COMPLETA, legible a mano: "clave=tipo:en_valOR:valor" separado
     * por ';'. tipo b/f/i, en_valOR 1 si la preferencia existía (0 = no estaba
     * puesta: al apagar se BORRA, para no dejar un default escrito de más).
     *
     * Se anotan SIEMPRE las seis booleanas, aunque la gama no escriba todas: la
     * lista de escrituras depende de la gama y la de la fotografía no, así que
     * apagar el modo en gama baja no se queda sin dónde mirar cdvd_precache.
     */
    static final String[] BOOL_KEYS = { DYN_RES, ADAPTIVE, EASU, TUNER, ASYNC_SHADERS, PRECACHE };

    static String snapshot(SharedPreferences prefs) {
        final StringBuilder sb = new StringBuilder();
        for (String key : BOOL_KEYS) sb.append(pair(sb, key, "b", prefs.getBoolean(key, defaultBooleanFor(key)) ? 1 : 0, prefs.contains(key)));
        sb.append(pair(sb, KEY_UPSCALE, "f", floatBits(prefs.getFloat(KEY_UPSCALE, 1f)), prefs.contains(KEY_UPSCALE)));
        sb.append(pair(sb, KEY_ISO_BUFFER, "i", prefs.getInt(KEY_ISO_BUFFER, 128), prefs.contains(KEY_ISO_BUFFER)));
        return sb.toString();
    }

    private static String pair(StringBuilder sb, String key, String type, long value, boolean present) {
        if (sb.length() > 0) sb.append(';');
        return key + "=" + type + ":" + (present ? 1 : 0) + ":" + value;
    }

    /** El float se guarda como sus bits: la fotografía no redondea nada. */
    private static long floatBits(float f) {
        return (long) Float.floatToRawIntBits(f) & 0xFFFFFFFFL;
    }

    /**
     * Lectura de la fotografía: {clave, tipo, estabaPuesta, valor}. null si la
     * cadena no es legible — entonces quien llama usa el plan B documentado.
     */
    static String[][] parseSnapshot(String raw) {
        if (raw == null || raw.trim().isEmpty()) return null;
        final ArrayList<String[]> out = new ArrayList<>();
        for (String part : raw.split(";")) {
            final int eq = part.indexOf('=');
            if (eq <= 0 || eq + 1 >= part.length()) return null;
            final String key = part.substring(0, eq).trim();
            final String[] bits = part.substring(eq + 1).trim().split(":");
            if (key.isEmpty() || bits.length != 3) return null;
            if (!"b".equals(bits[0]) && !"f".equals(bits[0]) && !"i".equals(bits[0])) return null;
            if (!"0".equals(bits[1]) && !"1".equals(bits[1])) return null;
            try {
                Long.parseLong(bits[2]);
            } catch (NumberFormatException e) {
                return null;
            }
            out.add(new String[] { key, bits[0], bits[1], bits[2] });
        }
        return out.isEmpty() ? null : out.toArray(new String[0][]);
    }

    private static List<String> restore(SharedPreferences prefs, String[][] snap) {
        final SharedPreferences.Editor ed = prefs.edit();
        final List<String> done = new ArrayList<>();
        for (String[] e : snap) {
            final String key = e[0];
            final boolean present = "1".equals(e[2]);
            final long v = Long.parseLong(e[3]);
            if (!present) ed.remove(key);
            else if ("b".equals(e[1])) ed.putBoolean(key, v != 0L);
            else if ("i".equals(e[1])) ed.putInt(key, (int) v);
            else ed.putFloat(key, Float.intBitsToFloat((int) v));
            done.add(key);
        }
        ed.apply();
        return done;
    }

    /**
     * Encender el modo. El orden importa por el mismo motivo que el diario del
     * sintonizador en 0.6.27: la fotografía se escribe ANTES de tocar nada, así
     * que un cierre a mitad deja estado suficiente para deshacer al volver.
     *
     * @return lo que el modo encendió, para poder contarlo.
     */
    public static Map<String, Boolean> turnOn(SharedPreferences prefs, int tier) {
        final Map<String, Boolean> want = valuesFor(tier);
        final float scale = scaleToWrite(prefs, tier);
        final String snap = snapshot(prefs);

        final SharedPreferences.Editor ed = prefs.edit();
        ed.putString(PREF_SNAPSHOT, snap);   // antes de cualquier escritura
        for (Map.Entry<String, Boolean> e : want.entrySet()) ed.putBoolean(e.getKey(), e.getValue());
        ed.putFloat(KEY_UPSCALE, scale);
        ed.remove(KEY_ISO_BUFFER);           // que hable la regla de fábrica
        ed.putBoolean(PREF_ON, true);
        ed.apply();

        // Notificar lo que se nota sin reiniciar. Las demás se leen desde la
        // preferencia en cada arranque o en cada tick del regidor: dynamic_res,
        // adaptive_perf y cpu_tuner no tienen setter nativo a propósito.
        NativeApp.renderUpscalemultiplierAsync(scale);
        NativeApp.setEASUReconstructAsync(true);
        NativeApp.setAsyncShaderCompileAsync(true);

        Log.i(TAG, "modo maxima jugabilidad ON (" + summaryFor(want) + " escala=" + scale + "x)");
        return want;
    }

    /**
     * Apagar el modo: devolver la fotografía, o los defaults si no la hay.
     *
     * @param tier gama de ESTE teléfono, para la re-notificación de la escala:
     *     si al restaurar la preferencia queda BORRADA (el usuario no la había
     *     tocado nunca), lo que hay que decirle al núcleo es el default de la
     *     gama — que es exactamente con lo que Cenit arranca ese teléfono — y no
     *     un 1x fijo, que dejaría en resolución nativa un teléfono que estaba en
     *     1.5x/2x por perfil. Se pasa como número porque la clase no tiene
     *     Context: quien llama (Ajustes) ya tiene la gama en la mano.
     * @return las claves restauradas (vacío si no había fotografía).
     */
    public static List<String> turnOff(SharedPreferences prefs, int tier) {
        final String raw = prefs.getString(PREF_SNAPSHOT, null);
        final String[][] snap = parseSnapshot(raw);
        final List<String> done;
        if (snap != null) {
            done = restore(prefs, snap);
        } else {
            Log.i(TAG, "apagando el modo SIN fotografia: se restauran los defaults declarados");
            done = new ArrayList<>();
            final SharedPreferences.Editor ed = prefs.edit();
            for (String key : BOOL_KEYS) {
                ed.putBoolean(key, defaultBooleanFor(key));
                done.add(key);
            }
            ed.apply();
        }
        final SharedPreferences.Editor ed = prefs.edit();
        ed.putBoolean(PREF_ON, false);
        ed.remove(PREF_SNAPSHOT);
        ed.apply();

        NativeApp.renderUpscalemultiplierAsync(prefs.getFloat(KEY_UPSCALE, scaleFor(tier)));
        NativeApp.setEASUReconstructAsync(prefs.getBoolean(EASU, defaultBooleanFor(EASU)));
        NativeApp.setAsyncShaderCompileAsync(
                prefs.getBoolean(ASYNC_SHADERS, defaultBooleanFor(ASYNC_SHADERS)));

        Log.i(TAG, "modo maxima jugabilidad OFF, restauradas " + done.size() + " claves");
        return done;
    }

    public static boolean isOn(SharedPreferences prefs) {
        return prefs.getBoolean(PREF_ON, false);
    }

    /**
     * El usuario movió UNA clave del tablero con el modo encendido: la
     * fotografía se actualiza a esa clave. Sin esto, apagar el modo más tarde
     * restauraría el valor de ANTES de encenderlo y le ganaría al cambio hecho
     * a mano — y la regla 2 del archivo dice que lo que el usuario toca después
     * es suyo. Llamarla desde cada interruptor del paquete (y del spinner de
     * escala) cuando isOn() es true.
     */
    public static void rebaseline(SharedPreferences prefs, String key) {
        if (!isOn(prefs)) return;
        rebaselineRaw(prefs.getString(PREF_SNAPSHOT, null), prefs, key)
                .ifPresent(s -> prefs.edit().putString(PREF_SNAPSHOT, s).apply());
    }

    /** Devuelve la fotografía con la clave re-anotada, si es una clave del paquete. */
    static java.util.Optional<String> rebaselineRaw(String raw, SharedPreferences prefs, String key) {
        final String[][] snap = parseSnapshot(raw);
        if (snap == null) return java.util.Optional.empty(); // sin foto: nada que anotar
        final StringBuilder sb = new StringBuilder();
        boolean hit = false;
        for (String[] e : snap) {
            if (e[0].equals(key)) {
                hit = true;
                appendEntry(sb, e[0], e[1], true, currentValue(e[1], prefs, e[0], e[3]));
            } else {
                appendEntry(sb, e[0], e[1], "1".equals(e[2]), e[3]);
            }
        }
        return hit ? java.util.Optional.of(sb.toString()) : java.util.Optional.empty();
    }

    private static void appendEntry(StringBuilder sb, String key, String type,
                                    boolean present, String value) {
        if (sb.length() > 0) sb.append(';');
        sb.append(key).append('=').append(type).append(':').append(present ? 1 : 0)
          .append(':').append(value);
    }

    /** El valor actual de la clave, en la forma textual de la fotografía. */
    private static String currentValue(String type, SharedPreferences prefs, String key, String old) {
        switch (type) {
            case "b": return prefs.getBoolean(key, defaultBooleanFor(key)) ? "1" : "0";
            case "i": return String.valueOf(prefs.getInt(key, 128));
            case "f": return String.valueOf(Float.floatToRawIntBits(prefs.getFloat(key, 1f)) & 0xFFFFFFFFL);
            default:  return old;
        }
    }

    /**
     * Texto honesto para la pantalla: qué trae y qué cuesta. La frase que no
     * puede faltar es el costo — si un interruptor da cuadros en silencio, el
     * usuario interpreta la imagen peor como un bug.
     */
    public static String describe(int tier) {
        final String gama = tier == PerfProfile.TIER_HIGH ? "gama alta"
                : tier == PerfProfile.TIER_MID ? "gama media" : "gama baja";
        final String precarga = tier == PerfProfile.TIER_LOW
                ? "fuera del paquete (en esta gama copiar el disco desaloja la caché "
                  + "del propio juego; puedes encenderla a mano aparte)"
                : "automática y medida, juego por juego";
        return "Todo lo que da cuadros, encendido a la vez y con su costo dicho: "
            + "resolución dinámica y memoria por juego (la imagen sube y baja sola), "
            + "reconstrucción EASU por debajo de 1x (se ve mejor que estirado, pero "
            + "no inventa detalle que nunca se renderizó), sintonizador CPU/VU (mide "
            + "una vez por juego y guarda SOLO lo que gana de verdad) y compilación "
            + "de shaders en segundo plano (un efecto que se estrena puede parpadear "
            + "uno o dos frames en vez de frenar el cuadro entero). Escala tope: "
            + scaleLabel(scaleFor(tier)) + "x en " + gama + ", y nunca por encima de la tuya. "
            + "Precarga en memoria: "
            + precarga + ". "
            + "El modo NO toca las cuotas de CPU, ni Fast CDVD, ni el motor VU por "
            + "dentro. Al apagarlo vuelve exactamente lo que tenías antes, incluida "
            + "tu escala. Experimental: si un juego va raro con el modo, apágalo y "
            + "cuéntame cuál es.";
    }

    /** Resumen de lo que se activó, para el registro y para FÁBRICA del reporte. */
    static String summaryFor(Map<String, Boolean> applied) {
        final StringBuilder sb = new StringBuilder();
        for (Map.Entry<String, Boolean> e : applied.entrySet()) {
            if (sb.length() > 0) sb.append(' ');
            sb.append(e.getKey()).append('=').append(e.getValue() ? 1 : 0);
        }
        return sb.toString();
    }
}
