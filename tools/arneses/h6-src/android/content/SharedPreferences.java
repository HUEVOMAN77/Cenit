package android.content;

import java.util.LinkedHashMap;
import java.util.Map;

/** Stub de SharedPreferences respaldado por un mapa, para el arnés de Cenit. */
public class SharedPreferences {
    public final Map<String, Object> values = new LinkedHashMap<>();

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
        public Editor putString(String key, String v) { values.put(key, v); return this; }
        public Editor remove(String key) { values.remove(key); return this; }
        public void apply() { }
    }
}
