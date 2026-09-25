package com.izzy2lost.psx2;

import android.content.Context;
import android.content.SharedPreferences;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Medidor por perfil de driver (Cenit 0.6.20).
 *
 * La regla de la casa es medir antes de afirmar, y aqui aplica dos veces: un perfil
 * TU_DEBUG puede ser mejor en un telefono y peor en otro, y lo mismo pasa entre juegos
 * del mismo telefono. Por esto el ajuste fino no se vende como "mas FPS": se registra
 * lo que el HUD realmente vio con cada combinacion y el dialogo lo muestra.
 *
 * Se muestrea desde el vigilante de MainActivity (cada 600 ms), que ya existe para la
 * ventana de gracia del guardarraya: no anade hilos ni timers nuevos. Cada muestra se
 * guarda por triple (driver, serial, perfil) como acumulado de FPS, de forma que dos
 * sesiones con el mismo perfil se suman y una con otro perfil queda en su propia fila.
 *
 * Formato en prefs (legible a mano, mismo espiritu que AdaptiveProfile):
 *   driverId|serial|profileId=n;sum;min;max   ...  separadas por ";;"
 */
final class DriverStats {

    private DriverStats() {}

    private static final String KEY = "turnip_stats";
    /** Cubo para las medidas tomadas con el ajuste fino suspendido (guardarraya). */
    static final String PROFILE_NO_TUNING = "sintune";
    private static final int MAX_ROWS = 64;

    static final class Row {
        final String driverId;
        final String serial;
        final String profileId;
        /** Clave tal como se guarda en prefs (driver|serial|perfil). */
        String persistKey;
        int samples;
        double sum;
        float min = Float.MAX_VALUE;
        float max = 0f;

        Row(String driverId, String serial, String profileId) {
            this.driverId = driverId;
            this.serial = serial;
            this.profileId = profileId;
        }

        double avg() {
            return samples == 0 ? 0.0 : sum / samples;
        }
    }

    // El muestreador de MainActivity gira cada 1.2 s en el hilo de UI. Escribir prefs en
    // cada tick seria reescribir todo el historial ~1.6 veces por minuto, asi que la
    // muestra del tramo en curso se acumula en memoria y se vuelca cada FLUSH_EVERY
    // muestras o al parar la VM (flush). apply() es asincrono, pero el trabajo de texto no.
    private static final int FLUSH_EVERY = 10;
    private static Row sLive;
    private static int sSinceFlush;

    /**
     * Registra una muestra. Se llama solo cuando la VM esta corriendo de verdad y el
     * FPS es plausible (0.5..120); los 0 de la pausa o del menú de PS2 no ensucian la
     * media, porque miden otra cosa.
     */
    static void sample(Context ctx, String driverId, String serial, String profileId, float fps) {
        if (driverId == null || driverId.isEmpty()) { // driver del sistema: no es dato de Turnip
            flush(ctx);
            return;
        }
        if (fps < 0.5f || fps > 120f) return;
        final String key = key(driverId, serial, profileId);
        if (sLive == null || !key.equals(sLive.persistKey)) {
            flush(ctx); // cambio de triple: lo anterior ya no es comparable
            sLive = new Row(driverId, serial, profileId);
            sLive.persistKey = key;
        }
        sLive.samples++;
        sLive.sum += fps;
        sLive.min = Math.min(sLive.min, fps);
        sLive.max = Math.max(sLive.max, fps);
        if (++sSinceFlush >= FLUSH_EVERY) flush(ctx);
    }

    /** Vuelca a prefs lo acumulado en memoria. Seguro llamar aunque no haya nada. */
    static void flush(Context ctx) {
        if (sLive == null || sLive.samples == 0) { sLive = null; sSinceFlush = 0; return; }
        final SharedPreferences p = prefs(ctx);
        final String line = sLive.persistKey + "=" + sLive.samples + ";" + fmt(sLive.sum, 2) + ";"
                + fmt(sLive.min == Float.MAX_VALUE ? 0f : sLive.min, 2) + ";" + fmt(sLive.max, 2);
        final List<String> rows = new ArrayList<>();
        final String raw = p.getString(KEY, "");
        if (raw != null && !raw.isEmpty()) {
            for (String r : raw.split(";;")) {
                if (!r.isEmpty()) rows.add(r);
            }
        }
        int hit = -1;
        for (int i = 0; i < rows.size(); i++) {
            if (rows.get(i).startsWith(sLive.persistKey + "=")) { hit = i; break; }
        }
        if (hit >= 0) {
            // Se acumula sobre lo ya guardado: dos sesiones con el mismo perfil suman.
            final Row prev = parse(rows.get(hit), sLive.persistKey);
            if (prev != null) {
                sLive.samples += prev.samples;
                sLive.sum += prev.sum;
                sLive.min = Math.min(sLive.min, prev.min);
                sLive.max = Math.max(sLive.max, prev.max);
                rows.set(hit, line0(sLive));
            } else {
                rows.set(hit, line);
            }
        } else {
            rows.add(line);
        }
        while (rows.size() > MAX_ROWS) rows.remove(0);
        p.edit().putString(KEY, join(rows)).apply();
        sLive = null;
        sSinceFlush = 0;
    }

