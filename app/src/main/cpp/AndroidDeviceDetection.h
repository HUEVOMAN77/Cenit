// SPDX-FileCopyrightText: 2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#ifdef __ANDROID__

#include <string>

namespace AndroidDeviceDetection
{
	enum class GPUVendor
	{
		Unknown,
		Qualcomm,  // Adreno (Snapdragon)
		ARM,       // Mali (Mediatek, Exynos, etc.)
		Imagination, // PowerVR
		Other
	};

	// Detect GPU vendor from system properties and GL/Vulkan strings
	GPUVendor DetectGPUVendor();

	// Check if device is Mediatek (Mali GPU)
	bool IsMediatek();

	// Check if device is Snapdragon (Adreno GPU)
	bool IsSnapdragon();

	// Get device manufacturer
	std::string GetManufacturer();

	// Get device model
	std::string GetModel();

	// Get GPU renderer string (requires GL context or Vulkan device)
	std::string GetGPURenderer();

	// Qualcomm SoC model number parsed from ro.soc.model / ro.hardware, e.g. 778.
	// Returns 0 when unknown. Used to size CPU/GPU profiles per generation.
	unsigned GetQualcommSocModel();

	// Cenit 0.6.14: mismo valor, pero diciendo DE DÓNDE salió. La revisión pide
	// que el SoC sea trazable: "7325" sin fuente no permite distinguir "lo dijo
	// ro.soc.model" de "lo deduje del codename lito" de "me lo inventé".
	// out_source queda con el nombre de la propiedad/codename que casó, o vacío.
	unsigned GetQualcommSocModelTraced(std::string* out_source);

	// Cenit 0.6.37: IsHighEndSnapdragon() se ELIMINA junto con su definicion. Decidia
	// "aguanta 2x" mirando el NUMERO DE MODELO comercial (serie >= 8, o 7xx con rest >=
	// 300) y era el primer paso de GetDeviceTier(); dejarla declarada sin llamadores es
	// dejar una segunda copia de una ley que ya no se aplica. El numero de modelo sigue
	// disponible arriba, como dato trazable para el log.

	// Cenit 0.6.37: 0 = gama baja, 1 = media, 2 = alta — MEDIDO sobre los clusters que
	// reportan los registros del kernel (MIDR), el reloj que expone cpufreq y la RAM
	// fisica. Antes (0.6.14..0.6.36) era `IsHighEndSnapdragon() ? 2 : 1` para TODO
	// Qualcomm mas dos listas de nombres para MediaTek/Exynos, o sea: la marca decidia
	// antes de mirar un numero del chip y un SoC con un nombre nuevo era gama baja por
	// definicion. La ley vive en CenitDeviceClass.h (C++ puro, verificable con g++ de
	// host via Harness15); aqui solo se recolectan los datos.
	int GetDeviceTier();

	// Cenit 0.6.37: lo que se midio y POR QUE salio ese tier. La revision anterior pidio
	// que el SoC fuera trazable ("de donde salio 7325"); esta pide lo mismo sobre la
	// clasificacion, porque "perfil=1" sin la razon no permite distinguir "a este chip le
	// falta reloj" de "el kernel no me dejo leer el reloj". `coherent` es el flag del
	// arnes: si DecideTier y los datos se contradicen, esta linea no se puede explicar y
	// hay que arreglar el recolector, no el telefono.
	struct DeviceMeasure
	{
		std::string tier_name;       // alta / media / baja
		std::string reason;          // cumple-todo, poca-ram, frecuencia-no-leida, ...
		std::string big_class_name;  // rendimiento / media / chica / desconocida
		int tier = -1;               // mismo valor que GetDeviceTier()
		unsigned long long big_freq_khz = 0;
		unsigned long long total_ram_mb = 0;
		unsigned long long avail_ram_mb = 0;
		unsigned big_cores = 0;
		unsigned total_cores = 0;
		unsigned clusters = 0;
		unsigned missing_fields = 0; // de 4: nucleos, RAM, clase, frecuencia
		bool cores_measured = false;
		bool ram_measured = false;
		bool class_measured = false;
		bool freq_measured = false;
		bool coherent = false;

