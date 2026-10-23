// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "GS/GSState.h"
#include "GS/Renderers/Common/CenitPresentStats.h"
#include <memory>
#include <string>

class GSRenderer : public GSState
{
private:
	bool Merge(int field);
	bool BeginPresentFrame(bool frame_skip);
	void EndPresentFrame();

	u64 m_shader_time_start = 0;

	std::string m_snapshot;
	u32 m_dump_frames = 0;
	u32 m_skipped_duplicate_frames = 0;
	bool m_saving_metrics = false;

	// Tracking draw counters for idle frame detection.
	u64 m_last_draw_n = 0;
	u64 m_last_transfer_n = 0;

protected:
	GSVector2i m_real_size{0, 0};

	// Cenit 0.6.26: evidencia de lo que se esta presentando de verdad, para el HUD.
	// Sin esto no se puede distinguir "EASU no corrio" de "EASU corrio y se ve mal",
	// que es exactamente como se perdio un ciclo de medicion entero en 0.6.25.
	GSVector2i m_present_src{0, 0};   ///< tamano del buffer interno que se presenta
	GSVector2i m_present_dst{0, 0};   ///< tamano de destino en pantalla (draw_rect)
	bool m_easu_active = false;       ///< la cadena de reconstruccion corrio este frame (NN o EASU)
	bool m_nn_active = false;         ///< Cenit 0.6.41: fue la cadena neuronal la que corrio
	int m_easu_stages = 0;            ///< cuantas pasadas uso la cadena (0 = no corrio)

	// Cenit 0.6.43 — gate adaptativo del NN. En el Nova 10, con God of War, el NN
	// lleva la GPU de 13.2 a 23.6 ms por cuadro: con la CPU sobrada, eso es la
	// diferencia entre 46 y 28 cuadros. La preferencia del usuario no se toca; esto
	// solo la SUSPENDE mientras la GPU ya este saturada, y la vuelve a probar cada
	// tanto para no quedarse apagada por un tramo pesado que ya paso.
	struct NNGateState
	{
		bool suspended = false;   ///< el NN esta suspendido por saturacion de GPU
		bool probing = false;     ///< se dejo correr el NN de nuevo para medir si ahora alcanza
		u32 backoff_seconds = 0;  ///< espera antes del proximo sondeo; crece si el sondeo vuelve a saturar
		u64 next_eval = 0;        ///< timestamp (Common::Timer) del proximo punto de decision
		u64 last_present = 0;     ///< timestamp del present anterior (para medir el intervalo real)
		u64 verified_until = 0;   ///< hasta cuando no se vuelve a sospechar tras un sondeo que salio bien
		u32 suspensions = 0;      ///< cuantas veces se suspendio en esta sesion (evidencia en el log)

		// El veredicto del sondeo no es "usa poca GPU": es "sin NN se juega igual".
		// Se compara el intervalo real entre cuadros con el NN apagado contra el del
		// sondeo encendido. Es la unica medida que no depende del vsync, del limite
		// de cuadros ni del juego.
		double off_interval_sum = 0.0;   ///< suma de intervalos (s) con el NN suspendido
		u32 off_frames = 0;
		double probe_interval_sum = 0.0; ///< suma de intervalos (s) durante el sondeo
		u32 probe_frames = 0;
		double off_interval_avg = 0.0;   ///< referencia congelada al empezar el sondeo
		bool off_interval_valid = false; ///< hubo suficientes cuadros para creerle a esa referencia
	};
	NNGateState m_nn_gate;
	bool UpdateNNGate();

	// Cenit 0.6.36: el HUD dice el frame ACTUAL; esto dice la SESION, y es lo que
	// viaja en el registro. Sin esto, un log puede probar "hubo cuadros por debajo
	// de 1x" (el regidor sostiene 0.5x) y no puede probar si EASU los reconstruyo,
	// los dejo estirados o fallo al intentarlos. Ver CenitPresentStats.h.
	cenit_present::Stats m_present_stats;

	virtual GSTexture* GetOutput(int i, float& scale, int& y_offset) = 0;
	virtual GSTexture* GetFeedbackOutput(float& scale) { return nullptr; }

public:
	GSRenderer();
	virtual ~GSRenderer();

	virtual void Reset(bool hardware_reset) override;

	virtual void Destroy();

	virtual void UpdateRenderFixes();

	virtual void VSync(u32 field, bool registers_written, bool idle_frame);
	virtual bool CanUpscale() { return false; }
	virtual float GetUpscaleMultiplier() { return 1.0f; }
	virtual float GetTextureScaleFactor() { return 1.0f; }
	GSVector2i GetInternalResolution();
	float GetModXYOffset();

	virtual GSTexture* LookupPaletteSource(u32 CBP, u32 CPSM, u32 CBW, GSVector2i& offset, float* scale, const GSVector2i& size);

	bool IsIdleFrame() const;

	bool SaveSnapshotToMemory(u32 window_width, u32 window_height, bool apply_aspect, bool crop_borders,
		u32* width, u32* height, std::vector<u32>* pixels);

	void QueueSnapshot(const std::string& path, const u32 gsdump_frames);
	void StopGSDump();
	void StartSavingMetrics(u32 seconds);
	void DumpSavedMetrics();
	bool IsSavingMetrics();
	void PresentCurrentFrame();
	static GSVector4 GetLastDrawRect();

	// Cenit 0.6.26: evidencia del present para el HUD (ver miembros arriba).
	struct PresentEvidence
	{
		GSVector2i src;
		GSVector2i dst;
		bool easu_active;
		bool nn_active; ///< Cenit 0.6.41: si no, la que corrio (si corrio) fue EASU
		int easu_stages;
	};
	PresentEvidence GetPresentEvidence() const
	{
		return {m_present_src, m_present_dst, m_easu_active, m_nn_active, m_easu_stages};
	}

	bool BeginCapture(std::string filename, const GSVector2i& size = GSVector2i(0, 0));
	void EndCapture();
};

extern std::unique_ptr<GSRenderer> g_gs_renderer;
