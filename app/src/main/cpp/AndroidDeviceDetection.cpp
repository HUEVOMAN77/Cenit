// SPDX-FileCopyrightText: 2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#ifdef __ANDROID__

#include "AndroidDeviceDetection.h"
#include "CenitDeviceClass.h"
#include "common/Console.h"
#include "common/HostSys.h"
#include <cpuinfo.h>
#include <sys/system_properties.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace AndroidDeviceDetection
{
	static std::string GetSystemProperty(const char* key)
	{
		char value[PROP_VALUE_MAX] = {};
		if (__system_property_get(key, value) > 0)
			return std::string(value);
		return "";
	}

	std::string GetManufacturer()
	{
		return GetSystemProperty("ro.product.manufacturer");
	}

	std::string GetModel()
	{
		return GetSystemProperty("ro.product.model");
	}

	std::string GetGPURenderer()
	{
		// This would need to be called from GL/Vulkan context
		// For now, we rely on hardware detection
		return GetSystemProperty("ro.hardware");
	}

	bool IsMediatek()
	{
		std::string hardware = GetSystemProperty("ro.hardware");
		std::string board = GetSystemProperty("ro.product.board");
		std::string platform = GetSystemProperty("ro.board.platform");
		
		Console.WriteLn("Device Detection: hardware='%s', board='%s', platform='%s'", 
			hardware.c_str(), board.c_str(), platform.c_str());
		
		// Convert to lowercase for comparison
		auto toLower = [](std::string str) {
			for (char& c : str) c = std::tolower(c);
			return str;
		};
		
		hardware = toLower(hardware);
		board = toLower(board);
		platform = toLower(platform);
		
		// Check for Mediatek identifiers
		return (hardware.find("mt") == 0 || 
		        hardware.find("mediatek") != std::string::npos ||
		        board.find("mt") == 0 ||
		        platform.find("mt") == 0 ||
		        platform.find("mediatek") != std::string::npos);
	}

	bool IsSnapdragon()
	{
		std::string hardware = GetSystemProperty("ro.hardware");
		std::string board = GetSystemProperty("ro.product.board");
		std::string platform = GetSystemProperty("ro.board.platform");
		
		Console.WriteLn("Device Detection: hardware='%s', board='%s', platform='%s'", 
			hardware.c_str(), board.c_str(), platform.c_str());
		
		auto toLower = [](std::string str) {
			for (char& c : str) c = std::tolower(c);
			return str;
		};
		
		hardware = toLower(hardware);
		board = toLower(board);
		platform = toLower(platform);
		
		// Check for Qualcomm/Snapdragon identifiers
		return (hardware.find("qcom") != std::string::npos ||
		        hardware.find("qualcomm") != std::string::npos ||
		        platform.find("msm") == 0 ||
		        platform.find("sdm") == 0 ||
		        platform.find("sm") == 0 ||
		        platform.find("qcom") != std::string::npos);
	}

	GPUVendor DetectGPUVendorUncached();

	GPUVendor DetectGPUVendor()
	{
		// Cenit 0.6.22: memoizado. El muestreador del dialogo de driver llama al
		// tier cada 1.2 s y cada consulta re-leia propiedades del sistema y
		// escribia 3-4 lineas de "Device Detection"/"Detected ..." en el log.
		// Mientras el canal de archivo estuvo muerto (ver Console.cpp) eso solo
		// inundaba logcat; con emulog.txt vivo habria enterrado la evidencia.
		// El hardware del telefono no cambia en vida del proceso: se calcula una
		// vez y se conserva. Las lineas de deteccion quedan (una sola vez, con
		// valor diagnostico real en el reporte).
		static const GPUVendor s_cached = DetectGPUVendorUncached();
		return s_cached;
	}

	GPUVendor DetectGPUVendorUncached()
	{
		if (IsSnapdragon())
		{
			Console.WriteLn("Detected Qualcomm Snapdragon (Adreno GPU)");
			return GPUVendor::Qualcomm;
		}
		
		if (IsMediatek())
		{
			Console.WriteLn("Detected Mediatek (Mali GPU)");
			return GPUVendor::ARM;
		}
		
		// Check for other vendors via hardware string
		std::string hardware = GetSystemProperty("ro.hardware");
		std::string board = GetSystemProperty("ro.product.board");
		std::string platform = GetSystemProperty("ro.board.platform");
		std::string manufacturer = GetSystemProperty("ro.product.manufacturer");
		
		auto toLower = [](std::string str) {
			for (char& c : str) c = std::tolower(c);
			return str;
		};
		
		hardware = toLower(hardware);
		board = toLower(board);
		platform = toLower(platform);
		manufacturer = toLower(manufacturer);

		// Tensor Pixels use ARM Mali/Immortalis GPUs, but their system properties
		// contain Google codenames (gs101/gs201/zuma/etc.) rather than "mali".
		// Older Qualcomm Pixels were already handled by IsSnapdragon() above.
		if (manufacturer.find("google") != std::string::npos)
		{
			Console.WriteLn("Detected Google Pixel/Tensor (ARM GPU), hardware: %s, board: %s, platform: %s",
				hardware.c_str(), board.c_str(), platform.c_str());
			return GPUVendor::ARM;
		}
		
		// Samsung Exynos devices (Mali GPU)
		if (hardware.find("exynos") != std::string::npos || 
		    hardware.find("universal") != std::string::npos ||
		    (manufacturer.find("samsung") != std::string::npos && hardware.find("samsungexynos") != std::string::npos))
		{
			Console.WriteLn("Detected Samsung Exynos (Mali GPU)");
			return GPUVendor::ARM;
		}
		
		// Kirin devices (Mali GPU)
		if (hardware.find("kirin") != std::string::npos || hardware.find("hi") == 0)
		{
			Console.WriteLn("Detected HiSilicon Kirin (Mali GPU)");
			return GPUVendor::ARM;
		}
		
		// Rockchip devices (Mali GPU)
		if (hardware.find("rk") == 0 || hardware.find("rockchip") != std::string::npos)
		{
			Console.WriteLn("Detected Rockchip (Mali GPU)");
			return GPUVendor::ARM;
		}
		
		Console.WriteLn("Unknown GPU vendor, hardware: %s, manufacturer: %s", hardware.c_str(), manufacturer.c_str());
		return GPUVendor::Unknown;
	}

	unsigned GetQualcommSocModel()
	{
		return GetQualcommSocModelTraced(nullptr);
	}

	namespace
	{
		using namespace CenitDeviceClass;

		// ------------------------------------------------------------------
		// Recolector. Su unica tarea es llenar Measurement con numeros que el
		// sistema efectivamente dijo, y apagar el flag de cada campo que NO pudo
		// leer. No clasifica nada: la clasificacion vive entera en CenitDeviceClass.h,
		// que es la unica pieza de este cambio que host puede romper con Harness15.
		//
		// De donde sale cada numero, y por que no se saca de otro lado:
		//  * MIDR del cluster: cpuinfo_cluster::midr, que cpuinfo lleno a partir de
		//    /sys/devices/system/cpu/cpuN/regs/identification/midr_el1 (o de
		//    /proc/cpuinfo cuando el kernel no deja leer el registro; en ambos casos es
		//    un registro del chip, no un nombre).
		//  * Frecuencia: leida A MANO de cpuinfo_max_freq. No de
		//    cpuinfo_cluster::frequency: en este arbol vendorizado ese campo queda SIN
		//    LLENAR en la ruta de Linux/ARM64 — el literal que construye el cluster en
		//    3rdparty/cpuinfo/src/arm/linux/init.c (linea ~713) inicializa
		//    processor_start, core_count, cluster_id, package, vendor, uarch y midr, y NO
		//    incluye .frequency; el unico `cluster->frequency =` del arbol esta en
		//    src/arm/windows/init-by-logical-sys-info.c. Confiar en el daria 0 kHz en
		//    todos los telefonos, y "0 kHz" con el flag encendido es exactamente el
		//    centinela-vuelto-dato que esta version viene persiguiendo.
		//  * RAM: GetPhysicalMemory()/GetAvailablePhysicalMemory() del common, que en
		//    Linux leen sysconf(_SC_PHYS_PAGES)*pagesize y MemAvailable. El telefono de
		//    referencia reporta por esa via 7349 MB fisicos y 2858 disponibles, o sea la
		//    unidad ya esta probada contra su propio log.
		// ------------------------------------------------------------------

		// kHz del procesador mas rapido del cluster. `-1` = no se pudo leer NINGUNO, lo
		// cual es distinto de `0` y el llamador lo trata como campo sin medir. Todo el
		// archivo esta bajo __ANDROID__, y android es linux, asi que cpuinfo_processor::
		// linux_id existe sin necesidad de otro guardado.
		long ReadClusterMaxFreqKhz(const cpuinfo_cluster* cluster)
		{
			long best = -1;
			const uint32_t end = cluster->processor_start + cluster->processor_count;
			for (uint32_t i = cluster->processor_start; i < end; i++)
			{
				const cpuinfo_processor* proc = cpuinfo_get_processor(i);
				if (!proc || proc->linux_id < 0)
					continue;

				// El indice va como %d de un entero ya validado arriba: no hay cadena
				// del usuario en el path, asi que no hay nada que escapar.
				char path[128];
				snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq",
					static_cast<int>(proc->linux_id));

				FILE* f = fopen(path, "r");
				if (!f)
					continue;

				unsigned long long khz = 0;
				const int got = fscanf(f, "%llu", &khz);
				fclose(f);

				if (got != 1 || khz == 0)
					continue;

				const long v = (khz > 0x7ffffffeULL) ? 0x7ffffffeL : static_cast<long>(khz);
				if (v > best)
					best = v;
			}
			return best;
		}

		Measurement CollectMeasurement(ClusterView* views, uint32_t cap, uint32_t* out_used, uint32_t* out_real)
		{
			Measurement m;
			*out_used = 0;
			*out_real = 0;

			if (!cpuinfo_initialize())
			{
				// pthread_once dentro de cpuinfo: es idempotente y no pisa la
				// inicializacion que VMManager ya hace. Si AUN asi fallo, no hay ningun
				// numero y la ley lo va a imprimir como "nada-medido", no como
				// "dos nucleos".
				Console.Error("Cenit equipo: cpuinfo_initialize() fallo, clasificacion sin medir");
				return m;
			}

			const uint32_t real_clusters = cpuinfo_get_clusters_count();
			*out_real = real_clusters;
			uint32_t used = 0;
			for (uint32_t i = 0; i < real_clusters; i++)
			{
				const cpuinfo_cluster* c = cpuinfo_get_cluster(i);
				if (!c)
					continue;

				if (used >= cap)
				{
					// Pasar el techo cambia el perfil, asi que se dice en el log en vez
					// de quedarse en un agregado silenciosamente recortado.
					Console.WriteLn("Cenit equipo: TECHO de clusters alcanzado (%u agregados de %u reales)",
						used, real_clusters);
					break;
				}

				ClusterView& v = views[used++];
				v.midr = c->midr;
				v.core_count = c->core_count;

				const long khz = ReadClusterMaxFreqKhz(c);
				if (khz > 0)
					v.frequency_khz = static_cast<uint32_t>(khz);
			}

			*out_used = used;

			const ClusterAggregate agg = Aggregate(views, used);
			m.big_class = agg.big_class;
			m.big_class_measured = agg.class_measured;
			m.big_freq_khz = agg.big_freq_khz;
			m.big_freq_measured = agg.freq_measured;
			m.big_cores = agg.big_cores;
			m.total_cores = agg.total_cores;
			m.clusters = agg.clusters;
			m.cores_measured = agg.cores_measured;

			// RAM. GetPhysicalMemory() documenta 0 como "no soportado", o sea el 0 aqui
			// SI es un centinela conocido: se apaga el flag en lugar de promediarlo.
			const u64 phys = GetPhysicalMemory();
			if (phys > 0)
			{
				m.total_ram_mb = static_cast<uint32_t>(phys / _1mb);
				m.ram_measured = true;
			}
			const u64 avail = GetAvailablePhysicalMemory();
			if (avail > 0)
			{
				m.avail_ram_mb = static_cast<uint32_t>(avail / _1mb);
				m.avail_ram_measured = true;
			}

			return m;
		}

		struct Measured
		{
			Measurement m;
			Reason r;
			bool coherent;
			uint32_t missing;

			// Los clusters crudos se conservan junto a la decision. Sin ellos el registro
			// podria decir "clase=desconocida" pero no podria decir CUAL part produjo esa
			// respuesta, y esa es la unica linea que permite extender la tabla con un numero
			// leido en un telefono en lugar de copiar una ficha comercial de memoria.
			ClusterView views[kMaxClusters];
			uint32_t used = 0;  // cuantos se agregaron
			uint32_t real = 0;  // cuantos reporto cpuinfo (used < real => hubo recorte)
		};

		Measured BuildMeasured()
		{
			Measured v;
			v.m = CollectMeasurement(v.views, kMaxClusters, &v.used, &v.real);
			v.r = DecideTier(v.m);
			v.coherent = Coherent(v.m, v.r);
			v.missing = MissingFields(v.m);
			return v;
		}

		const Measured& GetMeasured()
		{
			// Magic static por el mismo motivo que GetCachedDeviceProfile: microVU llama a
			// GetDeviceTier() al calcular el sentinel de opciones de un programa VU1, o sea
			// esto se lee en el hilo del VU y tiene que ser una medida, no un sysfs por
			// programa.
			static const Measured value = BuildMeasured();
			return value;
		}

		DeviceMeasure BuildDeviceMeasure()
		{
			const Measured& v = GetMeasured();
			DeviceMeasure d;
			d.tier = static_cast<int>(TierOf(v.r));
			d.tier_name = TierName(TierOf(v.r));
			d.reason = ReasonName(v.r);
			d.big_class_name = BigClassName(v.m.big_class);
			d.big_freq_khz = v.m.big_freq_khz;
			d.total_ram_mb = v.m.total_ram_mb;
			d.avail_ram_mb = v.m.avail_ram_mb;
			d.big_cores = v.m.big_cores;
			d.total_cores = v.m.total_cores;
			d.clusters = v.m.clusters;
			d.missing_fields = v.missing;
			d.cores_measured = v.m.cores_measured;
			d.ram_measured = v.m.ram_measured;
			d.class_measured = v.m.big_class_measured;
			d.freq_measured = v.m.big_freq_measured;
			d.coherent = v.coherent;

			// Los MIDR crudos, formateados por la ley del header (no por una cadena
			// inventada aqui). Si el buffer no dio para los `used` clusters, se marca: un
			// registro recortado tiene que decir que esta recortado.
			char dump[kMaxClusters * 64];
			const uint32_t written = FormatClusters(v.views, v.used, dump, sizeof(dump));
			d.clusters_dump = dump;
			d.clusters_printed = written;
			d.clusters_dump_truncated = (written < v.used);
			d.clusters_real = v.real;
			return d;
		}

		const char* YesNo(bool b)
		{
			return b ? "si" : "NO";
		}

		const char* VendorName(GPUVendor v)
		{
			switch (v)
			{
				case GPUVendor::Qualcomm: return "Qualcomm";
				case GPUVendor::ARM: return "ARM";
				case GPUVendor::Imagination: return "Imagination";
				case GPUVendor::Other: return "Other";
				case GPUVendor::Unknown: break;
			}
			return "Unknown";
		}

		DeviceProfile BuildProfile()
		{
			DeviceProfile p;
			p.soc_model_prop = GetSystemProperty("ro.soc.model");
			p.hardware_prop = GetSystemProperty("ro.hardware");
			p.board_platform = GetSystemProperty("ro.board.platform");
			p.product_board = GetSystemProperty("ro.product.board");
			p.resolved_model = GetQualcommSocModelTraced(&p.resolved_source);
			p.gpu_vendor = VendorName(DetectGPUVendor());
			// GetDeviceTier() no vuelve a leer propiedades: el perfil sale de la medida
			// ya guardada (GetMeasured, magic static). Se pide aqui y no en otro lado
			// para que el numero que ve el registro del perfil sea el mismo que aplican
			// los ajustes.
			p.profile = GetDeviceTier();

			Console.WriteLn("Cenit hardware: soc.model='%s' hardware='%s' platform='%s' board='%s'",
				p.soc_model_prop.c_str(), p.hardware_prop.c_str(),
				p.board_platform.c_str(), p.product_board.c_str());
			Console.WriteLn("Cenit hardware: resuelto=%u fuente='%s' vendor='%s' perfil=%d",
				p.resolved_model, p.resolved_source.empty() ? "n/d" : p.resolved_source.c_str(),
				p.gpu_vendor.c_str(), p.profile);

			// Las dos lineas de arriba dicen cual chip dice el OEM y de donde salio ese
			// numero. Estas dicen que se midio del chip y por que salio ese perfil; sin
			// ellas, un "perfil=0" en el log es indistinguible de "el kernel no me dejo
			// leer", y esa confusion es justo lo que este ciclo vino a cerrar.
			PrintDeviceMeasure();
			return p;
		}
	} // namespace

	const DeviceProfile& GetCachedDeviceProfile()
	{
		// Magic static: se construye en el primer uso y después queda inmutable,
		// así que el hilo del HUD lo lee sin lock. DetectGPUVendor/GetDeviceTier
		// escriben en el log, por eso se corre una sola vez y no en cada llamada.
		static const DeviceProfile profile = BuildProfile();
		return profile;
	}

	unsigned GetQualcommSocModelTraced(std::string* out_source)
	{
		if (out_source)
			out_source->clear();

		// Qualcomm numbers are normalized to 4 digits here, and they do NOT follow
		// the marketing name: SM7325 is the Snapdragon 778G, SM7250 is the 765G,
		// SM8450 is the 8 Gen 1. So the model is read from the SoC properties and
		// the marketing name is only a secondary source (e.g. "Snapdragon 778G"
		// from OEM builds that never expose ro.soc.model). Legacy msm/apq parts are
		// deliberately skipped: msm8998 is an 835-class chip and its number would
		// misclassify old hardware as high-end.
		static const char* const kKeys[] = {
			"ro.soc.model",
			"ro.hardware",
			"ro.board.platform",
			"ro.product.board",
			"ro.soc.manufacturer",
		};
		std::string candidates[] = {
			GetSystemProperty(kKeys[0]),
			GetSystemProperty(kKeys[1]),
			GetSystemProperty(kKeys[2]),
			GetSystemProperty(kKeys[3]),
			GetSystemProperty(kKeys[4]),
		};

		const auto first_digit_run = [](const std::string& s) {
			size_t i = s.find_first_of("0123456789");
			if (i == std::string::npos)
				return 0u;
			unsigned v = 0;
			int n = 0;
			for (; i < s.size() && s[i] >= '0' && s[i] <= '9' && n < 4; i++, n++)
				v = v * 10u + static_cast<unsigned>(s[i] - '0');
			// 3-digit marketing names (845, 778, 888) become 4-digit model space.
			if (n == 3)
				v *= 10u;
			return (n >= 3 && v >= 400) ? v : 0u;
		};

		for (size_t idx = 0; idx < std::size(candidates); idx++)
		{
			const std::string& raw = candidates[idx];
			std::string s = raw;
			for (char& c : s) c = static_cast<char>(std::tolower(c));
			if (s.empty())
				continue;

			const bool looks_qcom =
				s.rfind("sm", 0) == 0 || s.rfind("sd", 0) == 0 || s.rfind("qcs", 0) == 0 ||
				s.rfind("qcm", 0) == 0 || s.rfind("scg", 0) == 0 || s.rfind("sda", 0) == 0 ||
				s.find("snapdragon") != std::string::npos || s == "qcom" || s == "qualcomm";

			if (!looks_qcom)
				continue;

			// Skip a trailing "qualcomm"/"snapdragon" word before the digits, and the
			// "SM"/"SD" letter prefix: first_digit_run reads from the first digit.
			if (s == "qcom" || s == "qualcomm" || s == "snapdragon")
				continue;

			const unsigned v = first_digit_run(s);
			if (v)
			{
				if (out_source)
					*out_source = kKeys[idx];
				return v;
			}
		}

		// Some OEMs (Huawei/HarmonyOS in particular) expose only the Qualcomm
		// target codename in the board properties and no ro.soc.model at all.
		// Only well-documented codenames are listed; anything else falls back to
		// the conservative mid profile.
		static const struct { const char* codename; unsigned model; } kCodenames[] = {
			{"lito", 7325},      // SD 778G (Huawei Nova 10)
			{"holi", 7350},      // SD 780G
			{"kona", 8250},      // SD 865
			{"laihaina", 8350},  // SD 888
			{"taro", 8450},      // SD 8 Gen 1
			{"waipio", 8550},    // SD 8 Gen 2
			{"parrot", 6225},    // SD 680
			{"bengal", 6115},    // SD 662
		};
		for (size_t idx = 0; idx < std::size(candidates); idx++)
		{
			std::string s = candidates[idx];
			for (char& c : s) c = static_cast<char>(std::tolower(c));
			for (const auto& e : kCodenames)
			{
				if (s == e.codename)
				{
					if (out_source)
						*out_source = std::string("codename:") + e.codename + " via " + kKeys[idx];
					return e.model;
				}
			}
		}
		return 0;
	}

	// Cenit 0.6.37: IsHighEndSnapdragon() SE ELIMINA, no se deja "por si acaso". Era la
	// unica funcion que convertia el NUMERO DE MODELO comercial (serie >= 8, o 7xx con
	// rest >= 300) en "aguanta 2x", y era el primer paso de GetDeviceTier(). Dejarla viva
	// sin llamadores es dejar una segunda copia de una ley que ya no se aplica — el mismo
	// error que el proyecto viene persiguiendo desde h14: dos copias de una decision
	// siempre acaban diciendo cosas distintas. Lo que queda aca abajo, GetQualcommSocModel
	// y las propiedades, son DATOS trazables para el log; la decision es DecideTier.
	//
	// Cenit 0.6.37: el tier MEDIDO. Reemplaza dos cosas que estaban mal por separado:
	//
	//   if (vendor == GPUVendor::Qualcomm)
	//           return IsHighEndSnapdragon() ? 2 : 1;
	//
	// (cualquier Qualcomm salia >= 1 antes de mirar un numero del chip, y eso forzaba
	// VsyncQueueSize=0 en un Snapdragon de entrada) y las dos listas de nombres
	// kCapable[]/kWeak[] para MediaTek/Exynos (un Dimensity 700 = mt6853, 2x Cortex-A76 a
	// 2.2 GHz, estaba listado como DEBIL, y la cadena suelta "helio" arrastraba a toda la
	// serie G por el apellido, incluyendo los G99 que tambien llevan A76). Lo demas era
	// `return 0`: desconocido == gama baja, o sea ningun SoC con un nombre nuevo podia
	// clasificarse sin editar el archivo.
	//
	// Ahora el numero sale de ClusterView/Midr -> ClassOfMidr -> DecideTier, todo en
	// CenitDeviceClass.h. Las listas de nombres desaparecen: no quedan ni kCapable ni
	// kWeak, y no hay ninguna cadena de propiedad comparada en este camino.
	int GetDeviceTier()
	{
		return static_cast<int>(TierOf(GetMeasured().r));
	}

	const DeviceMeasure& GetCachedDeviceMeasure()
	{
		static const DeviceMeasure measure = BuildDeviceMeasure();
		return measure;
	}

	// Las cuatro proyecciones, rebotadas sobre la MISMA razon que devolvio el tier. Estan
	// aqui y no en native-lib.cpp para que el sitio de aplicacion no vuelva a tener una
	// copia de la condicion: en 0.6.36 la copia viva (`GetDeviceTier() == 0` y
	// `tier >= 1`) es exactamente lo que hacia que un telefono de entrada Qualcomm se
	// quedara sin las protecciones de entrada y con la cola de cuadro apagada, las dos
	// cosas al reves de lo que su hardware pedia.
	bool DeviceWantsFastSavestate()
	{
		return WantsFastSavestate(GetMeasured().r);
	}

	bool DeviceWantsUnsyncedReadback()
	{
		return WantsUnsyncedReadback(GetMeasured().r);
	}

	bool DeviceWantsPartialTexturePreload()
	{
		return WantsPartialTexturePreload(GetMeasured().r);
	}

	bool DeviceWantsOptimalPacing()
	{
		return WantsOptimalPacing(GetMeasured().r);
	}

	std::string FormatDeviceMeasureLines()
	{
		const DeviceMeasure& d = GetCachedDeviceMeasure();
		char line[1200];
		std::string out;

		// Cuatro lineas, UNA sola fuente de texto. Cada snprintf usa un buffer de
		// 1200 que cubre con holgura la mas larga (la de midr: "Cenit equipo: midr "
		// + clusters_dump, y clusters_dump esta topado a kMaxClusters*64 = 1024). Si
		// alguna vez el dump creciera, snprintf trunca y la linea pierde el final —
		// por eso el tamano se eligio contra el techo del dump, no al tanteo.
		std::snprintf(line, sizeof(line),
			"Cenit equipo: perfil=%s razon=%s | nucleos=%s(%u) grandes=%u/%u clusters=%u | clase=%s(%s)",
			d.tier_name.c_str(), d.reason.c_str(),
			YesNo(d.cores_measured), d.total_cores,
			d.big_cores, d.total_cores, d.clusters,
			YesNo(d.class_measured), d.big_class_name.c_str());
		out += line;
		out += '\n';

		std::snprintf(line, sizeof(line),
			"Cenit equipo: reloj=%s(%llu kHz) ram=%s(%llu MB) disponible=%llu MB sin-medir=%u/4 coherente=%s",
			YesNo(d.freq_measured), d.big_freq_khz,
			YesNo(d.ram_measured), d.total_ram_mb,
			d.avail_ram_mb, d.missing_fields, YesNo(d.coherent));
		out += line;
		out += '\n';

		std::snprintf(line, sizeof(line),
			"Cenit equipo: clusters usados=%u/%u impresos=%u recortado=%s",
			static_cast<unsigned>(d.clusters), d.clusters_real, d.clusters_printed,
			YesNo(d.clusters_dump_truncated));
		out += line;
		out += '\n';

		std::snprintf(line, sizeof(line), "Cenit equipo: midr %s", d.clusters_dump.c_str());
		out += line;
		out += '\n';
		return out;
	}

	void PrintDeviceMeasure()
	{
		const DeviceMeasure& d = GetCachedDeviceMeasure();

		// Las cuatro lineas de datos salen de FormatDeviceMeasureLines, la MISMA
		// funcion que consume el reporte del boton "Enviar registro" (native-lib).
		// Se parten por \n y se escribe cada renglon con su propio Console.WriteLn
		// para que el registro conserve el timestamp por linea de siempre: el texto
		// es identico al que veia 0.6.37, lo unico que cambia es que ahora hay una
		// sola copia del formateo en vez de cuatro llamadas sueltas.
		const std::string lines = FormatDeviceMeasureLines();
		std::string::size_type start = 0;
		while (start < lines.size())
		{
			const std::string::size_type nl = lines.find('\n', start);
			const std::string::size_type end = (nl == std::string::npos) ? lines.size() : nl;
			if (end > start)
				Console.WriteLn("%s", lines.substr(start, end - start).c_str());
			if (nl == std::string::npos)
				break;
			start = nl + 1;
		}

		if (!d.coherent)
		{
			// Ruido, no un valor: si la ley y los datos se contradicen, el perfil que se
			// esta aplicando no se puede explicar y hay que arreglar el recolector.
			// Unica linea que NO forma parte de FormatDeviceMeasureLines: es un grito
			// para el registro, no un hecho medido para el reporte.
			Console.Error("Cenit equipo: INCOHERENCIA entre la razon y los datos medidos "
				"— el perfil aplicado no se puede justificar, revisar el recolector");
		}
	}
}

#endif // __ANDROID__
