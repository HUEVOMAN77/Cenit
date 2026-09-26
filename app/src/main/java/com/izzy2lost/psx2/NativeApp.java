package com.izzy2lost.psx2;

import android.content.ContentResolver;
import android.content.Context;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.view.Surface;
import java.io.File;
import java.lang.ref.WeakReference;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicInteger;

public class NativeApp {
	static {
		try {
			System.loadLibrary("emucore");
			hasNoNativeBinary = false;
		} catch (UnsatisfiedLinkError e) {
			hasNoNativeBinary = true;
		}
	}

	public static boolean hasNoNativeBinary;

    private static final ExecutorService NATIVE_SETTINGS_EXECUTOR =
            Executors.newSingleThreadExecutor(r -> {
                Thread thread = new Thread(r, "NativeSettings");
                thread.setDaemon(true);
                return thread;
            });
    private static final AtomicInteger ASPECT_RATIO_REQUEST = new AtomicInteger();

    public static void runNativeSettingAsync(String name, Runnable task) {
        if (hasNoNativeBinary) {
            return;
        }

        NATIVE_SETTINGS_EXECUTOR.execute(() -> {
            try {
                task.run();
            } catch (Throwable t) {
                android.util.Log.e("NativeApp", name + " failed", t);
            }
        });
    }

	protected static WeakReference<Context> mContext;
	public static Context getContext() {
		return mContext != null ? mContext.get() : null;
	}

	public static void initializeOnce(Context context) {
		mContext = new WeakReference<>(context);
		File externalFilesDir = context.getExternalFilesDir(null);
		if (externalFilesDir == null) {
			externalFilesDir = context.getDataDir();
		}
		initialize(externalFilesDir.getAbsolutePath(), android.os.Build.VERSION.SDK_INT);
	}

    public static native void initialize(String path, int apiVer);
    public static native String getGameTitle(String path);
    public static native String getGameTitleFromUri(String gameUri);
	public static native String getGameSerial();
	public static native float getFPS();
	/** Cuadros por segundo reales, o 0 si el binario no está o la consulta falla.
	 *  0 también es el valor ANTES del primer cuadro presentado, así que un valor
	 *  positivo prueba que el juego está dibujando de verdad. */
	public static float safeGetFPS() {
		if (hasNoNativeBinary) return 0f;
		try { return getFPS(); } catch (Throwable t) { return 0f; }
	}

	public static native String getPauseGameTitle();
	public static native String getPauseGameSerial();

	public static native void setPadVibration(boolean isonoff);
	public static native void setPadButton(int index, int range, boolean iskeypressed);
	public static native boolean updateTouchscreenPointer(float x, float y, boolean pressed);
	public static native void resetKeyStatus();

	public static native void setAspectRatio(int type);
    public static void setAspectRatioAsync(int type) {
        final int request = ASPECT_RATIO_REQUEST.incrementAndGet();
        runNativeSettingAsync("setAspectRatio", () -> {
            if (request == ASPECT_RATIO_REQUEST.get()) {
                setAspectRatio(type);
            }
        });
    }
	public static native void speedhackLimitermode(int value);
	// Cenit 0.6.5: turbo de cargas del regidor. 0=Nominal 1=Turbo 2=Slomo
	// 3=Unlimited. Asíncrono como los demás setters: se invoca desde el hilo
	// principal del governor.
	public static void setLimiterModeAsync(int value) {
		runNativeSettingAsync("speedhackLimitermode", () -> speedhackLimitermode(value));
	}
	public static native void speedhackEecyclerate(int value);
	public static native void speedhackEecycleskip(int value);

