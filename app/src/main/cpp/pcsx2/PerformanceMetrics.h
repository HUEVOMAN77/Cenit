// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include <array>
#include "common/Threading.h"

namespace PerformanceMetrics
{
	enum class InternalFPSMethod
	{
		None,
		GSPrivilegedRegister,
		DISPFBBlit
	};

	class AvgFPS
	{
	private:
		static constexpr size_t FPS_BUFFER_SIZE = 16;
		std::array<float, FPS_BUFFER_SIZE> fps_buff{};
		size_t count = 0;
		size_t pos = 0;
		float avg_fps = 0.0f;

	public:
		void ClearStats()
		{
			count = 0;
			pos = 0;
			fps_buff.fill(0.0f);
			avg_fps = 0.0f;
		}

		void UpdateAvgFPS(float new_fps_val)
		{
			// If the change is quite dramatic, the user has either turned off the frame limiter or the game is dying.
			// Clear it to catch it up quickly.
			if (count > 0)
			{
				const float last_fps_val = fps_buff[(pos - 1) & (FPS_BUFFER_SIZE - 1)];
				if (std::abs(last_fps_val - new_fps_val) > last_fps_val * 0.20f)
					ClearStats();
			}

			fps_buff[pos] = new_fps_val;
			pos = (pos + 1) & (FPS_BUFFER_SIZE - 1); // if you use values which don't become all 1's, make it %.
			count = std::min(count + 1, FPS_BUFFER_SIZE);

			float avg_sum = 0.0f;

			for (size_t i = 0; i < count; i++)
			{
				avg_sum += fps_buff[i];
			}

			avg_fps = avg_sum / static_cast<float>(count);
		}

		float GetAvgFPS()
		{
			return avg_fps;
		}
	};

	static constexpr u32 NUM_FRAME_TIME_SAMPLES = 150;
	using FrameTimeHistory = std::array<float, NUM_FRAME_TIME_SAMPLES>;

	void Clear();
	void Reset();
	void Update(bool gs_register_write, bool fb_blit, bool is_skipping_present);
	void OnGPUPresent(float gpu_time, u64 vs_invocations, u64 ps_invocations);

	/// Logs the whole-session average framerate (frames since Clear() over
	/// wall time). Called at VM shutdown so every -logfile run records it.
	void LogSessionSummary();

	/// Sets the EE thread for CPU usage calculations.
	void SetCPUThread(Threading::ThreadHandle thread);

	/// Sets timers for GS software threads.
	void SetGSSWThreadCount(u32 count);
	void SetGSSWThread(u32 index, Threading::ThreadHandle thread);

	u64 GetFrameNumber();

	InternalFPSMethod GetInternalFPSMethod();
	bool IsInternalFPSValid();

	float GetFPS();
	float GetAvgVPS();
	float GetInternalFPS();
	float GetSpeed();
	float GetAverageFrameTime();
	float GetMinimumFrameTime();
	float GetMaximumFrameTime();

	double GetCPUThreadUsage();
	double GetCPUThreadAverageTime();
	float GetGSThreadUsage();
	float GetGSThreadAverageTime();
	float GetVUThreadUsage();
	float GetVUThreadAverageTime();
	float GetCaptureThreadUsage();
	float GetCaptureThreadAverageTime();

	u32 GetGSSWThreadCount();
	double GetGSSWThreadUsage(u32 index);
	double GetGSSWThreadAverageTime(u32 index);

	float GetGPUUsage();
	float GetGPUAverageTime();
	double GetGPUAverageVSInvocations();
	double GetGPUAveragePSInvocations();

	/// Cenit 0.6.13: coste de la sincronía EE<->VU1 en la última ventana de
	/// actualización (0.5 s). wait_ms = tiempo que el EE estuvo BLOQUEADO esperando
	/// al VU1; wait_calls = cuántas veces. exec_ms = tiempo publicando trabajo.
	/// Con esto se responde con números "¿el frame lento esperaba al VU1 o el VU1
	/// estaba ocupado de verdad?" — la pregunta que la revisión de ingeniería pidió
	/// responder antes de tocar ningún hack.
	struct MtvuSyncStats
	{
		double wait_ms = 0.0;
		u32 wait_calls = 0;
		double exec_ms = 0.0;
		u32 exec_calls = 0;
	};
	MtvuSyncStats GetMtvuSyncStats();

	const FrameTimeHistory& GetFrameTimeHistory();
	u32 GetFrameTimeHistoryPos();

	/// Cenit 0.6.30 (contrato CenitLoadSample): presents de la ventana CERRADA
	/// (la misma que produjó s_gpu_usage y s_fps), NO el contador vivo de la
	/// ventana que está corriendo ahora — ese se pone a 0 al cerrar, así que
	/// leerlo desde JNI después de un Update() sería la ventana siguiente.
	/// Es lo único que puede distinguir "GPU realmente holgada" de "el driver no
	/// reporta tiempo de GPU", y por eso se expone el CRUDO, no un bool derivado.
	u32 GetPresentsInWindow();

	/// Cenit 0.6.30 (contrato CenitLoadSample): segundos reales de la última
	/// ventana cerrada. El clasificador necesita saber si la ventana fue
	/// completa o un Update() recortado, porque un denominador chico infla o
	/// anula los porcentajes.
	float GetLastWindowSeconds();

	/// Cenit 0.6.30 (contrato CenitLoadSample, Regla 3): si la lectura de EE de la
	/// última ventana tiene denominador valido (hilo EE vivo, ticks_delta > 0 y
	/// GetThreadTicksPerSecond() > 0). NO se deriva de "EE% > 0": eso confundiria
	/// metrica invalida con EE ocioso. Se calcula en el mismo punto donde se
	/// calcula s_cpu_thread_usage, y se limpia junto con ella en Clear().
	bool GetEEThreadMetricValid();

	/// Cenit 0.6.30 (contrato CenitLoadSample): secuencia de ventana. Se
	/// incrementa al FINAL de Update() (donde se limpian los contadores de
	/// ventana, ~L385-387). Sirve para detectar si el snapshot es de un
	/// Update() posterior al último ResetStatistics/Clear (arranque de VM,
	/// cambio de juego).
	u64 GetWindowSequence();

	struct SavedMetrics
	{
		float num_samples;
		float frames;
		float time;
		float fps;
		float internal_fps;
		float cpu_thread_usage;
		float cpu_thread_time;
		float gs_thread_usage;
		float gs_thread_time;
		float gpu_time;
		float gpu_usage;
	};

	void StartSavingMetrics(u32 seconds);
	bool IsSavingMetrics();
	void DumpSavedMetrics();
} // namespace PerformanceMetrics
