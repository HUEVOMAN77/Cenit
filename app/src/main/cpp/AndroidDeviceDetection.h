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

	// True when the SoC is Adreno-class strong enough to run hardware GS at 2x
	// comfortably (SD 8-series or SD 778+).
	bool IsHighEndSnapdragon();

	// Cenit 0.6.14: 0 = gama baja (A53/A55 o desconocido), 1 = gama media
	// (Snapdragon no-top o MediaTek A76+), 2 = gama alta Snapdragon. Reemplaza
	// al tier binario de antes y ahora clasifica también MediaTek por codename
	// de plataforma (plan del inge §3.1: "no heurística genérica").
	int GetDeviceTier();

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
	// Compatibilidad: se calcula con las mismas propiedades que ya usaban
	// IsSnapdragon/GetDeviceTier, así que Qualcomm, Mali, PowerVR, Exynos,
	// Kirin, Rockchip y Tensor siguen clasificando igual; solo se anota de
	// dónde salió cada valor.
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
