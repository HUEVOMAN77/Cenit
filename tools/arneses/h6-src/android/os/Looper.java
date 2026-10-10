package android.os;

/** Looper de juguete: main == actual, para que reset() corra en linea. */
public class Looper {
    private static final Looper MAIN = new Looper();
    public static Looper getMainLooper() { return MAIN; }
    public static Looper myLooper() { return MAIN; }
}