	// Cenit 0.6.3: estos dos hacían falta de verdad. Antes eran stubs vacíos.
	public static void setEECycleRateAsync(int value) {
		runNativeSettingAsync("speedhackEecyclerate", () -> speedhackEecyclerate(value));
	}
	// Velocidad real de emulación en % (100 = a tiempo). Barata, lectura pura.
	public static native float getEmulationSpeed();
	public static float safeGetEmulationSpeed() {
		if (hasNoNativeBinary) return 100f;
		try { return getEmulationSpeed(); } catch (Throwable t) { return 100f; }
	}
	// Resolución interna realmente en uso (la capa por juego manda sobre el INI
	// global; el regidor la lee para no insistir sobre una escala que no puede tocar).
	public static native float getEffectiveUpscale();
	public static float safeGetEffectiveUpscale() {
		if (hasNoNativeBinary) return 0f;
		try { return getEffectiveUpscale(); } catch (Throwable t) { return 0f; }
	}
	// Cenit 0.6.12: Ciclo EE (EECycleRate) REALMENTE en uso, capa por-juego
	// incluida. 0 con la consola apagada. El regidor de resolución ya no se
	// congela mirando solo la preferencia global: un -1 guardado en el INI del
	// juego congela ahora igual que lo hacía el global, y un 0 por-juego que
	// anule un global viejo deja de tener al regidor dormido para siempre.
	public static native int getEffectiveEECycleRate();
	public static int safeGetEffectiveEECycleRate() {
		if (hasNoNativeBinary) return 0;
		try { return getEffectiveEECycleRate(); } catch (Throwable t) { return 0; }
	}
	// Cenit 0.6.4 (regidor v2): uso de GPU como fracción (1.0 = GPU justo a
	// tiempo) y milisegundos medios de GPU por cuadro. Con esto el regidor
	// distingue un bache de GPU (bajar resolución ayuda) de uno de CPU
	// emulada (bajar resolución solo empeora la imagen gratis).
	public static native float getGPUUsage();
	public static float safeGetGPUUsage() {
		if (hasNoNativeBinary) return 0f;
		try { return getGPUUsage(); } catch (Throwable t) { return 0f; }
	}
	public static native float getGPUAverageTime();
	public static float safeGetGPUAverageTime() {
		if (hasNoNativeBinary) return 0f;
		try { return getGPUAverageTime(); } catch (Throwable t) { return 0f; }
	}

	public static native void renderUpscalemultiplier(float value);
    public static void renderUpscalemultiplierAsync(float value) {
        runNativeSettingAsync("renderUpscalemultiplier", () -> renderUpscalemultiplier(value));
    }
	public static native void renderMipmap(int value);
	public static native void renderHalfpixeloffset(int value);
	public static native void renderGpu(int value);
    public static void renderGpuAsync(int value) {
        runNativeSettingAsync("renderGpu", () -> renderGpu(value));
    }
	public static native void renderPreloading(int value);

	// HUD/OSD visibility toggle
	public static native void setHudVisible(boolean visible);
    public static void setHudVisibleAsync(boolean visible) {
        runNativeSettingAsync("setHudVisible", () -> setHudVisible(visible));
    }
	
	// Widescreen and interlacing patches
	    public static native void setWidescreenPatches(boolean enabled);
    public static void setWidescreenPatchesAsync(boolean enabled) {
        runNativeSettingAsync("setWidescreenPatches", () -> setWidescreenPatches(enabled));
    }
    public static native void setNoInterlacingPatches(boolean enabled);
    public static void setNoInterlacingPatchesAsync(boolean enabled) {
        runNativeSettingAsync("setNoInterlacingPatches", () -> setNoInterlacingPatches(enabled));
    }
    
    // Texture loading options for texture packs
    public static native void setLoadTextures(boolean enabled);
    public static void setLoadTexturesAsync(boolean enabled) {
        runNativeSettingAsync("setLoadTextures", () -> setLoadTextures(enabled));
    }
    public static native void reloadTextureReplacements();
    public static void reloadTextureReplacementsAsync() {
        runNativeSettingAsync("reloadTextureReplacements",
                NativeApp::reloadTextureReplacements);
    }
    public static native void setAsyncTextureLoading(boolean enabled);
    public static void setAsyncTextureLoadingAsync(boolean enabled) {
        runNativeSettingAsync("setAsyncTextureLoading", () -> setAsyncTextureLoading(enabled));
    }
    public static native void setPrecacheTextureReplacements(boolean enabled);
    public static void setPrecacheTextureReplacementsAsync(boolean enabled) {
        runNativeSettingAsync("setPrecacheTextureReplacements", () -> setPrecacheTextureReplacements(enabled));
    }
    public static native void setBlendingAccuracy(int level);
    public static void setBlendingAccuracyAsync(int level) {
        runNativeSettingAsync("setBlendingAccuracy", () -> setBlendingAccuracy(level));
    }

    // --- Cenit: opciones GS adicionales (aplicación en caliente) ---
    public static native void setTextureFiltering(int mode);
    public static void setTextureFilteringAsync(int mode) {
        runNativeSettingAsync("setTextureFiltering", () -> setTextureFiltering(mode));
    }
    public static native void setHWMipmap(boolean enabled);
    public static void setHWMipmapAsync(boolean enabled) {
        runNativeSettingAsync("setHWMipmap", () -> setHWMipmap(enabled));
    }
    public static native void setMaxAnisotropy(int level);
    public static void setMaxAnisotropyAsync(int level) {
        runNativeSettingAsync("setMaxAnisotropy", () -> setMaxAnisotropy(level));
    }
    public static native void setCASMode(int mode, int sharpness);
    public static void setCASModeAsync(int mode, int sharpness) {
        runNativeSettingAsync("setCASMode", () -> setCASMode(mode, sharpness));
    }
    // Cenit 0.6.4: el desplazado de medio píxel global era una función fantasma
    // (MaskUserHacks lo borraba en cada ApplySettings). El control real vive en
    // setGameUserHackInt/getGameUserHackInt, por juego.
    public static native void setVsyncEnabled(boolean enabled);
    public static void setVsyncEnabledAsync(boolean enabled) {
        runNativeSettingAsync("setVsyncEnabled", () -> setVsyncEnabled(enabled));
    }

