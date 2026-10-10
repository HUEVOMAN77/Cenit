package android.content;

import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Stub de SharedPreferences para h8. Copia el de h6 (mapa) mas una cosa que hace
 * falta aqui y no en h6: el DIARIO del sintonizador se escribe en prefs, y la
 * promesa del prompt es "diario ANTES de cada escritura del INI". Para poder
 * comprobar un ORDEN entre dos almacenes distintos, los eventos del diario se
 * vuelcan al mismo stream ordenado que las escrituras del mock nativo
 * (NativeApp.writes). Sin eso, "primero el diario y luego la cuota" seria una
 * afirmacion que el arnes no puede ver.
 */
public class SharedPreferences {
    public final Map<String, Object> values = new LinkedHashMap<>();

    static final String JOURNAL = "cpu_tuner_pending:";

    private static void journalEvent(String kind, String key, Object v) {
        if (key != null && key.startsWith(JOURNAL)) {
            com.izzy2lost.psx2.NativeApp.writes.add(kind + ":" + (v == null ? "" : v));
        }
    }

    public boolean getBoolean(String key, boolean def) {
        Object v = values.get(key);
        return v instanceof Boolean ? (Boolean) v : def;
    }
    public int getInt(String key, int def) {
        Object v = values.get(key);
        return v instanceof Integer ? (Integer) v : def;
    }
    public long getLong(String key, long def) {
        Object v = values.get(key);
        return v instanceof Long ? (Long) v : def;
    }
    public float getFloat(String key, float def) {
        Object v = values.get(key);
        return v instanceof Float ? (Float) v : def;
    }
    public String getString(String key, String def) {
        Object v = values.get(key);
        return v instanceof String ? (String) v : def;
    }
    public boolean contains(String key) { return values.containsKey(key); }
    public Map<String, ?> getAll() { return new LinkedHashMap<>(values); }
    public Editor edit() { return new Editor(); }

    public class Editor {
        public Editor putBoolean(String key, boolean v) { values.put(key, v); return this; }
        public Editor putInt(String key, int v) { values.put(key, v); return this; }
        public Editor putLong(String key, long v) { values.put(key, v); return this; }
        public Editor putFloat(String key, float v) { values.put(key, v); return this; }
        public Editor putString(String key, String v) {
            values.put(key, v);
            journalEvent("journal-put", key, v);
            return this;
        }
        public Editor remove(String key) {
            journalEvent("journal-del", key, null);
            values.remove(key);
            return this;
        }
        public void apply() { }
    }
}
