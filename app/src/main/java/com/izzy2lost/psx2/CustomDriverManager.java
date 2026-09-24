package com.izzy2lost.psx2;

import android.content.Context;
import android.net.Uri;
import android.util.Log;

import androidx.annotation.Nullable;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Custom Vulkan driver management for Android. Lets a user replace the stock
 * OEM Vulkan ICD with a Mesa Turnip (or similar) build for the GS Vulkan
 * renderer, via libadrenotools (see VKLoader.cpp).
 *
 * Drivers are imported from a user-picked .zip (adrenotools driver-pack
 * schema: meta.json + a .so at the zip root -- the same layout Yuzu /
 * Strato / Vita3K driver packs use) and extracted under
 * {@code <filesDir>/drivers/<id>/}. No network fetching here by design --
 * the user brings their own zip (e.g. downloaded from
 * github.com/K11MCH1/AdrenoToolsDrivers).
 */
public final class CustomDriverManager {
    private CustomDriverManager() {}

    private static final String TAG = "CustomDriverManager";

    /** Sane default for the driver's library soname when meta.json doesn't
     *  include one -- every Turnip release we care about uses this name,
     *  but the field is technically optional in the schema. */
    private static final String DEFAULT_LIBRARY_NAME = "libvulkan_freedreno.so";

    /** A driver extracted under {@code <filesDir>/drivers/<id>/}. */
    public static final class InstalledDriver {
        public final String id;
        public final String name;
        public final String description;
        public final String author;
        public final String vendor;
        public final String version;
        public final String libraryName;
        public final File driverDir;

        InstalledDriver(String id, String name, String description, String author,
                         String vendor, String version, String libraryName, File driverDir) {
            this.id = id;
            this.name = name;
            this.description = description;
            this.author = author;
            this.vendor = vendor;
            this.version = version;
            this.libraryName = libraryName;
            this.driverDir = driverDir;
        }

        /** Subdirectory the driver may write its shader cache into. The
         *  redirect hook captures the driver's file IO to this prefix so
         *  it doesn't try to write under /system/. */
        public File redirectDir() {
            return new File(driverDir, "cache");
        }
    }

    // ---- Installed drivers --------------------------------------------------

    /** Drivers MUST live under internal app-private storage (filesDir,
     *  resolves to /data/user/0/<pkg>/files/). getExternalFilesDir returns
     *  the sdcard-style /storage/emulated/0/... mount which is hardened
     *  against dlopen -- dlopen reports "Permission denied" trying to map
     *  shared-object segments from there. The adrenotools driver.h header
     *  explicitly calls this out: the custom driver dir "MUST NOT be on
     *  sdcard/storage". */
    private static File driversRoot(Context context) {
        File root = new File(context.getFilesDir(), "drivers");
        root.mkdirs();
        return root;
    }

    /** Enumerate installed drivers. Skips dirs that don't have a meta.json
     *  or whose .so file is missing -- those are mid-install or corrupted
     *  and the user can re-import. */
    public static List<InstalledDriver> listInstalled(Context context) {
        List<InstalledDriver> out = new ArrayList<>();
        File[] dirs = driversRoot(context).listFiles(File::isDirectory);
        if (dirs == null)
            return out;
        for (File dir : dirs) {
            File metaFile = new File(dir, "meta.json");
            String text = readTextQuietly(metaFile);
            if (text == null)
                continue;
            JSONObject json;
            try {
                json = new JSONObject(text);
            } catch (Exception e) {
                continue;
            }
            String libName = json.optString("libraryName", "");
            if (libName.isEmpty())
                libName = DEFAULT_LIBRARY_NAME;
            if (!new File(dir, libName).exists())
                continue;
            String name = json.optString("name", "");
            if (name.isEmpty())
                name = dir.getName();
            String version = json.optString("driverVersion", "");
            if (version.isEmpty())
                version = json.optString("packageVersion", "");
            out.add(new InstalledDriver(dir.getName(), name, json.optString("description", ""),
                    json.optString("author", ""), json.optString("vendor", ""), version, libName, dir));
        }
        Collections.sort(out, Comparator.comparing(d -> d.name.toLowerCase()));
        return out;
    }

    /** Recursively remove an installed driver. */
    public static void delete(InstalledDriver installed) {
        deleteRecursive(installed.driverDir);
    }

    // ---- Local import ---------------------------------------------------