    // --- Cenit 0.6.6: los tres mandos nuevos (bloque 1, 2 y 3) --------------
    // Fijado de hilos al núcleo rápido. El core ya hace el reparto; esto solo da
    // el permiso. Default encendido: apagarlo solo tiene sentido para diagnosticar.
    public static native void setThreadPinning(boolean enabled);
    public static void setThreadPinningAsync(boolean enabled) {
        runNativeSettingAsync("setThreadPinning", () -> setThreadPinning(enabled));
    }
    // Cola de cuadros (0 = ritmo óptimo con menos input lag; 1..n = más
    // amortiguación). Por defecto depende de la gama: 0 si el teléfono sobra.
    public static native void setFrameLatencyQueue(int frames);
    public static void setFrameLatencyQueueAsync(int frames) {
        runNativeSettingAsync("setFrameLatencyQueue", () -> setFrameLatencyQueue(frames));
    }
    // Pre-carga de texturas: 0 apagada, 1 parcial (gama baja), 2 completa.
    public static native void setTexturePreloading(int level);
    public static void setTexturePreloadingAsync(int level) {
        runNativeSettingAsync("setTexturePreloading", () -> setTexturePreloading(level));
    }
    // El default de las dos anteriores sigue al hardware detectado por el núcleo.
    // Gama baja (tier 0): cola=2 y pre-carga parcial. Gama media/alta: cola=0
    // (ritmo óptimo) y pre-carga completa (2). Se escribe como clave por si el
    // usuario la cambia en Ajustes; si no, el default vive aquí.
    public static int defaultFrameLatencyQueue() {
        return safeGetDevicePerformanceTier() == 0 ? 2 : 0;
    }
    public static int defaultTexturePreloading() {
        return safeGetDevicePerformanceTier() == 0 ? 1 : 2;
    }
    // --- Cenit 0.6.7: los dos speedhacks que el perfil dejaba sin interruptor --
    // Fast CDVD quita la latencia de lectura del DVD. En un teléfono el disco es
    // un archivo en memoria flash, así que no hay búsqueda real que ahorrar:
    // casi no gana nada y sí rompe juegos que leen sincronizado (Shadow of the
    // Colossus muere al arrancar con esto encendido). Default APAGADO.
    public static native void setFastCDVD(boolean enabled);
    public static void setFastCDVDAsync(boolean enabled) {
        runNativeSettingAsync("setFastCDVD", () -> setFastCDVD(enabled));
    }
    // MTVU pasa el VU1 a un hilo propio. Es una ganancia grande en 3+ núcleos,
    // pero el propio motor avisa que "algunos juegos son incompatibles y pueden
    // colgarse". Default encendido solo si hay 3+ núcleos, igual que el perfil.
    public static native void setMTVU(boolean enabled);
    public static void setMTVUAsync(boolean enabled) {
        runNativeSettingAsync("setMTVU", () -> setMTVU(enabled));
    }
    public static boolean defaultMTVU() {
        // Misma pregunta que hace el perfil en C++ (std::thread::hardware_concurrency()
        // >= 3), respondida por el mismo sitio: si Java contara los núcleos por su
        // cuenta, un desacuerdo escribiría un default distinto al del núcleo.
        if (hasNoNativeBinary) return false;
        try { return coresAllowMTVU(); } catch (Throwable t) { return false; }
    }
    public static native boolean coresAllowMTVU();
    // --- Cenit 0.6.15: Fase 1 del motor de superbloques VU (sonda de trazas) --
    // Medición pura: entradas al dispatcher, ejecuciones de bloques VU1 y
    // secuencias repetidas. Apagada por defecto; encenderla pausa la caché de
    // programas VU en disco (nada instrumentado toca el disco) y vuelve a
    // compilar todo instrumentado. El informe sale al apagarla, al parar el
    // juego, o a petición (dumpVUTraceReport): logs/vu_probe.txt + resumen en
    // emulog.txt. Solo existe en el JIT arm64; en otros SO la llamada es no-op.
    public static native void setVUTraceProbe(boolean enabled);
    public static void setVUTraceProbeAsync(boolean enabled) {
        runNativeSettingAsync("setVUTraceProbe", () -> setVUTraceProbe(enabled));
    }
    public static native boolean getVUTraceProbeEnabled();
    public static native boolean getVUTraceProbeEffective();
    public static native void dumpVUTraceReport(String reason);
    /** True si la sonda está midiendo ahora mismo (el HUD la pinta). */
    public static boolean safeVUTraceProbeEffective() {
        if (hasNoNativeBinary) return false;
        try { return getVUTraceProbeEffective(); } catch (Throwable t) { return false; }
    }
    // --- Cenit 0.6.21: Fases 2-5 del motor de superbloques VU ---------------
    // Fusiona en un solo bloque los bloques VU1 calientes que caen
    // incondicionalmente uno sobre otro, y los valida por replay diferencial
    // (parejas normal/superbloque con entrada idéntica: MATCH cuenta,
    // DIVERGENCE invalida la variante y, a las 3, apaga el motor por sesión).
    // EXPERIMENTAL y APAGADA por defecto: el GATE del documento de arquitectura
    // exige cero divergencias y mejora sostenida medida en el dispositivo antes
    // de habilitarla. Encenderla enciende la sonda y pausa la caché de programas
    // VU en disco; el toggle recompila todo (invalida las cachés del
    // recompiler). Informe: logs/vu_superblock.txt + resumen en emulog.txt.
    // Solo existe en el JIT arm64; en otros SO la llamada es no-op.
    public static native void setVUSuperblock(boolean enabled);
    public static void setVUSuperblockAsync(boolean enabled) {
        runNativeSettingAsync("setVUSuperblock", () -> setVUSuperblock(enabled));
    }
    public static native boolean getVUSuperblockEnabled();
    public static native boolean getVUSuperblockEffective();
    public static native void dumpVUSuperblockReport(String reason);
    /** True si el motor está validando/actuando ahora mismo (el HUD lo pinta). */
    public static boolean safeVUSuperblockEffective() {
        if (hasNoNativeBinary) return false;
        try { return getVUSuperblockEffective(); } catch (Throwable t) { return false; }
    }
    public static native String getLogDirectory();
    /** Carpeta de logs según el núcleo, o null si el binario nativo no está. */
    public static String safeGetLogDirectory() {
        if (hasNoNativeBinary) return null;
        try {
            String d = getLogDirectory();
            return (d == null || d.isEmpty()) ? null : d;
        } catch (Throwable t) { return null; }
    }
    public static int safeGetDevicePerformanceTier() {
        if (hasNoNativeBinary) return 0;
        try { return getDevicePerformanceTier(); } catch (Throwable t) { return 0; }
    }
    
