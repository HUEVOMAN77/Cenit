package com.izzy2lost.psx2;

import android.content.Context;
import android.content.SharedPreferences;
import android.util.Log;

import java.util.HashMap;
import java.util.Map;

/**
 * Cenit 0.6.28 — Reglas de fábrica por juego.
 *
 * El canal existía en la cabeza de todos pero en ningún sitio: el sintonizador
 * CPU/VU (0.6.27) mide, adopta y guarda el veredicto SOLO en el teléfono que lo
 * midió. Este es el paso que faltaba para convertir un veredicto medido en
 * "configuración de fábrica" (el punto 2 del plan del README): una tabla que
 * viaja en el APK y se escribe en la CAPA BASE del INI antes de arrancar cada
 * juego, para que el beneficio llegue a todo el mundo sin que nadie edite nada.
 *
 * Tres reglas de precedencia, y son lo importante del archivo:
 *
 *   1. La orden explícita del usuario manda siempre. Cada regla comprueba
 *      prefs.contains(clave): si alguien tocó ese interruptor, la fábrica se
 *      calla. Sin esta comprobación, "mejorar el juego por defecto" se
 *      convertiría en "rehacerle los ajustes a quien ya los eligió".
 *   2. La evidencia de ESTE teléfono manda sobre la fábrica. Si el juego ya
 *      tiene veredicto del sintonizador (adoptado O "ninguno"), la regla no se
 *      aplica: ese teléfono ya demostró qué necesita, y un -1 heredado de otro
 *      hardware le quitaría cuadros gratis. La regla es un puente para el
 *      primer arranque, no un dogma permanente.
 *   3. La capa por-juego del núcleo (gamesettings/<SERIAL>.ini) manda sobre la
 *      capa base por construcción — la capa de juego se busca antes que la
 *      base en LayeredSettingsInterface — así que ni hace falta comprobarla.
 *
 * Y una regla que no necesita tabla ni veredicto, porque sale de medir: el
 * tamaño del disco. Los juegos que NO caben en RAM (los ISOs DVD-9 de más de
 * 4 GB, God of War entre ellos) se juegan por streaming, y ahí la única
 * palanca es la ventana de lectura por adelantado. Cenit la sube de fábrica
 * cuando la sonda confirma que el disco es grande, que es justo el caso donde
 * cada lectura corta sale cara.
 *
 * Formato de la tabla: serial normalizada -> overrides. Los seriales son los
 * verificados en PS2 DataCenter (disco en sí, no el número impreso en la caja),
 * con sus variantes regionales, porque el usuario puede traer el disco que
 * traiga. Meter aquí un veredicto sin medir es exactamente el error que costó
 * el ciclo 0.6.25→0.6.26; cada entrada lleva al lado POR QUÉ está y con qué
 * evidencia.
 */
final class FactoryRules {

    private static final String TAG = "FactoryRules";

    /** Un ajuste que la fábrica propone. -99 = "sin opinión", no escribir. */
    static final int NONE = -99;

    static final class Rule {
        int rate = NONE;              // EmuCore/Speedhacks/EECycleRate (capa base)
        int mtvu = NONE;              // -1 sin opinión, 0/1 valor
        /** Qué preferencia de usuario anula cada override (null = ninguna). */
        String ratePref = "ee_cycle_rate";
        String mtvuPref = "mtvu";
        /** Por qué existe la regla: se imprime en el registro y en el reporte. */
        String why = "";

        boolean empty() {
            return rate == NONE && mtvu == NONE;
        }
    }

    /**
     * Tabla por serial. Vacía a propósito en 0.6.28: aún no tengo veredictos
     * ajenos al teléfono que los midió. Lo que se llena aquí viene de los
     * reportes (bloque "FÁBRICA" del reporte de errores), nunca de suposiciones.
     */
    private static final Map<String, Rule> BY_SERIAL = new HashMap<>();

    static {
        // Ninguna entrada todavía. Las variantes regionales verificadas se
        // registran aquí cuando exista evidencia:
        //   SCUS-97399 / SCES-53133  God of War (DVD-9)
        //   SCUS-97481 / SCES-54878  God of War II
        //   SCUS-97472 / SCES-53326  Shadow of the Colossus
    }

    private FactoryRules() {}

    /** Serial normalizada que Cenit declara para este juego (caché, nombre, VM). */
    static String serialFor(Context ctx, String gameUri) {
        try {
            return TurnipTuning.serialForGame(ctx, gameUri);
        } catch (Throwable t) {
            return "";
        }
    }

    static Rule ruleFor(String serial) {
        if (serial == null || serial.isEmpty()) return null;
        final Rule r = BY_SERIAL.get(serial.toUpperCase(java.util.Locale.ROOT));
        return (r == null || r.empty()) ? null : r;
    }