		// Los MIDR CRUDOS de cada cluster, ya formateados por FormatClusters del header:
		// "#0 midr=0x41d41002 n=1 khz=2400000 clase=rendimiento | ...". Sin esta linea, un
		// clase=desconocida NO se puede atribuir: "el kernel no reporto el registro" y "el
		// part existe pero no esta en la tabla" se ven identicos, y la unica forma de
		// extender la tabla con datos en vez de con fichas de memoria es que el numero
		// llegue en el log. `clusters_dump_truncated` dice si el buffer no dio para todos.
		std::string clusters_dump;
		unsigned clusters_real = 0;    // los que cpuinfo reporto (clusters puede ser menor)
		unsigned clusters_printed = 0; // los que cupieron en clusters_dump
		bool clusters_dump_truncated = false;
	};

	// Cached del mismo modo que GetCachedDeviceProfile: se mide una vez, despues queda
	// inmutable y el hilo del HUD lo lee sin lock.
	const DeviceMeasure& GetCachedDeviceMeasure();

	// Imprime las dos lineas "Cenit equipo:" con los denominadores de cada campo. Lo llama
	// BuildProfile() una sola vez, junto a las lineas "Cenit hardware:" que ya existian:
	// las propiedades dicen QUE chip dice el OEM, y estas dicen QUE se midio y POR QUE
	// salio ese perfil.
	void PrintDeviceMeasure();

	// Las cuatro proyecciones que antes estaban escritas como `GetDeviceTier() == 0` y
	// `tier >= 1` sueltos en native-lib.cpp. Viven aqui, rebotadas sobre la ley del
	// header, para que el sitio de aplicacion no vuelva a tener una copia de la
	// condicion.
	bool DeviceWantsFastSavestate();
	bool DeviceWantsUnsyncedReadback();
	bool DeviceWantsPartialTexturePreload();
	bool DeviceWantsOptimalPacing();

	// Cenit 0.6.14: todo lo que usamos para decidir el perfil, junto con la
	// FUENTE de cada número. La revisión pide que SoC y GPU sean trazables:
	// "SM7325" sin origen no permite distinguir "lo dijo ro.soc.model" de "lo
	// deduje del codename lito".
	//
	// Aquí NO se guarda el nombre de la GPU: ese lo reporta el renderer Vulkan
	// (g_gs_device->GetName()) y el HUD lo lee directo. Mezclar en una misma
	// estructura un dato inmutable de properties con uno que cambia al abrir el
	// device obligaría a sincronizar entre hilos para nada.
	//
	// Compatibilidad: LOS CUATRO CAMPOS DE PROPIEDADES se calculan con las mismas
	// propiedades de siempre (siguen siendo la fuente trazable del nombre del SoC). Lo
	// que NO es compatible es `profile`: hasta 0.6.36 salia de listas de nombres, y desde
	// 0.6.37 sale de los numeros medidos del chip, así que un telefono puede cambiar de
	// perfil sin que nadie edite una lista — que es el punto del cambio. Las cadenas de
	// propiedad ya no deciden nada.
	struct DeviceProfile
	{
		std::string soc_model_prop; // ro.soc.model crudo (suele venir vacío en Huawei)
		std::string hardware_prop;  // ro.hardware crudo
		std::string board_platform; // ro.board.platform crudo
		std::string product_board;  // ro.product.board crudo
		std::string resolved_source; // de dónde salió resolved_model
		unsigned resolved_model = 0; // 7325 etc., 0 = desconocido
		int profile = -1;            // GetDeviceTier(): 2 alto, 1 medio, 0 bajo
		std::string gpu_vendor;      // Qualcomm / ARM / Imagination / Other / Unknown
	};

	// Inicializado una vez (magic static: thread-safe y luego inmutable), porque
	// lo lee el hilo del HUD en cada refresco y leer propiedades de Android por
	// frame no es aceptable.
	const DeviceProfile& GetCachedDeviceProfile();
}

#endif // __ANDROID__
