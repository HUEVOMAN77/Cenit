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

	// True when the SoC is Adreno-class strong enough to run hardware GS at 2x
	// comfortably (SD 8-series or SD 778+).
	bool IsHighEndSnapdragon();

	// Cenit 0.6.4: 0 = gama baja (A53/A55 o desconocido), 1 = gama media
	// (Snapdragon no-top o MediaTek A76+), 2 = gama alta Snapdragon. Reemplaza
	// al tier binario de antes y ahora clasifica también MediaTek por codename
	// de plataforma (plan del inge §3.1: "no heurística genérica").
	int GetDeviceTier();
}

#endif // __ANDROID__
