package android.content;

import android.content.pm.PackageManager;
import java.util.HashMap;
import java.util.Map;

/** Stub de Context para h8: prefs por archivo + PackageManager (identityOf lo usa). */
public class Context {
    public final Map<String, SharedPreferences> files = new HashMap<>();
    public static final int MODE_PRIVATE = 0;
    private final PackageManager pm = new PackageManager();

    public SharedPreferences getSharedPreferences(String name, int mode) {
        return files.computeIfAbsent(name, k -> new SharedPreferences());
    }
    public Context getApplicationContext() { return this; }
    public PackageManager getPackageManager() { return pm; }
    public String getPackageName() { return "com.izzy2lost.psx2"; }
}
