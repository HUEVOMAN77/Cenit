#include <jni.h>
#include <android/native_window_jni.h>
#include <android/log.h>
#include <unistd.h>
#include <iterator> // std::size, usado por la tabla de hacks por-juego
#include "PrecompiledHeader.h"
#include "AchievementsJNI.h"
#include "common/StringUtil.h"
#include "common/FileSystem.h"
#include "common/Error.h"
#include "common/ZipHelpers.h"
#include "pcsx2/GS.h"
#include "pcsx2/VMManager.h"
#include "CDVD/CDVD.h"
#include "PerformanceMetrics.h"
#include "GameList.h"
#include "GameDatabase.h"
#include "GS/GSPerfMon.h"
#include "GS/Renderers/HW/GSRendererHW.h"
#include "GS/Renderers/HW/GSTextureReplacements.h"
#include "GSDumpReplayer.h"
#include "ImGui/ImGuiManager.h"
#include "common/Path.h"
#include "common/MemorySettingsInterface.h"
#include "pcsx2/INISettingsInterface.h"
#include "SIO/Pad/Pad.h"
#include "Input/InputManager.h"
#include "ImGui/ImGuiFullscreen.h"
#include "Achievements.h"
#include "Host.h"
#include "ImGui/FullscreenUI.h"
#include "SIO/Pad/PadDualshock2.h"
#include "DEV9/ACJV.h"
#include "USB/USB.h"
#include "MTGS.h"
#include "GS/Renderers/Vulkan/VKLoader.h"
#include "SDL3/SDL.h"
#ifdef __aarch64__
// Cenit 0.6.15 (Fase 1): sonda de trazas VU — header puro C++, sin vixl.
#include "pcsx2/arm64/MvuTraceProbe-arm64.h"
// Cenit 0.6.21 (Fases 2-5): motor de superbloques VU — mismo estilo, header
// puro C++ (el estado vive en MvuSuperblock-arm64.cpp, linkeado en el core).
#include "pcsx2/arm64/MvuSuperblock-arm64.h"
#endif
#include <atomic>
#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#ifdef __ANDROID__
#include "SDL3/SDL.h"
#include "AndroidDeviceDetection.h"
#include <thread>
#endif


bool s_execute_exit;
int s_window_width = 0;
int s_window_height = 0;
ANativeWindow* s_window = nullptr;

static std::mutex s_window_mutex;
static std::mutex s_vm_start_mutex;
// Held for the entire lifetime of a VM thread. VMManager::Shutdown() flips the
// state to Shutdown before CPUThreadShutdown() has finished tearing down MTGS/
// SysMemory, so a state check alone lets the next boot race the old teardown.
static std::mutex s_vm_lifecycle_mutex;
static std::mutex s_vm_error_mutex;
static ANativeWindow* s_render_window = nullptr;
static std::atomic_bool s_shutdown_requested{false};
// Bumped by prepareVMStart() before every boot. shutdown() hands its work to a
// detached thread, so without this a stop request issued while nothing is
// running can be scheduled late and kill the VM that was started right after
// it. The detached thread compares the generation it captured and bails when a
// newer boot has already begun.
static std::atomic<uint64_t> s_vm_start_generation{0};
static std::string s_last_vm_error;
static MemorySettingsInterface s_settings_interface;
static int s_pending_renderer = -1; // -1 = none; else 12=OpenGL,13=SW,14=Vulkan
static std::string s_verified_bios_usa;
static std::string s_verified_bios_europe;
static std::string s_verified_bios_japan;
static std::string s_verified_bios_arcade;

struct TouchscreenPointerUpdate
{
    float x;
    float y;
    bool pressed;
};

static std::mutex s_touchscreen_pointer_mutex;
static std::deque<TouchscreenPointerUpdate> s_touchscreen_pointer_updates;

// Work handed to the CPU thread by core code (patches, save-state hotkeys,
// achievements, PINE). Drained by Host::PumpMessagesOnCPUThread().
static std::mutex s_cpu_thread_task_mutex;
static std::deque<std::function<void()>> s_cpu_thread_tasks;
static std::atomic<std::thread::id> s_cpu_thread_id{};

static void QueueTouchscreenPointerUpdate(float x, float y, bool pressed)
{
    std::lock_guard lock(s_touchscreen_pointer_mutex);

    // MOVE events can arrive faster than the emulated input poll. Keep the
    // newest coordinates for the current state, but never discard a press or
    // release edge.
    if (!s_touchscreen_pointer_updates.empty() &&
        s_touchscreen_pointer_updates.back().pressed == pressed)
    {
        s_touchscreen_pointer_updates.back() = {x, y, pressed};
    }
    else
    {
        s_touchscreen_pointer_updates.push_back({x, y, pressed});
    }
}

static void ClearTouchscreenPointerUpdates()
{
    std::lock_guard lock(s_touchscreen_pointer_mutex);
    s_touchscreen_pointer_updates.clear();
}

// Renderer values already match GSRendererType. OpenGL is linked on Android and
// must not be silently rewritten to Vulkan.
static int NormalizeAndroidRenderer(int renderer)
{
    return renderer;
}

// ---------------------------------------------------------------------------
// Hardware performance profile
//
// Applied once at startup, BEFORE the user's saved settings are pushed, so any
// explicit choice in the drawer still wins. It only touches global baselines:
// speedhacks (the big EE-side wins), GS defaults that stop the GPU waiting on
// the CPU, and log verbosity. Per-game recommendations from GameIndex.yaml are
// layered on top of this by the normal game-settings layer.
// ---------------------------------------------------------------------------
static void ApplyHardwarePerformanceProfile()
{
#ifdef __ANDROID__
    const AndroidDeviceDetection::GPUVendor vendor = AndroidDeviceDetection::DetectGPUVendor();
    const bool snapdragon = (vendor == AndroidDeviceDetection::GPUVendor::Qualcomm);
    const bool high_end = snapdragon && AndroidDeviceDetection::IsHighEndSnapdragon();

    Console.WriteLn("Perf profile: vendor=%d snapdragon=%d high_end=%d soc=%u",
        static_cast<int>(vendor), static_cast<int>(snapdragon), static_cast<int>(high_end),
        snapdragon ? AndroidDeviceDetection::GetQualcommSocModel() : 0u);

    // Speedhacks: these are the recommendations from the official compatibility
    // database for mid-tier ARM devices and they are what carries 30/60 fps on
    // the Snapdragon 778G class.
    //   IntcStat / WaitLoop  - safe on effectively every game, big EE win.
    //   vuFlagHack           - microVU flag stall skip; needs no MTVU.
    //   vu1Instant           - instant VU1 transfer when VU1 is not threaded.
    //   fastCDVD             - REMOVED from the forced set in 0.6.7. It deletes
    //                          the emulated DVD seek latency, and on a phone the
    //                          disc is an ISO on flash storage: there is no real
    //                          seek to win back, so the "speedup" is close to
    //                          zero — while the latency it removes IS part of the
    //                          pacing some games expect. Shadow of the Colossus
    //                          streams its FMV and opening data straight off the
    //                          DVD and dies on startup with it on (the engine
    //                          itself warns "this may break games",
    //                          VMManager.cpp:3633). Upstream default is off; we
    //                          were overriding it for a gain that does not exist
    //                          on this hardware. It stays available as a switch.
    //   vuThread (MTVU)      - the hardware-dependent defaults already enable it
    //                          for >=3-core SoCs and turn on thread pinning on
    //                          big.LITTLE (that is what keeps the EE/VU threads
    //                          on the 778G's gold cores), so mirror it here.
    s_settings_interface.SetBoolValue("EmuCore/Speedhacks", "IntcStat", true);
    s_settings_interface.SetBoolValue("EmuCore/Speedhacks", "WaitLoop", true);
    s_settings_interface.SetBoolValue("EmuCore/Speedhacks", "vuFlagHack", true);
    s_settings_interface.SetBoolValue("EmuCore/Speedhacks", "vu1Instant", true);
    s_settings_interface.SetBoolValue("EmuCore/Speedhacks", "fastCDVD", false);
    s_settings_interface.SetBoolValue("EmuCore/Speedhacks", "vuThread",
        std::thread::hardware_concurrency() >= 3);
    // Neutral cycle rate/skip: cycle skipping can break audio/video timing and
    // the 778G does not need it. Games that do are handled by GameIndex.yaml.
    s_settings_interface.SetIntValue("EmuCore/Speedhacks", "EECycleRate", 0);
    s_settings_interface.SetIntValue("EmuCore/Speedhacks", "EECycleSkip", 0);

    // Recompilers must be on (they are by default) — fastmem is the single
    // biggest EE memory win on arm64; assert it rather than trust the default.
    s_settings_interface.SetBoolValue("EmuCore/CPU/Recompiler", "EnableEE", true);
    s_settings_interface.SetBoolValue("EmuCore/CPU/Recompiler", "EnableIOP", true);
    s_settings_interface.SetBoolValue("EmuCore/CPU/Recompiler", "EnableVU0", true);
    s_settings_interface.SetBoolValue("EmuCore/CPU/Recompiler", "EnableVU1", true);
    s_settings_interface.SetBoolValue("EmuCore/CPU/Recompiler", "EnableFastmem", true);
    s_settings_interface.SetBoolValue("EmuCore/CPU/Recompiler", "EnableVUProgramCache", true);

    // BLOQUE 1 (0.6.6): fijado de hilos al núcleo rápido, garantizado. El core
    // YA sabe hacerlo — VMManager::SetEmuThreadAffinities ordena los procesadores
    // por frecuencia (cpuinfo, ignorando SMT) y asigna EE/VU/GS a los núcleos más
    // rápidos: VMManager.cpp:4118-4151, MTGS incluido. Lo que no estaba garantizado
    // es que el bit llegue encendido: el core solo lo fuerza cuando los clústeres
    // reportados son >1 Y hay >=3 núcleos (VMManager.cpp:4035). Hay SoCs que
    // big.LITTLE que cpuinfo agrupa en un solo clúster, y teléfonos de 2 núcleos
    // rápidos — en ambos el pinning se quedaba apagado y el kernel del teléfono
    // movía el hilo del EE a un E-core a mitad de frame. Aquí se garantiza siempre:
    // si la lista de procesadores sale corta, SetEmuThreadAffinities ya se
    // auto-deshace con SetAffinity(0) (VMManager.cpp:4113), así que forzar no
    // puede romper nada. Java lo re-escribe después con la preferencia del usuario.
    s_settings_interface.SetBoolValue("EmuCore", "EnableThreadPinning", true);

    // GS: stop the CPU spinning on GPU readbacks (huge on Adreno). The hardware
    // renderer default download mode already keeps MTGS off the EE's critical
    // path; per-game UserHacks come from GameIndex.yaml via the game-settings
    // layer, so nothing manual is set here on purpose.
    s_settings_interface.SetBoolValue("EmuCore/GS", "HWSpinCPUForReadbacks", false);
    s_settings_interface.SetBoolValue("EmuCore/GS", "HWSpinGPUForReadbacks", false);
    s_settings_interface.SetBoolValue("EmuCore/GS", "SkipDuplicateFrames", true);

    // §4.2: el método por defecto ya es Zstandard (barato y bueno); lo que se
    // ajusta en gama baja es el NIVEL: Medium (valor por defecto) -> Low (0),
    // el nivel más rápido de zstd. Guardar una estado no debe costar cuadros;
    // cambia tamaño, no fiabilidad.
    if (AndroidDeviceDetection::GetDeviceTier() == 0)
    {
        s_settings_interface.SetIntValue("EmuCore", "SavestateCompressionRatio", 0);

        // §2.2: en gama baja el ancho de banda es el recurso más escaso y el
        // readback síncrono del MTGS es lo que más traba el pipeline. 3 =
        // Unsynchronized: la lectura sigue HACIéndose (GSReadLocalMemoryUnsync,
        // agua/reflejos siguen teniendo datos reales con ventana de carrera)
        // pero el EE no espera al GPU. El 4 (Disabled) fue descartado porque en
        // MTGS.cpp:289 devuelve MEMSET de ceros: rompe efectos enteros.
        // Se escribe en la capa BASE, así que el orden del núcleo sigue siendo:
        // base -> GameDB -> gamesettings/<serial>.ini. Un juego con reflejos
        // problemáticos se excepciona desde GameIndex.yaml con HWDownloadMode
        // (la infraestructura por-título ya existe) o desde el INI del propio
        // juego, sin tocar el perfil. El aviso de arranque de VMManager.cpp:3659
        // ("may break rendering in some games") sale a propósito: es honesto.
        s_settings_interface.SetIntValue("EmuCore/GS", "HWDownloadMode", 3);

        // BLOQUE 3 (0.6.6): pre-carga de texturas. "Full" (el default del core)
        // sube a la GPU TODA textura que el juego toca, incluyendo las que nunca
        // se dibujan en pantalla — en un teléfono con 4-6 GB eso son cientos de MB
        // de VRAM compartida y micro-cortes cuando el cacheador purga. "Partial"
        // (1) solo precarga las que van a salir: menos RAM, menos stutter en el
        // primer contacto con una zona nueva. La clave real es "texture_preloading"
        // en EmuCore/GS (Pcsx2Config.cpp:1086), y GS.cpp:958 recarga la caché al
        // cambiarla, así que se puede tocar en caliente. El GameDB sigue mandando:
        // los juegos que exigen Full lo fijan en su capa, esta escritura es solo
        // el valor base.
        s_settings_interface.SetIntValue("EmuCore/GS", "texture_preloading", 1);
    }
    else
    {
        // BLOQUE 2 (0.6.6): ritmo de cuadro. VsyncQueueSize=0 (lo que FullscreenUI
        // llama "Optimal Frame Pacing") hace que el EE espere a que el GS termine
        // CADA cuadro (MTGS.cpp:274) en vez de ir dos cuadros por delante. Menos
        // input lag y el ritmo irregular desaparece cuando el teléfono DE VERDAD
        // sobra para el juego. Justo por eso solo se fuerza en tier >= 1: en gama
        // baja quitar los dos cuadros de amortiguación convierte cualquier pico
        // del GPU en el EE durmiendo, y la velocidad de emulación cae. Con cola=2
        // (el default del core) los picos se absorben. No hay riesgo de cuelgue:
        // el GS publica el semáforo al vaciar el anillo incluso sin trabajo
        // (MTGS.cpp:598).
        s_settings_interface.SetIntValue("EmuCore/GS", "VsyncQueueSize", 0);
    }

    // Renderer/upscale are NOT set here: MainActivity pushes the user's saved
    // values after initialize() (applyGlobalSettingsBatch), so a native write
    // would just be overwritten. The device tier is exposed via
    // getDevicePerformanceTier() and Java uses it as the FIRST-RUN default for
    // upscale_multiplier instead.

    // Logging: EnableSystemConsole+EnableVerbose dump every log line through a
    // JNI call per line, which is measurable on its own at 60 fps — those stay
    // off. File logging is ON as of 0.6.7: with it off, a game that dies during
    // startup leaves zero evidence and every fix becomes a guess (that is
    // exactly what happened with Shadow of the Colossus). emulog.txt is written
    // by the core itself (VMManager.cpp:579, <dataRoot>/logs/emulog.txt) at
    // INFO level, which is one line per notable event, not per frame — the cost
    // is not what the console/verbose path was.
    s_settings_interface.SetBoolValue("Logging", "EnableVerbose", false);
    s_settings_interface.SetBoolValue("Logging", "EnableSystemConsole", false);
    s_settings_interface.SetBoolValue("Logging", "EnableTimestamps", true);
    s_settings_interface.SetBoolValue("Logging", "EnableFileLogging", true);

    // Audio: 150/40 is what the defaults use; keep the buffer but let it mix on
    // a real-time thread (Oboe already does) — nothing to write, left documented.
#endif
}

// Device performance tier for first-run defaults (see PerfProfile on the Java
// side). GetDeviceTier() has the full curated table: 2 = high-end Snapdragon,
// 1 = other Snapdragon + capable MediaTek/Exynos, 0 = low-end/unknown.
extern "C"
JNIEXPORT jint JNICALL
Java_com_izzy2lost_psx2_NativeApp_getDevicePerformanceTier(JNIEnv*, jclass)
{
#ifdef __ANDROID__
    return AndroidDeviceDetection::GetDeviceTier();
#else
    return 2;
#endif
}

// Fallback JNI access for content:// when SDL's Android env is not yet ready
// (no JNI fallback)

////
std::string GetJavaString(JNIEnv *env, jstring jstr) {
    if (!jstr) {
        return "";
    }
    const char *str = env->GetStringUTFChars(jstr, nullptr);
    std::string cpp_string = std::string(str);
    env->ReleaseStringUTFChars(jstr, str);
    return cpp_string;
}

static std::string GetGameSerialForPath(const std::string& game_path)
{
    if (game_path.empty())
        return {};

    if (VMManager::isArcadeManifest(game_path))
    {
        INISettingsInterface manifest(game_path);
        return manifest.Load() ? manifest.GetStringValue("game", "gameid", "") : std::string();
    }

    // Determine serial via CDVD using the same path the core will open
    Error error;
    std::string serial;
    auto* prev = CDVD;
    CDVD = &CDVDapi_Iso;
    if (CDVD->open(game_path, &error))
    {
        (void)DoCDVDdetectDiskType();
        cdvdGetDiscInfo(&serial, nullptr, nullptr, nullptr, nullptr, nullptr);
        DoCDVDclose();
    }
    CDVD = prev;
    return serial;
}

