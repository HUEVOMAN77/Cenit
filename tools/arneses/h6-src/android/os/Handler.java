package android.os;

import java.util.ArrayDeque;
import java.util.Deque;

/**
 * Handler de juguete: ignora el retardo y deja la cola a mano del arnés, que la
 * vacia tick a tick. Asi se puede simular "el regidor llevo N segundos corriendo"
 * sin dormir el proceso. removeCallbacks se respeta de verdad porque el regidor lo
 * usa al recibir un aviso termico.
 */
public class Handler {
    public static final Deque<Runnable> queue = new ArrayDeque<>();

    public Handler(Looper looper) { }

    public boolean postDelayed(Runnable r, long delayMillis) { queue.add(r); return true; }
    public boolean post(Runnable r) { queue.add(r); return true; }
    public void removeCallbacks(Runnable r) { queue.remove(r); }

    /** El arnés: corre un tick pendiente, o false si no hay. */
    public static boolean runOne() {
        final Runnable r = queue.poll();
        if (r == null) return false;
        r.run();
        return true;
    }
    public static void run(int n) { for (int i = 0; i < n; i++) if (!runOne()) break; }
}
