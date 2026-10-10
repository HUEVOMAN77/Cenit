package android.content.pm;

/**
 * Stub de PackageManager. throwOnQuery ejercita la caida de
 * TunerProfileStore.identityOf ("core = ?" -> identidad incompleta).
 */
public class PackageManager {
    public static String versionName = "0.6.31";
    public static boolean throwOnQuery = false;

    public PackageInfo getPackageInfo(String pkg, int flags) {
        if (throwOnQuery) throw new RuntimeException("stub: sin paquete");
        return new PackageInfo();
    }
}