    private static String line0(Row r) {
        return r.persistKey + "=" + r.samples + ";" + fmt(r.sum, 2) + ";"
                + fmt(r.min == Float.MAX_VALUE ? 0f : r.min, 2) + ";" + fmt(r.max, 2);
    }

    private static String key(String driverId, String serial, String profileId) {
        return driverId + "|" + s(serial) + "|" + s(profileId);
    }

    private static String s(String v) {
        return v == null ? "" : v;
    }

    /** Filas de un driver y juego dados, con la media redondeada para mostrar. */
    static List<Row> rowsFor(Context ctx, String driverId, String serial) {
        List<Row> out = new ArrayList<>();
        final String raw = prefs(ctx).getString(KEY, "");
        if (raw == null || raw.isEmpty()) return out;
        for (String r : raw.split(";;")) {
            final int eq = r.indexOf('=');
            if (eq <= 0) continue;
            final String key = r.substring(0, eq);
            final String[] parts = key.split("\\|", 3);
            if (parts.length < 3) continue;
            if (driverId != null && !driverId.equals(parts[0])) continue;
            if (serial != null && !serial.isEmpty() && !serial.equals(parts[1])) continue;
            final Row row = parse(r, key);
            if (row != null) out.add(row);
        }
        return out;
    }

    static void clear(Context ctx) {
        prefs(ctx).edit().remove(KEY).apply();
    }

    /** Nombre legible de una etiqueta de medida (perfil, regla de juego, o suspendido). */
    static String labelFor(String bucket) {
        if (PROFILE_NO_TUNING.equals(bucket)) return "sin ajuste fino (suspendido)";
        if (bucket.startsWith("juego:")) return "regla de este juego (" + bucket.substring(6) + ")";
        for (TurnipTuning.Profile pr : TurnipTuning.PROFILES)
            if (bucket.equals(pr.id)) return pr.label;
        if (bucket.endsWith("+cache")) return labelFor(bucket.substring(0, bucket.length() - 6)) + " + cache";
        return bucket;
    }

    /** Resumen en una linea para el dialogo: "perfil 58.3 fps (120 muestras)". */
    static String brief(Row r) {
        if (r.samples == 0) return "sin datos";
        return String.format(Locale.US, "%.1f fps (%d muestras, min %.1f)",
                r.avg(), r.samples, r.min == Float.MAX_VALUE ? 0f : r.min);
    }

    private static Row parse(String line, String key) {
        final int eq = line.indexOf('=');
        if (eq <= 0) return null;
        final String[] parts = key.split("\\|", 3);
        if (parts.length < 3) return null;
        final Row row = new Row(parts[0], parts[1], parts[2]);
        final String[] vals = line.substring(eq + 1).split(";");
        if (vals.length < 4) return null;
        try {
            row.samples = Integer.parseInt(vals[0]);
            row.sum = Double.parseDouble(vals[1]);
            row.min = Float.parseFloat(vals[2]);
            row.max = Float.parseFloat(vals[3]);
        } catch (NumberFormatException e) {
            return null;
        }
        return row;
    }

    private static String join(List<String> rows) {
        StringBuilder sb = new StringBuilder();
        for (String r : rows) {
            if (sb.length() > 0) sb.append(";;");
            sb.append(r);
        }
        return sb.toString();
    }

    private static String fmt(double v, int decimals) {
        return String.format(Locale.US, "%." + decimals + "f", v);
    }

    private static SharedPreferences prefs(Context ctx) {
        return ctx.getApplicationContext().getSharedPreferences(TurnipTuning.PREFS, Context.MODE_PRIVATE);
    }
}
