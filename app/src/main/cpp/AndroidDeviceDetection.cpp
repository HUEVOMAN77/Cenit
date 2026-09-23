// SPDX-FileCopyrightText: 2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#ifdef __ANDROID__

#include "AndroidDeviceDetection.h"
#include "common/Console.h"
#include <sys/system_properties.h>
#include <cctype>
#include <cstring>

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

	GPUVendor DetectGPUVendor()
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
		// Qualcomm numbers are normalized to 4 digits here, and they do NOT follow
		// the marketing name: SM7325 is the Snapdragon 778G, SM7250 is the 765G,
		// SM8450 is the 8 Gen 1. So the model is read from the SoC properties and
		// the marketing name is only a secondary source (e.g. "Snapdragon 778G"
		// from OEM builds that never expose ro.soc.model). Legacy msm/apq parts are
		// deliberately skipped: msm8998 is an 835-class chip and its number would
		// misclassify old hardware as high-end.
		std::string candidates[] = {
			GetSystemProperty("ro.soc.model"),
			GetSystemProperty("ro.hardware"),
			GetSystemProperty("ro.board.platform"),
			GetSystemProperty("ro.product.board"),
			GetSystemProperty("ro.soc.manufacturer"),
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

		for (const std::string& raw : candidates)
		{
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
				return v;
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
		for (const std::string& raw : candidates)
		{
			std::string s = raw;
			for (char& c : s) c = static_cast<char>(std::tolower(c));
			for (const auto& e : kCodenames)
			{
				if (s == e.codename)
					return e.model;
			}
		}
		return 0;
	}

	bool IsHighEndSnapdragon()
	{
		if (DetectGPUVendor() != GPUVendor::Qualcomm)
			return false;

		const unsigned soc = GetQualcommSocModel();
		if (soc == 0)
			return false;

		// 8-series is always fine. For 7-series the hundreds digit tracks the
		// generation: SM7250 (765G) -> 250, SM7325 (778G) -> 325, SM7450 (7 Gen 1)
		// -> 450, SM7475 (7+ Gen 2) -> 475, SM7550 (7+ Gen 3) -> 550. Kryo 670 /
		// Adreno 642L and newer (the 778G upward) handle hardware GS at 2x on most
		// of the catalogue; older 7-series (710/720/765) are kept on the conservative
		// mid profile so they don't drop frames trying.
		const unsigned series = soc / 1000u;
		const unsigned rest = soc % 1000u;
		if (series >= 8)
			return true;
		if (series == 7 && rest >= 300)
			return true;
		return false;
	}

	// Cenit 0.6.4 (plan del inge §3.1): el tier anterior era casi binario para
	// Snapdragon y mandaba TODO lo demás a gama baja, incluyendo Dimensity que
	// corren hardware GS a 1.5x sin despeinarse. Ahora MediaTek/Exynos se
	// clasifican por el codename de plataforma (ro.board.platform), que es lo
	// que Android expone de forma fiable en esos SoCs.
	//   2 = gama alta: sostiene 2x de entrada
	//   1 = gama media: 1.5x de entrada
	//   0 = gama baja: 1x de entrada, governor y CAS cargan con el resto
	int GetDeviceTier()
	{
		const GPUVendor vendor = DetectGPUVendor();

		if (vendor == GPUVendor::Qualcomm)
			return IsHighEndSnapdragon() ? 2 : 1;

		auto toLower = [](std::string str) {
			for (char& c : str) c = std::tolower(c);
			return str;
		};
		std::string hay = toLower(GetSystemProperty("ro.board.platform"));
		if (hay.empty())
			hay = toLower(GetSystemProperty("ro.hardware"));
		if (hay.empty())
			hay = toLower(GetSystemProperty("ro.product.board"));

		// Big cores Cortex-A78/A7x modernos y GPU de su liga: medio-alto.
		static const char* const kCapable[] = {
			"mt6873", "mt6875", "mt6877", "mt6883", "mt6885", "mt6889",
			"mt6890", "mt6891", "mt6892", "mt6893", "mt6895", "mt6897",
			"mt6980", "mt6983", "mt6985", "mt6989", "mt6990", "mt6991", "mt6993",
			"mt8183", "mt8192", "mt8195", // tablets, pero la detección es la misma
			"exynos9810", "exynos9820", "exynos9825", "exynos980", "exynos9815",
			"exynos1380", "exynos2100", "exynos2200", "exynos2400",
		};
		for (const char* p : kCapable)
		{
			if (hay.find(p) != std::string::npos)
				return 1;
		}

		// Gama baja conocida (A53/A55 puros o big cores débiles): 0 explícito.
		static const char* const kWeak[] = {
			"mt6739", "mt6761", "mt6762", "mt6763", "mt6765", "mt6768", "mt6771",
			"mt6779", "mt6785", "mt6832", "mt6833", "mt6853", "mt6855",
			"exynos7870", "exynos7880", "exynos7884", "exynos7885", "exynos850",
			"exynos9609", "exynos9610", "exynos9611",
			"kirin710", "kirin620", "kirin650", "helio",
		};
		for (const char* p : kWeak)
		{
			if (hay.find(p) != std::string::npos)
				return 0;
		}

		// Desconocido: conservador, que el governor suba si da.
		return 0;
	}
}

#endif // __ANDROID__
