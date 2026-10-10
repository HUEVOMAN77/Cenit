package com.izzy2lost.psx2;

import android.content.Context;
import android.content.SharedPreferences;

import java.net.URLDecoder;
import java.net.URLEncoder;

/**
 * Cenit 0.6.31 (Fase 1 del prompt directo) — Perfil de ensayo del sintonizador,
 * indexado por IDENTIDAD COMPLETA y no por URI de partida.
 *
 * Por que existe otra clase en vez de ampliar AdaptiveProfile: AdaptiveProfile es
 * la memoria del REGIDOR DE RESOLUCION (escala sostenida, cpuBound, mezcla de
 * cargas) y vive por URI. Un candidato de speedhacks no es transferible por URI:
 * "este disco va mejor con Rate -1" es una afirmacion sobre ESTE telefono, ESTE
 * backend y ESTA version del motor. La Fase 1 del prompt lo exige explicitamente
 * (seccion 8: la clave debe incluir juego/serial/CRC, hash de dispositivo, SoC o
 * clase, backend, driver si existe, version del core y version del tuner), y tiene
 * razon: un perfil adoptado en un Snapdragon 778G con Vulkan impuesto en un Mali
 * con el mismo disco es exactamente el tipo de regla ajena al hardware que ya costo
 * el ciclo 0.6.25 -> 0.6.26.
 *
 * Separarlo tambien evita un problema de formato: AdaptiveProfile serializa con
 * ";" y "=" (el separador de clave interna), y una clave de identidad con "#" y
 * campos variables meteria ambiguity en ese parseo. Aqui cada registro vive en su
 * propia clave de prefs, con el identificador en el nombre.
 *
 * La clave CANONICA (orden fijo, siempre presente):
 *   crc|serial|soc|renderer|driver|core|tuner
 * - crc:    el del disco (getGameCrcSafe, que no abre el ISO con la VM viva).
 * - serial: el del disco, como respaldo legible para el reporte y para el caso de
 *           CRC igual entre versiones (raro, y entonces el serial lo distingue).
 * - soc:    Build.SOC_MODEL/SOC_MANUFACTURER con caida a MANUFACTURER/MODEL, por
 *           reflexion (mismo motivo que la calibracion de umbrales: el SDK de CI no
 *           siempre resuelve las APIs nuevas).
 * - renderer: la preferencia "renderer" (-1 = sin elegir). Es EL backend grafico.
 * - driver: id del driver personalizado, o "-" cuando usa el del sistema.
 * - core:   la version del APK (versionName, pasada al nativo como CENIT_APP_VERSION
 *           y leida por JNI). Un cambio de core puede cambiar el recompilador.
 * - tuner:  TUNER_VERSION. Se sube cuando cambia la METODOLOGIA de ensayo
 *           (ventanas, criterios, escalera). Es la unica pata que no se lee del
 *           sistema: es mi responsabilidad de mantenimiento, y está dicha en el log.
 *
 * Un registro cuyo id no coincide con el de ESTE momento se lee pero NO se usa para
 * actuar (es historia, no mandato). Eso es lo que impide que un perfil viejo siga
 * aplicandose despues de cambiar de driver o de version.
 */
final class TunerProfileStore {

    /**
     * Version de la METODOLOGIA del ensayo, no del APK. 1 = el sintonizador de
     * 0.6.27..0.6.30 (una ventana base seguida de una por candidato, decision por
     * mediana de FPS). 2 = el de 0.6.31 (A/B/A/B alternado con P95 y criterios de
     * audio, verificacion post-adopcion, suspension por crash). Subir este numero
     * invalida por si solo todo veredicto guardado con el metodo anterior, que es
     * justo lo que quiero: una mediana de 12 cuadros no es comparable con un P50/P95
     * de dos ventanas alternadas, y pretender que si seria comparar a ciegas.
     */
    static final int TUNER_VERSION = 2;

    static final String PREF_PREFIX = "tuner2:";
    /** Pref donde el tuner apunta el id canónico de la partida en curso. */
    static final String PREF_ACTIVE_ID = "tuner_active_id";

    /** Veredictos persistentes. "recuperado" viene del diario anti-cierre. */
    static final String V_ADOPTED = "adoptado";
    static final String V_NONE = "ninguno";
    static final String V_REJECTED = "rechazado";
    static final String V_SUSPENDED = "suspendido";
    static final String V_RECOVERED = "recuperado";

    /** Cuanto caduca un "ninguno"/"rechazado" (un driver nuevo puede cambiar todo). */
    private static final long STALE_MS = 7L * 24 * 3600 * 1000;

    /** Un crash o una regresion grave suspende el juego; esto es el cooldown. */
    private static final long SUSPEND_COOLDOWN_MS = 24L * 3600 * 1000;

    /** Identidad completa del momento, ya serializada. Nunca null. */
    static final class Identity {
        final String id;
        final String serial;
        final String crc;
        final String soc;
        final String renderer;
        final String driver;
        final String core;
        final int tunerVersion;

