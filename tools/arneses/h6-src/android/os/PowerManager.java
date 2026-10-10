package android.os;

import java.util.ArrayList;
import java.util.List;

/** Stub con la escucha termica real, para que la reflexion del regidor la encuentre. */
public class PowerManager {
    public interface OnThermalStatusChangedListener { void onThermalStatusChanged(int status); }
    public static int status = -1;
    public static final List<Object> listeners = new ArrayList<>();
    public void addThermalStatusChangedListener(OnThermalStatusChangedListener l) { listeners.add(l); }
    public void removeThermalStatusChangedListener(OnThermalStatusChangedListener l) { listeners.remove(l); }
    public int getCurrentThermalStatus() { return status; }
}
