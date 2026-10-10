package android.net;

/** Stub minimo: el regidor solo necesita el ultimo segmento para la clave de calibracion. */
public class Uri {
    private final String whole;
    private Uri(String s) { this.whole = s; }
    public static Uri parse(String s) { return new Uri(s == null ? "" : s); }
    public static String decode(String s) { return s; }
    public String getLastPathSegment() {
        String s = whole;
        int q = s.indexOf('?');
        if (q >= 0) s = s.substring(0, q);
        int slash = Math.max(s.lastIndexOf('/'), s.lastIndexOf('\\'));
        return slash + 1 < s.length() ? s.substring(slash + 1) : s;
    }
    public String toString() { return whole; }
}