    // Audio output device (0 = follow system routing)
    public static native void setAudioOutputDevice(int deviceId);
    public static void setAudioOutputDeviceAsync(int deviceId) {
        runNativeSettingAsync("setAudioOutputDevice", () -> setAudioOutputDevice(deviceId));
    }

    // Edge cropping (hides junk pixels at the left/right screen edges)
    public static native void setEdgeCrop(int pixels);
    public static void setEdgeCropAsync(int pixels) {
        runNativeSettingAsync("setEdgeCrop", () -> setEdgeCrop(pixels));
    }

    // Shade Boost (brightness/contrast/saturation)
    public static native void setShadeBoost(boolean enabled);
    public static void setShadeBoostAsync(boolean enabled) {
        runNativeSettingAsync("setShadeBoost", () -> setShadeBoost(enabled));
    }
    public static native void setShadeBoostBrightness(int brightness);
    public static void setShadeBoostBrightnessAsync(int brightness) {
        runNativeSettingAsync("setShadeBoostBrightness", () -> setShadeBoostBrightness(brightness));
    }
    public static native void setShadeBoostContrast(int contrast);
    public static void setShadeBoostContrastAsync(int contrast) {
        runNativeSettingAsync("setShadeBoostContrast", () -> setShadeBoostContrast(contrast));
    }
    public static native void setShadeBoostSaturation(int saturation);
    public static void setShadeBoostSaturationAsync(int saturation) {
        runNativeSettingAsync("setShadeBoostSaturation", () -> setShadeBoostSaturation(saturation));
    }

    // Apply multiple settings in one atomic batch (safer live updates)
    public static native void applyGlobalSettingsBatch(int renderer,
                                                       float upscaleMultiplier,
                                                       int aspectRatio,
                                                       int blendingAccuracy,
                                                       boolean widescreenPatches,
                                                       boolean noInterlacingPatches,
                                                       boolean loadTextures,
                                                       boolean asyncTextureLoading,
                                                       boolean vsyncEnabled,
                                                       boolean hudVisible);
    public static void applyGlobalSettingsBatchAsync(int renderer,
                                                     float upscaleMultiplier,
                                                     int aspectRatio,
                                                     int blendingAccuracy,
                                                     boolean widescreenPatches,
                                                     boolean noInterlacingPatches,
                                                     boolean loadTextures,
                                                     boolean asyncTextureLoading,
                                                     boolean vsyncEnabled,
                                                     boolean hudVisible) {
        runNativeSettingAsync("applyGlobalSettingsBatch", () ->
                applyGlobalSettingsBatch(renderer, upscaleMultiplier, aspectRatio, blendingAccuracy,
                        widescreenPatches, noInterlacingPatches, loadTextures, asyncTextureLoading,
                        vsyncEnabled, hudVisible));
    }
    