        Identity(String id, String serial, String crc, String soc, String renderer,
                 String driver, String core, int tunerVersion) {
            this.id = id; this.serial = serial; this.crc = crc; this.soc = soc;
            this.renderer = renderer; this.driver = driver; this.core = core;
            this.tunerVersion = tunerVersion;
        }

        @Override public String toString() { return id; }
    }

    /** Lo que se persiste de un ensayo terminado. */
    static final class Record {
        String id = "";
        String uri = "";
        String verdict = "";      // V_*
        String adopted = "";      // mascara "r=-1,s=0,m=1"
        long at = 0L;             // millis del ultimo cierre de ensayo
        int suspensions = 0;      // crashs o regresiones acumuladas
        long lastFpsBase = 0L;    // x1000, para no cambiar el tipo de todo lo de arriba
        long lastFpsAdopted = 0L;
        long lastGainPct = 0L;    // x100
        String detail = "";       // motivo legible (para Ajustes y el reporte)

        boolean known() { return !id.isEmpty(); }

        /** true si el veredicto sigue siendo accionable hoy. */
        boolean fresh(long now) {
            if (at <= 0) return false;
            final long age = now - at;
            if (V_SUSPENDED.equals(verdict)) return age < SUSPEND_COOLDOWN_MS;
            if (V_ADOPTED.equals(verdict)) return true; // no caduca: esta en vigor
            return age < STALE_MS;
        }

        /** true = hay que dejarlo en paz: ya tiene adopcion viva o esta suspendido. */
        boolean blocksRetry(long now) {
            if (V_ADOPTED.equals(verdict) && !adopted.isEmpty()) return true;
            if (V_SUSPENDED.equals(verdict) && age(now) < SUSPEND_COOLDOWN_MS) return true;
            if (V_RECOVERED.equals(verdict)) return false; // dejo reintentar
            return fresh(now) && (V_NONE.equals(verdict) || V_REJECTED.equals(verdict));
        }

        private long age(long now) { return at <= 0 ? Long.MAX_VALUE : now - at; }
    }

    private final SharedPreferences prefs;
    private final String uri;
    private final Identity identity;

    TunerProfileStore(Context app, String uri, Identity identity) {
        this.prefs = app.getApplicationContext()
                .getSharedPreferences("app_prefs", Context.MODE_PRIVATE);
        this.uri = uri == null ? "" : uri;
        this.identity = identity == null ? new Identity("", "", "", "", "", "", "", 0) : identity;
    }

    static String keyFor(String id) { return PREF_PREFIX + id; }

    /** Id de la clave activa guardada (para saber a que identidad apunta una adopcion). */
    static String activeId(SharedPreferences p) { return p.getString(PREF_ACTIVE_ID, ""); }

    /**
     * Construye la identidad CANONICA. Todo lo que no se pueda leer se registra
     * como "?" y el llamante decide: una identidad incompleta NO debe autorizar una
     * adopcion automatica (es "no se donde estoy", no "estoy en un sitio raro").
     */
    static Identity identityOf(String uri, Context ctx) {
        String serial = "";
        try { serial = GameSerialUtils.serialFromUri(uri); } catch (Throwable ignored) {}
        if (serial == null) serial = "";
        String crc = "";
        try { crc = NativeApp.getGameCrcSafe(uri); } catch (Throwable ignored) {}
        if (crc == null) crc = "";
        final String soc = socClass();
        final String renderer = String.valueOf(
                ctx.getSharedPreferences("app_prefs", Context.MODE_PRIVATE)
                        .getInt("renderer", -1));
        String driver = "-";
        try {
            final String d = ctx.getSharedPreferences("app_prefs", Context.MODE_PRIVATE)
                    .getString("custom_driver_id", "");
            if (d != null && !d.isEmpty()) driver = d;
        } catch (Throwable ignored) {}
        // La version del core se lee por PackageManager, no por JNI, y es la misma
        // cadena que el nativo hornea como CENIT_APP_VERSION: build.gradle.kts pasa
        // defaultConfig.versionName al compilador, asi que ambas patas salen del
        // mismo numero. Preferi el camino de Java por dos razones concretas:
        // (1) no abre una superficie nativa nueva solo para un string, y (2) sigue
        // funcionando con hasNoNativeBinary (el profile se sigue pudiendo borrar).
        String core = "";
        try {
            core = ctx.getPackageManager().getPackageInfo(ctx.getPackageName(), 0).versionName;
        } catch (Throwable ignored) {}
        if (core == null || core.isEmpty()) core = "?";
        final String id = join(crc.isEmpty() ? "?" : crc, serial.isEmpty() ? "?" : serial,
                soc, renderer, driver, core, String.valueOf(TUNER_VERSION));
        return new Identity(id, serial, crc, soc, renderer, driver, core, TUNER_VERSION);
    }

