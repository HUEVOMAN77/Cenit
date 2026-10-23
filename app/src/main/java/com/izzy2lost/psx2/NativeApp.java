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
	// Cenit 0.6.27 (governor v4): el CycleSkip global existía como native suelto
	// pero la UI solo lo usa por juego, así que nadie lo llamaba en caliente. El
	// sintonizador CPU/VU sí escribe el global durante sus ensayos y lo devuelve
	// al terminar: mismo executor serie-a-serie que el Rate, para que dos
	// ApplySettings no se pisen entre hilos.
	public static void setEECycleSkipAsync(int value) {
		runNativeSettingAsync("speedhackEecycleskip", () -> speedhackEecycleskip(value));
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
	// Cenit 0.6.27 (governor v4): las otras dos lecturas efectivas que el
	// sintonizador CPU/VU necesita para (a) anotar la línea base real del juego
	// y (b) verificar que cada ensayo llegó de verdad a la emulación. Misma
	// semántica que arriba: con VM válida es la cascada completa (incluida la
	// capa por-juego y el GameDB); sin VM, lo que se va a aplicar al arrancar.
	// El fallback ante cualquier fallo es -1 ("sin dato"), nunca un valor que se
	// parezca a una decisión: el governor trata -1 como "no medir, no insistir".
	public static native int getEffectiveEECycleSkip();
	public static int safeGetEffectiveEECycleSkip() {
		if (hasNoNativeBinary) return -1;
		try { return getEffectiveEECycleSkip(); } catch (Throwable t) { return -1; }
	}
	public static native boolean getEffectiveMTVU();
	/** -1 = sin dato (binario ausente o fallo), 0/1 = el valor operativo real. */
	public static int safeGetEffectiveMTVU() {
		if (hasNoNativeBinary) return -1;
		try { return getEffectiveMTVU() ? 1 : 0; } catch (Throwable t) { return -1; }
	}
	// Valor de una clave del INI GLOBAL (s_settings_interface). El CycleSkip
	// global no tiene preferencia Java, así que esta es su única lectura.
	public static native int getGlobalSettingInt(String section, String key, int fallback);
	public static int safeGetGlobalSettingInt(String section, String key, int fallback) {
		if (hasNoNativeBinary) return fallback;
		try { return getGlobalSettingInt(section, key, fallback); } catch (Throwable t) { return fallback; }
	}
	// --- evidencia para clasificar el cuello: ¿espera el EE al VU1? ---------
	// Ventana de 0.5 s del núcleo, en ceros con MTVU apagado. Con binario ausente
	// o fallo devuelven 0, que es "sin evidencia", nunca "sí espera".
	public static native float getMtvuWaitMs();
	public static float safeGetMtvuWaitMs() {
		if (hasNoNativeBinary) return 0f;
		try { return getMtvuWaitMs(); } catch (Throwable t) { return 0f; }
	}
	public static native long getMtvuWaitCalls();
	public static long safeGetMtvuWaitCalls() {
		if (hasNoNativeBinary) return 0L;
		try { return getMtvuWaitCalls(); } catch (Throwable t) { return 0L; }
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

	// --- Cenit 0.6.29: lecturas para el clasificador universal de carga ---------
	// Son del mismo tipo que getGPUUsage: un atomic del motor renovado cada
	// ventana de 0,5 s. Ninguna escribe, pausa ni reconfigura nada, así que no
	// hay riesgo nuevo para el bucle emulacion. Sirven para dejar de comparar
	// numeros contra constantes sin unidad (el bug que GameLoadClassifier
	// documenta) y para que la decision no dependa del nombre del juego.

	/** Frecuencia VERTICAL que el juego publica en el GS emulado (~49,76/50/
	 *  59,83/59,94/60; 0 = sin senal). OJO: NO son los fps del juego — un titulo
	 *  que diseña a 30 en entrelazado presenta campo a campo y sigue publicando
	 *  59,94 —, así que sirve para LEER la evidencia, no para decidir con ella.
	 *  GameLoadClassifier tiene el porque verificado contra Counters.cpp. */
	public static native float getFrameRateTarget();
	public static float safeGetFrameRateTarget() {
		if (hasNoNativeBinary) return 0f;
		try { return getFrameRateTarget(); } catch (Throwable t) { return 0f; }
	}

	/** % de reloj que el hilo de emulacion (EE) trabajo en la ultima ventana: la
	 *  MISMA unidad que getGPUUsage, y por eso el clasificador puede sumarlas. */
	public static native float getEEThreadUsage();
	public static float safeGetEEThreadUsage() {
		if (hasNoNativeBinary) return 0f;
		try { return getEEThreadUsage(); } catch (Throwable t) { return 0f; }
	}

	// --- Cenit 0.6.30: snapshot unificado de carga (Fase 0/1 del prompt maestro) --
	// Un solo cruce JNI por tick en vez de doce lecturas sueltas. El double[] de
	// 24 elementos es un SNAPSHOT de un mismo instante de PerformanceMetrics::Update,
	// asi que GPU%, EE%, speed, frametime, MTVU, etc. son todos de la MISMA ventana
	// de 0,5 s. Ver CONTRATO_CENIT_LOAD_SAMPLE.md para los 24 indices y sus unidades.
	// null = "no hay muestra" (binario ausente o fallo), NUNCA ceros.
	public static native double[] getCenitSample();
	/** Snapshot unificado de carga. null si el binario no esta o la llamada falla. */
	public static double[] safeGetCenitSample() {
		if (hasNoNativeBinary) return null;
		try { return getCenitSample(); } catch (Throwable t) { return null; }
	}

	// --- Cenit 0.6.30: publicacion del veredicto al HUD nativo (Fase 1.8) ---------
	// Guarda el veredicto en CenitTelemetry.h (objeto inline de atomicos) y
	// ImGuiOverlays.cpp lo pinta en una linea nueva junto a la de VUSB, solo si
	// confidencePct >= 0. No toca nada de emulacion: es solo para que el HUD
	// muestre el estado de carga.
	// OJO con el SEXTO parametro (thermalStatus): lo agregue en el nativo porque
	// el estado THERMAL_BOUND del clasificador no se puede verificar en pantalla
	// sin ver el termico, y ese dato SOLO lo tiene Java (PowerManager, por
	// reflexion). La firma tiene que coincidir EXACTA con el .cpp: una firma JNI
	// desalineada no falla al compilar, falla en tiempo de ejecucion con
	// UnsatisfiedLinkError — que aqui se tragaria el catch y el HUD quedaria
	// mudo para siempre sin ningun aviso.
	// stateOrdinal: {0=UNKNOWN,1=NO_BOTTLENECK,2=GPU_BOUND,3=CPU_BOUND,4=VU_BOUND,
	//                5=THERMAL_BOUND,6=INVALID_SAMPLE}
	// actionCode: {0=HOLD,1=ALLOW_RESOLUTION_DROP,2=BLOCK_RESOLUTION_DROP,3=WAIT_FOR_EVIDENCE}
	// thermalStatus: el entero de PowerManager.getCurrentThermalStatus (0..6), o
	//                -1 cuando el telefono no expone la API o aun no se leyo.
	public static native void setCenitLoadVerdict(int stateOrdinal, int confidencePct,
	                                               int actionCode, double gpuPct, double eePct,
	                                               int thermalStatus);
	/** Publica el veredicto al HUD nativo. No-op si el binario no esta o la llamada falla. */
	public static void safeSetCenitLoadVerdict(int stateOrdinal, int confidencePct,
	                                            int actionCode, double gpuPct, double eePct,
	                                            int thermalStatus) {
		if (hasNoNativeBinary) return;
		try { setCenitLoadVerdict(stateOrdinal, confidencePct, actionCode, gpuPct, eePct, thermalStatus); }
		catch (Throwable t) { /* el HUD no debe romper la emulacion */ }
	}

	/**
	 * Borra el veredicto del HUD (vuelve a "todavia no hay"). Sin argumentos, asi que
	 * el riesgo de firma desalineada es el minimo: JNI sin parametros no puede
	 * desfasarse.
	 */
	public static native void resetCenitLoadVerdict();
	public static void safeResetCenitLoadVerdict() {
		if (hasNoNativeBinary) return;
		try { resetCenitLoadVerdict(); } catch (Throwable t) { /* idem: el HUD no rompe nada */ }
	}

	// --- Cenit 0.6.31 (Fase 1): el audio como metrica de adopcion --------------
	// long[3] = {underflows con silencio, chunks descartados, entradas en
	// timestretch}, ACUMULADOS desde que arranco el proceso. Se leen en delta: un
	// candidato no puede compararse contra el historial de diez minutos del juego,
	// solo contra lo que rompio DURANTE su ventana. null = sin cifra (binario
	// ausente o fallo de JNI), que es "no sabemos", NO "audio perfecto".
	public static native long[] getAudioFaultStats();
	public static long[] safeGetAudioFaultStats() {
		if (hasNoNativeBinary) return null;
		try { return getAudioFaultStats(); } catch (Throwable t) { return null; }
	}
	/** Pone los tres contadores a cero. Se llama al arrancar un ensayo, no por tick. */
	public static native void resetAudioFaultStats();
	public static void safeResetAudioFaultStats() {
		if (hasNoNativeBinary) return;
		try { resetAudioFaultStats(); } catch (Throwable t) { /* el audio no se toca */ }
	}

	// --- Cenit 0.6.31 (Fase 1): estado del sintonizador CPU/VU para el HUD -------
	// Mismo contrato que setCenitLoadVerdict: Java mide y publica, el C++ solo lee.
	// Los decimales viajan escalados a entero (fps x100, frame time x10) y con -1
	// como "no medido": el HUD no puede distinguir un NaN de un cero por su cuenta,
	// y pintar 0,0 ms como si fuera una medida real es justamente el tipo de numero
	// inventado que el prompt prohibe.
	// p50Ms/p95Ms y profileId se escalan/hashes AQUI, no en C++, para que la firma
	// nativa sea puramente de enteros: una JNI con String por campo es una superficie
	// de fuga y de carrera en el hilo de ImGui, y aqui solo hace falta un identificador
	// corto que el usuario puede cruzar con el log "perfil <id>".
	public static native void setCenitTunerStatus(int state, int mode, int gainPermille,
	                                              int fpsBeforeX100, int fpsAfterX100,
	                                              int p50DeciMs, int p95DeciMs,
	                                              int thermalStatus, long profileHash);
	public static void safeSetCenitTunerStatus(int state, int mode, int gainPermille,
	                                            int fpsBeforeX100, int fpsAfterX100,
	                                            float p50Ms, float p95Ms,
	                                            int thermalStatus, String profileId) {
		if (hasNoNativeBinary) return;
		final int p50 = (Float.isNaN(p50Ms) || p50Ms < 0f) ? -1 : Math.round(p50Ms * 10f);
		final int p95 = (Float.isNaN(p95Ms) || p95Ms < 0f) ? -1 : Math.round(p95Ms * 10f);
		try {
			setCenitTunerStatus(state, mode, gainPermille, fpsBeforeX100, fpsAfterX100,
			                    p50, p95, thermalStatus, profileHash(profileId));
		} catch (Throwable t) { /* el HUD no debe romper la emulacion */ }
	}
	public static native void resetCenitTunerStatus();
	public static void safeResetCenitTunerStatus() {
		if (hasNoNativeBinary) return;
		try { resetCenitTunerStatus(); } catch (Throwable t) { /* idem */ }
	}
	/** FNV-1a de 64 bits. Estable entre versiones y sin dependencias. */
	static long profileHash(String s) {
		if (s == null || s.isEmpty()) return 0L;
		long h = 0xcbf29ce484222325L;
		for (int i = 0; i < s.length(); i++) {
			h ^= (long) s.charAt(i);
			h *= 0x100000001b3L;
		}
		return h;
	}

	// --- Cenit 0.6.31 (Fase 1): carga de texturas como puerta de aprendizaje -----
	// Numero de reemplazos ya en cache. No hace falta traducir saf:// ni abrir el
	// disco: el core lo cuenta en su propio mutex. Devuelve -1 cuando NO hay senal
	// (binario ausente o excepcion), y CpuVUTuner interpreta -1 como "desconocido",
	// nunca como "carga terminada" (Regla 3).
	public static native long getLoadedTextureCount();
	public static long safeGetLoadedTextureCount() {
		if (hasNoNativeBinary) return -1L;
		try { return getLoadedTextureCount(); } catch (Throwable t) { return -1L; }
	}

	// --- Cenit 0.6.31 (Fase 1): borrar una clave del INI GLOBAL ------------------
	// Precedente: deleteGameSettingKey (capa por-juego). El diario anti-cierre apunta
	// si la clave EXISTIA antes del ensayo; si el tuner la creó, restaurar un 0 no es
	// neutro (siembra una clave que pisa el default del core y del GameDB), hay que
	// borrarla. Solo quita, nunca escribe. false = no se pudo (no hay binario o JNI
	// fallo); el llamador lo registra, porque un "no se pudo revertir" silencioso es
	// exactamente como se hereda un CycleSkip de prueba.
	public static native boolean deleteGlobalSettingKey(String section, String key);
	public static boolean safeDeleteGlobalSettingKey(String section, String key) {
		if (hasNoNativeBinary) return false;
		try { return deleteGlobalSettingKey(section, key); } catch (Throwable t) { return false; }
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
    // Cenit 0.6.39 — ANGLE: OpenGL ES sobre Vulkan usando las librerías
    // empaquetadas en el APK (libEGL_angle/libGLESv2_angle). En Mali/MediaTek
    // el driver OpenGL del sistema suele rendir muy por debajo de su Vulkan.
    // El contexto se crea al arrancar el juego: aplica en el próximo arranque.
    // getAngleStatus responde la verdad: 0 apagado, 1 pedido, 2 cargado,
    // 3 pedido pero no disponible (se usó el EGL del sistema).
    public static native void setAngleEnabled(boolean enabled);
    public static void setAngleEnabledAsync(boolean enabled) {
        runNativeSettingAsync("setAngleEnabled", () -> setAngleEnabled(enabled));
    }
    public static native int getAngleStatus();
    // FXAA: suaviza bordes en la presentación. Cuesta casi nada en un móvil
    // y se aplica en caliente (GSRenderer lee la bandera cada cuadro).
    public static native void setFxaa(boolean enabled);
    public static void setFxaaAsync(boolean enabled) {
        runNativeSettingAsync("setFxaa", () -> setFxaa(enabled));
    }
    // Colchón del stream de audio (SPU2/BufferMS). 50 ms es el valor del núcleo;
    // subirlo amortigua silencios y cortes en teléfonos cargados a cambio de algo
    // más de retraso entre imagen y sonido.
    public static native void setAudioBufferMs(int ms);
    public static void setAudioBufferMsAsync(int ms) {
        runNativeSettingAsync("setAudioBufferMs", () -> setAudioBufferMs(ms));
    }
    public static boolean defaultMTVU() {
        // Misma pregunta que hace el perfil en C++ (std::thread::hardware_concurrency()
        // >= 3), respondida por el mismo sitio: si Java contara los núcleos por su
        // cuenta, un desacuerdo escribiría un default distinto al del núcleo.
        if (hasNoNativeBinary) return false;
        try { return coresAllowMTVU(); } catch (Throwable t) { return false; }
    }
    public static native boolean coresAllowMTVU();

    // --- Cenit 0.6.28: cargar el juego en memoria (precache de disco) ---------
    // EmuCore/CdvdPrecache existía en el núcleo pero era inutilizable en este
    // puerto (arranque con ProgressCallback nulo = SIGSEGV; ya desactivado en
    // VMManager::PrecacheCDVDFile) y sin forma de saber antes si la imagen
    // cabe. Este par de llamadas pone lo que faltaba:
    //
    //   setCdvdPrecacheGlobal  -> escribe la decisión de Cenit en la capa base.
    //   precacheProbe          -> pregunta, con la política real del núcleo
    //                             (reserva de 2 GB) y el tamaño que cada lector
    //                             reserva de verdad, si la imagen cabe en la RAM
    //                             disponible AHORA. Mide el archivo por su propia
    //                             URI: no toca el disco puesto ni InputIsoFile,
    //                             así que es seguro llamarla con el VM vivo
    //                             (lección 0.6.23).
    //
    // Como CdvdPrecache se lee al inicializar el VM, el efecto es para el
    // PRÓXIMO arranque del juego. La preferencia "cdvd_precache" guarda el modo
    // automático; la capa por-juego (clave EmuCore/CdvdPrecache) manda encima
    // cuando el usuario fija Sí o No a mano.
    public static native void setCdvdPrecacheGlobal(boolean enabled);
    /**
     * Variante SÍNCRONA, para el hilo del arranque: la decisión medida se
     * escribe en la capa base justo antes de runVMThread, y ahí no vale el
     * executor (la escritura podría caer después de que el núcleo ya aplicó los
     * ajustes). Sin binario nativo es no-op en vez de UnsatisfiedLinkError.
     */
    public static void safeSetCdvdPrecacheGlobal(boolean enabled) {
        if (hasNoNativeBinary) return;
        try { setCdvdPrecacheGlobal(enabled); } catch (Throwable t) {
            android.util.Log.e("NativeApp", "setCdvdPrecacheGlobal failed", t);
        }
    }

    /**
     * Cenit 0.6.28: ventana de streaming del disco, en KB por buffer (el lector
     * tiene dos). 128 KB = el valor histórico del núcleo, que es el default
     * también aquí: sin tocar nada, el comportamiento es idéntico al de siempre.
     * Subirla ayuda a los juegos que NO caben en RAM (God of War), donde cada
     * salto del lector es un viaje al archivo por el canal SAF. El nativo blinda
     * los bordes (128 KB..4 MB): aquí solo se pasa la voluntad del usuario.
     * Se aplica en cada arranque (applySavedSettings) y vale para el PRÓXIMO
     * juego: un lector ya abierto tiene sus buffers puestos.
     */
    public static native void setIsoReadBufferKb(int kb);
    public static void safeSetIsoReadBufferKb(int kb) {
        if (hasNoNativeBinary) return;
        try { setIsoReadBufferKb(kb); } catch (Throwable t) {
            android.util.Log.e("NativeApp", "setIsoReadBufferKb failed", t);
        }
    }
    /**
     * Veredicto de precache para una imagen. Posiciones del arreglo:
     *   [0] 0=sin URI, 1=formato no soportado (.gz/.dump/manifiesto arcade),
     *       2=no se pudo medir, 3=cabe, 4=no cabe
     *   [1] bytes que el lector va a reservar   [2] tope de precache
     *   [3] memoria física disponible
     * Devuelve null si el nativo no está o la llamada falla (sin binario, o
     * URI vacía): la UI lo trata como "no se puede prometer nada".
     */
    public static native long[] precacheProbe(String gameUri);
    public static long[] safePrecacheProbe(String gameUri) {
        if (hasNoNativeBinary || gameUri == null || gameUri.isEmpty()) return null;
        // Deliberadamente FUERA de CDVD_LOCK, a diferencia de las lecturas de
        // identidad: la sonda abre un descriptor nuevo sobre la URI y pregunta
        // solo su tamaño (fseek a END + tell), sin pasar por InputIsoFile ni por
        // el lector del disco puesto, así que no hay nada global que serializar.
        // meterla bajo el candado significaría que un proveedor de documentos
        // lento bloquearía la escritura de ajustes desde la interfaz.
        try {
            long[] r = precacheProbe(gameUri);
            return (r != null && r.length >= 4) ? r : null;
        } catch (Throwable t) { return null; }
    }
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
    // APAGADA por defecto: el GATE del documento de arquitectura
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
    // Cenit 0.6.31 (Fase 1): ¿hay superbloques en ventana de validacion? Es la
    // puerta "superblockBusy" del sintonizador CPU/VU (un solo dueño por
    // intervalo). Distinta de getVUSuperblockEffective: esa dice si el motor esta
    // ON, esta dice si esta OCUPADO midiendo, que es lo que realmente interfiere.
    // false sin binario o sin arm64 = no hay motor, y la puerta no bloquea nada.
    public static native boolean superblockValidating();
    public static boolean safeSuperblockValidating() {
        if (hasNoNativeBinary) return false;
        try { return superblockValidating(); } catch (Throwable t) { return false; }
    }
    public static native void dumpVUSuperblockReport(String reason);
    /** True si el motor está validando/actuando ahora mismo (el HUD lo pinta). */
    public static boolean safeVUSuperblockEffective() {
        if (hasNoNativeBinary) return false;
        try { return getVUSuperblockEffective(); } catch (Throwable t) { return false; }
    }
    // --- Cenit 0.6.24: compilación asíncrona de pipelines TFX (Vulkan) --------
    // APAGADA por defecto: el costo es un intercambio que elige el usuario
    // (parpadeo de uno o dos frames vs. el tirón de compilar en el hilo de
    // dibujo); en el log de este teléfono corrió y compiló todo lo encolado.
    // Sin esta bandera, cada pipeline nuevo
    // se compila en el hilo de dibujo (vkCreateGraphicsPipelines) y eso es el
    // pico de frame time al aparecer efectos/shaders inéditos. Con ON, el
    // trabajo se manda a un hilo de fondo y el primitivo afectado se omite uno
    // o dos frames (parpadeo pequeño), en vez de frenar todo el frame. Se
    // aplica sobre la VM viva, sin reiniciar el emulador.
    public static native void setAsyncShaderCompile(boolean enabled);
    public static void setAsyncShaderCompileAsync(boolean enabled) {
        runNativeSettingAsync("setAsyncShaderCompile", () -> setAsyncShaderCompile(enabled));
    }

    // --- Cenit 0.6.25: reconstrucción EASU de FSR1 (Vulkan) -----------------
    // Cenit 0.6.26: encendida por defecto, y la cadena es autocontenida. Cuando la
    // resolución interna baja de 1x (0.5x / 0.75x del governor), la imagen llegaría
    // al panel estirada con bilinear. Ahora Cenit encadena pasadas EASU de hasta 2x
    // lineales cada una (el rango para el que AMD escribió el filtro) hasta la
    // resolución de salida, y aplica CAS en modo "solo enfocar" haciendo el papel de
    // RCAS, con un piso de nitidez. Ya NO requiere CAS encendido: ese gate de 0.6.25
    // fue un error y la función nunca ejecutaba en un teléfono normal.
    // No inventa detalle que nunca se renderizó: se nota mucho mejor que
    // estirar, no idéntico a nativo. Solo Vulkan; se aplica en caliente.
    public static native void setEASUReconstruct(boolean enabled);
    public static void setEASUReconstructAsync(boolean enabled) {
        runNativeSettingAsync("setEASUReconstruct", () -> setEASUReconstruct(enabled));
    }

    // --- Cenit 0.6.41: upscaler neuronal (Anime4K v3.2 S, MIT) ----------------
    // Cuatro convoluciones 3x3 con ReLU y un depth-to-space por cada x2, como
    // pasadas compute de Vulkan al lado de EASU. No hay runtime de inferencia ni
    // binario nuevo: los pesos viajan dentro de los shaders. Cuando esta encendida
    // se prueba primero y, si falla, Present cae a EASU en el mismo cuadro; por eso
    // apagarla es exactamente "volver a 0.6.40". Solo Vulkan, en caliente.
    public static native void setNNUpscale(boolean enabled);
    public static void setNNUpscaleAsync(boolean enabled) {
        runNativeSettingAsync("setNNUpscale", () -> setNNUpscale(enabled));
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

    // Cenit 0.6.38 paso 2 — las cuatro lineas "Cenit equipo:" ya formateadas, para
    // la cabecera del reporte. Ver el native: es el MISMO texto que imprime el
    // registro (una sola fuente, FormatDeviceMeasureLines). Se pide al nucleo en vez
    // de leerlo del log porque el reporte solo lleva las ultimas lineas y estas se
    // imprimen al arrancar, o sea se cortan.
    public static native String getDeviceMeasureLines();
    /** Lineas de equipo medido, o null si el binario nativo no esta o no devolvio nada. */
    public static String safeGetDeviceMeasureLines() {
        if (hasNoNativeBinary) return null;
        try {
            String s = getDeviceMeasureLines();
            return (s == null || s.isEmpty()) ? null : s;
        } catch (Throwable t) { return null; }
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
    /** SERIAL o "" si el binario no está o la consulta falla. */
    public static String safeGetCurrentGameSerial() {
        if (hasNoNativeBinary) return "";
        try {
            final String s = getCurrentGameSerial();
            return s == null ? "" : s;
        } catch (Throwable t) { return ""; }
    }

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
    // Cenit 0.6.27 (governor v4): borra UNA clave de la capa por-juego. Es la
    // marcha atrás limpia de una adopción del sintonizador CPU/VU: la clave
    // desaparece y con ella la prioridad de la capa, así el GameDB / el INI
    // global vuelven a mandar sin dejar un 0 huérfano que pise un -1 manual.
    // El nativo recarga la capa en caliente si ese juego está corriendo.
    public static native boolean deleteGameSettingKey(String gameUri, String section, String key);
    public static boolean safeDeleteGameSettingKey(String gameUri, String section, String key) {
        if (hasNoNativeBinary || gameUri == null || gameUri.isEmpty()) return false;
        synchronized (CDVD_LOCK) {
            try { return deleteGameSettingKey(gameUri, section, key); } catch (Throwable t) { return false; }
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

	/** Total de cuadros presentados por el VM desde que arrancó. Solo avanza
	 *  cuando la emulación entrega un cuadro real, así que un hang se manifiesta
	 *  como "deja de avanzar" — a diferencia de getFPS(), cuyo último valor puede
	 *  seguir pareciendo vivo durante una congelación. */
	public static native long getFrameNumber();
	/** -1 si el binario nativo no está o la consulta falla (el vigilante debe
	 *  ignorar -1 en lugar de interpretarlo como congelación). */
	public static long safeGetFrameNumber() {
		if (hasNoNativeBinary) return -1L;
		try { return getFrameNumber(); } catch (Throwable t) { return -1L; }
	}
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