    // Apply per-game settings (subset) in one batch
    public static native void applyPerGameSettingsBatch(int renderer,
                                                        float upscaleMultiplier,
                                                        int blendingAccuracy,
                                                        boolean widescreenPatches,
                                                        boolean noInterlacingPatches,
                                                        boolean enablePatches,
                                                        boolean enableCheats);
    public static void applyPerGameSettingsBatchAsync(int renderer,
                                                      float upscaleMultiplier,
                                                      int blendingAccuracy,
                                                      boolean widescreenPatches,
                                                      boolean noInterlacingPatches,
                                                      boolean enablePatches,
                                                      boolean enableCheats) {
        runNativeSettingAsync("applyPerGameSettingsBatch", () ->
                applyPerGameSettingsBatch(renderer, upscaleMultiplier, blendingAccuracy,
                        widescreenPatches, noInterlacingPatches, enablePatches, enableCheats));
    }

    // Query current runtime renderer from the core (reflects global/per-game)
    public static native int getCurrentRenderer();

    // Per-game settings
    public static native void saveGameSettings(String filename, int blendingAccuracy, int renderer, 
                                              int resolution, boolean widescreenPatches, 
                                              boolean noInterlacingPatches, boolean enablePatches, 
                                              boolean enableCheats);
    public static native void saveGameSettingsToPath(String fullPath, int blendingAccuracy, int renderer, 
                                                     int resolution, boolean widescreenPatches, 
                                                     boolean noInterlacingPatches, boolean enablePatches, 
                                                     boolean enableCheats);
    public static native void deleteGameSettings(String filename);
    public static native String getGameSerial(String gameUri);
    public static native String getGameCrc(String gameUri);
    public static native String getCurrentGameSerial();

    // Cenit 0.6.4 (plan del inge §1.1): hacks de hardware POR JUEGO. El INI
    // global no sirve: LoadCoreSettings() aplica MaskUserHacks() y borra todos
    // los UserHacks_* salvo UserHacks=true, y eso global apagaría los fixes
    // automáticos del GameDB. La capa gamesettings/<SERIAL>.ini sí manda, y el
    // nativo siembra allí los gsHWFixes de la base de datos antes de la primera
    // edición manual para que no se pierda nada (God of War II, por ejemplo).
    // Devuelve false si no se pudo resolver el juego; no lanza.
    public static native boolean setGameUserHackInt(String gameUri, String key, int value);
    // Valor sin definir en ese juego = p_fallback (la capa por juego puede no
    // existir todavía; el nativo cae al GameDB y de ahí al fallback).
    public static native int getGameUserHackInt(String gameUri, String key, int fallback);
    // Cenit 0.6.5: variante con sección explícita, para claves que NO son user
    // hacks (modo cuotas: EECycleSkip en EmuCore/Speedhacks). Si la clave resulta
    // ser un hack conocido, el nativo siembra igualmente antes de escribir.
    public static native boolean setGameSettingInt(String gameUri, String section, String key, int value);
    public static native int getGameSettingInt(String gameUri, String section, String key, int fallback);
    public static boolean safeSetGameSettingInt(String gameUri, String section, String key, int value) {
        if (hasNoNativeBinary || gameUri == null || gameUri.isEmpty()) return false;
        synchronized (CDVD_LOCK) {
            try { return setGameSettingInt(gameUri, section, key, value); } catch (Throwable t) { return false; }
        }
    }
    public static int safeGetGameSettingInt(String gameUri, String section, String key, int fallback) {
        if (hasNoNativeBinary || gameUri == null || gameUri.isEmpty()) return fallback;
        synchronized (CDVD_LOCK) {
            try { return getGameSettingInt(gameUri, section, key, fallback); } catch (Throwable t) { return fallback; }
        }
    }
    public static boolean safeSetGameUserHackInt(String gameUri, String key, int value) {
        if (hasNoNativeBinary || gameUri == null || gameUri.isEmpty()) return false;
        // Mismo candado que getGameCrcSafe: el nativo abre el ISO para el CRC.
        synchronized (CDVD_LOCK) {
            try { return setGameUserHackInt(gameUri, key, value); } catch (Throwable t) { return false; }
        }
    }
    public static int safeGetGameUserHackInt(String gameUri, String key, int fallback) {
        if (hasNoNativeBinary || gameUri == null || gameUri.isEmpty()) return fallback;
        synchronized (CDVD_LOCK) {
            try { return getGameUserHackInt(gameUri, key, fallback); } catch (Throwable t) { return fallback; }
        }
    }
    // Claves INI soportadas por el nativo (coincidentes con los nombres del núcleo).
    public static final String HACK_HALF_PIXEL_OFFSET = "UserHacks_HalfPixelOffset";
    