    /**
     * Regla de tamaño de disco (no necesita tabla): un ISO que no cabe en RAM se
     * juega entero por streaming, y ahí la ventana de 128 KB del núcleo es corta.
     *
     * Con >= 2.5 GB se sube a 512 KB por buffer (1 MB de cola leída por
     * adelantado). Digo de dónde sale el número y de dónde NO sale: el umbral es
     * medible (los DVD-9 del catálogo problemático arrancan ahí, y un CD de 700 MB
     * jamás entra), pero el multiplicador es un criterio de ingeniería — cuatro
     * veces la ventana histórica, no dieciséis, porque todavía no tengo en este
     * teléfono la curva "ventana contra tiempo de cuadro". El usuario la puede
     * subir a mano en Ajustes, y esa elección (una preferencia existente) manda
     * sobre la fábrica. Es un paso chico y reversible, no una promesa.
     *
     * Se decide con la MEDIDA del archivo, no con la sospecha: {@code needBytes}
     * es el tamaño que devolvió la sonda (o -1 si no se midió, en cuyo caso no
     * hay regla).
     */
    static int bufferKbForSize(long needBytes) {
        if (needBytes < 0) return NONE;
        if (needBytes < 2_500_000_000L) return NONE;   // cabe de sobra: no tocar
        return 512;
    }

    /**
     * Aplica la fábrica a la capa base para el juego que va a arrancar. Corre
     * en el HILO DE LA VM (justo antes de runVMThread), porque la decisión de
     * tamaño necesita la sonda y la sonda puede tardar su cota.
     *
     * @param needBytes tamaño medido del disco, o -1 si no se pudo medir.
     * @return resumen legible para el registro y el reporte ("" si no aplicó).
     */
    static String applyForBoot(Context ctx, SharedPreferences prefs, String gameUri, long needBytes) {
        if (ctx == null || prefs == null || gameUri == null || gameUri.isEmpty()) return "";
        final StringBuilder applied = new StringBuilder();

        // 1) Regla por serial, solo si este teléfono todavía no opinó.
        final String serial = serialFor(ctx, gameUri);
        final Rule r = ruleFor(serial);
        if (r != null && !phoneAlreadyKnows(ctx, gameUri)) {
            if (r.rate != NONE && !prefs.contains(r.ratePref)) {
                NativeApp.speedhackEecyclerate(r.rate);
                append(applied, "rate=" + r.rate);
            }
            // EECycleSkip NO se toca: es la lección de 0.6.27. El CycleSkip
            // global no tiene preferencia que lo re-escriba en cada arranque,
            // así que una regla que lo bajara por un juego se quedaría puesta
            // para TODA la biblioteca. Lo que manda por juego vive en el INI de
            // gamesettings, y eso es terreno del sintonizador, no de la fábrica.
            if (r.mtvu != NONE && !prefs.contains(r.mtvuPref)) {
                NativeApp.setMTVU(r.mtvu == 1);
                append(applied, "mtvu=" + r.mtvu);
            }
            if (applied.length() > 0 && r.why != null && !r.why.isEmpty())
                append(applied, "por " + r.why);
        }

        // 2) Regla de tamaño: streaming más ancho cuando el disco no cabe.
        final int kb = bufferKbForSize(needBytes);
        if (kb != NONE && !prefs.contains("iso_buffer_kb")) {
            NativeApp.safeSetIsoReadBufferKb(kb);
            append(applied, "ventana=" + kb + "KB por disco de " + (needBytes / (1024 * 1024)) + "MB");
        }

        if (applied.length() == 0) return "";
        Log.i(TAG, "Fabrica " + (serial.isEmpty() ? "(sin serial)" : serial) + ": " + applied);
        return applied.toString();
    }

    /**
     * ¿Este teléfono ya midió este juego? Con veredicto del sintonizador —
     * adoptado o "ninguno" — la fábrica se aparta: hay evidencia local y pesa
     * más que una regla heredada de otro hardware.
     *
     * De paquete, no privada: con la tabla por serial aún vacía es la única
     * forma de que el arnés compruebe la regla 2 sin tener que inventarse
     * entradas en BY_SERIAL.
     */
    static boolean phoneAlreadyKnows(Context ctx, String gameUri) {
        try {
            final AdaptiveProfile p = new AdaptiveProfile(ctx, gameUri);
            if (!p.known()) return false;
            return p.tunerAplicado || (p.tunerVerdict != null && !p.tunerVerdict.isEmpty());
        } catch (Throwable t) {
            return false;
        }
    }

    private static void append(StringBuilder sb, String s) {
        if (sb.length() > 0) sb.append(' ');
        sb.append(s);
    }

    /** Cuántas reglas hay dentro del APK: lo dice la pantalla de ajustes. */
    static int ruleCount() {
        int n = 0;
        for (Rule r : BY_SERIAL.values()) if (!r.empty()) n++;
        return n;
    }
}