    private static String join(String... parts) { return String.join("|", parts); }

    /**
     * Clase del SoC por REFLEXION, copia del criterio de la calibracion de umbrales
     * (mismo motivo, misma caida): Build.SOC_MODEL es API 31 y el SDK que usa CI no
     * siempre lo resuelve. En un telefono viejo la clave es menos especifica, nunca
     * errónea: MANUFACTURER/MODEL distinguen familia de SoC en la practica.
     */
    private static String socClass() {
        for (String field : new String[]{"SOC_MODEL", "SOC_MANUFACTURER"}) {
            try {
                final Object v = android.os.Build.class.getField(field).get(null);
                if (v instanceof String && !((String) v).isEmpty()) return (String) v;
            } catch (Throwable ignored) {}
        }
        final String m = android.os.Build.MANUFACTURER + "/" + android.os.Build.MODEL;
        return m.isEmpty() ? "?" : m;
    }

    /** true si la identidad tiene todas sus patas leidas (puede autorizar acciones). */
    static boolean identityComplete(Identity id) {
        return id != null && !id.id.isEmpty() && !id.id.contains("|?|")
                && !id.crc.isEmpty() && !id.core.equals("?");
    }

    Record load() {
        final Record r = new Record();
        r.id = identity.id;
        final String raw = prefs.getString(keyFor(identity.id), null);
        if (raw == null) return r;
        for (String part : raw.split(";")) {
            final int eq = part.indexOf('=');
            if (eq <= 0 || eq + 1 >= part.length()) continue;
            final String k = part.substring(0, eq).trim();
            final String v = part.substring(eq + 1).trim();
            try {
                switch (k) {
                    case "u": r.uri = decode(v); break;
                    case "v": r.verdict = decode(v); break;
                    case "a": r.adopted = v; break;
                    case "t": r.at = Long.parseLong(v); break;
                    case "s": r.suspensions = Integer.parseInt(v); break;
                    case "fb": r.lastFpsBase = Long.parseLong(v); break;
                    case "fa": r.lastFpsAdopted = Long.parseLong(v); break;
                    case "g": r.lastGainPct = Long.parseLong(v); break;
                    case "d": r.detail = decode(v); break;
                    default: break;
                }
            } catch (NumberFormatException ignored) {
                // prefs tocadas a mano: se descarta ese campo, no el registro entero
            }
        }
        return r;
    }

    void save(Record r) {
        if (identity.id.isEmpty()) return;
        final StringBuilder sb = new StringBuilder();
        sb.append("u=").append(encode(r.uri));
        sb.append(";v=").append(encode(r.verdict));
        sb.append(";a=").append(r.adopted);
        sb.append(";t=").append(r.at);
        sb.append(";s=").append(r.suspensions);
        sb.append(";fb=").append(r.lastFpsBase);
        sb.append(";fa=").append(r.lastFpsAdopted);
        sb.append(";g=").append(r.lastGainPct);
        sb.append(";d=").append(encode(r.detail));
        prefs.edit().putString(keyFor(identity.id), sb.toString()).apply();
    }

    /** Apunta la identidad de la partida en curso (la lee Ajustes para mostrar). */
    void markActive() {
        if (identity.id.isEmpty()) return;
        prefs.edit().putString(PREF_ACTIVE_ID, identity.id).apply();
    }

    /**
     * "Borrar perfil aprendido" (Ajustes). Borra TODOS los registros del juego
     * actual, no solo el de esta identidad: si no, al cambiar de driver apareceria
     * el viejo y el boton no habria hecho lo que dice. Devuelve cuantos se fueron,
     * para que el aviso no prometa un borrado que no ocurrio.
     */
    static int forgetForUri(Context app, String uri) {
        final SharedPreferences p = app.getApplicationContext()
                .getSharedPreferences("app_prefs", Context.MODE_PRIVATE);
        int n = 0;
        final SharedPreferences.Editor ed = p.edit();
        for (String k : p.getAll().keySet()) {
            if (k == null || !k.startsWith(PREF_PREFIX)) continue;
            final String raw = p.getString(k, "");
            if (raw == null) continue;
            String recUri = "";
            for (String part : raw.split(";")) {
                if (part.startsWith("u=")) {
                    try { recUri = URLDecoder.decode(part.substring(2), "UTF-8"); }
                    catch (Throwable ignored) {}
                }
            }
            if (uri == null || uri.isEmpty() || uri.equals(recUri)) {
                ed.remove(k);
                n++;
            }
        }
        ed.apply();
        return n;
    }

    private static String encode(String v) {
        if (v == null || v.isEmpty()) return "";
        try { return URLEncoder.encode(v, "UTF-8"); } catch (Throwable t) { return ""; }
    }

    private static String decode(String v) {
        if (v == null || v.isEmpty()) return "";
        try { return URLDecoder.decode(v, "UTF-8"); } catch (Throwable t) { return v; }
    }
}