    // Synchronization object for CDVD operations to prevent crashes
    private static final Object CDVD_LOCK = new Object();
    
    // Synchronized wrapper for getGameSerial to prevent CDVD race conditions
    public static String getGameSerialSafe(String gameUri) {
        synchronized (CDVD_LOCK) {
            try {
                return getGameSerial(gameUri);
            } catch (Exception e) {
                return "";
            }
        }
    }
    
    // Synchronized wrapper for getGameTitleFromUri to prevent CDVD race conditions
    public static String getGameTitleFromUriSafe(String gameUri) {
        synchronized (CDVD_LOCK) {
            try {
                return getGameTitleFromUri(gameUri);
            } catch (Exception e) {
                return "";
            }
        }
    }
    
    // Synchronized wrapper for getGameCrc to prevent CDVD race conditions
    public static String getGameCrcSafe(String gameUri) {
        synchronized (CDVD_LOCK) {
            try {
                return getGameCrc(gameUri);
            } catch (Exception e) {
                return "";
            }
        }
    }

    // Get list of saves on a memory card
    // Returns array of strings in format "filename|size|isDirectory"
    public static native String[] getMemoryCardSaves(String memcardPath);
    public static native void setMemoryCardSlots(String slot1Filename, boolean slot1Enabled,
                                                 String slot2Filename, boolean slot2Enabled);

    // RetroAchievements native methods
    public static native boolean achievementsIsActive();
    public static native boolean achievementsIsHardcoreMode();
    public static native boolean achievementsHasActiveGame();
    public static native String achievementsGetGameTitle();
    public static native int achievementsGetGameId();
    public static native String achievementsGetRichPresence();
    public static native void achievementsLogin(String username, String password);
    public static native void achievementsLogout();
    public static native void achievementsInitialize();
    public static native void achievementsShutdown();
    public static native Achievement[] achievementsGetAchievementList();
    public static native void achievementsSetHardcoreMode(boolean enabled);
    public static native void achievementsLoginWithToken(String username, String token);

    // Save achievements credentials to SharedPreferences (called from native code)
    public static void saveAchievementsCredentials(String username, String token, String loginTimestamp) {
        Context context = getContext();
        if (context == null) {
            android.util.Log.e("Achievements", "Cannot save credentials: context is null");
            return;
        }
        
        android.content.SharedPreferences prefs = context.getSharedPreferences("RetroAchievements", Context.MODE_PRIVATE);
        android.content.SharedPreferences.Editor editor = prefs.edit();
        editor.putString("username", username);
        editor.putString("token", token);
        editor.putString("login_timestamp", loginTimestamp);
        editor.apply();
        
        android.util.Log.i("Achievements", "Credentials saved: username=" + username + ", has_token=" + (!token.isEmpty()));
    }

    // Load achievements credentials from SharedPreferences and attempt auto-login
    public static void loadAndLoginAchievements() {
        Context context = getContext();
        if (context == null) {
            android.util.Log.e("Achievements", "Cannot load credentials: context is null");
            return;
        }
        if (hasNoNativeBinary) {
            android.util.Log.w("Achievements", "Skipping auto-login: native core not loaded");
            return;
        }
        
        android.content.SharedPreferences prefs = context.getSharedPreferences("RetroAchievements", Context.MODE_PRIVATE);
        boolean enabled = prefs.getBoolean("enabled", false);
        
        if (!enabled) {
            android.util.Log.d("Achievements", "Achievements not enabled, skipping auto-login");
            return;
        }
        
        String username = prefs.getString("username", "");
        String token = prefs.getString("token", "");
        
        if (username.isEmpty() || token.isEmpty()) {
            android.util.Log.d("Achievements", "No saved credentials found");
            return;
        }
        
        android.util.Log.i("Achievements", "Attempting auto-login with saved token for user: " + username);
        
        // Initialize achievements system first
        new Thread(() -> {
            try {
                achievementsInitialize();
                Thread.sleep(500); // Give it time to initialize
                achievementsLoginWithToken(username, token);
                android.util.Log.i("Achievements", "Auto-login initiated");
            } catch (Throwable e) {
                android.util.Log.e("Achievements", "Auto-login failed", e);
                // Avoid startup crash loops by disabling auto-login until the user re-enables it manually
                prefs.edit().putBoolean("enabled", false).apply();
            }
        }).start();
    }

	public static native void onNativeSurfaceCreated();
	public static native void onNativeSurfaceChanged(Surface surface, int w, int h);
	public static native void onNativeSurfaceDestroyed();

