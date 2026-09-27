// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "GS/GSState.h"
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
	bool m_easu_active = false;       ///< la cadena EASU -> RCAS corri6 este frame
	int m_easu_stages = 0;            ///< cuantas pasadas uso la cadena (0 = no corrio)

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
		int easu_stages;
	};
	PresentEvidence GetPresentEvidence() const { return {m_present_src, m_present_dst, m_easu_active, m_easu_stages}; }

	bool BeginCapture(std::string filename, const GSVector2i& size = GSVector2i(0, 0));
	void EndCapture();
};

extern std::unique_ptr<GSRenderer> g_gs_renderer;
