package android.util;

/** Log de Android volcado a stdout en el arnés. */
public class Log {
    public static int i(String tag, String msg) { System.out.println("[I/" + tag + "] " + msg); return 0; }
    public static int e(String tag, String msg) { System.out.println("[E/" + tag + "] " + msg); return 0; }
    public static int e(String tag, String msg, Throwable t) { System.out.println("[E/" + tag + "] " + msg + " :: " + t); return 0; }
    public static int d(String tag, String msg) { System.out.println("[D/" + tag + "] " + msg); return 0; }
    public static int w(String tag, String msg) { System.out.println("[W/" + tag + "] " + msg); return 0; }
}