    public static native boolean runVMThread(String path);
    public static native void prepareVMStart();
    // Custom Vulkan driver (e.g. Mesa Turnip). Pass four empty strings to revert
    // to the system loader. Must be called before runVMThread — see
    // Vulkan::SetCustomDriverPath in VKLoader.cpp for why.
    public static native void setCustomVulkanDriver(String driverDir, String driverName,
                                                     String redirectDir, String hookLibDir);

    // Ajuste fino del driver personalizado (Cenit 0.6.20): banderas TU_DEBUG, ruta del
    // cache de shaders en disco y applicationName que la instancia Vulkan declara para
    // que el driconf del driver enganche sus reglas por-juego. Cadena vacia = no aplicar.
    // Debe ir despues de setCustomVulkanDriver y antes de runVMThread.
    public static native void setCustomVulkanDriverTuning(String tuDebug, String shaderCacheDir,
                                                          String appName);
    public static native String getLastVMError();
    public static native void setVerifiedBiosFiles(String usaBios, String europeBios,
                                                   String japanBios, String arcadeBios);
    public static native boolean isVMActive();

    // 0 = gama baja (incluidos Mali y desconocidos), 1 = Snapdragon medio o
    // MediaTek/Exynos capaz, 2 = Snapdragon gama alta (778G+). Tabla curada en
    // AndroidDeviceDetection.cpp:GetDeviceTier(). Define el upscale de primera
    // ejecución (PerfProfile) y el perfil nativo de speedhacks.
    public static native int getDevicePerformanceTier();

	public static native void pause();
	public static native void resume();
	public static native boolean isPaused();
	public static native void setFastForward(boolean enabled);
	public static native void shutdown();

	public static native boolean saveStateToSlot(int slot);
	public static native boolean loadStateFromSlot(int slot);
	public static native String getGamePathSlot(int slot);
	public static native byte[] getImageSlot(int slot);

	// Call jni
    public static int openContentUri(String uriString) {
        Context _context = getContext();
        if(_context != null) {
            ContentResolver _contentResolver = _context.getContentResolver();
            try {
                ParcelFileDescriptor filePfd = _contentResolver.openFileDescriptor(Uri.parse(uriString), "r");
                if (filePfd != null) {
                    return filePfd.detachFd();  // Take ownership of the fd.
                }
            } catch (Exception ignored) {}
        }
        return -1;
    }

    // Indicates whether a SAF Data Root has been selected by the user.
    public static boolean hasSafDataRoot() {
        return SafManager.getDataRootUri(getContext()) != null;
    }

    // Open a SAF content Uri with the requested mode ("r", "w", or "rw"). Returns a detached FD or -1.
    public static int openContentUriMode(String uriString, String mode) {
        Context _context = getContext();
        if(_context != null) {
            ContentResolver _contentResolver = _context.getContentResolver();
            try {
                ParcelFileDescriptor filePfd = _contentResolver.openFileDescriptor(Uri.parse(uriString), mode);
                if (filePfd != null) {
                    return filePfd.detachFd();
                }
            } catch (Exception ignored) {}
        }
        return -1;
    }

    // Resolve a child document Uri within the SAF Data Root.
    // subdir: e.g., "gamesettings", filename: e.g., "SLUS-12345.ini". If create is true, creates file.
    public static String resolveSafChildUri(String subdir, String filename, boolean create) {
        Uri root = SafManager.getDataRootUri(getContext());
        if (root == null) return null;
        try {
            androidx.documentfile.provider.DocumentFile df;
            if (create) {
                df = SafManager.createChild(getContext(), new String[]{subdir}, filename, "application/octet-stream");
            } else {
                df = SafManager.getChild(getContext(), new String[]{subdir}, filename);
            }
            return (df != null) ? df.getUri().toString() : null;
        } catch (Throwable ignored) { }
        return null;
    }

    // Resolve a file path relative to the SAF Data Root. Accepts nested paths like
    // "textures/SLUS-12345/replacements/subdir/file.png". If create is true, creates the file.
    public static String resolveSafPathUri(String relativePath, boolean create) {
        if (relativePath == null) return null;
        Uri root = SafManager.getDataRootUri(getContext());
        if (root == null) return null;
        try {
            String[] parts = relativePath.split("/");
            if (parts.length == 0) return null;
            String[] dirSegs;
            String filename;
            if (parts.length == 1) {
                dirSegs = new String[]{};
                filename = parts[0];
            } else {
                dirSegs = new String[parts.length - 1];
                System.arraycopy(parts, 0, dirSegs, 0, parts.length - 1);
                filename = parts[parts.length - 1];
            }
            androidx.documentfile.provider.DocumentFile df;
            if (create) {
                df = SafManager.createChild(getContext(), dirSegs, filename, "application/octet-stream");
            } else {
                df = SafManager.getChild(getContext(), dirSegs, filename);
            }
            return (df != null) ? df.getUri().toString() : null;
        } catch (Throwable ignored) { }
        return null;
    }

