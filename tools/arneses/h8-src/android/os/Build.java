package android.os;

public class Build {
    public static class VERSION { public static int SDK_INT = 31; }
    // A diferencia de h6, aqui SOC_MODEL SI existe (es el camino primario de
    // TunerProfileStore.socClass). Los casos que quieren la caida a
    // MANUFACTURER/MODEL lo ponen a null: la reflexion lo ve y lo salta.
    public static String SOC_MODEL = "Qualcomm Snapdragon 778G";
    public static String MANUFACTURER = "HUAWEI";
    public static String MODEL = "NCO-LX3";
}