    /** Install a driver from a user-picked local .zip URI (SAF
     *  OpenDocument). Synchronous IO -- call off the main thread. Returns
     *  the InstalledDriver on success, null on any extract/validation
     *  failure. The id is derived from the URI's last segment so
     *  re-importing the same file is idempotent. */
    public static InstalledDriver installFromUri(Context context, Uri uri) {
        String filename = lastPathSegment(uri);
        String id = makeId(filename != null ? filename : "imported_driver.zip");
        InputStream stream;
        try {
            stream = context.getContentResolver().openInputStream(uri);
        } catch (Exception e) {
            stream = null;
        }
        if (stream == null) {
            Log.w(TAG, "installFromUri: couldn't open " + uri);
            return null;
        }
        try {
            return installFromStream(context, id, stream);
        } finally {
            try {
                stream.close();
            } catch (Exception ignored) {
            }
        }
    }

    /** Extract+commit path for installFromUri. Reads the whole zip into a
     *  tmp dir, validates meta.json + library .so (synthesizing a minimal
     *  meta.json for packs that ship a bare .so with none), then renames
     *  into place under drivers/<id>/. */
    private static InstalledDriver installFromStream(Context context, String id, InputStream stream) {
        File targetDir = new File(driversRoot(context), id);
        File tmpDir = new File(driversRoot(context), id + ".tmp");
        if (tmpDir.exists())
            deleteRecursive(tmpDir);
        tmpDir.mkdirs();

        try (ZipInputStream zin = new ZipInputStream(stream)) {
            ZipEntry entry;
            byte[] buf = new byte[64 * 1024];
            while ((entry = zin.getNextEntry()) != null) {
                String name = entry.getName();
                if (name.contains("..") || name.startsWith("/")) {
                    Log.w(TAG, "install: skipping suspicious entry " + name);
                    continue;
                }
                // K11MCH1-style packs put files at the zip root. Flatten to root
                // for our adrenotools driverDir contract (driverName resolves
                // directly inside driverDir) in case a zip ever nests.
                String outName = name.substring(name.lastIndexOf('/') + 1);
                if (outName.isEmpty() || entry.isDirectory())
                    continue;
                File outFile = new File(tmpDir, outName);
                try (FileOutputStream fos = new FileOutputStream(outFile)) {
                    int n;
                    while ((n = zin.read(buf)) > 0)
                        fos.write(buf, 0, n);
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "install: extract failed: " + e.getMessage());
            deleteRecursive(tmpDir);
            return null;
        }

        // Some driver packs ship a bare libvulkan_*.so with no meta.json.
        // Synthesize a minimal manifest so those still install.
        File meta = new File(tmpDir, "meta.json");
        if (!meta.exists()) {
            File soFile = findSoFile(tmpDir);
            if (soFile != null) {
                String synthLib = new File(tmpDir, DEFAULT_LIBRARY_NAME).exists()
                        ? DEFAULT_LIBRARY_NAME : soFile.getName();
                try {
                    JSONObject synth = new JSONObject();
                    synth.put("schemaVersion", 1);
                    synth.put("name", id);
                    synth.put("description", "Custom Vulkan driver (synthesized manifest)");
                    synth.put("author", "");
                    synth.put("vendor", "");
                    synth.put("driverVersion", "");
                    synth.put("minApi", 24);
                    synth.put("libraryName", synthLib);
                    writeText(meta, synth.toString());
                    Log.i(TAG, "install: synthesized meta.json (" + synthLib + ")");
                } catch (Exception ignored) {
                }
            }
        }
        if (!meta.exists()) {
            Log.w(TAG, "install: zip missing meta.json");
            deleteRecursive(tmpDir);
            return null;
        }
        String libName = DEFAULT_LIBRARY_NAME;
        String metaText = readTextQuietly(meta);
        try {
            if (metaText != null) {
                String opt = new JSONObject(metaText).optString("libraryName", "");
                if (!opt.isEmpty())
                    libName = opt;
            }
        } catch (Exception ignored) {
        }
        if (!new File(tmpDir, libName).exists()) {
            Log.w(TAG, "install: zip missing " + libName);
            deleteRecursive(tmpDir);
            return null;
        }

        if (targetDir.exists())
            deleteRecursive(targetDir);
        if (!tmpDir.renameTo(targetDir)) {
            Log.w(TAG, "install: rename " + tmpDir + " -> " + targetDir + " failed");
            deleteRecursive(tmpDir);
            return null;
        }
        new File(targetDir, "cache").mkdirs();

        for (InstalledDriver d : listInstalled(context)) {
            if (d.id.equals(id))
                return d;
        }
        return null;
    }

    // ---- Guardarraya de arranque (Cenit 0.6.8) ------------------------------
    //
    // Un driver Turnip que no va con CIERTO juego mata el proceso nativo durante
    // el arranque, y eso no es una excepción que Java pueda atrapar: el hilo de
    // emulación simplemente desaparece y la app vuelve al inicio sin decir nada.
    // Peor: la selección del driver queda guardada, así que cada intento futuro
    // reproduce el mismo cierre. El usuario no tiene forma de saber que fue el
    // driver, porque la app no muestra ningún error.
    //
    // Se corta el ciclo con una marca de "intento sin confirmar": antes de arrancar
    // se anota qué driver se va a usar con qué juego, y solo se borra cuando el
    // juego demuestra que está dando cuadros de verdad. Si la app arranca y
    // encuentra una marca sin confirmar, es que ese intento murió a medias: ese
    // par driver+juego se apunta como fallido y, de ahí en adelante, ese juego
    // arranca con el driver del sistema en vez de quedarse cerrado para siempre.
    // El driver sigue activo para el resto de los juegos.

    private static final String PREFS = "app_prefs";
    private static final String KEY_PENDING = "cdv_pending";
    private static final String KEY_FAILED = "cdv_failed";
    private static final String KEY_NOTICE = "cdv_notice";

    private static android.content.SharedPreferences prefs(Context c) {
        return c.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    /** Identificador del intento: driver + juego. Por juego, porque un driver
     *  puede ir perfecto en diez títulos y matar el onceavo. */
    private static String attemptKey(String driverId, String gameKey) {
        return driverId + "|" + gameKey;
    }

    /** true si ese par ya falló antes: no volver a usar el driver con él. */
    public static boolean isKnownBadCombo(Context context, String driverId, String gameKey) {
        if (driverId == null || gameKey == null || gameKey.isEmpty()) return false;
        String bad = prefs(context).getString(KEY_FAILED, "");
        return bad != null && bad.contains(";" + attemptKey(driverId, gameKey) + ";");
    }

    /** Marca el intento en curso. Llamar ANTES de arrancar el VM, con el driver
     *  que realmente se va a usar. */
    public static void beginAttempt(Context context, String driverId, String gameKey) {
        if (driverId == null || driverId.isEmpty() || gameKey == null || gameKey.isEmpty()) return;
        prefs(context).edit().putString(KEY_PENDING, attemptKey(driverId, gameKey)).apply();
    }

    /** true si hay un intento de arranque sin confirmar en este momento. */
    public static boolean hasPendingAttempt(Context context) {
        final String pending = prefs(context).getString(KEY_PENDING, null);
        return pending != null && !pending.isEmpty();
    }

    /** El juego ya da cuadros: el driver funcionó. Borra la marca. */
    public static void confirmBoot(Context context) {
        if (prefs(context).getString(KEY_PENDING, null) == null) return;
        prefs(context).edit().remove(KEY_PENDING).apply();
    }

    /** Descarta un intento en curso sin marcar nada (p. ej. el driver ya fue
     *  excluido para este juego, o no hay driver activo). */
    public static void clearAttempt(Context context) {
        if (prefs(context).getString(KEY_PENDING, null) == null) return;
        prefs(context).edit().remove(KEY_PENDING).apply();
    }

    /** Quita todos los fracasos atribuidos a un driver: el usuario volvió a
     *  elegirlo conscientemente en el diálogo y merece una oportunidad limpia. */
    public static void clearFailuresForDriver(Context context, String driverId) {
        if (driverId == null || driverId.isEmpty()) return;
        final android.content.SharedPreferences p = prefs(context);
        final String bad = p.getString(KEY_FAILED, "");
        if (bad == null || bad.isEmpty()) return;
        StringBuilder kept = new StringBuilder(";");
        for (String entry : bad.split(";")) {
            // Se CONSERVAN los fracasos de otros drivers; se descartan solo los
            // de este, que el usuario acaba de volver a elegir a conciencia.
            if (entry.isEmpty() || entry.startsWith(driverId + "|")) continue;
            kept.append(entry).append(';');
        }
        p.edit().putString(KEY_FAILED, kept.length() == 1 ? "" : kept.toString()).apply();
    }

    /** Aviso de una sola vez, preparado cuando el guardarraya devuelve al driver
     *  del sistema; lo consume MainActivity para explicárselo al usuario. */
    public static void setFallbackNotice(Context context, String message) {
        prefs(context).edit().putString(KEY_NOTICE, message).apply();
    }

    @Nullable
    public static String consumeFallbackNotice(Context context) {
        final android.content.SharedPreferences p = prefs(context);
        final String msg = p.getString(KEY_NOTICE, null);
        if (msg != null) p.edit().remove(KEY_NOTICE).apply();
        return msg;
    }

    /** Convierte el intento pendiente en fracaso atribuido a ese driver+juego. */
    public static void markPendingAttemptFailed(Context context) {
        final android.content.SharedPreferences p = prefs(context);
        final String pending = p.getString(KEY_PENDING, null);
        if (pending == null || pending.isEmpty()) return;
        p.edit().remove(KEY_PENDING).apply();
        String bad = p.getString(KEY_FAILED, "");
        if (bad == null) bad = "";
        if (!bad.contains(";" + pending + ";")) {
            p.edit().putString(KEY_FAILED, ";" + bad.replaceFirst("^;", "") + pending + ";").apply();
            Log.i(TAG, "driver attempt " + pending + " failed -> marked as bad");
        }
    }

    /**
     * Se llama al arrancar la app, antes de aplicar la selección de driver.
     * Un cierre del código nativo se lleva el proceso por delante y no deja
     * excepción que Java atrape: si al volver hay una marca sin confirmar, ese
     * intento murió a medias y el par se registra como fallido. Devuelve la clave
     * del par fallido, o null si no había nada pendiente.
     */
    public static String harvestUnconfirmedAttempt(Context context) {
        final android.content.SharedPreferences p = prefs(context);
        final String pending = p.getString(KEY_PENDING, null);
        if (pending == null || pending.isEmpty()) return null;
        markPendingAttemptFailed(context);
        return pending;
    }

    // ---- Native bridge ------------------------------------------------------

    /** Push the active selection to native. Pass null to revert to the
     *  system loader. The native side reads these on the next
     *  Vulkan::LoadVulkanLibrary call (first MTGS::Open), so this must be
     *  called BEFORE runVMThread. */
    public static void applyToNative(Context context, InstalledDriver installed) {
        if (installed == null) {
            NativeApp.setCustomVulkanDriver("", "", "", "");
            return;
        }        // adrenotools' path resolution wants the driver dir to end with a
        // slash. The redirect dir doesn't strictly require it but we pass
        // it the same way for consistency.
        String driverDirPath = installed.driverDir.getAbsolutePath() + "/";
        File redirect = installed.redirectDir();
        redirect.mkdirs();
        String redirectDirPath = redirect.getAbsolutePath() + "/";
        String hookLibDir = context.getApplicationInfo().nativeLibraryDir;
        NativeApp.setCustomVulkanDriver(driverDirPath, installed.libraryName, redirectDirPath, hookLibDir);
    }

    // ---- helpers --------------------------------------------------------

    /** Stable id from the imported filename. Strips ".zip" + non-filename
     *  chars so the driver dir name is dlopen-safe (paths get fed straight
     *  to adrenotools, which feeds them to dlopen). */
    private static String makeId(String assetName) {
        String base = assetName;
        if (base.toLowerCase().endsWith(".zip"))
            base = base.substring(0, base.length() - 4);
        return base.replaceAll("[^A-Za-z0-9._-]", "_").toLowerCase();
    }

    private static String lastPathSegment(Uri uri) {
        String seg = uri.getLastPathSegment();
        if (seg == null)
            return null;
        int slash = seg.lastIndexOf('/');
        if (slash >= 0)
            seg = seg.substring(slash + 1);
        int colon = seg.lastIndexOf(':');
        if (colon >= 0)
            seg = seg.substring(colon + 1);
        return seg;
    }

    private static File findSoFile(File dir) {
        File[] files = dir.listFiles((d, name) -> name.endsWith(".so"));
        return (files != null && files.length > 0) ? files[0] : null;
    }

    private static String readTextQuietly(File f) {
        if (!f.exists())
            return null;
        try (FileInputStream fis = new FileInputStream(f)) {
            ByteArrayOutputStream bos = new ByteArrayOutputStream();
            byte[] buf = new byte[8192];
            int n;
            while ((n = fis.read(buf)) > 0)
                bos.write(buf, 0, n);
            return bos.toString(StandardCharsets.UTF_8.name());
        } catch (Exception e) {
            return null;
        }
    }

    private static void writeText(File f, String text) throws Exception {
        try (FileOutputStream fos = new FileOutputStream(f)) {
            fos.write(text.getBytes(StandardCharsets.UTF_8));
        }
    }

    private static void deleteRecursive(File f) {
        if (f == null || !f.exists())
            return;
        File[] children = f.listFiles();
        if (children != null) {
            for (File c : children)
                deleteRecursive(c);
        }
        f.delete();
    }
}
