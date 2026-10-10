package android.os;
public class Build {
    public static class VERSION { public static int SDK_INT = 31; }
    // Campos de identidad: MANUFACTURER/MODEL existen desde API 1; SOC_MODEL es API
    // 31 y aqui se omite a proposito, para que el arnes ejercite la caida a
    // MANUFACTURER/MODEL igual que un telefono viejo.
    public static String MANUFACTURER = "HUAWEI";
    public static String MODEL = "NCO-LX3";
}