static void ApplyPerGameSettingsForSerial(const std::string& serial)
{
    if (serial.empty())
        return;

    // Build settings path and load
    const std::string settings_dir = Path::Combine(EmuFolders::DataRoot, "gamesettings");
    const std::string settings_path = Path::Combine(settings_dir, serial + ".ini");
    INISettingsInterface per_game(settings_path);
    if (!per_game.Load())
        return;

    // Map known keys into our in-memory settings layer and apply where possible
    std::string s;
    float fval = 0.0f;
    bool bval = false;

    if (per_game.GetStringValue("EmuCore/GS", "Renderer", &s))
    {
        s_settings_interface.SetStringValue("EmuCore/GS", "Renderer", s.c_str());
        // Defer actual renderer switch until VM is initialized
        int rend = -1;
        if (StringUtil::Strcasecmp(s.c_str(), "OpenGL") == 0) rend = 12;
        else if (StringUtil::Strcasecmp(s.c_str(), "Software") == 0) rend = 13;
        else if (StringUtil::Strcasecmp(s.c_str(), "Vulkan") == 0) rend = 14;
        if (rend >= 0)
            s_pending_renderer = rend;
    }
    if (per_game.GetFloatValue("EmuCore/GS", "upscale_multiplier", &fval))
        s_settings_interface.SetFloatValue("EmuCore/GS", "upscale_multiplier", fval);
    int abl_int = -1;
    if (per_game.GetIntValue("EmuCore/GS", "accurate_blending_unit", &abl_int))
    {
        s_settings_interface.SetStringValue("EmuCore/GS", "accurate_blending_unit", StringUtil::StdStringFromFormat("%d", abl_int).c_str());
    }
    else if (per_game.GetStringValue("EmuCore/GS", "accurate_blending_unit", &s))
    {
        int lvl = 1;
        if (StringUtil::Strcasecmp(s.c_str(), "Minimum") == 0) lvl = 0;
        else if (StringUtil::Strcasecmp(s.c_str(), "Basic") == 0) lvl = 1;
        else if (StringUtil::Strcasecmp(s.c_str(), "Medium") == 0) lvl = 2;
        else if (StringUtil::Strcasecmp(s.c_str(), "High") == 0) lvl = 3;
        else if (StringUtil::Strcasecmp(s.c_str(), "Full") == 0) lvl = 4;
        else if (StringUtil::Strcasecmp(s.c_str(), "Maximum") == 0) lvl = 5;
        s_settings_interface.SetStringValue("EmuCore/GS", "accurate_blending_unit", StringUtil::StdStringFromFormat("%d", lvl).c_str());
    }

    if (per_game.GetBoolValue("EmuCore", "EnableWideScreenPatches", &bval))
        s_settings_interface.SetBoolValue("EmuCore", "EnableWideScreenPatches", bval);
    if (per_game.GetBoolValue("EmuCore", "EnableNoInterlacingPatches", &bval))
        s_settings_interface.SetBoolValue("EmuCore", "EnableNoInterlacingPatches", bval);
    if (per_game.GetBoolValue("EmuCore", "EnablePatches", &bval))
        s_settings_interface.SetBoolValue("EmuCore", "EnablePatches", bval);
    if (per_game.GetBoolValue("EmuCore", "EnableCheats", &bval))
        s_settings_interface.SetBoolValue("EmuCore", "EnableCheats", bval);
}

enum class VerifiedBiosRegion
{
    Unknown,
    USA,
    Europe,
    Japan,
};

static std::string NormalizeSerialPrefix(const std::string& serial)
{
    std::string out;
    out.reserve(serial.size());
    for (unsigned char ch : serial)
    {
        if (std::isalnum(ch))
            out.push_back(static_cast<char>(std::toupper(ch)));
    }
    return out;
}

static VerifiedBiosRegion GetBiosRegionForSerial(const std::string& serial)
{
    const std::string normalized = NormalizeSerialPrefix(serial);
    if (normalized.empty())
        return VerifiedBiosRegion::Unknown;

    if (StringUtil::StartsWithNoCase(normalized, "SCUS") ||
        StringUtil::StartsWithNoCase(normalized, "SLUS"))
    {
        return VerifiedBiosRegion::USA;
    }

    if (StringUtil::StartsWithNoCase(normalized, "SCES") ||
        StringUtil::StartsWithNoCase(normalized, "SCED") ||
        StringUtil::StartsWithNoCase(normalized, "SLES") ||
        StringUtil::StartsWithNoCase(normalized, "SLED"))
    {
        return VerifiedBiosRegion::Europe;
    }

    if (StringUtil::StartsWithNoCase(normalized, "SCPS") ||
        StringUtil::StartsWithNoCase(normalized, "SLPS") ||
        StringUtil::StartsWithNoCase(normalized, "SLPM"))
    {
        return VerifiedBiosRegion::Japan;
    }

    return VerifiedBiosRegion::Unknown;
}

static const std::string& FirstVerifiedBios()
{
    if (!s_verified_bios_usa.empty())
        return s_verified_bios_usa;
    if (!s_verified_bios_europe.empty())
        return s_verified_bios_europe;
    return s_verified_bios_japan;
}

static void SelectVerifiedBiosForSerial(const std::string& serial, bool arcade)
{
    if (arcade)
    {
        s_settings_interface.SetStringValue("Filenames", "BIOS", s_verified_bios_arcade.c_str());
        EmuConfig.BaseFilenames.Bios = s_verified_bios_arcade;
        if (!s_verified_bios_arcade.empty())
            Console.WriteLn("Selected verified COH-H arcade BIOS '%s'", s_verified_bios_arcade.c_str());
        return;
    }

    const VerifiedBiosRegion region = GetBiosRegionForSerial(serial);
    const std::string* selected = nullptr;

    switch (region)
    {
        case VerifiedBiosRegion::USA:
            selected = !s_verified_bios_usa.empty() ? &s_verified_bios_usa : nullptr;
            break;
        case VerifiedBiosRegion::Europe:
            selected = !s_verified_bios_europe.empty() ? &s_verified_bios_europe : nullptr;
            break;
        case VerifiedBiosRegion::Japan:
            selected = !s_verified_bios_japan.empty() ? &s_verified_bios_japan : nullptr;
            break;
        case VerifiedBiosRegion::Unknown:
        default:
            break;
    }

    const std::string& fallback = FirstVerifiedBios();
    if (!selected && !fallback.empty())
        selected = &fallback;

    if (!selected || selected->empty())
        return;

    s_settings_interface.SetStringValue("Filenames", "BIOS", selected->c_str());
    EmuConfig.BaseFilenames.Bios = *selected;
    Console.WriteLn("Selected verified BIOS '%s' for game serial '%s'", selected->c_str(), serial.c_str());
}

