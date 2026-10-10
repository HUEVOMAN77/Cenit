package android.content;

import java.util.HashMap;
import java.util.Map;

/** Stub de Context: reparte prefs y hace de PowerManager. */
public class Context {
    public final Map<String, SharedPreferences> files = new HashMap<>();
    public static final int MODE_PRIVATE = 0;
    public static final String POWER_SERVICE = "power";
    public android.os.PowerManager power;

    public SharedPreferences getSharedPreferences(String name, int mode) {
        return files.computeIfAbsent(name, k -> new SharedPreferences());
    }
    public Context getApplicationContext() { return this; }
    public Object getSystemService(String name) {
        return POWER_SERVICE.equals(name) ? power : null;
    }
}
