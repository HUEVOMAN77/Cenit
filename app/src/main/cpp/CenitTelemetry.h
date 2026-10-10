// SPDX-FileCopyrightText: 2002-2026 Cenit (fork de PCSX2)
// SPDX-License-Identifier: GPL-3.0+

#pragma once

// Cenit 0.6.30 — Telemetría del clasificador de carga, del lado de Java hacia
// el HUD.
//
// Dirección única: Java mide, clasifica y PUBLICA su veredicto aquí; el HUD de
// ImGui (C++, hilo GS) lo LEE y lo pinta. Nada de lo que vive aquí entra nunca
// en la simulación: es un cuadro de instrumentos. Por eso:
//  - memoria order relaxed en todo (un valor medio segundo viejo no cambia un
//    frame, y un acquire/acquire cruzando GS<->VM sería coste sin beneficio);
//  - tipos escalares atómicos, sin punteros ni cadenas (un std::string entre TUs
//    con hilos que no se sincronizan es una carrera de destrucción);
//  - el lado C++ no escribe jamás: si alguien necesita "ver" el thermal status,
//    se agrega un setter junto a PublishLoadVerdict, no se toca el estado.
//
// Por qué un header y no `extern` declarado a mano en ImGuiOverlays.cpp: los
// símbolos con enlace interno (static / namespace anónimo) NO se ven desde otro
// TU — declarar `extern std::atomic<int> foo;` y definirlo en un `namespace {}`
// compila y revienta en el link. Este header define los accesors en línea sobre
// un solo objeto de estado, así que el enlace es correcto por construcción y no
// depende de que los dos lados escriban la misma firma a mano.

#include <atomic>

namespace CenitTelemetry
{
	// Estado/acción según docs/CONTRATO_CENIT_LOAD_SAMPLE.md (los define Java;
	// el HUD solo los traduce a texto).
	enum class LoadState : int
	{
		Unknown = 0,
		NoBottleneck = 1,
		GpuBound = 2,
		CpuBound = 3,
		VuBound = 4,
		ThermalBound = 5,
		InvalidSample = 6,
	};

	enum class LoadAction : int
	{
		Hold = 0,
		AllowResolutionDrop = 1,
		BlockResolutionDrop = 2,
		WaitForEvidence = 3,
	};

	struct LoadVerdict
	{
		std::atomic<int> state{static_cast<int>(LoadState::Unknown)};
		std::atomic<int> confidence_pct{-1}; // <0 = "todavia no hay veredicto"
		std::atomic<int> action{static_cast<int>(LoadAction::Hold)};
		std::atomic<double> gpu_pct{0.0};
		std::atomic<double> ee_pct{0.0};
		std::atomic<int> thermal_status{-1};  // -1 = sin reportar (no es "0=nominal")

		LoadVerdict& operator=(const LoadVerdict&) = delete;
		LoadVerdict(const LoadVerdict&) = delete;
		LoadVerdict() = default;
	};

	inline LoadVerdict g_load_verdict;

	// Publicación desde JNI (hilo de la UI de Android).
	inline void PublishLoadVerdict(const int state, const int confidence_pct,
		const int action, const double gpu_pct, const double ee_pct,
		const int thermal_status)
	{
		LoadVerdict& v = g_load_verdict;
		v.state.store(state, std::memory_order_relaxed);
		v.confidence_pct.store(confidence_pct, std::memory_order_relaxed);
		v.action.store(action, std::memory_order_relaxed);
		v.gpu_pct.store(gpu_pct, std::memory_order_relaxed);
		v.ee_pct.store(ee_pct, std::memory_order_relaxed);
		v.thermal_status.store(thermal_status, std::memory_order_relaxed);
	}

	// Reinicio del cuadro al arrancar/apagar la partida: sin esto, la partida nueva
	// mostraría el veredicto del juego anterior colgado en el overlay, que es
	// justamente el tipo de fantasma que hace confiar en un número viejo. Lo llama
	// Java (JNI resetCenitLoadVerdict) desde el start() y el stop() del regidor, no
	// el núcleo: el núcleo no sabe cuándo "cambia la partida" para el HUD, y el
	// regidor sí.
	inline void ResetLoadVerdict()
	{
		LoadVerdict& v = g_load_verdict;
		v.state.store(static_cast<int>(LoadState::Unknown), std::memory_order_relaxed);
		v.confidence_pct.store(-1, std::memory_order_relaxed);
		v.action.store(static_cast<int>(LoadAction::Hold), std::memory_order_relaxed);
		v.gpu_pct.store(0.0, std::memory_order_relaxed);
		v.ee_pct.store(0.0, std::memory_order_relaxed);
		v.thermal_status.store(-1, std::memory_order_relaxed);
	}
}