// (renderGpu JNI defined later; keep only one definition)

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setHudVisible(JNIEnv* env, jclass clazz, jboolean p_visible)
{
    const bool visible = (p_visible == JNI_TRUE);
    MemorySettingsInterface& si = s_settings_interface;

    // Toggle most HUD/OSD elements together
    si.SetBoolValue("EmuCore/GS", "OsdShowSpeed", visible);
    si.SetBoolValue("EmuCore/GS", "OsdShowFPS", visible);
    si.SetBoolValue("EmuCore/GS", "OsdShowVPS", visible);
    si.SetBoolValue("EmuCore/GS", "OsdShowCPU", visible);
    si.SetBoolValue("EmuCore/GS", "OsdShowGPU", visible);
    si.SetBoolValue("EmuCore/GS", "OsdShowResolution", visible);
    si.SetBoolValue("EmuCore/GS", "OsdShowGSStats", visible);
    si.SetBoolValue("EmuCore/GS", "OsdShowIndicators", visible);
    si.SetBoolValue("EmuCore/GS", "OsdShowSettings", visible);
    si.SetBoolValue("EmuCore/GS", "OsdShowInputs", visible);
    si.SetBoolValue("EmuCore/GS", "OsdShowFrameTimes", visible);
    si.SetBoolValue("EmuCore/GS", "OsdShowVersion", visible);
    si.SetBoolValue("EmuCore/GS", "OsdShowHardwareInfo", visible);
    si.SetBoolValue("EmuCore/GS", "OsdShowVideoCapture", visible);
    si.SetBoolValue("EmuCore/GS", "OsdShowInputRec", visible);

    // Apply changes to the running VM/renderer if active
    VMManager::ApplySettings();
    if (MTGS::IsOpen())
        MTGS::ApplySettings();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setMemoryCardSlots(JNIEnv* env, jclass,
                                                      jstring slot1_filename, jboolean slot1_enabled,
                                                      jstring slot2_filename, jboolean slot2_enabled)
{
    const std::string slot1 = GetJavaString(env, slot1_filename);
    const std::string slot2 = GetJavaString(env, slot2_filename);
    s_settings_interface.SetBoolValue("MemoryCards", "Slot1_Enable", slot1_enabled == JNI_TRUE);
    s_settings_interface.SetStringValue("MemoryCards", "Slot1_Filename", slot1.c_str());
    s_settings_interface.SetBoolValue("MemoryCards", "Slot2_Enable", slot2_enabled == JNI_TRUE);
    s_settings_interface.SetStringValue("MemoryCards", "Slot2_Filename", slot2.c_str());

    if (VMManager::HasValidVM())
        VMManager::ApplySettings();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setBlendingAccuracy(JNIEnv* env, jclass, jint level)
{
    // level: 0..5 -> numeric string
    if (level < 0) level = 0; if (level > 5) level = 5;
    s_settings_interface.SetStringValue("EmuCore/GS", "accurate_blending_unit", StringUtil::StdStringFromFormat("%d", level).c_str());
    if (VMManager::HasValidVM())
        VMManager::ApplySettings();
    if (MTGS::IsOpen())
        MTGS::ApplySettings();
}

// --- Cenit: setters de opciones GS que se aplican en caliente -----------------
// Todos escriben en la sección EmuCore/GS del INI y luego piden ApplySettings,
// igual que setBlendingAccuracy. VMManager::ApplySettings recarga EmuConfig desde
// el INI, así que el valor se nota sin reiniciar el juego.

// Filtro de texturas: 0 Nearest, 1 Bilinear (forzado), 2 PS2 (predeterminado),
// 3 Bilinear pero sprites Nearest. Clave "filter".
extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setTextureFiltering(JNIEnv* env, jclass, jint mode)
{
    if (mode < 0) mode = 0; if (mode > 3) mode = 3;
    s_settings_interface.SetIntValue("EmuCore/GS", "filter", mode);
    if (VMManager::HasValidVM()) VMManager::ApplySettings();
    if (MTGS::IsOpen()) MTGS::ApplySettings();
}

// Mipmapado en hardware (hw_mipmap). Suaviza texturas lejanas; puede romper
// algunos juegos, por eso va aparte del filtro base.
extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setHWMipmap(JNIEnv* env, jclass, jboolean enabled)
{
    s_settings_interface.SetBoolValue("EmuCore/GS", "hw_mipmap", enabled == JNI_TRUE);
    if (VMManager::HasValidVM()) VMManager::ApplySettings();
    if (MTGS::IsOpen()) MTGS::ApplySettings();
}

// Filtro anisotrópico: guarda el multiplicador real (0=auto/apagado, 2, 4, 8, 16).
extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setMaxAnisotropy(JNIEnv* env, jclass, jint level)
{
    if (level != 2 && level != 4 && level != 8 && level != 16) level = 0;
    s_settings_interface.SetIntValue("EmuCore/GS", "MaxAnisotropy", level);
    if (VMManager::HasValidVM()) VMManager::ApplySettings();
    if (MTGS::IsOpen()) MTGS::ApplySettings();
}

// Cenit 0.6.6: pre-carga de texturas (Off/Partial/Full -> 0/1/2, el orden del
// enum TexturePreloadingLevel). Cambiarla recarga la caché de texturas por sí
// solo (GS.cpp:958), así que se nota sin reiniciar el juego: la primera pantalla
// puede tardar un poco más en rellenarse, y después va más suelta.
extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setTexturePreloading(JNIEnv* env, jclass, jint level)
{
    if (level < 0) level = 0; if (level > 2) level = 2;
    s_settings_interface.SetIntValue("EmuCore/GS", "texture_preloading", level);
    if (VMManager::HasValidVM()) VMManager::ApplySettings();
    if (MTGS::IsOpen()) MTGS::ApplySettings();
}

// Cenit 0.6.6: ritmo de cuadro. 0 = "óptimo" (el EE espera a que el GS drena el
// cuadro antes de seguir: mínimo input lag, más exigente); 1..n = cuadros en
// cola (amortigua los picos de GPU a costa de latencia). Ojo: MTGS.cpp:274
// aplica el límite SIEMPRE (la excepción de "sin vsync" está comentada ahí
// arriba desde hace años), así que esto cambia el ritmo también con VSync
// apagado. Se aplica en caliente: ApplySettings releen EmuConfig y la cola se
// consulta en cada vsync.
extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setFrameLatencyQueue(JNIEnv* env, jclass, jint frames)
{
    if (frames < 0) frames = 0; if (frames > 6) frames = 6;
    s_settings_interface.SetIntValue("EmuCore/GS", "VsyncQueueSize", frames);
    if (VMManager::HasValidVM()) VMManager::ApplySettings();
    if (MTGS::IsOpen()) MTGS::ApplySettings();
}

// Cenit 0.6.6: fijado de hilos al núcleo rápido. El trabajo fino lo hace el
// núcleo (VMManager::SetEmuThreadAffinities); este setter solo enciende o apaga
// el bit, y ApplySettings re-afina las afinidades al cambiar (la comprobación
// de VMManager.cpp:3512 dispara justo cuando EnableThreadPinning cambia).
extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setThreadPinning(JNIEnv* env, jclass, jboolean enabled)
{
    s_settings_interface.SetBoolValue("EmuCore", "EnableThreadPinning", enabled == JNI_TRUE);
    if (VMManager::HasValidVM()) VMManager::ApplySettings();
    if (MTGS::IsOpen()) MTGS::ApplySettings();
}

// Cenit 0.6.7: Fast CDVD y MTVU dejan de ser decisiones silenciosas del perfil.
// Ambos se escriben en la capa base (la misma que llena ApplyHardwarePerformance-
// Profile), así que el INI por-juego y el GameDB siguen teniendo prioridad por
// encima. Se aplican en caliente: CheckForCPUConfigChanges (VMManager.cpp:3355)
// detecta el cambio en la estructura de speedhacks y limpia las cachés del
// recompiler, y el hilo de VU1 lee el bit en cada bloque recompilado
// (microVU-arm64.cpp:729). Mismo camino que usa la propia UI de ajustes del
// motor (FullscreenUI_Settings.cpp:2694).
extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setFastCDVD(JNIEnv* env, jclass, jboolean enabled)
{
    s_settings_interface.SetBoolValue("EmuCore/Speedhacks", "fastCDVD", enabled == JNI_TRUE);
    if (VMManager::HasValidVM()) VMManager::ApplySettings();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setMTVU(JNIEnv* env, jclass, jboolean enabled)
{
    s_settings_interface.SetBoolValue("EmuCore/Speedhacks", "vuThread", enabled == JNI_TRUE);
    if (VMManager::HasValidVM()) VMManager::ApplySettings();
}

// Cenit 0.6.15 — Fase 1 del motor de superbloques VU: sonda de trazas.
// Medición pura (entradas al dispatcher, bloques VU1, secuencias repetidas);
// apagada por defecto y SIN forcer en la capa base (a diferencia de
// EnableVUProgramCache, arriba): es un ajuste de diagnóstico opt-in.
//
// El cambio de config cae en RecompilerOptions, así que CheckForCPUConfig-
// Changes detecta el toggle y limpia las cachés del recompiler: con la sonda
// ON se recompila todo instrumentado, y al apagarla se reconstruye el cache
// limpio ANTES de cualquier despacho (el volcado del informe ocurre en
// mVUreset, vía SyncFromConfig, dentro de esa cascada). El disco queda
// intocado en ambos flancos: Init/Save/Hydrate están guardados mientras la
// sonda está ON.
extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setVUTraceProbe(JNIEnv* env, jclass, jboolean enabled)
{
#ifdef __aarch64__
    s_settings_interface.SetBoolValue("EmuCore/CPU/Recompiler", "EnableVUTraceProbe", enabled == JNI_TRUE);
    if (VMManager::HasValidVM()) VMManager::ApplySettings();
#else
    (void)env; (void)enabled; // la sonda solo existe en el JIT arm64
#endif
}

// Lecturas para la UI: qué pide el usuario (INI) vs qué está midiendo el
// núcleo ahora mismo (efectiva). Divergen entre el toggle y el siguiente
// reset del recompiler, o si un cambio de settings se descartó sin VM.
extern "C"
JNIEXPORT jboolean JNICALL
Java_com_izzy2lost_psx2_NativeApp_getVUTraceProbeEnabled(JNIEnv*, jclass)
{
    return s_settings_interface.GetBoolValue("EmuCore/CPU/Recompiler", "EnableVUTraceProbe", false)
        ? JNI_TRUE : JNI_FALSE;
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_izzy2lost_psx2_NativeApp_getVUTraceProbeEffective(JNIEnv*, jclass)
{
#ifdef __aarch64__
    return (EmuConfig.Cpu.Recompiler.EnableVUTraceProbe && VMManager::HasValidVM()) ? JNI_TRUE : JNI_FALSE;
#else
    return JNI_FALSE;
#endif
}

// Vuelca el informe bajo demanda (botón de la UI / depuración): escribe
// logs/vu_probe.txt con el detalle y el resumen al emulog. No cambia nada.
extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_dumpVUTraceReport(JNIEnv* env, jclass, jstring reason)
{
#ifdef __aarch64__
    const std::string r = reason ? GetJavaString(env, reason) : std::string("peticion");
    mVUTraceProbe::DumpReport(r.c_str());
#else
    (void)env; (void)reason;
#endif
}

// Cenit 0.6.21 — Fases 2-5 del motor de superbloques VU (PDF de arquitectura).
// Encendido por defecto NO: el GATE del documento exige cero divergencias y
// mejora sostenida medida antes de habilitar; hasta que el usuario valide en su
// dispositivo, la bandera es experimental. Encender el motor enciende la sonda
// (la elegibilidad vive de sus contadores) y pausa la caché de programas en
// disco; apagarlo vuelca el informe (logs/vu_superblock.txt + resumen en
// emulog). El toggle cae en RecompilerOptions: CheckForCPUConfigChanges limpia
// las cachés del recompiler en ambos flancos, así que ninguna variante
// sobrevive a un cambio de bandera. Solo existe en el JIT arm64.
extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setVUSuperblock(JNIEnv* env, jclass, jboolean enabled)
{
#ifdef __aarch64__
    s_settings_interface.SetBoolValue("EmuCore/CPU/Recompiler", "EnableVUSuperblock", enabled == JNI_TRUE);
    if (VMManager::HasValidVM()) VMManager::ApplySettings();
#else
    (void)env; (void)enabled; // el motor solo existe en el JIT arm64
#endif
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_izzy2lost_psx2_NativeApp_getVUSuperblockEnabled(JNIEnv*, jclass)
{
    return s_settings_interface.GetBoolValue("EmuCore/CPU/Recompiler", "EnableVUSuperblock", false)
        ? JNI_TRUE : JNI_FALSE;
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_izzy2lost_psx2_NativeApp_getVUSuperblockEffective(JNIEnv*, jclass)
{
#ifdef __aarch64__
    // Efectiva == config aplicada con VM corriendo. El auto-apagado por
    // divergencias se espeja aquí: si el motor se mató solo, g_enabled ya es
    // false aunque el INI pida ON — la UI debe mostrar la verdad.
    return (EmuConfig.Cpu.Recompiler.EnableVUSuperblock && VMManager::HasValidVM()
        && mVUSuperblock::IsEnabled()) ? JNI_TRUE : JNI_FALSE;
#else
    return JNI_FALSE;
#endif
}

// Vuelca el informe bajo demanda (botón de la UI / depuración): escribe
// logs/vu_superblock.txt con el detalle de validación y el resumen al emulog.
// No cambia nada.
extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_dumpVUSuperblockReport(JNIEnv* env, jclass, jstring reason)
{
#ifdef __aarch64__
    const std::string r = reason ? GetJavaString(env, reason) : std::string("peticion");
    mVUSuperblock::DumpReport(r.c_str());
#else
    (void)env; (void)reason;
#endif
}

// La misma condición que usa ApplyHardwarePerformanceProfile para decidir el
// default de MTVU, expuesta a Java para que el default de la preferencia no
// pueda divergir del del núcleo.
extern "C"
JNIEXPORT jboolean JNICALL
Java_com_izzy2lost_psx2_NativeApp_coresAllowMTVU(JNIEnv*, jclass)
{
    return std::thread::hardware_concurrency() >= 3 ? JNI_TRUE : JNI_FALSE;
}

// Nitidez CAS (Contrast Adaptive Sharpening): modo 0 apagado, 1 solo enfocar,
// 2 enfocar + reescalar; nitidez 0..100.
extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setCASMode(JNIEnv* env, jclass, jint mode, jint sharpness)
{
    if (mode < 0) mode = 0; if (mode > 2) mode = 2;
    if (sharpness < 0) sharpness = 0; if (sharpness > 100) sharpness = 100;
    s_settings_interface.SetIntValue("EmuCore/GS", "CASMode", mode);
    s_settings_interface.SetIntValue("EmuCore/GS", "CASSharpness", sharpness);
    if (VMManager::HasValidVM()) VMManager::ApplySettings();
    if (MTGS::IsOpen()) MTGS::ApplySettings();
}

// Desplazamiento de medio píxel: corrige el "pixel shifting" de texturas en HW.
// 0 apagado, 1 normal, 2 especial, 3 especial agresivo, 4 nativo, 5 nativo+textura.
//
// Cenit 0.6.4 (plan del inge §1.1): escribir esto en el INI GLOBAL era una
// función fantasma — LoadCoreSettings() corre MaskUserHacks() después de cargar
// (VMManager.cpp:743) y, como este port nunca pone UserHacks=true, el valor
// vuelto a cero en cada ApplySettings. El control ahora vive por JUEGO en el
// INI de gamesettings/ (setGameUserHackInt abajo), que sí tiene prioridad.
extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setHalfPixelOffset(JNIEnv* env, jclass, jint mode)
{
    // Intencionalmente vacío desde 0.6.4: conservarlo escribiendo al vacío era
    // peor (confundía). Java ya no lo llama; se borra en la limpieza de 0.7.0.
    (void)mode;
}

// ---------------------------------------------------------------------------
// Hacks de hardware POR JUEGO (Cenit 0.6.4, plan del inge §1.1)
//
// El núcleo borra todos los UserHacks_* globales salvo que UserHacks=true, pero
// UserHacks=true global desactivaría los gsHWFixes automáticos del GameDB
// (GameDatabase.cpp:709) — rompería, por ejemplo, los 4 fixes de God of War II.
// La solución correcta es la capa por-juego: escribir UserHacks=true + los
// hacks en gamesettings/<SERIAL>_CRC.ini. Esa capa manda sobre el INI base,
// MaskUserHacks respeta los valores de la capa, y como la capa solo existe para
// ESTE juego, los demás siguen recibiendo sus fixes automáticos del DB intactos.
//
// El riesgo que el inge marcó (perder los fixes del DB al entrar en modo
// manual para ese juego) se cubre con la SIEMBRA: la primera escritura copia al
// INI del juego TODOS los gsHWFixes que el DB tiene para ese serial, para que
// el modo manual parta exactamente de lo que el DB ya aplicaba.
// ---------------------------------------------------------------------------

// Mapeo GSHWFixId -> clave INI. Los índices son los del enum GameDatabaseSchema::
// GSHWFixId (Config-side names verificados contra s_gs_hw_fix_names en
// GameDatabase.cpp:361). nullptr = fix que no requiere sembrarse: o no es un
// user hack (mipmap, PCRTC*, blending, deinterlace, texturePreloading) y el DB
// lo aplica igual con UserHacks=true, o su semántica es compuesta y sembrarlo
// a ciegas cambiaría el resultado (gpuPaletteConversion depende de
// texturePreloading; los recommended* solo elevan, no fijan).
//
// isUserHackHWFix (GameDatabase.cpp:422, estático en ese TU) dice que todo id
// >= TrilinearFiltering salvo las 3 excepciones de arriba es user hack; se
// respeta esa regla aquí por posición.
static const char* const s_game_user_hack_ini_keys[] = {
    /* AutoFlush                */ "UserHacks_AutoFlushLevel",
    /* CPUFramebufferConversion */ "UserHacks_CPU_FB_Conversion",
    /* FlushTCOnClose           */ "UserHacks_ReadTCOnClose",
    /* DisableDepthSupport      */ "UserHacks_DisableDepthSupport",
    /* PreloadFrameData         */ "preload_frame_with_gs_data",
    /* DisablePartialInvalidat. */ "UserHacks_DisablePartialInvalidation",
    /* TextureInsideRT          */ "UserHacks_TextureInsideRt",
    /* Limit24BitDepth          */ "UserHacks_Limit24BitDepth",
    /* AlignSprite              */ "UserHacks_align_sprite_X",
    /* MergeSprite              */ "UserHacks_merge_pp_sprite",
    /* Mipmap (no-sembrar)      */ nullptr,
    /* AccurateAlphaTest        */ "HWAccurateAlphaTest",
    /* ForceEvenSpritePosition  */ "UserHacks_ForceEvenSpritePosition",
    /* BilinearUpscale          */ "UserHacks_BilinearHack",
    /* NativePaletteDraw        */ "UserHacks_NativePaletteDraw",
    /* EstimateTextureRegion    */ "UserHacks_EstimateTextureRegion",
    /* DrawBuffering            */ "UserHacks_DrawBuffering",
    /* RewriteLargeSTCoords     */ "UserHacks_RewriteLargeSTCoords",
    /* PCRTCOffsets (no user)   */ nullptr,
    /* PCRTCOverscan (no user)  */ nullptr,
    /* TrilinearFiltering       */ nullptr, // "TriFilter": el DB lo trata aparte y no lo borra MaskUserHacks
    /* SkipDrawStart            */ "UserHacks_SkipDraw_Start",
    /* SkipDrawEnd              */ "UserHacks_SkipDraw_End",
    /* HalfPixelOffset          */ "UserHacks_HalfPixelOffset",
    /* RoundSprite              */ "UserHacks_round_sprite_offset",
    /* NativeScaling            */ "UserHacks_native_scaling",
    /* TexturePreloading        */ nullptr, // el DB lo trata aparte (no-user-hack)
    /* Deinterlace              */ nullptr,
    /* CPUSpriteRenderBW        */ "UserHacks_CPUSpriteRenderBW",
    /* CPUSpriteRenderLevel     */ "UserHacks_CPUSpriteRenderLevel",
    /* CPUCLUTRender            */ "UserHacks_CPUCLUTRender",
    /* GPUTargetCLUT            */ "UserHacks_GPUTargetCLUTMode",
    /* GPUPaletteConversion     */ "paltex", // MaskUserHacks sí lo borra: sembrar
    /* MinimumBlendingLevel     */ nullptr,
    /* MaximumBlendingLevel     */ nullptr,
    /* RecommendedBlendingLevel */ nullptr,
    /* RecommendedAccurateAlpha */ nullptr,
    /* RecommendedHWAA1         */ nullptr,
    /* GetSkipCount             */ nullptr,
    /* BeforeDraw               */ nullptr,
    /* MoveHandler              */ nullptr,
};

static bool GameUserHackIdsMatch(GameDatabaseSchema::GSHWFixId id, const char* key)
{
    const u32 index = static_cast<u32>(id);
    return index < std::size(s_game_user_hack_ini_keys) &&
           s_game_user_hack_ini_keys[index] &&
           StringUtil::Strcasecmp(s_game_user_hack_ini_keys[index], key) == 0;
}

static std::string ResolveGameSettingsPathForUri(const std::string& game_path)
{
    if (game_path.empty())
        return {};

    const std::string serial = GetGameSerialForPath(game_path);
    u32 crc = 0;
    if (!VMManager::isArcadeManifest(game_path))
    {
        Error error;
        auto* prev = CDVD;
        CDVD = &CDVDapi_Iso;
        if (CDVD->open(game_path, &error))
        {
            (void)DoCDVDdetectDiskType();
            cdvdGetDiscInfo(nullptr, nullptr, nullptr, nullptr, &crc, nullptr);
            DoCDVDclose();
        }
        CDVD = prev;
    }

    // Misma cascada que usa el núcleo al cargar (UpdateGameSettingsLayer):
    // SERIAL_CRC.ini, luego SERIAL.ini. Reusar la ruta ya existente en vez de
    // crear la variante con CRC evita partir el estado del juego en dos files
    // (el diálogo por-juego escribe gamesettings/SERIAL.ini desde Java).
    const std::string with_crc = VMManager::GetGameSettingsPath(serial, crc);
    if (serial.empty() || FileSystem::FileExists(with_crc.c_str()))
        return with_crc;
    const std::string plain = VMManager::GetGameSettingsPath(serial, 0);
    if (FileSystem::FileExists(plain.c_str()))
        return plain;
    return with_crc;
}

// Escritura genérica en la capa por-juego. Si la clave es un user hack conocido
// (tabla s_game_user_hack_ini_keys), primero SIEMBRA los gsHWFixes del GameDB y
// activa UserHacks=true; si no (p. ej. EECycleSkip en Speedhacks, que MaskUserHacks
// no toca), escribe directo sin activar el modo manual. Luego, si ese juego es
// el que está corriendo, recarga la capa en caliente (igual que FullscreenUI.cpp:717,
// en el hilo de emulación porque ReloadGameSettings -> ApplySettings espera MTGS/VU).
static bool WriteGameLayerInt(const std::string& game_path, const char* section,
                              const std::string& key, int value)
{
    const std::string path = ResolveGameSettingsPathForUri(game_path);
    if (path.empty() || key.empty() || !section || !*section)
        return false;

    bool is_user_hack = false;
    for (const char* const k : s_game_user_hack_ini_keys)
    {
        if (k && StringUtil::Strcasecmp(k, key.c_str()) == 0)
        {
            is_user_hack = true;
            break;
        }
    }

    INISettingsInterface game_settings(path);
    game_settings.Load(); // puede no existir todavía: carga vacío

    if (is_user_hack && !game_settings.GetBoolValue("EmuCore/GS", "UserHacks", false))
    {
        const std::string serial = GetGameSerialForPath(game_path);
        if (!serial.empty())
        {
            GameDatabase::ensureLoaded();
            if (const auto* game = GameDatabase::findGame(serial))
            {
                // Sembrar TODO fix de usuario que el DB tenga para este serial,
                // para que activar el modo manual del juego no pierda nada.
                for (const auto& [id, value_db] : game->gsHWFixes)
                {
                    const u32 index = static_cast<u32>(id);
                    if (index >= std::size(s_game_user_hack_ini_keys))
                        continue;
                    const char* ini_key = s_game_user_hack_ini_keys[index];
                    if (ini_key)
                        game_settings.SetIntValue("EmuCore/GS", ini_key, value_db);
                }
            }
        }
        game_settings.SetBoolValue("EmuCore/GS", "UserHacks", true);
    }

    game_settings.SetIntValue(section, key.c_str(), value);
    if (!game_settings.Save())
    {
        // Antes esto devolvía false en silencio y Java solo sabía que "no se
        // pudo". El log dice la ruta exacta para distinguir "el documento no se
        // creó" de "se creó pero el núcleo no lo ve" (el bug saf:// de 0.6.13).
        Console.Error("Per-game settings: NO se pudo guardar %s [%s/%s=%d]",
            path.c_str(), section, key.c_str(), value);
        return false;
    }

    Console.WriteLn("Per-game settings: written INI %s [%s/%s=%d]",
        path.c_str(), section, key.c_str(), value);

    if (VMManager::HasValidVM())
    {
        const std::string edited_serial = GetGameSerialForPath(game_path);
        if (!edited_serial.empty())
        {
            Host::RunOnCPUThread([edited_serial]() {
                if (VMManager::GetState() != VMState::Running && VMManager::GetState() != VMState::Paused)
                    return;
                if (StringUtil::Strcasecmp(VMManager::GetDiscSerial().c_str(), edited_serial.c_str()) == 0)
                    VMManager::ReloadGameSettings();
            });
        }
    }
    return true;
}

// Escribe (o siembra+escribe) un entero en el INI por-juego. Devuelve false si
// no se pudo resolver el juego. Si el VM está corriendo ESE juego, recarga la
// capa por-juego en caliente (VMManager::ReloadGameSettings).
extern "C"
JNIEXPORT jboolean JNICALL
Java_com_izzy2lost_psx2_NativeApp_setGameUserHackInt(JNIEnv* env, jclass,
                                                     jstring p_gameUri,
                                                     jstring p_key,
                                                     jint p_value)
{
    const std::string game_path = GetJavaString(env, p_gameUri);
    const std::string key = GetJavaString(env, p_key);
    return WriteGameLayerInt(game_path, "EmuCore/GS", key, (int)p_value) ? JNI_TRUE : JNI_FALSE;
}

// Cenit 0.6.5: variante con sección explícita para claves que NO son user hacks
// y viven en otra sección (EmuCore/Speedhacks: EECycleSkip/EECycleRate del modo
// cuotas). Mismo ciclo siembra/recarga que arriba, pero sin activar UserHacks
// salvo que la clave pertenezca a la tabla de hacks.
extern "C"
JNIEXPORT jboolean JNICALL
Java_com_izzy2lost_psx2_NativeApp_setGameSettingInt(JNIEnv* env, jclass,
                                                    jstring p_gameUri,
                                                    jstring p_section,
                                                    jstring p_key,
                                                    jint p_value)
{
    const std::string game_path = GetJavaString(env, p_gameUri);
    const std::string section = GetJavaString(env, p_section);
    const std::string key = GetJavaString(env, p_key);
    return WriteGameLayerInt(game_path, section.c_str(), key, (int)p_value) ? JNI_TRUE : JNI_FALSE;
}

// Lectura genérica por sección (la específica de user hacks sigue abajo).
extern "C"
JNIEXPORT jint JNICALL
Java_com_izzy2lost_psx2_NativeApp_getGameSettingInt(JNIEnv* env, jclass,
                                                    jstring p_gameUri,
                                                    jstring p_section,
                                                    jstring p_key,
                                                    jint p_fallback)
{
    const std::string game_path = GetJavaString(env, p_gameUri);
    const std::string section = GetJavaString(env, p_section);
    const std::string key = GetJavaString(env, p_key);
    const std::string path = ResolveGameSettingsPathForUri(game_path);
    if (path.empty() || key.empty() || section.empty())
        return p_fallback;

    INISettingsInterface game_settings(path);
    if (game_settings.Load())
    {
        int value = 0;
        if (game_settings.GetIntValue(section.c_str(), key.c_str(), &value))
            return value;
    }
    return p_fallback;
}

// Lectura para la UI: valor efectivo de un hack de este juego (capa por-juego;
// si no existe, lo que el DB aplicaría: se pasa por la propia tabla DB->clave).
extern "C"
JNIEXPORT jint JNICALL
Java_com_izzy2lost_psx2_NativeApp_getGameUserHackInt(JNIEnv* env, jclass,
                                                     jstring p_gameUri,
                                                     jstring p_key,
                                                     jint p_fallback)
{
    const std::string game_path = GetJavaString(env, p_gameUri);
    const std::string key = GetJavaString(env, p_key);
    const std::string path = ResolveGameSettingsPathForUri(game_path);
    if (path.empty() || key.empty())
        return p_fallback;

    INISettingsInterface game_settings(path);
    if (game_settings.Load())
    {
        int value = 0;
        if (game_settings.GetIntValue("EmuCore/GS", key.c_str(), &value))
            return value;
    }

    // Sin entrada propia: devolver lo que el GameDB fija para ese juego, si
    // lo fija (la UI debe mostrar el valor que REALMENTE está en uso).
    const std::string serial = GetGameSerialForPath(game_path);
    if (!serial.empty())
    {
        GameDatabase::ensureLoaded();
        if (const auto* game = GameDatabase::findGame(serial))
        {
            for (const auto& [id, value] : game->gsHWFixes)
            {
                if (GameUserHackIdsMatch(id, key.c_str()))
                    return value;
            }
        }
    }
    return p_fallback;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setVsyncEnabled(JNIEnv* env, jclass, jboolean enabled)
{
    s_settings_interface.SetBoolValue("EmuCore/GS", "VsyncEnable", enabled == JNI_TRUE);
    if (VMManager::HasValidVM())
        VMManager::ApplySettings();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_initialize(JNIEnv *env, jclass clazz,
                                                jstring p_szpath, jint p_apiVer) {
    std::string _szPath = GetJavaString(env, p_szpath);
    EmuFolders::AppRoot = _szPath;
    EmuFolders::DataRoot = _szPath;
    EmuFolders::SetResourcesDirectory();

    Log::SetConsoleOutputLevel(LOGLEVEL_DEBUG);
    ImGuiManager::SetFontPathAndRange(Path::Combine(EmuFolders::Resources, "fonts" FS_OSPATH_SEPARATOR_STR "Roboto-Regular.ttf"), {});

    bool _SettingsIsEmpty = s_settings_interface.IsEmpty();
    if(_SettingsIsEmpty) {
        // don't provide an ini path, or bother loading. we'll store everything in memory.
        MemorySettingsInterface &si = s_settings_interface;
        Host::Internal::SetBaseSettingsLayer(&si);

        // The achievements code stores the RetroAchievements token in the
        // secrets settings layer and dereferences it unconditionally, so it
        // must exist. Back it with an INI file so the login survives restarts.
        static std::unique_ptr<INISettingsInterface> s_secrets_settings_interface;
        s_secrets_settings_interface =
            std::make_unique<INISettingsInterface>(Path::Combine(EmuFolders::DataRoot, "secrets.ini"));
        s_secrets_settings_interface->Load();
        Host::Internal::SetSecretsSettingsLayer(s_secrets_settings_interface.get());

        // Initialize emulator folders and ensure they exist (including GameSettings)
        EmuFolders::SetDefaults(si);
        EmuFolders::LoadConfig(si);
        EmuFolders::EnsureFoldersExist();

        // Cenit 0.6.7: el núcleo abre emulog.txt en modo "wb" (common/Console.cpp:364),
        // o sea que cada arranque BORRA el registro anterior. Justo el caso que no
        // vale: si el juego se cierra durante la carga (Shadow of the Colossus), al
        // reabrir Cenit la única evidencia ya no estaría. Se rota antes de que
        // LoadStartupSettings -> UpdateLoggingSettings abra el archivo nuevo, así la
        // sesión anterior queda en emulog.prev.txt y el botón "Enviar registro"
        // puede adjuntar los dos.
        //
        // Cenit 0.6.16: el fallback anterior hacìa DeleteFilePath(cur) cuando el
        // rename fallaba — en el FUSE de Huawei (EMUI, Android 12) el rename entre
        // los archivos de la app falla con frecuencia, así que cada arranque BORRABA
        // el log de la sesión anterior en vez de rotarlo (emulog.prev.txt en 0 bytes
        // en el reporte del usuario). Ahora: rename -> si falla, COPY y deja cur
        // intacto (el "wb" del núcleo lo truncará, pero la copia ya existe); si la
        // copia también falla, no toca nada — perder la rotación es mejor que
        // perder la evidencia.
        {
            static bool s_log_rotated = false;
            const std::string cur = Path::Combine(EmuFolders::Logs, "emulog.txt");
            const std::string prev = Path::Combine(EmuFolders::Logs, "emulog.prev.txt");
            if (!s_log_rotated && FileSystem::FileExists(cur.c_str()))
            {
                s_log_rotated = true;
                if (!FileSystem::RenamePath(cur.c_str(), prev.c_str()))
                    FileSystem::CopyFilePath(cur.c_str(), prev.c_str(), true);
            }
        }

        VMManager::SetDefaultSettings(si, true, true, true, true, true);

        // Cenit 0.6.4: "FrameLimitEnable" ya NO existe como key en este núcleo
        // (el limitador vive en VMManager::UpdateTargetSpeed/Throttle). Se estaba
        // escribiendo al vacío en cada arranque; eliminado. Vsync off sigue siendo
        // el camino real para "arrancar lo más rápido posible".
        si.SetBoolValue("EmuCore/GS", "VsyncEnable", false);

        // ensure all input sources are disabled, we're not using them
        si.SetBoolValue("InputSources", "SDL", true);
        si.SetBoolValue("InputSources", "XInput", false);

        // audio output by default on Android
        si.SetStringValue("SPU2/Output", "Backend", "Oboe");
        si.SetIntValue("SPU2/Output", "BufferMS", 150);
        si.SetIntValue("SPU2/Output", "OutputLatencyMS", 40);

        // none of the bindings are going to resolve to anything
        Pad::ClearPortBindings(si, 0);
        si.ClearSection("Hotkeys");

        // force logging
        //si.SetBoolValue("Logging", "EnableSystemConsole", !s_no_console);
        si.SetBoolValue("Logging", "EnableSystemConsole", true);
        si.SetBoolValue("Logging", "EnableTimestamps", true);
        si.SetBoolValue("Logging", "EnableVerbose", true);

        // Default to a clean screen: hide HUD/OSD overlays by default
        si.SetBoolValue("EmuCore/GS", "OsdShowSpeed", false);
        si.SetBoolValue("EmuCore/GS", "OsdShowFPS", false);
        si.SetBoolValue("EmuCore/GS", "OsdShowVPS", false);
        si.SetBoolValue("EmuCore/GS", "OsdShowCPU", false);
        si.SetBoolValue("EmuCore/GS", "OsdShowGPU", false);
        si.SetBoolValue("EmuCore/GS", "OsdShowResolution", false);
        si.SetBoolValue("EmuCore/GS", "OsdShowGSStats", false);
        si.SetBoolValue("EmuCore/GS", "OsdShowIndicators", false);
        si.SetBoolValue("EmuCore/GS", "OsdShowSettings", false);
        si.SetBoolValue("EmuCore/GS", "OsdShowInputs", false);
        si.SetBoolValue("EmuCore/GS", "OsdShowFrameTimes", false);
        si.SetBoolValue("EmuCore/GS", "OsdShowVersion", false);
        si.SetBoolValue("EmuCore/GS", "OsdShowHardwareInfo", false);
        si.SetBoolValue("EmuCore/GS", "OsdShowVideoCapture", false);
        si.SetBoolValue("EmuCore/GS", "OsdShowInputRec", false);

//        // remove memory cards, so we don't have sharing violations
//        for (u32 i = 0; i < 2; i++)
//        {
//            si.SetBoolValue("MemoryCards", fmt::format("Slot{}_Enable", i + 1).c_str(), false);
//            si.SetStringValue("MemoryCards", fmt::format("Slot{}_Filename", i + 1).c_str(), "");
//        }

        // Enable RetroAchievements
        si.SetBoolValue("Achievements", "Enabled", true);
        si.SetBoolValue("Achievements", "HardcoreMode", false);
        si.SetBoolValue("Achievements", "Notifications", true);
        si.SetBoolValue("Achievements", "LeaderboardNotifications", true);
        si.SetBoolValue("Achievements", "SoundEffects", true);
        si.SetBoolValue("Achievements", "EncoreMode", false);
        si.SetBoolValue("Achievements", "SpectatorMode", false);
        si.SetBoolValue("Achievements", "UnofficialTestMode", false);
    }

    // Hardware-tuned baseline: speedhacks, readback spins, log noise and upscale
    // for this device class. Runs before LoadStartupSettings() so the values land
    // in EmuConfig too, and before Java pushes the user's saved prefs, so any
    // explicit choice in the drawer still overrides everything set here.
    ApplyHardwarePerformanceProfile();

    VMManager::Internal::LoadStartupSettings();
    
    // Initialize RetroAchievements JNI bridge
    if (!AchievementsJNI::Initialize(env))
    {
        __android_log_print(ANDROID_LOG_ERROR, "PCSX2", "Failed to initialize AchievementsJNI");
    }
    else
    {
        __android_log_print(ANDROID_LOG_INFO, "PCSX2", "AchievementsJNI initialized successfully");
    }
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_izzy2lost_psx2_NativeApp_getGameTitle(JNIEnv *env, jclass clazz,
                                                  jstring p_szpath) {
    std::string _szPath = GetJavaString(env, p_szpath);

    const GameList::Entry *entry;
    entry = GameList::GetEntryForPath(_szPath.c_str());
    // Cenit 0.6.4: GetEntryForPath devuelve null si el juego no está en la lista
    // (URI suelta, lista aún no escaneada). El código anterior desreferenciaba a
    // ciegas: crash nativo. Salida vacía y Java usa su propio resolver.
    if (!entry)
        return env->NewStringUTF("");

    std::string ret;
    ret.append(entry->title);
    ret.append("|");
    ret.append(entry->serial);
    ret.append("|");
    ret.append(StringUtil::StdStringFromFormat("%s (%08X)", entry->serial.c_str(), entry->crc));

    return env->NewStringUTF(ret.c_str());
}

// Cenit 0.6.4 (plan del inge §1.2): el port declaraba este nativo en Java pero
// nunca existió el export: cada llamada lanzaba UnsatisfiedLinkError y caía al
// resolver Java. Implementado con la misma lógica que getGameTitle.
extern "C"
JNIEXPORT jstring JNICALL
Java_com_izzy2lost_psx2_NativeApp_getGameTitleFromUri(JNIEnv *env, jclass clazz,
                                                  jstring p_szuri) {
    return Java_com_izzy2lost_psx2_NativeApp_getGameTitle(env, clazz, p_szuri);
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_izzy2lost_psx2_NativeApp_getCurrentGameSerial(JNIEnv *env, jclass clazz) {
    std::string ret = VMManager::GetDiscSerial();
    return env->NewStringUTF(ret.c_str());
}

extern "C"
JNIEXPORT jfloat JNICALL
Java_com_izzy2lost_psx2_NativeApp_getFPS(JNIEnv *env, jclass clazz) {
    return (jfloat)PerformanceMetrics::GetFPS();
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_izzy2lost_psx2_NativeApp_getPauseGameTitle(JNIEnv *env, jclass clazz) {
    std::string ret = VMManager::GetTitle(true);
    return env->NewStringUTF(ret.c_str());
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_izzy2lost_psx2_NativeApp_getPauseGameSerial(JNIEnv *env, jclass clazz) {
    std::string ret = StringUtil::StdStringFromFormat("%s (%08X)", VMManager::GetDiscSerial().c_str(), VMManager::GetDiscCRC());
    return env->NewStringUTF(ret.c_str());
}

// Cenit 0.6.7: carpeta donde el núcleo escribe emulog.txt. Java la necesita para
// poder adjuntar el registro al compartirlo — está dentro de Android/data/, que
// el usuario no puede abrir con un explorador desde Android 11.
extern "C"
JNIEXPORT jstring JNICALL
Java_com_izzy2lost_psx2_NativeApp_getLogDirectory(JNIEnv* env, jclass)
{
    return env->NewStringUTF(EmuFolders::Logs.c_str());
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_izzy2lost_psx2_NativeApp_getGameSerial(JNIEnv* env, jclass, jstring p_uri)
{
    if (!p_uri)
        return env->NewStringUTF("");
    const std::string path = GetJavaString(env, p_uri);

    // Reads the gameid out of an .acgame manifest and falls back to a direct
    // CDVD open (which supports content:// URIs) for ISO/CHD. An arcade
    // manifest is not a disc image, so probing it with CDVD yields no serial.
    return env->NewStringUTF(GetGameSerialForPath(path).c_str());
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_izzy2lost_psx2_NativeApp_getGameCrc(JNIEnv* env, jclass, jstring p_uri)
{
    if (!p_uri)
        return env->NewStringUTF("");
    std::string path = GetJavaString(env, p_uri);

    Error error;
    u32 crc = 0;
    auto* prev = CDVD;
    CDVD = &CDVDapi_Iso;
    if (CDVD->open(path, &error))
    {
        (void)DoCDVDdetectDiskType();
        cdvdGetDiscInfo(nullptr, nullptr, nullptr, nullptr, &crc, nullptr);
        DoCDVDclose();
    }
    CDVD = prev;

    const std::string crc_hex = (crc != 0) ? StringUtil::StdStringFromFormat("%08X", crc) : std::string("");
    return env->NewStringUTF(crc_hex.c_str());
}


extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setPadVibration(JNIEnv *env, jclass clazz,
                                                     jboolean p_isOnOff) {
}


extern "C" JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setPadButton(JNIEnv *env, jclass clazz,
                                                  jint p_key, jint p_range, jboolean p_keyPressed) {
    PadDualshock2::Inputs _key;
    switch (p_key) {
        case 19: _key = PadDualshock2::Inputs::PAD_UP; break;
        case 22: _key = PadDualshock2::Inputs::PAD_RIGHT; break;
        case 20: _key = PadDualshock2::Inputs::PAD_DOWN; break;
        case 21: _key = PadDualshock2::Inputs::PAD_LEFT; break;
        case 100: _key = PadDualshock2::Inputs::PAD_TRIANGLE; break;
        case 97: _key = PadDualshock2::Inputs::PAD_CIRCLE; break;
        case 96: _key = PadDualshock2::Inputs::PAD_CROSS; break;
        case 99: _key = PadDualshock2::Inputs::PAD_SQUARE; break;
        case 109: _key = PadDualshock2::Inputs::PAD_SELECT; break;
        case 108: _key = PadDualshock2::Inputs::PAD_START; break;
        case 102: _key = PadDualshock2::Inputs::PAD_L1; break;
        case 104: _key = PadDualshock2::Inputs::PAD_L2; break;
        case 103: _key = PadDualshock2::Inputs::PAD_R1; break;
        case 105: _key = PadDualshock2::Inputs::PAD_R2; break;
        case 106: _key = PadDualshock2::Inputs::PAD_L3; break;
        case 107: _key = PadDualshock2::Inputs::PAD_R3; break;
        case 110: _key = PadDualshock2::Inputs::PAD_L_UP; break;
        case 111: _key = PadDualshock2::Inputs::PAD_L_RIGHT; break;
        case 112: _key = PadDualshock2::Inputs::PAD_L_DOWN; break;
        case 113: _key = PadDualshock2::Inputs::PAD_L_LEFT; break;
        case 120: _key = PadDualshock2::Inputs::PAD_R_UP; break;
        case 121: _key = PadDualshock2::Inputs::PAD_R_RIGHT; break;
        case 122: _key = PadDualshock2::Inputs::PAD_R_DOWN; break;
        case 123: _key = PadDualshock2::Inputs::PAD_R_LEFT; break;
        default: _key = PadDualshock2::Inputs::PAD_CROSS ; break;
    }

    float value = p_keyPressed ? 1.0f : 0.0f;
    if (p_keyPressed && p_range > 0) {
        const float denom = (p_range > 255) ? 32766.0f : 255.0f;
        value = std::clamp(static_cast<float>(p_range) / denom, 0.0f, 1.0f);
    }
    Pad::SetControllerState(0, static_cast<u32>(_key), value);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_izzy2lost_psx2_NativeApp_updateTouchscreenPointer(JNIEnv*, jclass,
                                                            jfloat x, jfloat y,
                                                            jboolean pressed) {
    const JVS_MODE mode = ACJV::GetMode();
    if (ACJV::GetGameId().empty() || (mode != JVS_MODE::LIGHTGUN && mode != JVS_MODE::TOUCH))
        return JNI_FALSE;

    // The Android UI thread must not mutate InputManager, ImGui or USB device
    // state while the CPU/GS threads are polling and rendering. The queue is
    // drained by Host::PumpMessagesOnCPUThread() at the next input poll.
    QueueTouchscreenPointerUpdate(x, y, pressed == JNI_TRUE);

    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_resetKeyStatus(JNIEnv *env, jclass clazz) {
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setEnableCheats(JNIEnv *env, jclass clazz,
                                                     jboolean p_isonoff) {
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setAspectRatio(JNIEnv *env, jclass clazz,
                                                    jint p_type) {
    // AspectRatio values: 0=Stretch, 1=Auto 4:3/3:2, 2=4:3, 3=16:9, 4=10:7
    const char* aspect_ratio_names[] = {
        "Stretch",
        "Auto 4:3/3:2", 
        "4:3",
        "16:9",
        "10:7"
    };
    
    if (p_type >= 0 && p_type < 5) {
        s_settings_interface.SetStringValue("EmuCore/GS", "AspectRatio", aspect_ratio_names[p_type]);
        
        // Apply settings immediately if emulation is running
        if (VMManager::HasValidVM()) {
            VMManager::ApplySettings();
        }
    }
}

// Cenit 0.6.3: estos tres stubs venían vacíos del port (botones zombi que no
// hacían nada). Quedan implementados escribiendo el INI y aplicando en caliente,
// igual que los demás setters. El recompilador ARM64 lee EECycleRate bloque a
// bloque, así que el cambio se nota sin reiniciar el juego.

// Cenit 0.6.4 (plan del inge §1.3): antes era un stub vacío. Ahora cablea el
// modo de limitador REAL del núcleo (VMManager::SetLimiterMode, mismo que usa
// el fast-forward de arriba). 0=Normal 1=Turbo 2=Cámara lenta 3=Sin límite.
// No hay key INI: es estado de runtime, y así se documenta. Se aplica en el
// hilo de emulación para no pelearse con el limiter mientras corre.
extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_speedhackLimitermode(JNIEnv *env, jclass clazz,
                                                          jint p_value) {
    if (p_value < 0 || p_value > 3)
        p_value = 0;
    const LimiterModeType mode = static_cast<LimiterModeType>(p_value);
    if (!VMManager::HasValidVM())
        return;
    Host::RunOnCPUThread([mode]() {
        if (VMManager::GetState() == VMState::Running || VMManager::GetState() == VMState::Paused)
            VMManager::SetLimiterMode(mode);
    });
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_speedhackEecyclerate(JNIEnv *env, jclass clazz,
                                                          jint p_value) {
    // 0 = normal; -1/-2/-3 = 75/60/50% (menos carga por cuadro), 1..3 = turbo.
    if (p_value < -3) p_value = -3;
    if (p_value > 3) p_value = 3;
    s_settings_interface.SetIntValue("EmuCore/Speedhacks", "EECycleRate", (int)p_value);
    if (VMManager::HasValidVM()) VMManager::ApplySettings();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_speedhackEecycleskip(JNIEnv *env, jclass clazz,
                                                          jint p_value) {
    if (p_value < 0) p_value = 0;
    if (p_value > 3) p_value = 3;
    s_settings_interface.SetIntValue("EmuCore/Speedhacks", "EECycleSkip", (int)p_value);
    if (VMManager::HasValidVM()) VMManager::ApplySettings();
}

// Velocidad real de emulación en % (100 = a tiempo). El governor de resolución
// dinámica en Java lee esto una vez cada pocos ticks.
extern "C"
JNIEXPORT jfloat JNICALL
Java_com_izzy2lost_psx2_NativeApp_getEmulationSpeed(JNIEnv *env, jclass clazz) {
    return (jfloat)PerformanceMetrics::GetSpeed();
}

// Resolución interna realmente en uso. Puede no coincidir con lo que se pidió:
// la capa de ajustes por juego (UpdateGameSettingsLayer) tiene prioridad sobre el
// INI global, y el regidor necesita saberlo para no insistir sobre oídos sordos.
extern "C"
JNIEXPORT jfloat JNICALL
Java_com_izzy2lost_psx2_NativeApp_getEffectiveUpscale(JNIEnv *env, jclass clazz) {
    return (jfloat)EmuConfig.GS.UpscaleMultiplier;
}

// Cenit 0.6.12: Ciclo EE (EECycleRate) REALMENTE en uso, capa por-juego incluida.
// Con VM válido, EmuConfig ya refleja la cascada completa (perfil de hardware ->
// INI global -> INI por-juego), así que esto es la verdad operativa: el juego con
// su -1 guardado congela al regidor igual que lo hacía el -1 global, y un 0
// escrito a mano en el INI del juego deja de tener al regidor dormido para siempre.
// Sin VM (consola apagada, juego todavía arrancando) se lee el INI global: es lo
// que se va a aplicar en cuanto el VM nazca, y evita una ventana de un segundo en
// la que el regidor se creería que todo va al 100% durante el logo de PS2.
extern "C"
JNIEXPORT jint JNICALL
Java_com_izzy2lost_psx2_NativeApp_getEffectiveEECycleRate(JNIEnv *env, jclass clazz) {
    if (VMManager::HasValidVM())
        return (jint)EmuConfig.Speedhacks.EECycleRate;
    int rate = 0;
    s_settings_interface.GetIntValue("EmuCore/Speedhacks", "EECycleRate", &rate);
    return (jint)rate;
}

// Cenit 0.6.4 (plan del inge §5): porcentaje de uso de GPU (misma métrica que
// muestra el HUD). El regidor v2 la compara con la velocidad de emulación para
// saber si el bache es de GPU (bajar resolución ayuda) o de CPU emulada (bajar
// resolución NO ayuda: solo empeora la imagen gratis).
extern "C"
JNIEXPORT jfloat JNICALL
Java_com_izzy2lost_psx2_NativeApp_getGPUUsage(JNIEnv *env, jclass clazz) {
    return (jfloat)PerformanceMetrics::GetGPUUsage();
}

// Tiempo medio de GPU por frame en ms. 0 = sin device GS abierto todavía.
extern "C"
JNIEXPORT jfloat JNICALL
Java_com_izzy2lost_psx2_NativeApp_getGPUAverageTime(JNIEnv *env, jclass clazz) {
    return (jfloat)PerformanceMetrics::GetGPUAverageTime();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_renderUpscalemultiplier(JNIEnv *env, jclass clazz,
                                                             jfloat p_value) {
    if (p_value < 1.0f) p_value = 1.0f;  // Ensure minimum 1x
    if (p_value > 12.0f) p_value = 12.0f; // Cap at maximum 12x
    
    s_settings_interface.SetFloatValue("EmuCore/GS", "upscale_multiplier", p_value);
    
    // Apply the settings immediately if emulation is running
    if (VMManager::HasValidVM()) {
        VMManager::ApplySettings();
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setWidescreenPatches(JNIEnv *env, jclass clazz,
                                                          jboolean p_enabled) {
    s_settings_interface.SetBoolValue("EmuCore", "EnableWideScreenPatches", p_enabled);
    
    // Apply the settings immediately if emulation is running
    if (VMManager::HasValidVM()) {
        VMManager::ApplySettings();
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setNoInterlacingPatches(JNIEnv *env, jclass clazz,
                                                            jboolean p_enabled) {
    s_settings_interface.SetBoolValue("EmuCore", "EnableNoInterlacingPatches", p_enabled);
    
    // Apply the settings immediately if emulation is running
    if (VMManager::HasValidVM()) {
        VMManager::ApplySettings();
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setLoadTextures(JNIEnv *env, jclass clazz,
                                                   jboolean p_enabled) {
    s_settings_interface.SetBoolValue("EmuCore/GS", "LoadTextureReplacements", p_enabled);
    
    // Apply the settings immediately if emulation is running
    if (VMManager::HasValidVM()) {
        VMManager::ApplySettings();
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_reloadTextureReplacements(JNIEnv*, jclass) {
    if (!VMManager::HasValidVM() || !MTGS::IsOpen())
        return;

    MTGS::RunOnGSThread([]() {
        if (!g_gs_renderer || !GSConfig.LoadTextureReplacements)
            return;

        GSTextureReplacements::ReloadReplacementMap();
        g_gs_renderer->PurgeTextureCache(true, false, true);
    });
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setAsyncTextureLoading(JNIEnv *env, jclass clazz,
                                                          jboolean p_enabled) {
    s_settings_interface.SetBoolValue("EmuCore/GS", "LoadTextureReplacementsAsync", p_enabled);
    
    // Apply the settings immediately if emulation is running
    if (VMManager::HasValidVM()) {
        VMManager::ApplySettings();
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setPrecacheTextureReplacements(JNIEnv *env, jclass clazz,
                                                                 jboolean p_enabled) {
    s_settings_interface.SetBoolValue("EmuCore/GS", "PrecacheTextureReplacements", p_enabled);

    // Apply the settings immediately if emulation is running
    if (VMManager::HasValidVM()) {
        VMManager::ApplySettings();
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setShadeBoost(JNIEnv *env, jclass clazz,
                                                 jboolean p_enabled) {
    s_settings_interface.SetBoolValue("EmuCore/GS", "ShadeBoost", p_enabled);
    
    if (VMManager::HasValidVM()) {
        VMManager::ApplySettings();
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setShadeBoostBrightness(JNIEnv *env, jclass clazz,
                                                           jint p_brightness) {
    int brightness = std::max(1, std::min(100, (int)p_brightness));
    s_settings_interface.SetIntValue("EmuCore/GS", "ShadeBoost_Brightness", brightness);
    
    if (VMManager::HasValidVM()) {
        VMManager::ApplySettings();
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setShadeBoostContrast(JNIEnv *env, jclass clazz,
                                                         jint p_contrast) {
    int contrast = std::max(1, std::min(100, (int)p_contrast));
    s_settings_interface.SetIntValue("EmuCore/GS", "ShadeBoost_Contrast", contrast);
    
    if (VMManager::HasValidVM()) {
        VMManager::ApplySettings();
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setShadeBoostSaturation(JNIEnv *env, jclass clazz,
                                                           jint p_saturation) {
    int saturation = std::max(1, std::min(100, (int)p_saturation));
    s_settings_interface.SetIntValue("EmuCore/GS", "ShadeBoost_Saturation", saturation);
    
    if (VMManager::HasValidVM()) {
        VMManager::ApplySettings();
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setEdgeCrop(JNIEnv *env, jclass clazz,
                                               jint p_pixels) {
    // Trims junk columns the PS2 left at the edges of the visible framebuffer, which
    // a CRT's overscan hid. Cropped in native PS2 pixels, applied symmetrically so the
    // picture stays centred; the core rescales the remaining area to fill the display.
    const int pixels = std::max(0, std::min(32, (int)p_pixels));
    s_settings_interface.SetIntValue("EmuCore/GS", "CropLeft", pixels);
    s_settings_interface.SetIntValue("EmuCore/GS", "CropRight", pixels);

    if (VMManager::HasValidVM()) {
        VMManager::ApplySettings();
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setAudioOutputDevice(JNIEnv *env, jclass clazz,
                                                       jint p_device_id) {
    // Pins game audio to a specific AAudio output device. Needed because a USB
    // controller that exposes an audio interface (e.g. the Amazon Luna pad) makes
    // Android route everything to the controller's headphone jack. A non-positive id
    // means "no preference", restoring stock system routing.
    //
    // The id rides in SPU2/Output DeviceName because SPU2 already recreates the output
    // stream when that value changes, so a switch takes effect while a game is running.
    if (p_device_id > 0) {
        s_settings_interface.SetStringValue("SPU2/Output", "DeviceName",
            StringUtil::StdStringFromFormat("%d", (int)p_device_id).c_str());
    } else {
        s_settings_interface.SetStringValue("SPU2/Output", "DeviceName", "");
    }

    if (VMManager::HasValidVM()) {
        VMManager::ApplySettings();
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_saveGameSettings(JNIEnv *env, jclass clazz, jstring p_filename, 
                                                     jint p_blending_accuracy, jint p_renderer, 
                                                     jint p_resolution, jboolean p_widescreen_patches,
                                                     jboolean p_no_interlacing_patches, jboolean p_enable_patches,
                                                     jboolean p_enable_cheats)
{
    if (!p_filename)
        return;

    const char* filename_chars = env->GetStringUTFChars(p_filename, nullptr);
    if (!filename_chars)
        return;

    // Use DataRoot directly for game settings to ensure write permissions
    std::string settings_dir = Path::Combine(EmuFolders::DataRoot, "gamesettings");
    std::string settings_path = Path::Combine(settings_dir, filename_chars);
    env->ReleaseStringUTFChars(p_filename, filename_chars);

    // Debug logging
    printf("PCSX2: Saving game settings to: %s\n", settings_path.c_str());
    printf("PCSX2: Settings directory: %s\n", settings_dir.c_str());
    printf("PCSX2: Blending: %d, Renderer: %d, Resolution: %d\n", p_blending_accuracy, p_renderer, p_resolution);
    printf("PCSX2: Widescreen: %d, NoInterlacing: %d, Patches: %d, Cheats: %d\n", 
           p_widescreen_patches, p_no_interlacing_patches, p_enable_patches, p_enable_cheats);

    // Ensure directory exists
    FileSystem::CreateDirectoryPath(settings_dir.c_str(), false);

    // Build and write INI content directly to avoid any ambiguous formatting
    const char* renderers[] = {"Auto", "Vulkan", "OpenGL", "Software"};

    std::string ini;
    ini.reserve(512);
    ini += "[EmuCore/GS]\n";
    // Renderer
    if (p_renderer >= 0 && p_renderer < 4)
        ini += std::string("Renderer=") + renderers[p_renderer] + "\n";
    // Resolution scale (1..8)
    if (p_resolution >= 0 && p_resolution <= 7)
    {
        float multiplier = 1.0f + (float)p_resolution;
        ini += "upscale_multiplier=" + StringUtil::StdStringFromFormat("%.2f", multiplier) + "\n";
    }
    // Blending accuracy as numeric (0..5)
    if (p_blending_accuracy >= 0 && p_blending_accuracy < 6)
        ini += std::string("accurate_blending_unit=") + StringUtil::StdStringFromFormat("%d", p_blending_accuracy) + "\n";

    ini += "\n[EmuCore]\n";
    ini += std::string("EnableWideScreenPatches=") + (p_widescreen_patches ? "true" : "false") + "\n";
    ini += std::string("EnableNoInterlacingPatches=") + (p_no_interlacing_patches ? "true" : "false") + "\n";
    ini += std::string("EnablePatches=") + (p_enable_patches ? "true" : "false") + "\n";
    ini += std::string("EnableCheats=") + (p_enable_cheats ? "true" : "false") + "\n";

    const bool ok = FileSystem::WriteStringToFile(settings_path.c_str(), ini);
    printf("PCSX2: Settings write %s: %s\n", ok ? "OK" : "FAILED", settings_path.c_str());
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_saveGameSettingsToPath(JNIEnv *env, jclass clazz, jstring p_full_path, 
                                                           jint p_blending_accuracy, jint p_renderer, 
                                                           jint p_resolution, jboolean p_widescreen_patches,
                                                           jboolean p_no_interlacing_patches, jboolean p_enable_patches,
                                                           jboolean p_enable_cheats)
{
    if (!p_full_path)
        return;

    const char* path_chars = env->GetStringUTFChars(p_full_path, nullptr);
    if (!path_chars)
        return;

    std::string settings_path(path_chars);
    env->ReleaseStringUTFChars(p_full_path, path_chars);

    // Debug logging
    printf("PCSX2: Saving game settings to full path: %s\n", settings_path.c_str());
    printf("PCSX2: Blending: %d, Renderer: %d, Resolution: %d\n", p_blending_accuracy, p_renderer, p_resolution);
    printf("PCSX2: Widescreen: %d, NoInterlacing: %d, Patches: %d, Cheats: %d\n", 
           p_widescreen_patches, p_no_interlacing_patches, p_enable_patches, p_enable_cheats);

    // Ensure parent directory exists
    std::string parent_dir(Path::GetDirectory(settings_path));
    printf("PCSX2: Parent directory: %s\n", parent_dir.c_str());
    bool dir_created = FileSystem::CreateDirectoryPath(parent_dir.c_str(), false);
    printf("PCSX2: Directory creation result: %s\n", dir_created ? "SUCCESS" : "FAILED");

    // Check if we can write to the directory
    bool can_write = FileSystem::DirectoryExists(parent_dir.c_str());
    printf("PCSX2: Directory exists: %s\n", can_write ? "YES" : "NO");

    INISettingsInterface game_settings(settings_path);
    
    // Blending accuracy (0=Minimum, 1=Basic, 2=Medium, 3=High, 4=Full, 5=Maximum)
    const char* blend_levels[] = {"Minimum", "Basic", "Medium", "High", "Full", "Maximum"};
    if (p_blending_accuracy >= 0 && p_blending_accuracy < 6) {
        game_settings.SetStringValue("EmuCore/GS", "accurate_blending_unit", blend_levels[p_blending_accuracy]);
    }

    // Renderer (0=Auto, 1=Vulkan, 2=OpenGL, 3=Software)
    const char* renderers[] = {"Auto", "Vulkan", "OpenGL", "Software"};
    if (p_renderer >= 0 && p_renderer < 4) {
        game_settings.SetStringValue("EmuCore/GS", "Renderer", renderers[p_renderer]);
    }

    // Resolution multiplier (same as global scale entries)
    if (p_resolution >= 0 && p_resolution <= 7) {
        float multiplier = 1.0f + (float)p_resolution;
        game_settings.SetFloatValue("EmuCore/GS", "upscale_multiplier", multiplier);
    }

    // Patches
    game_settings.SetBoolValue("EmuCore", "EnableWideScreenPatches", p_widescreen_patches);
    game_settings.SetBoolValue("EmuCore", "EnableNoInterlacingPatches", p_no_interlacing_patches);
    game_settings.SetBoolValue("EmuCore", "EnablePatches", p_enable_patches);
    game_settings.SetBoolValue("EmuCore", "EnableCheats", p_enable_cheats);

    // Test basic file write first
    std::FILE* test_file = std::fopen(settings_path.c_str(), "w");
    if (test_file) {
        fprintf(test_file, "# Test file write\n");
        std::fclose(test_file);
        printf("PCSX2: Basic file write test: SUCCESS\n");
    } else {
        printf("PCSX2: Basic file write test: FAILED - errno: %d\n", errno);
        return;
    }

    bool save_result = game_settings.Save();
    printf("PCSX2: Settings save result: %s\n", save_result ? "SUCCESS" : "FAILED");
    
    // Check if file actually exists and has content
    if (FileSystem::FileExists(settings_path.c_str())) {
        s64 file_size = FileSystem::GetPathFileSize(settings_path.c_str());
        printf("PCSX2: File exists with size: %lld bytes\n", static_cast<long long>(file_size));
    } else {
        printf("PCSX2: File does not exist after save attempt\n");
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_deleteGameSettings(JNIEnv *env, jclass clazz, jstring p_filename)
{
    if (!p_filename)
        return;

    const char* filename_chars = env->GetStringUTFChars(p_filename, nullptr);
    if (!filename_chars)
        return;

    // Use DataRoot directly for game settings to ensure write permissions
    std::string settings_dir = Path::Combine(EmuFolders::DataRoot, "gamesettings");
    std::string settings_path = Path::Combine(settings_dir, filename_chars);
    env->ReleaseStringUTFChars(p_filename, filename_chars);

    if (FileSystem::FileExists(settings_path.c_str())) {
        FileSystem::DeleteFilePath(settings_path.c_str());
    }
}



extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_renderMipmap(JNIEnv *env, jclass clazz,
                                                  jint p_value) {
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_renderHalfpixeloffset(JNIEnv *env, jclass clazz,
                                                           jint p_value) {
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_renderPreloading(JNIEnv *env, jclass clazz,
                                                      jint p_value) {
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_renderGpu(JNIEnv *env, jclass clazz,
                                               jint p_value) {
    // Accept -1(Auto), 12(OpenGL), 13(Software), 14(Vulkan)
    if (p_value != -1 && p_value != 12 && p_value != 13 && p_value != 14)
    {
        return;
    }
    p_value = NormalizeAndroidRenderer(p_value);

    // Persist to base settings and apply immediately if possible
    s_settings_interface.SetIntValue("EmuCore/GS", "Renderer", (int)p_value);
    EmuConfig.GS.Renderer = static_cast<GSRendererType>(p_value);
    if (MTGS::IsOpen())
        MTGS::ApplySettings();
}

// Apply a set of global settings in one shot to avoid repeated ApplySettings calls
extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_applyGlobalSettingsBatch(JNIEnv* env, jclass,
                                                            jint renderer,
                                                            jfloat upscaleMultiplier,
                                                            jint aspectRatio,
                                                            jint blendingAccuracy,
                                                            jboolean widescreenPatches,
                                                            jboolean noInterlacingPatches,
                                                            jboolean loadTextures,
                                                            jboolean asyncTextureLoading,
                                                            jboolean vsyncEnabled,
                                                            jboolean hudVisible)
{
	renderer = NormalizeAndroidRenderer(renderer);
    // Clamp/normalize
    if (upscaleMultiplier < 1.0f) upscaleMultiplier = 1.0f;
    if (upscaleMultiplier > 12.0f) upscaleMultiplier = 12.0f;
    if (blendingAccuracy < 0) blendingAccuracy = 0; if (blendingAccuracy > 5) blendingAccuracy = 5;
    if (aspectRatio < 0) aspectRatio = 0; if (aspectRatio > 4) aspectRatio = 4; // 0..4 valid

    // Update in-memory settings layer
    // Renderer may be -1 (Auto) or 12/13/14; store and set into EmuConfig for immediate effect
    s_settings_interface.SetIntValue("EmuCore/GS", "Renderer", (int)renderer);
    EmuConfig.GS.Renderer = static_cast<GSRendererType>(renderer);

    s_settings_interface.SetFloatValue("EmuCore/GS", "upscale_multiplier", upscaleMultiplier);

    // Aspect ratio as string per existing helpers
    const char* aspect_ratio_names[] = { "Stretch", "Auto 4:3/3:2", "4:3", "16:9", "10:7" };
    s_settings_interface.SetStringValue("EmuCore/GS", "AspectRatio", aspect_ratio_names[aspectRatio]);

    // Blending accuracy numeric string 0..5
    s_settings_interface.SetStringValue("EmuCore/GS", "accurate_blending_unit",
        StringUtil::StdStringFromFormat("%d", (int)blendingAccuracy).c_str());

    // Widescreen, interlacing, textures
    s_settings_interface.SetBoolValue("EmuCore", "EnableWideScreenPatches", (widescreenPatches == JNI_TRUE));
    s_settings_interface.SetBoolValue("EmuCore", "EnableNoInterlacingPatches", (noInterlacingPatches == JNI_TRUE));
    s_settings_interface.SetBoolValue("EmuCore/GS", "LoadTextureReplacements", (loadTextures == JNI_TRUE));
    s_settings_interface.SetBoolValue("EmuCore/GS", "LoadTextureReplacementsAsync", (asyncTextureLoading == JNI_TRUE));
    s_settings_interface.SetBoolValue("EmuCore/GS", "VsyncEnable", (vsyncEnabled == JNI_TRUE));

    // HUD/OSD bundle
    const bool hv = (hudVisible == JNI_TRUE);
    s_settings_interface.SetBoolValue("EmuCore/GS", "OsdShowSpeed", hv);
    s_settings_interface.SetBoolValue("EmuCore/GS", "OsdShowFPS", hv);
    s_settings_interface.SetBoolValue("EmuCore/GS", "OsdShowVPS", hv);
    s_settings_interface.SetBoolValue("EmuCore/GS", "OsdShowCPU", hv);
    s_settings_interface.SetBoolValue("EmuCore/GS", "OsdShowGPU", hv);
    s_settings_interface.SetBoolValue("EmuCore/GS", "OsdShowResolution", hv);
    s_settings_interface.SetBoolValue("EmuCore/GS", "OsdShowGSStats", hv);
    s_settings_interface.SetBoolValue("EmuCore/GS", "OsdShowIndicators", hv);
    s_settings_interface.SetBoolValue("EmuCore/GS", "OsdShowSettings", hv);
    s_settings_interface.SetBoolValue("EmuCore/GS", "OsdShowInputs", hv);
    s_settings_interface.SetBoolValue("EmuCore/GS", "OsdShowFrameTimes", hv);
    s_settings_interface.SetBoolValue("EmuCore/GS", "OsdShowVersion", hv);
    s_settings_interface.SetBoolValue("EmuCore/GS", "OsdShowHardwareInfo", hv);
    s_settings_interface.SetBoolValue("EmuCore/GS", "OsdShowVideoCapture", hv);
    s_settings_interface.SetBoolValue("EmuCore/GS", "OsdShowInputRec", hv);

    // Apply once
    if (VMManager::HasValidVM())
        VMManager::ApplySettings();
    if (MTGS::IsOpen())
        MTGS::ApplySettings();
}

// Apply per-game settings quickly without touching global-only fields
extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_applyPerGameSettingsBatch(JNIEnv* env, jclass,
                                                             jint renderer,
                                                             jfloat upscaleMultiplier,
                                                             jint blendingAccuracy,
                                                             jboolean widescreenPatches,
                                                             jboolean noInterlacingPatches,
                                                             jboolean enablePatches,
                                                             jboolean enableCheats)
{
	renderer = NormalizeAndroidRenderer(renderer);
    if (upscaleMultiplier < 1.0f) upscaleMultiplier = 1.0f;
    if (upscaleMultiplier > 12.0f) upscaleMultiplier = 12.0f;
    if (blendingAccuracy < 0) blendingAccuracy = 0; if (blendingAccuracy > 5) blendingAccuracy = 5;

    // Renderer (allow -1/12/13/14)
    s_settings_interface.SetIntValue("EmuCore/GS", "Renderer", (int)renderer);
    EmuConfig.GS.Renderer = static_cast<GSRendererType>(renderer);

    // Core per-game options
    s_settings_interface.SetFloatValue("EmuCore/GS", "upscale_multiplier", upscaleMultiplier);
    s_settings_interface.SetStringValue("EmuCore/GS", "accurate_blending_unit",
        StringUtil::StdStringFromFormat("%d", (int)blendingAccuracy).c_str());
    s_settings_interface.SetBoolValue("EmuCore", "EnableWideScreenPatches", (widescreenPatches == JNI_TRUE));
    s_settings_interface.SetBoolValue("EmuCore", "EnableNoInterlacingPatches", (noInterlacingPatches == JNI_TRUE));
    s_settings_interface.SetBoolValue("EmuCore", "EnablePatches", (enablePatches == JNI_TRUE));
    s_settings_interface.SetBoolValue("EmuCore", "EnableCheats", (enableCheats == JNI_TRUE));

    // Apply once
    if (VMManager::HasValidVM())
        VMManager::ApplySettings();
    if (MTGS::IsOpen())
        MTGS::ApplySettings();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_onNativeSurfaceCreated(JNIEnv *env, jclass clazz) {
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_onNativeSurfaceChanged(JNIEnv *env, jclass clazz,
                                                            jobject p_surface, jint p_width, jint p_height) {
    ANativeWindow* new_window = nullptr;
    if(p_surface != nullptr) {
        new_window = ANativeWindow_fromSurface(env, p_surface);
    }

    ANativeWindow* old_window = nullptr;
    {
        std::lock_guard lock(s_window_mutex);
        old_window = s_window;
        s_window = new_window;
        s_window_width = (new_window && p_width > 0) ? p_width : 0;
        s_window_height = (new_window && p_height > 0) ? p_height : 0;
    }

    if(old_window) {
        ANativeWindow_release(old_window);
    }

    if(MTGS::IsOpen()) {
        MTGS::UpdateDisplayWindow();
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_onNativeSurfaceDestroyed(JNIEnv *env, jclass clazz) {
    ANativeWindow* old_window = nullptr;
    {
        std::lock_guard lock(s_window_mutex);
        old_window = s_window;
        s_window = nullptr;
        s_window_width = 0;
        s_window_height = 0;
    }

    if(old_window) {
        ANativeWindow_release(old_window);
    }
}


extern "C"
JNIEXPORT jint JNICALL
Java_com_izzy2lost_psx2_NativeApp_getCurrentRenderer(JNIEnv*, jclass)
{
    return static_cast<jint>(EmuConfig.GS.Renderer);
}


std::optional<WindowInfo> Host::AcquireRenderWindow(bool recreate_window)
{
    ANativeWindow* window = nullptr;
    ANativeWindow* previous_render_window = nullptr;
    int window_width = 0;
    int window_height = 0;

    {
        std::lock_guard lock(s_window_mutex);
        if(!s_window || s_window_width <= 0 || s_window_height <= 0) {
            previous_render_window = s_render_window;
            s_render_window = nullptr;
        } else {
            window = s_window;
            ANativeWindow_acquire(window);
            previous_render_window = s_render_window;
            s_render_window = window;
            window_width = s_window_width;
            window_height = s_window_height;
        }
    }

    if(previous_render_window) {
        ANativeWindow_release(previous_render_window);
    }

    if(!window) {
        WindowInfo _windowInfo;
        memset(&_windowInfo, 0, sizeof(_windowInfo));
        _windowInfo.type = WindowInfo::Type::Surfaceless;
        _windowInfo.surface_scale = 1.0f;
        return _windowInfo;
    }

    float _fScale = 1.0;
    if (window_width > 0 && window_height > 0) {
        int _nSize = window_width;
        if (window_width <= window_height) {
            _nSize = window_height;
        }
        _fScale = (float)_nSize / 800.0f;
    }
    ////
    WindowInfo _windowInfo;
    memset(&_windowInfo, 0, sizeof(_windowInfo));
    _windowInfo.type = WindowInfo::Type::Android;
    _windowInfo.surface_width = window_width;
    _windowInfo.surface_height = window_height;
    _windowInfo.surface_scale = _fScale;
    _windowInfo.window_handle = window;

    return _windowInfo;
}

void Host::ReleaseRenderWindow()
{
    ANativeWindow* window = nullptr;
    {
        std::lock_guard lock(s_window_mutex);
        window = s_render_window;
        s_render_window = nullptr;
    }

    if(window) {
        ANativeWindow_release(window);
    }

}

static s32 s_loop_count = 1;

// Owned by the GS thread.
static u32 s_dump_frame_number = 0;
static u32 s_loop_number = s_loop_count;
static double s_last_internal_draws = 0;
static double s_last_draws = 0;
static double s_last_render_passes = 0;
static double s_last_barriers = 0;
static double s_last_copies = 0;
static double s_last_uploads = 0;
static double s_last_readbacks = 0;
static u64 s_total_internal_draws = 0;
static u64 s_total_draws = 0;
static u64 s_total_render_passes = 0;
static u64 s_total_barriers = 0;
static u64 s_total_copies = 0;
static u64 s_total_uploads = 0;
static u64 s_total_readbacks = 0;
static u32 s_total_frames = 0;
static u32 s_total_drawn_frames = 0;

void Host::BeginPresentFrame() {
    if (GSIsHardwareRenderer())
    {
        const u32 last_draws = s_total_internal_draws;
        const u32 last_uploads = s_total_uploads;

        static constexpr auto update_stat = [](GSPerfMon::counter_t counter, u64& dst, double& last) {
            // perfmon resets every 30 frames to zero
            const double val = g_perfmon.GetCounter(counter);
            dst += static_cast<u64>((val < last) ? val : (val - last));
            last = val;
        };

        update_stat(GSPerfMon::Draw, s_total_internal_draws, s_last_internal_draws);
        update_stat(GSPerfMon::DrawCalls, s_total_draws, s_last_draws);
        update_stat(GSPerfMon::RenderPasses, s_total_render_passes, s_last_render_passes);
        update_stat(GSPerfMon::Barriers, s_total_barriers, s_last_barriers);
        update_stat(GSPerfMon::TextureCopies, s_total_copies, s_last_copies);
        update_stat(GSPerfMon::TextureUploads, s_total_uploads, s_last_uploads);
        update_stat(GSPerfMon::Readbacks, s_total_readbacks, s_last_readbacks);

        const bool idle_frame = s_total_frames && (last_draws == s_total_internal_draws && last_uploads == s_total_uploads);

        if (!idle_frame)
            s_total_drawn_frames++;

        s_total_frames++;

        std::atomic_thread_fence(std::memory_order_release);
    }
}

void Host::OnGameChanged(const std::string& title, const std::string& elf_override, const std::string& disc_path,
                         const std::string& disc_serial, u32 disc_crc, u32 current_crc) {
}

void Host::PumpMessagesOnCPUThread() {
    // Drain queued CPU-thread work first: the arcade pointer handling below returns
    // early for non-arcade games, which would otherwise strand these tasks forever.
    for (;;)
    {
        std::function<void()> task;
        {
            std::lock_guard lock(s_cpu_thread_task_mutex);
            if (s_cpu_thread_tasks.empty())
                break;
            task = std::move(s_cpu_thread_tasks.front());
            s_cpu_thread_tasks.pop_front();
        }
        task();
    }

    std::deque<TouchscreenPointerUpdate> updates;
    {
        std::lock_guard lock(s_touchscreen_pointer_mutex);
        updates.swap(s_touchscreen_pointer_updates);
    }

    const JVS_MODE mode = ACJV::GetMode();
    if (ACJV::GetGameId().empty() || (mode != JVS_MODE::LIGHTGUN && mode != JVS_MODE::TOUCH))
        return;

    // Arcade light-gun manifests attach GunCon2 on USB1. Its trigger binding
    // also forwards the correct per-game trigger bit to the JVS board.
    static constexpr u32 GUNCON2_TRIGGER_BIND = 13;
    for (const TouchscreenPointerUpdate& update : updates)
    {
        // Android sends coordinates normalized to its SurfaceView. Convert to
        // the current native presentation size here so display-resolution and
        // orientation changes cannot offset the gun aim.
        const float window_x = update.x * static_cast<float>(ImGuiManager::GetWindowWidth());
        const float window_y = update.y * static_cast<float>(ImGuiManager::GetWindowHeight());
        InputManager::UpdatePointerAbsolutePosition(0, window_x, window_y);
        InputManager::InvokeEvents(InputManager::MakePointerButtonKey(0, 0), update.pressed ? 1.0f : 0.0f);

        if (mode == JVS_MODE::LIGHTGUN)
        {
            // GunCon2 A is the cabinet pedal for Time Crisis/Cobra. Raise the
            // pedal before pulling the trigger, and release the trigger before
            // returning to cover. Games without a pedal retain normal trigger-
            // only touchscreen behavior (notably Vampire Night).
            static constexpr u32 GUNCON2_PEDAL_BIND = 3;
            const float value = update.pressed ? 1.0f : 0.0f;
            const bool has_pedal = (ACJV::GetGunMapping().pedal != 0);
            if (update.pressed && has_pedal)
                USB::SetDeviceBindValue(0, GUNCON2_PEDAL_BIND, value);
            USB::SetDeviceBindValue(0, GUNCON2_TRIGGER_BIND, value);
            if (!update.pressed && has_pedal)
                USB::SetDeviceBindValue(0, GUNCON2_PEDAL_BIND, value);
        }
    }
}

int FileSystem::OpenFDFileContent(const char* filename)
{
    auto *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
    if(env == nullptr) {
        return -1;
    }
    jclass NativeApp = env->FindClass("com/izzy2lost/psx2/NativeApp");
    jmethodID openContentUri = env->GetStaticMethodID(NativeApp, "openContentUri", "(Ljava/lang/String;)I");

    jstring j_filename = env->NewStringUTF(filename);
    int fd = env->CallStaticIntMethod(NativeApp, openContentUri, j_filename);
    return fd;
}

#ifdef __ANDROID__
// Helpers callable from core for SAF bridging
static jclass GetNativeAppClass(JNIEnv* env)
{
    return env->FindClass("com/izzy2lost/psx2/NativeApp");
}

std::string ResolveSafPathUriJNI(const char* relative_path, bool create)
{
    JNIEnv* env = reinterpret_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (!env)
        return {};
    jclass cls = GetNativeAppClass(env);
    if (!cls)
        return {};
    jmethodID mid = env->GetStaticMethodID(cls, "resolveSafPathUri", "(Ljava/lang/String;Z)Ljava/lang/String;");
    if (!mid)
        return {};
    jstring jrel = env->NewStringUTF(relative_path);
    jobject juri = env->CallStaticObjectMethod(cls, mid, jrel, (jboolean)create);
    env->DeleteLocalRef(jrel);
    if (!juri)
        return {};
    const char* cstr = env->GetStringUTFChars((jstring)juri, nullptr);
    std::string out = cstr ? std::string(cstr) : std::string();
    if (cstr)
        env->ReleaseStringUTFChars((jstring)juri, cstr);
    env->DeleteLocalRef(juri);
    return out;
}

std::string ResolveArcadeAssetUriJNI(const char* manifest_uri, const char* relative_path)
{
    JNIEnv* env = reinterpret_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (!env)
        return {};
    jclass cls = GetNativeAppClass(env);
    if (!cls)
        return {};
    jmethodID mid = env->GetStaticMethodID(cls, "resolveArcadeAssetUri",
        "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    if (!mid)
        return {};

    jstring jmanifest = env->NewStringUTF(manifest_uri);
    jstring jrelative = env->NewStringUTF(relative_path);
    jstring jresult = static_cast<jstring>(
        env->CallStaticObjectMethod(cls, mid, jmanifest, jrelative));
    env->DeleteLocalRef(jmanifest);
    env->DeleteLocalRef(jrelative);
    if (!jresult)
        return {};

    const char* chars = env->GetStringUTFChars(jresult, nullptr);
    std::string result = chars ? chars : "";
    if (chars)
        env->ReleaseStringUTFChars(jresult, chars);
    env->DeleteLocalRef(jresult);
    return result;
}

std::vector<std::string> SafListRecursiveFilesJNI(const char* relative_dir)
{
    std::vector<std::string> out;
    JNIEnv* env = reinterpret_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (!env)
        return out;
    jclass cls = GetNativeAppClass(env);
    if (!cls)
        return out;
    jmethodID mid = env->GetStaticMethodID(cls, "listSafRecursiveFiles", "(Ljava/lang/String;)[Ljava/lang/String;");
    if (!mid)
        return out;
    jstring jarg = env->NewStringUTF(relative_dir);
    jobjectArray arr = (jobjectArray)env->CallStaticObjectMethod(cls, mid, jarg);
    env->DeleteLocalRef(jarg);
    if (!arr)
        return out;
    jsize len = env->GetArrayLength(arr);
    out.reserve((size_t)len);
    for (jsize i = 0; i < len; i++)
    {
        jstring js = (jstring)env->GetObjectArrayElement(arr, i);
        if (!js) continue;
        const char* c = env->GetStringUTFChars(js, nullptr);
        if (c)
        {
            out.emplace_back(c);
            env->ReleaseStringUTFChars(js, c);
        }
        env->DeleteLocalRef(js);
    }
    env->DeleteLocalRef(arr);
    return out;
}

std::vector<std::string> SafListFilesFlatJNI(const char* relative_dir)
{
    std::vector<std::string> out;
    JNIEnv* env = reinterpret_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (!env)
        return out;
    jclass cls = GetNativeAppClass(env);
    if (!cls)
        return out;
    jmethodID mid = env->GetStaticMethodID(cls, "listSafFilesFlat", "(Ljava/lang/String;)[Ljava/lang/String;");
    if (!mid)
        return out;
    jstring jarg = env->NewStringUTF(relative_dir);
    jobjectArray arr = (jobjectArray)env->CallStaticObjectMethod(cls, mid, jarg);
    env->DeleteLocalRef(jarg);
    if (!arr)
        return out;
    jsize len = env->GetArrayLength(arr);
    out.reserve((size_t)len);
    for (jsize i = 0; i < len; i++)
    {
        jstring js = (jstring)env->GetObjectArrayElement(arr, i);
        if (!js) continue;
        const char* c = env->GetStringUTFChars(js, nullptr);
        if (c)
        {
            out.emplace_back(c);
            env->ReleaseStringUTFChars(js, c);
        }
        env->DeleteLocalRef(js);
    }
    env->DeleteLocalRef(arr);
    return out;
}
#endif // __ANDROID__

int FileSystem::OpenFDFileContentWithMode(const char* filename, const char* mode)
{
    auto *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
    if(env == nullptr) {
        return -1;
    }
    jclass NativeApp = env->FindClass("com/izzy2lost/psx2/NativeApp");
    jmethodID openContentUriMode = env->GetStaticMethodID(NativeApp, "openContentUriMode", "(Ljava/lang/String;Ljava/lang/String;)I");
    jstring j_filename = env->NewStringUTF(filename);
    jstring j_mode = env->NewStringUTF(mode);
    int fd = env->CallStaticIntMethod(NativeApp, openContentUriMode, j_filename, j_mode);
    return fd;
}

std::string ResolveSafChildUriJNI(const char* subdir, const char* filename, bool create)
{
    auto *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
    if(env == nullptr) return {};
    jclass NativeApp = env->FindClass("com/izzy2lost/psx2/NativeApp");
    jmethodID resolve = env->GetStaticMethodID(NativeApp, "resolveSafChildUri", "(Ljava/lang/String;Ljava/lang/String;Z)Ljava/lang/String;");
    jstring j_sub = env->NewStringUTF(subdir);
    jstring j_file = env->NewStringUTF(filename);
    jstring j_uri = (jstring)env->CallStaticObjectMethod(NativeApp, resolve, j_sub, j_file, (jboolean)create);
    if (!j_uri) return {};
    const char* cstr = env->GetStringUTFChars(j_uri, nullptr);
    std::string ret(cstr);
    env->ReleaseStringUTFChars(j_uri, cstr);
    env->DeleteLocalRef(j_uri);
    return ret;
}


extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_prepareVMStart(JNIEnv *env, jclass clazz) {
    // Invalidate any stop request that has not been carried out yet, so it can
    // never land on the VM this call is about to start.
    s_vm_start_generation.fetch_add(1, std::memory_order_acq_rel);
    s_shutdown_requested.store(false, std::memory_order_release);
    ClearTouchscreenPointerUpdates();
    std::lock_guard error_lock(s_vm_error_mutex);
    s_last_vm_error.clear();
}

// Custom Vulkan driver pin. Called from MainActivity.startEmuThread() BEFORE the
// VM starts so the first MTGS::Open (which triggers Vulkan::LoadVulkanLibrary)
// picks up the custom driver. Empty strings revert to the system loader.
// See Vulkan::SetCustomDriverPath in VKLoader.cpp for the splice.
extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setCustomVulkanDriver(
    JNIEnv* env, jclass clazz,
    jstring driverDir, jstring driverName,
    jstring redirectDir, jstring hookLibDir) {
#if defined(__ANDROID__)
    const std::string dir   = GetJavaString(env, driverDir);
    const std::string name  = GetJavaString(env, driverName);
    const std::string redir = GetJavaString(env, redirectDir);
    const std::string hook  = GetJavaString(env, hookLibDir);
    Vulkan::SetCustomDriverPath(
        dir.c_str(), name.c_str(), redir.c_str(), hook.c_str());
#endif
}

// Ajuste fino del driver personalizado (Cenit 0.6.20). Vacios = sin ajuste.
extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setCustomVulkanDriverTuning(
    JNIEnv* env, jclass clazz,
    jstring tuDebug, jstring shaderCacheDir, jstring appName) {
#if defined(__ANDROID__)
    const std::string flags = GetJavaString(env, tuDebug);
    const std::string cache = GetJavaString(env, shaderCacheDir);
    const std::string app   = GetJavaString(env, appName);
    Vulkan::SetCustomDriverEnv(flags.c_str(), cache.c_str(), app.c_str());
#endif
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_izzy2lost_psx2_NativeApp_getLastVMError(JNIEnv *env, jclass clazz) {
    std::lock_guard error_lock(s_vm_error_mutex);
    return env->NewStringUTF(s_last_vm_error.c_str());
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setVerifiedBiosFiles(JNIEnv *env, jclass clazz,
                                                       jstring p_usa_bios,
                                                       jstring p_europe_bios,
                                                       jstring p_japan_bios,
                                                       jstring p_arcade_bios) {
    s_verified_bios_usa = GetJavaString(env, p_usa_bios);
    s_verified_bios_europe = GetJavaString(env, p_europe_bios);
    s_verified_bios_japan = GetJavaString(env, p_japan_bios);
    s_verified_bios_arcade = GetJavaString(env, p_arcade_bios);
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_izzy2lost_psx2_NativeApp_runVMThread(JNIEnv *env, jclass clazz,
                                                 jstring p_szpath) {
    std::string _szPath = GetJavaString(env, p_szpath);

    // Serialize with any previous VM thread that is still unwinding. If a VM is
    // actively running, refuse the duplicate start instead of blocking; if the
    // previous thread is merely finishing its teardown, wait for it to exit so
    // we never initialize globals (MTGS, SysMemory, GS device) concurrently
    // with their shutdown.
    std::unique_lock lifecycle_lock(s_vm_lifecycle_mutex, std::defer_lock);
    if (!lifecycle_lock.try_lock()) {
        const VMState st = VMManager::GetState();
        if (st != VMState::Shutdown && st != VMState::Stopping) {
            Console.Warning("runVMThread ignored duplicate start while VM state is %d", static_cast<int>(st));
            return false;
        }
        Console.WriteLn("runVMThread waiting for previous VM thread to finish shutting down...");
        lifecycle_lock.lock();
    }

    std::unique_lock vm_start_lock(s_vm_start_mutex);

    VMState current_state = VMManager::GetState();
    if (current_state != VMState::Shutdown) {
        Console.Warning("runVMThread ignored duplicate start while VM state is %d", static_cast<int>(current_state));
        return false;
    }
    if (s_shutdown_requested.load(std::memory_order_acquire)) {
        Console.Warning("runVMThread ignored start because shutdown was requested before initialization");
        return false;
    }

    /////////////////////////////

    s_execute_exit = false;

//    const char* error;
//    if (!VMManager::PerformEarlyHardwareChecks(&error)) {
//        return false;
//    }

    // fast_boot : (false:bios->game, true:game)
    VMBootParameters boot_params;
    boot_params.filename = _szPath;
    
    // Enable fast boot when booting BIOS-only (no game loaded)
    // This skips the BIOS animation and goes straight to the PS2 menu
    Console.WriteLn("runVMThread: path='%s', length=%zu", _szPath.c_str(), _szPath.length());
    if (_szPath.empty()) {
        boot_params.fast_boot = true;
        Console.WriteLn("BIOS-only boot: Fast boot ENABLED to skip animation");
    } else {
        Console.WriteLn("Game boot: path=%s, fast_boot will use default behavior", _szPath.c_str());
    }

    // Detect the disc serial before VM startup so settings and BIOS region can follow the game.
    const std::string game_serial = GetGameSerialForPath(_szPath);
    SelectVerifiedBiosForSerial(game_serial, VMManager::isArcadeManifest(_szPath));

    // Apply per-game settings (if any) before applying core settings
    ApplyPerGameSettingsForSerial(game_serial);

    if (s_shutdown_requested.load(std::memory_order_acquire)) {
        Console.Warning("runVMThread cancelled before VM initialize because shutdown was requested");
        return false;
    }

    current_state = VMManager::GetState();
    if (current_state != VMState::Shutdown) {
        Console.Warning("runVMThread aborted before VM initialize because VM state changed to %d", static_cast<int>(current_state));
        return false;
    }

    // Claim CPU-thread identity for Host::RunOnCPUThread before any core code runs.
    s_cpu_thread_id.store(std::this_thread::get_id(), std::memory_order_release);

    if (!VMManager::Internal::CPUThreadInitialize()) {
        Console.Error("CPUThreadInitialize failed");
        VMManager::Internal::CPUThreadShutdown();
        return false;
    }

    VMManager::ApplySettings();
    GSDumpReplayer::SetIsDumpRunner(false);

    if (s_shutdown_requested.load(std::memory_order_acquire)) {
        Console.Warning("runVMThread cancelled after applying settings because shutdown was requested");
        VMManager::Internal::CPUThreadShutdown();
        return false;
    }

    current_state = VMManager::GetState();
    if (current_state != VMState::Shutdown) {
        Console.Warning("runVMThread aborted after applying settings because VM state changed to %d", static_cast<int>(current_state));
        VMManager::Internal::CPUThreadShutdown();
        return false;
    }

    Error startup_error;
    const bool initialized =
        (VMManager::Initialize(boot_params, &startup_error) == VMBootResult::StartupSuccess);
    if (!initialized && startup_error.IsValid())
    {
        Console.ErrorFmt("VM startup failed: {}", startup_error.GetDescription());
        std::lock_guard error_lock(s_vm_error_mutex);
        s_last_vm_error = startup_error.GetDescription();
    }
    vm_start_lock.unlock();

    if (initialized)
    {
        if (s_shutdown_requested.load(std::memory_order_acquire)) {
            Console.Warning("runVMThread shutting down immediately because shutdown was requested during startup");
            VMManager::Shutdown(false);
            VMManager::Internal::CPUThreadShutdown();
            return false;
        }

        // If a per-game renderer was requested, apply it now that VM is up.
        if (s_pending_renderer >= 0)
        {
            EmuConfig.GS.Renderer = static_cast<GSRendererType>(s_pending_renderer);
            s_pending_renderer = -1;
            if (MTGS::IsOpen())
                MTGS::ApplySettings();
        }
        VMState _vmState = VMState::Running;
        VMManager::SetState(_vmState);
        ////
        while (true) {
            _vmState = VMManager::GetState();
            if (_vmState == VMState::Stopping || _vmState == VMState::Shutdown) {
                break;
            } else if (_vmState == VMState::Running) {
                s_execute_exit = false;
                VMManager::Execute();
                s_execute_exit = true;
            } else {
                usleep(250000);
            }
        }
        ////
        VMManager::Shutdown(false);
    }
    ////
    VMManager::Internal::CPUThreadShutdown();

    // Nothing can service queued work once this thread goes away.
    s_cpu_thread_id.store(std::thread::id(), std::memory_order_release);
    {
        std::lock_guard lock(s_cpu_thread_task_mutex);
        s_cpu_thread_tasks.clear();
    }

    return initialized;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_pause(JNIEnv *env, jclass clazz) {
    const uint64_t generation = s_vm_start_generation.load(std::memory_order_acquire);
    std::thread([generation] {
        // Don't let a pause meant for the previous game land on the new one.
        if (s_vm_start_generation.load(std::memory_order_acquire) != generation)
            return;
        VMManager::SetPaused(true);
    }).detach();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_resume(JNIEnv *env, jclass clazz) {
    const uint64_t generation = s_vm_start_generation.load(std::memory_order_acquire);
    std::thread([generation] {
        if (s_vm_start_generation.load(std::memory_order_acquire) != generation)
            return;
        VMManager::SetPaused(false);
    }).detach();
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_izzy2lost_psx2_NativeApp_isPaused(JNIEnv *env, jclass clazz) {
    return VMManager::GetState() == VMState::Paused;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_setFastForward(JNIEnv *, jclass, jboolean enabled) {
    const uint64_t generation = s_vm_start_generation.load(std::memory_order_acquire);
    Host::RunOnCPUThread([generation, enabled = (enabled == JNI_TRUE)] {
        // Speed changes belong to this game and must run on the emulation thread.
        if (s_vm_start_generation.load(std::memory_order_acquire) != generation)
            return;
        const VMState state = VMManager::GetState();
        if (state != VMState::Running && state != VMState::Paused)
            return;

        static uint64_t previous_generation = 0;
        static std::optional<LimiterModeType> previous_mode;
        if (previous_generation != generation) {
            previous_generation = generation;
            previous_mode.reset();
        }
        if (enabled && state == VMState::Running) {
            if (!previous_mode.has_value())
                previous_mode = VMManager::GetLimiterMode();
            VMManager::SetLimiterMode(LimiterModeType::Turbo);
        } else if (previous_mode.has_value()) {
            VMManager::SetLimiterMode(*previous_mode);
            previous_mode.reset();
        }
    });
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_izzy2lost_psx2_NativeApp_isVMActive(JNIEnv *env, jclass clazz) {
    return VMManager::GetState() != VMState::Shutdown;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_izzy2lost_psx2_NativeApp_shutdown(JNIEnv *env, jclass clazz) {
    const uint64_t generation = s_vm_start_generation.load(std::memory_order_acquire);
    s_shutdown_requested.store(true, std::memory_order_release);
    std::thread([generation] {
        // A boot that started after this request was made owns the VM now.
        if (s_vm_start_generation.load(std::memory_order_acquire) != generation) {
            Console.WriteLn("shutdown dropped: a newer VM start superseded it");
            return;
        }
        const VMState state = VMManager::GetState();
        if (state == VMState::Running || state == VMState::Paused || state == VMState::Resetting) {
            VMManager::SetState(VMState::Stopping);
        } else if (state != VMState::Shutdown && state != VMState::Stopping) {
            Console.Warning("shutdown ignored while VM state is %d", static_cast<int>(state));
        }
    }).detach();
}


extern "C"
JNIEXPORT jboolean JNICALL
Java_com_izzy2lost_psx2_NativeApp_saveStateToSlot(JNIEnv *env, jclass clazz, jint p_slot) {
    if (!VMManager::HasValidVM()) {
        return false;
    }

    std::future<bool> ret = std::async([p_slot]
    {
       if(VMManager::GetDiscCRC() != 0) {
           if(VMManager::GetState() != VMState::Paused) {
               VMManager::SetPaused(true);
           }

           // wait 5 sec
           for (int i = 0; i < 5; ++i) {
               if (s_execute_exit) {
                   VMManager::SaveStateToSlot(p_slot, false, [](const std::string& error) {
                       if (!error.empty())
                           ERROR_LOG("Failed to save state: {}", error);
                   });
                   return true;
               }
               sleep(1);
           }
       }
       return false;

    });

    return ret.get();
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_izzy2lost_psx2_NativeApp_loadStateFromSlot(JNIEnv *env, jclass clazz, jint p_slot) {
    if (!VMManager::HasValidVM()) {
        return false;
    }

    std::future<bool> ret = std::async([p_slot]
    {
       u32 _crc = VMManager::GetDiscCRC();
       if(_crc != 0) {
           if (VMManager::HasSaveStateInSlot(VMManager::GetDiscSerial().c_str(), _crc, p_slot)) {
               if(VMManager::GetState() != VMState::Paused) {
                   VMManager::SetPaused(true);
               }

               // wait 5 sec
               for (int i = 0; i < 5; ++i) {
                   if (s_execute_exit) {
                       if(VMManager::LoadStateFromSlot(p_slot)) {
                           return true;
                       }
                       break;
                   }
                   sleep(1);
               }
           }
       }
       return false;
    });

    return ret.get();
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_izzy2lost_psx2_NativeApp_getGamePathSlot(JNIEnv *env, jclass clazz, jint p_slot) {
    std::string _filename = VMManager::GetSaveStateFileName(VMManager::GetDiscSerial().c_str(), VMManager::GetDiscCRC(), p_slot);
    if(!_filename.empty()) {
        return env->NewStringUTF(_filename.c_str());
    }
    return nullptr;
}

extern "C"
JNIEXPORT jbyteArray JNICALL
Java_com_izzy2lost_psx2_NativeApp_getImageSlot(JNIEnv *env, jclass clazz, jint p_slot) {
    jbyteArray retArr = nullptr;

    std::string _filename = VMManager::GetSaveStateFileName(VMManager::GetDiscSerial().c_str(), VMManager::GetDiscCRC(), p_slot);
    if(!_filename.empty())
    {
        zip_error_t ze = {};
        auto zf = zip_open_managed(_filename.c_str(), ZIP_RDONLY, &ze);
        if (zf) {
            auto zff = zip_fopen_managed(zf.get(), "Screenshot.png", 0);
            if(zff) {
                std::optional<std::vector<u8>> optdata(ReadBinaryFileInZip(zff.get()));
                if (optdata.has_value()) {
                    std::vector<u8> vec = std::move(optdata.value());
                    ////
                    auto length = static_cast<jsize>(vec.size());
                    retArr = env->NewByteArray(length);
                    if (retArr != nullptr) {
                        env->SetByteArrayRegion(retArr, 0, length,
                                                reinterpret_cast<const jbyte *>(vec.data()));
                    }
                }
            }
        }
    }

    return retArr;
}


void Host::CommitBaseSettingChanges()
{
    // Save achievements settings to Android SharedPreferences
    // This is called after login to persist the token
    
    auto lock = Host::GetSettingsLock();
    SettingsInterface* si = Host::GetSettingsInterface();
    if (!si)
    {
        __android_log_print(ANDROID_LOG_ERROR, "PCSX2", "No settings interface available");
        return;
    }

    // Get achievements credentials from settings
    std::string username = si->GetStringValue("Achievements", "Username", "");
    std::string token = si->GetStringValue("Achievements", "Token", "");
    std::string loginTimestamp = si->GetStringValue("Achievements", "LoginTimestamp", "");

    if (username.empty() && token.empty())
    {
        // Nothing to save
        return;
    }

    __android_log_print(ANDROID_LOG_INFO, "PCSX2", "Saving achievements credentials to SharedPreferences");
    
    // Call the Java method to save to SharedPreferences
    // We'll use JNI to call NativeApp.saveAchievementsCredentials()
    JavaVM* jvm = AchievementsJNI::GetJavaVM();
    if (!jvm)
    {
        __android_log_print(ANDROID_LOG_ERROR, "PCSX2", "JavaVM not available");
        return;
    }

    JNIEnv* env = nullptr;
    bool attached = false;
    
    // Get JNI environment
    if (jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK)
    {
        // Try to attach current thread
        if (jvm->AttachCurrentThread(&env, nullptr) == JNI_OK)
        {
            attached = true;
        }
        else
        {
            __android_log_print(ANDROID_LOG_ERROR, "PCSX2", "Failed to attach thread to JVM");
            return;
        }
    }

    // Find the NativeApp class and saveAchievementsCredentials method
    jclass nativeAppClass = env->FindClass("com/izzy2lost/psx2/NativeApp");
    if (nativeAppClass)
    {
        jmethodID saveMethod = env->GetStaticMethodID(nativeAppClass, "saveAchievementsCredentials",
            "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
        if (saveMethod)
        {
            jstring jUsername = env->NewStringUTF(username.c_str());
            jstring jToken = env->NewStringUTF(token.c_str());
            jstring jTimestamp = env->NewStringUTF(loginTimestamp.c_str());
            
            env->CallStaticVoidMethod(nativeAppClass, saveMethod, jUsername, jToken, jTimestamp);
            
            env->DeleteLocalRef(jUsername);
            env->DeleteLocalRef(jToken);
            env->DeleteLocalRef(jTimestamp);
            
            __android_log_print(ANDROID_LOG_INFO, "PCSX2", "Achievements credentials saved successfully");
        }
        else
        {
            __android_log_print(ANDROID_LOG_ERROR, "PCSX2", "Could not find saveAchievementsCredentials method");
            env->ExceptionClear();
        }
        env->DeleteLocalRef(nativeAppClass);
    }
    else
    {
        __android_log_print(ANDROID_LOG_ERROR, "PCSX2", "Could not find NativeApp class");
        env->ExceptionClear();
    }

    // Detach thread if we attached it
    if (attached)
    {
        jvm->DetachCurrentThread();
    }
}

void Host::LoadSettings(SettingsInterface& si, std::unique_lock<std::mutex>& lock)
{
}

void Host::CheckForSettingsChanges(const Pcsx2Config& old_config)
{
}

bool Host::RequestResetSettings(bool folders, bool core, bool controllers, bool hotkeys, bool ui)
{
    // not running any UI, so no settings requests will come in
    return false;
}

void Host::SetDefaultUISettings(SettingsInterface& si)
{
    // nothing
}

std::unique_ptr<ProgressCallback> Host::CreateHostProgressCallback()
{
    return nullptr;
}

void Host::ReportErrorAsync(const std::string_view title, const std::string_view message)
{
    if (!title.empty() && !message.empty())
        ERROR_LOG("ReportErrorAsync: {}: {}", title, message);
    else if (!message.empty())
        ERROR_LOG("ReportErrorAsync: {}", message);
}

void Host::OpenURL(const std::string_view url)
{
    // noop
}

std::string Host::GetTextFromClipboard()
{
    return {};
}

int Host::LocaleSensitiveCompare(std::string_view lhs, std::string_view rhs)
{
    const size_t length = std::min(lhs.size(), rhs.size());
    const int result = std::char_traits<char>::compare(lhs.data(), rhs.data(), length);
    if (result != 0)
        return result;
    return (lhs.size() > rhs.size()) - (lhs.size() < rhs.size());
}

bool Common::InhibitScreensaver(bool inhibit)
{
    return true;
}

bool Common::PlaySoundAsync(const char* path)
{
    AchievementsJNI::PlaySound(path);
    return true;
}

bool Host::CopyTextToClipboard(const std::string_view text)
{
    return false;
}

void Host::BeginTextInput()
{
    // noop
}

void Host::EndTextInput()
{
    // noop
}

std::optional<WindowInfo> Host::GetTopLevelWindowInfo()
{
    return std::nullopt;
}

void Host::OnInputDeviceConnected(const std::string_view identifier, const std::string_view device_name)
{
}

void Host::OnInputDeviceDisconnected(const InputBindingKey key, const std::string_view identifier)
{
}

void Host::SetMouseMode(bool relative_mode, bool hide_cursor)
{
}

void Host::RequestResizeHostDisplay(s32 width, s32 height)
{
}

void Host::OnVMStarting()
{
}

void Host::OnVMStarted()
{
}

void Host::OnVMDestroyed()
{
}

void Host::OnVMPaused()
{
}

void Host::OnVMResumed()
{
}

void Host::OnPerformanceMetricsUpdated()
{
}

void Host::OnSaveStateLoading(const std::string_view filename)
{
}

void Host::OnSaveStateLoaded(const std::string_view filename, bool was_successful)
{
}

void Host::OnSaveStateSaved(const std::string_view filename)
{
}

void Host::RunOnCPUThread(std::function<void()> function, bool block /* = false */)
{
    if (!function)
        return;

    const std::thread::id cpu_thread = s_cpu_thread_id.load(std::memory_order_acquire);

    // Already on the CPU thread: run inline. A blocking caller would otherwise wait
    // forever for a queue only it can drain.
    if (cpu_thread == std::this_thread::get_id())
    {
        function();
        return;
    }

    // No CPU thread to run on. These tasks act on VM state that does not exist yet,
    // so dropping them is correct -- and far better than the abort this used to be.
    if (cpu_thread == std::thread::id())
    {
        Console.Warning("Host::RunOnCPUThread called with no CPU thread; dropping task.");
        return;
    }

    if (!block)
    {
        std::lock_guard lock(s_cpu_thread_task_mutex);
        s_cpu_thread_tasks.push_back(std::move(function));
        return;
    }

    struct BlockingTask
    {
        std::mutex mutex;
        std::condition_variable cv;
        bool done = false;
    };
    const auto state = std::make_shared<BlockingTask>();

    {
        std::lock_guard lock(s_cpu_thread_task_mutex);
        s_cpu_thread_tasks.push_back([func = std::move(function), state]() {
            func();
            {
                std::lock_guard done_lock(state->mutex);
                state->done = true;
            }
            state->cv.notify_all();
        });
    }

    // Bounded wait: if the VM stops before draining the queue, time out rather than
    // hanging the caller forever. The task keeps the state alive either way.
    std::unique_lock done_lock(state->mutex);
    if (!state->cv.wait_for(done_lock, std::chrono::seconds(5), [&state]() { return state->done; }))
        Console.Warning("Host::RunOnCPUThread timed out waiting for the CPU thread.");
}

void Host::RefreshGameListAsync(bool invalidate_cache)
{
}

void Host::CancelGameListRefresh()
{
}

bool Host::IsFullscreen()
{
    return false;
}

void Host::SetFullscreen(bool enabled)
{
}

void Host::OnCaptureStarted(const std::string& filename)
{
}

void Host::OnCaptureStopped()
{
}

void Host::RequestExitApplication(bool allow_confirm)
{
}

void Host::RequestExitBigPicture()
{
}

void Host::RequestVMShutdown(bool allow_confirm, bool allow_save_state, bool default_save_state)
{
    VMManager::SetState(VMState::Stopping);
}

void Host::OnAchievementsLoginSuccess(const char* username, u32 points, u32 sc_points, u32 unread_messages)
{
    // noop
}

void Host::OnAchievementsLoginRequested(Achievements::LoginRequestReason reason)
{
    // noop
}

void Host::OnAchievementsHardcoreModeChanged(bool enabled)
{
    // noop
}

void Host::OnAchievementsRefreshed()
{
    // noop
}

void Host::OnCoverDownloaderOpenRequested()
{
    // noop
}

void Host::OnCreateMemoryCardOpenRequested()
{
    // noop
}

bool Host::ShouldPreferHostFileSelector()
{
    return false;
}

void Host::OpenHostFileSelectorAsync(std::string_view title, bool select_directory, FileSelectorCallback callback,
                                     FileSelectorFilters filters, std::string_view initial_directory)
{
    callback(std::string());
}

std::optional<u32> InputManager::ConvertHostKeyboardStringToCode(const std::string_view str)
{
    return std::nullopt;
}

std::optional<std::string> InputManager::ConvertHostKeyboardCodeToString(u32 code)
{
    return std::nullopt;
}

const char* InputManager::ConvertHostKeyboardCodeToIcon(u32 code)
{
    return nullptr;
}

s32 Host::Internal::GetTranslatedStringImpl(
        const std::string_view context, const std::string_view msg, char* tbuf, size_t tbuf_space)
{
    if (msg.size() > tbuf_space)
        return -1;
    else if (msg.empty())
        return 0;

    std::memcpy(tbuf, msg.data(), msg.size());
    return static_cast<s32>(msg.size());
}

std::string Host::TranslatePluralToString(const char* context, const char* msg, const char* disambiguation, int count)
{
    TinyString count_str = TinyString::from_format("{}", count);

    std::string ret(msg);
    for (;;)
    {
        std::string::size_type pos = ret.find("%n");
        if (pos == std::string::npos)
            break;

        ret.replace(pos, pos + 2, count_str.view());
    }

    return ret;
}

void Host::ReportInfoAsync(const std::string_view title, const std::string_view message)
{
}

bool Host::LocaleCircleConfirm()
{
    return false;
}

bool Host::InNoGUIMode()
{
    return false;
}

// JNI: report if a SAF Data Root is configured
bool HasSafDataRootJNI()
{
    JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (!env) return false;
    jclass cls = env->FindClass("com/izzy2lost/psx2/NativeApp");
    if (!cls) return false;
    jmethodID mid = env->GetStaticMethodID(cls, "hasSafDataRoot", "()Z");
    if (!mid) return false;
    jboolean res = env->CallStaticBooleanMethod(cls, mid);
    return (res == JNI_TRUE);
}
static std::vector<std::string> SafListFilesJNI(const char* subdir)
{
    std::vector<std::string> ret;
    JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (!env) return ret;
    jclass cls = env->FindClass("com/izzy2lost/psx2/NativeApp");
    if (!cls) return ret;
    jmethodID mid = env->GetStaticMethodID(cls, "listSafFilenames", "(Ljava/lang/String;)[Ljava/lang/String;");
    if (!mid) return ret;
    jstring j_sub = env->NewStringUTF(subdir);
    jobjectArray arr = (jobjectArray)env->CallStaticObjectMethod(cls, mid, j_sub);
    env->DeleteLocalRef(j_sub);
    if (!arr) return ret;
    jsize n = env->GetArrayLength(arr);
    ret.reserve(n);
    for (jsize i = 0; i < n; i++) {
        jstring s = (jstring)env->GetObjectArrayElement(arr, i);
        if (!s) continue;
        const char* cs = env->GetStringUTFChars(s, nullptr);
        if (cs) ret.emplace_back(cs);
        env->ReleaseStringUTFChars(s, cs);
        env->DeleteLocalRef(s);
    }
    env->DeleteLocalRef(arr);
    return ret;
}

// Get list of saves on a memory card using PCSX2's native parsing
extern "C"
JNIEXPORT jobjectArray JNICALL
Java_com_izzy2lost_psx2_NativeApp_getMemoryCardSaves(JNIEnv* env, jclass, jstring p_memcard_path)
{
    if (!p_memcard_path) {
        return env->NewObjectArray(0, env->FindClass("java/lang/String"), nullptr);
    }

    const char* path_chars = env->GetStringUTFChars(p_memcard_path, nullptr);
    if (!path_chars) {
        return env->NewObjectArray(0, env->FindClass("java/lang/String"), nullptr);
    }

    std::string memcard_path(path_chars);
    env->ReleaseStringUTFChars(p_memcard_path, path_chars);

    std::vector<std::string> saves;

    // Open the memory card file
    auto fp = FileSystem::OpenManagedCFile(memcard_path.c_str(), "rb");
    if (!fp) {
        return env->NewObjectArray(0, env->FindClass("java/lang/String"), nullptr);
    }

    // Read superblock to get root directory cluster
    u8 superblock[512];
    if (std::fread(superblock, 1, 512, fp.get()) != 512) {
        return env->NewObjectArray(0, env->FindClass("java/lang/String"), nullptr);
    }

    // Extract alloc_offset (at 0x34) and rootdir_cluster (at 0x3C)
    u32 alloc_offset = *(u32*)&superblock[0x34];
    u32 rootdir_cluster = *(u32*)&superblock[0x3C];

    // Calculate directory start position (each cluster is 1024 bytes)
    u64 dir_start = (u64)(alloc_offset + rootdir_cluster) * 1024;

    // Seek to directory
    if (FileSystem::FSeek64(fp.get(), dir_start, SEEK_SET) != 0) {
        return env->NewObjectArray(0, env->FindClass("java/lang/String"), nullptr);
    }

    // Read directory entries (each entry is 512 bytes)
    for (int i = 0; i < 100; i++) {
        u8 entry[512];
        if (std::fread(entry, 1, 512, fp.get()) != 512) break;

        // Read mode (first 4 bytes)
        u32 mode = *(u32*)&entry[0];

        // Skip empty entries
        if (mode == 0 || mode == 0xFFFFFFFF) continue;

        // Check if used (0x8000 flag)
        if (!(mode & 0x8000)) continue;

        // Read filename (at offset 0x40, max 32 bytes)
        u8 name_bytes[32];
        std::memcpy(name_bytes, &entry[0x40], 32);

        // Convert to string, stopping at null terminator
        std::string name_str;
        for (int j = 0; j < 32; j++) {
            if (name_bytes[j] == 0) break;
            // Only include printable ASCII
            if (name_bytes[j] >= 32 && name_bytes[j] <= 126) {
                name_str += (char)name_bytes[j];
            }
        }

        // Skip "." and ".." entries
        if (name_str == "." || name_str == "..") continue;
        if (name_str.empty()) continue;

        // Read length field (at offset 0x04)
        u32 length = *(u32*)&entry[0x04];

        // Check if it's a directory (0x0020 flag)
        bool is_dir = (mode & 0x0020) != 0;

        // Basic sanity check - skip if length is suspiciously large
        if (length > 1000000000) continue; // 1 billion is clearly wrong

        // Format: "filename|size|isDirectory"
        std::string save_info = StringUtil::StdStringFromFormat("%s|%u|%d", 
            name_str.c_str(), length, is_dir ? 1 : 0);
        saves.push_back(save_info);
    }

    // Convert to Java string array
    jobjectArray result = env->NewObjectArray(saves.size(), env->FindClass("java/lang/String"), nullptr);
    for (size_t i = 0; i < saves.size(); i++) {
        jstring str = env->NewStringUTF(saves[i].c_str());
        env->SetObjectArrayElement(result, i, str);
        env->DeleteLocalRef(str);
    }

    return result;
}