    // Resolve a file named by an .acgame manifest relative to that manifest.
    // Game folders are opened through ACTION_OPEN_DOCUMENT_TREE, so document IDs
    // retain the relative path even though native code only sees content:// URIs.
    public static String resolveArcadeAssetUri(String manifestUri, String relativePath) {
        Context context = getContext();
        if (context == null || manifestUri == null || relativePath == null) return null;
        try {
            Uri manifest = Uri.parse(manifestUri);
            String documentId = DocumentsContract.getDocumentId(manifest);
            int slash = documentId.lastIndexOf('/');
            String targetId = (slash >= 0) ? documentId.substring(0, slash) : documentId;

            for (String part : relativePath.replace('\\', '/').split("/")) {
                if (part.isEmpty() || ".".equals(part)) continue;
                if ("..".equals(part)) {
                    int parentSlash = targetId.lastIndexOf('/');
                    if (parentSlash >= 0) targetId = targetId.substring(0, parentSlash);
                    continue;
                }
                targetId += "/" + part;
            }

            Uri target;
            try {
                target = DocumentsContract.buildDocumentUriUsingTree(manifest, targetId);
            } catch (IllegalArgumentException ignored) {
                target = DocumentsContract.buildDocumentUri(manifest.getAuthority(), targetId);
            }

            try (ParcelFileDescriptor ignored =
                         context.getContentResolver().openFileDescriptor(target, "r")) {
                return target.toString();
            }
        } catch (Throwable t) {
            android.util.Log.w("NativeApp", "Unable to resolve arcade asset " + relativePath, t);
        }
        return null;
    }

    // List files under a relative SAF directory recursively. Returns full relative paths from the root.
    public static String[] listSafRecursiveFiles(String relativeDir) {
        java.util.ArrayList<String> out = new java.util.ArrayList<>();
        try {
            androidx.documentfile.provider.DocumentFile base = SafManager.getOrCreateDir(getContext(), relativeDir.split("/"));
            if (base == null || !base.exists()) return new String[0];
            walkDirRecursive(base, relativeDir, out);
        } catch (Throwable ignored) { }
        return out.toArray(new String[0]);
    }

    private static void walkDirRecursive(androidx.documentfile.provider.DocumentFile dir, String relPrefix, java.util.ArrayList<String> out) {
        androidx.documentfile.provider.DocumentFile[] arr = dir.listFiles();
        if (arr == null) return;
        for (androidx.documentfile.provider.DocumentFile f : arr) {
            if (f == null) continue;
            String name = f.getName();
            if (name == null || name.isEmpty()) continue;
            if (f.isDirectory()) {
                walkDirRecursive(f, relPrefix + "/" + name, out);
            } else if (f.isFile()) {
                out.add(relPrefix + "/" + name);
            }
        }
    }

    // List files directly under a relative SAF directory (non-recursive). Returns full relative paths.
    public static String[] listSafFilesFlat(String relativeDir) {
        java.util.ArrayList<String> out = new java.util.ArrayList<>();
        try {
            androidx.documentfile.provider.DocumentFile dir = SafManager.getOrCreateDir(getContext(), relativeDir.split("/"));
            if (dir == null || !dir.isDirectory()) return new String[0];
            androidx.documentfile.provider.DocumentFile[] arr = dir.listFiles();
            if (arr != null) {
                for (androidx.documentfile.provider.DocumentFile f : arr) {
                    if (f != null && f.isFile()) {
                        String name = f.getName();
                        if (name != null && !name.isEmpty()) out.add(relativeDir + "/" + name);
                    }
                }
            }
        } catch (Throwable ignored) { }
        return out.toArray(new String[0]);
    }

    // List filenames (files only) under a SAF subdirectory (e.g., "cheats", "patches").
    public static String[] listSafFilenames(String subdir) {
        try {
            androidx.documentfile.provider.DocumentFile dir = SafManager.getOrCreateDir(getContext(), subdir);
            if (dir == null || !dir.isDirectory()) return new String[0];
            androidx.documentfile.provider.DocumentFile[] arr = dir.listFiles();
            java.util.ArrayList<String> out = new java.util.ArrayList<>();
            if (arr != null) {
                for (androidx.documentfile.provider.DocumentFile f : arr) {
                    if (f != null && f.isFile()) {
                        String name = f.getName();
                        if (name != null && !name.isEmpty()) out.add(name);
                    }
                }
            }
            return out.toArray(new String[0]);
        } catch (Throwable ignored) { }
        return new String[0];
    }
}
