// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// Cuerpo de la sonda de trazas de la Fase 1 del motor de superbloques VU.
// Ver MvuTraceProbe-arm64.h para el diseño completo y las garantías.
//
// Este TU NO incluye microVU-arm64.h: no necesita ver el struct (el array de
// slots vive dentro de microVU y se expone por SlotArray(), definido allí),
// y así se mantiene libre de la superficie de vixl.

#include "MvuTraceProbe-arm64.h"

#include "BuildVersion.h"
#include "Config.h"
#include "VMManager.h"
#include "common/Console.h"
#include "common/FileSystem.h"
#include "common/Path.h"

#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace mVUTraceProbe
{
	std::atomic<bool> g_enabled{false};
	VuFlow g_flow[2];
	BlockShape g_shape[2][kBlkPcs];
	TransitionEntry g_transitions[2][kTransitionCap];
	ProgramEntry g_programs[2][kProgramCap];
	std::atomic<u32> g_usedTransitions[2];
	std::atomic<u32> g_usedPrograms[2];
	std::atomic<u64> g_dropped[2];

	// ------------------------------------------------------------------
	// Internos.
	//
	// Regla de escritura: las tablas estructuradas (transiciones/programas)
	// SOLO se tocan desde ObserveDispatch, y ObserveDispatch solo se llama
	// con la sonda encendida (comprobación en el call site del dispatcher,
	// que es ademas el hilo único escritor de ese VU). Por eso el flanco
	// OFF->ON puede limpiar sin locks: en ese instante nadie escribe — el
	// unico riesgo residual es un ObserveDispatch en vuelo de la sesion ON
	// anterior (hilo desprogramado entre la comprobacion y la escritura);
	// lo peor que hace es dejar un contador roto en la ventana vieja, y
	// como la limpieza es anterior a encender, ni siquiera eso: la escritura
	// en vuelo cae sobre memoria ya puesta a cero. Telemetria, nunca estado
	// de emulacion.
	// ------------------------------------------------------------------
	namespace
	{
		// Ultimo despacho resuelto por VU — encadena las transiciones. Solo
		// lo toca el hilo dispatcher de ese VU.
		struct VuHistory
		{
			u64 prevHashLo = 0;
			u32 prevPC = 0;
			bool hasPrev = false;
		};
		VuHistory g_hist[2];

		// Firma de la transicion: mezcla (prevHash, prevPC, curHash, curPC)
		// con avalanche multiply/xor-shift.
		u64 MixTransitionKey(u64 prevHashLo, u32 prevPC, u64 curHashLo, u32 curPC)
		{
			u64 h = prevHashLo;
			h ^= curHashLo + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
			const u64 pcs = (static_cast<u64>(prevPC) << 16) | curPC;
			h ^= pcs + 0xff51afd7ed558ccdull + (h << 6) + (h >> 2);
			h ^= h >> 31;
			h *= 0x94d049bb133111ebull;
			h ^= h >> 29;
			return h ? h : 1u; // key == 0 reserva el hueco vacio
		}

		__fi u32 KeyToBucket(u64 key)
		{
			// La firma ya sale avalanchada; los bits bajos estan bien.
			return static_cast<u32>(key) & kTransitionMask;
		}

		__fi u32 HashLoToProgBucket(u64 hashLo)
		{
			return static_cast<u32>(hashLo ^ (hashLo >> 32)) & kProgramMask;
		}

		void ResetAllState()
		{
			for (int vu = 0; vu < 2; vu++)
			{
				auto& f = g_flow[vu];
				f.stubCalls.store(0, std::memory_order_relaxed);
				f.stubHits.store(0, std::memory_order_relaxed);
				f.fastHits.store(0, std::memory_order_relaxed);
				f.slowMiss.store(0, std::memory_order_relaxed);
				f.compiles.store(0, std::memory_order_relaxed);
				f.jumpEntries.store(0, std::memory_order_relaxed);
				f.jumpCacheHits.store(0, std::memory_order_relaxed);
				f.microWrites.store(0, std::memory_order_relaxed);
				std::memset(g_transitions[vu], 0, sizeof(g_transitions[vu]));
				std::memset(g_programs[vu], 0, sizeof(g_programs[vu]));
				g_usedTransitions[vu].store(0, std::memory_order_relaxed);
				g_usedPrograms[vu].store(0, std::memory_order_relaxed);
				g_dropped[vu].store(0, std::memory_order_relaxed);
				g_hist[vu] = {};
			}
		}

		std::mutex s_dumpMutex; // serializa DumpReport (toggle + JNI + close)

		// Escritor de linea del informe con chequeo de formato en tiempo de
		// compilacion (attribute format printf). El lambda con auto... que
		// reenviaba a fprintf disparaba -Wformat-security y ademas NO validaba
		// ninguna cadena: con esto cada P(...) del informe se audita al
		// compilar. fp==null es no-op (el informe siempre va al emulog por
		// Console, el archivo es el extra).
		void ProbePrintf(std::FILE* fp, const char* fmt, ...)
#if defined(__GNUC__)
			__attribute__((format(printf, 2, 3)))
#endif
			;
		void ProbePrintf(std::FILE* fp, const char* fmt, ...)
		{
			if (!fp)
				return;
			va_list ap;
			va_start(ap, fmt);
			std::vfprintf(fp, fmt, ap);
			va_end(ap);
		}
	}

	void SyncFromConfig(bool enabled, u64* slots0, u64* slots1)
	{
		const bool was = g_enabled.load(std::memory_order_relaxed);
		if (was == enabled)
			return;

		if (was)
		{
			// ON -> OFF: apaga primero, despues vuelca lo medido. No limpia:
			// los contadores de la ultima sesion quedan legibles hasta el
			// proximo encendido (el HUD etiqueta "sonda apagada").
			g_enabled.store(false, std::memory_order_relaxed);
			Console.WriteLn("VUprobe: sonda DESACTIVADA — volcando medicion");
			DumpReport("sonda desactivada");
			return;
		}

		// OFF -> ON: con la sonda apagada nadie escribe las tablas ni los
		// slots, asi que la limpieza total va ANTES de encender.
		ResetAllState();
		std::memset(slots0, 0, kProbeSlots * sizeof(u64));
		std::memset(slots1, 0, kProbeSlots * sizeof(u64));
		std::memset(g_shape, 0, sizeof(g_shape)); // Fase 1.5: formas viejas fuera
		g_enabled.store(true, std::memory_order_relaxed);
		Console.WriteLn(Color_StrongGreen, "VUprobe: sonda ACTIVADA — midiendo dispatcher/bloques/secuencias "
			"(cache en disco en pausa; el informe sale al apagar la sonda, al parar el juego o a peticion)");
	}

	void ObserveDispatch(int vu, u32 startPC_bytes, u64 hashLo)
	{
		// --- tabla de programas ---
		{
			ProgramEntry* pe = g_programs[vu];
			u32 idx = HashLoToProgBucket(hashLo);
			bool done = false;
			for (u32 p = 0; p < 8 && !done; p++)
			{
				ProgramEntry& e = pe[idx];
				if (e.hashLo == hashLo)
				{
					e.execs++;
					e.lastPC = startPC_bytes;
					done = true;
				}
				else if (e.hashLo == 0)
				{
					if (g_usedPrograms[vu].load(std::memory_order_relaxed) < kProgramCap)
					{
						e.hashLo = hashLo;
						e.execs = 1;
						e.lastPC = startPC_bytes;
						g_usedPrograms[vu].fetch_add(1, std::memory_order_relaxed);
					}
					else
					{
						g_dropped[vu].fetch_add(1, std::memory_order_relaxed);
					}
					done = true;
				}
				else
				{
					idx = (idx + 1) & kProgramMask;
				}
			}
			if (!done)
				g_dropped[vu].fetch_add(1, std::memory_order_relaxed);
		}

		// --- transicion desde el despacho anterior del mismo VU ---
		VuHistory& h = g_hist[vu];
		if (h.hasPrev)
		{
			const u64 key = MixTransitionKey(h.prevHashLo, h.prevPC, hashLo, startPC_bytes);
			const u32 pcs = (static_cast<u32>(h.prevPC) << 16) | startPC_bytes;
			TransitionEntry* te = g_transitions[vu];
			u32 t = KeyToBucket(key);
			bool done = false;
			for (u32 p = 0; p < 8 && !done; p++)
			{
				TransitionEntry& e = te[t];
				if (e.count == 0)
				{
					if (g_usedTransitions[vu].load(std::memory_order_relaxed) < kTransitionCap)
					{
						e.key = key;
						e.pcs = pcs;
						e.count = 1;
						g_usedTransitions[vu].fetch_add(1, std::memory_order_relaxed);
					}
					else
					{
						g_dropped[vu].fetch_add(1, std::memory_order_relaxed);
					}
					done = true;
				}
				else if (e.key == key && e.pcs == pcs)
				{
					e.count++;
					done = true;
				}
				else
				{
					t = (t + 1) & kTransitionMask;
				}
			}
			if (!done)
				g_dropped[vu].fetch_add(1, std::memory_order_relaxed);
		}

		h.prevHashLo = hashLo;
		h.prevPC = startPC_bytes;
		h.hasPrev = true;
	}

	u64 BlockExecTotal(int vu)
	{
		const u64* slots = SlotArray(vu);
		u64 total = 0;
		for (u32 i = 0; i < kProbeSlots; i++)
			total += slots[i];
		return total;
	}

	void RecordBlockShape(int vu, u32 startPC_bytes, u16 ops, u16 cycles, u16 reason)
	{
		// Llamado desde mVUcompile (hilo compilador de ese VU, sonda ON). El
		// slot es el MISMO indice que el contador de ejecuciones JIT, asi que
		// shape[i] describe el bloque cuyas entradas cuenta slots[i]. Ultima
		// compilacion gana: es la forma con la que el bloque se ejecuta ahora.
		const u32 i = (startPC_bytes >> 3) & kProbeSlotMask;
		BlockShape& s = g_shape[vu][i];
		s.ops = ops;
		s.cycles = cycles;
		s.reason = reason;
		s.pad = 0;
	}

	// ------------------------------------------------------------------
	// Informe.
	// ------------------------------------------------------------------
	namespace
	{
		static constexpr u32 kTopSlots = 40; // PCs calientes por VU
		static constexpr u32 kTopTrans = 40; // transiciones repetidas por VU
		static constexpr u32 kTopProgs = 16; // programas por despachos, por VU

		struct SlotHit { u32 pcBytes; u64 execs; };
		struct TransHit { u32 pcs; u32 count; };
		struct ProgHit { u64 hashLo; u32 execs; u32 lastPC; };
		// Fase 1.5: bloque ponderado por TRABAJO = ejecuciones x instrucciones
		// de microcodigo. Es la metrica de seleccion de candidatos de fusion:
		// un PC con muchas entradas pero 3 ops no paga una fusion; uno con
		// 20M entradas x 30 ops, si.
		struct WorkHit { u32 pcBytes; u64 execs; u64 work; u16 ops; u16 cycles; u16 reason; };
	}

	void DumpReport(const char* reason)
	{
		// Orden de locks: s_info_mutex (dentro de GetDiscSerial, se toma y
		// suelta) ANTES de s_dumpMutex. Al reves habria inversion ABBA real:
		// la ruta JNI toma s_dumpMutex y luego pide el serial, mientras el
		// cierre del VM (mVUclose) puede tener s_info_mutex cogido y pedir
		// s_dumpMutex.
		const std::string serial = VMManager::HasValidVM() ? VMManager::GetDiscSerial() : std::string();

		std::lock_guard<std::mutex> lock(s_dumpMutex);

		SlotHit topSlots[2][kTopSlots];
		u32 nSlots[2] = {};
		u64 grand[2] = {};
		WorkHit topWork[2][kTopSlots];
		u32 nWork[2] = {};
		u64 workTotal[2] = {};
		u64 workShown[2] = {};
		for (int vu = 0; vu < 2; vu++)
		{
			const u64* slots = SlotArray(vu);
			grand[vu] = 0;
			for (u32 i = 0; i < kProbeSlots; i++)
			{
				const u64 v = slots[i];
				if (!v)
					continue;
				grand[vu] += v;
				SlotHit c{i * 8u, v};
				const u64 key = v;
				const u32 k = kTopSlots;
				if (nSlots[vu] == k && key <= (topSlots[vu][k - 1].execs))
					continue;
				u32 j = (nSlots[vu] == k) ? k - 1 : nSlots[vu];
				for (; j > 0 && key > topSlots[vu][j - 1].execs; j--)
					topSlots[vu][j] = topSlots[vu][j - 1];
				topSlots[vu][j] = c;
				if (nSlots[vu] < k)
					nSlots[vu]++;
			}
			// Fase 1.5 — ranking por trabajo (ejecuciones x ops). workTotal
			// suma sobre todos los PCs con forma registrada; es la cifra que
			// la Fase 2 intenta bajar al fusionar entradas de bloque.
			for (u32 i = 0; i < kBlkPcs; i++)
			{
				const u64 v = slots[i];
				const BlockShape& s = g_shape[vu][i];
				if (!v || s.reason == 0)
					continue; // sin forma (PC compilado con sonda OFF)
				const u64 w = v * static_cast<u64>(s.ops);
				workTotal[vu] += w;
				WorkHit c{i * 8u, v, w, s.ops, s.cycles, s.reason};
				const u32 k = kTopSlots;
				if (nWork[vu] == k && w <= topWork[vu][k - 1].work)
					continue;
				u32 j = (nWork[vu] == k) ? k - 1 : nWork[vu];
				for (; j > 0 && w > topWork[vu][j - 1].work; j--)
					topWork[vu][j] = topWork[vu][j - 1];
				topWork[vu][j] = c;
				if (nWork[vu] < k)
					nWork[vu]++;
			}
			for (u32 i = 0; i < nWork[vu]; i++)
				workShown[vu] += topWork[vu][i].work;
		}

		TransHit topTrans[2][kTopTrans];
		u32 nTrans[2] = {};
		ProgHit topProgs[2][kTopProgs];
		u32 nProgs[2] = {};
		for (int vu = 0; vu < 2; vu++)
		{
			for (u32 i = 0; i < kTransitionCap; i++)
			{
				const TransitionEntry& e = g_transitions[vu][i];
				if (e.count < 2) // una aparicion suelta no es una secuencia
					continue;
				TransHit c{e.pcs, e.count};
				const u32 k = kTopTrans;
				if (nTrans[vu] == k && e.count <= topTrans[vu][k - 1].count)
					continue;
				u32 j = (nTrans[vu] == k) ? k - 1 : nTrans[vu];
				for (; j > 0 && e.count > topTrans[vu][j - 1].count; j--)
					topTrans[vu][j] = topTrans[vu][j - 1];
				topTrans[vu][j] = c;
				if (nTrans[vu] < k)
					nTrans[vu]++;
			}
			for (u32 i = 0; i < kProgramCap; i++)
			{
				const ProgramEntry& e = g_programs[vu][i];
				if (e.hashLo == 0)
					continue;
				ProgHit c{e.hashLo, e.execs, e.lastPC};
				const u32 k = kTopProgs;
				if (nProgs[vu] == k && e.execs <= topProgs[vu][k - 1].execs)
					continue;
				u32 j = (nProgs[vu] == k) ? k - 1 : nProgs[vu];
				for (; j > 0 && e.execs > topProgs[vu][j - 1].execs; j--)
					topProgs[vu][j] = topProgs[vu][j - 1];
				topProgs[vu][j] = c;
				if (nProgs[vu] < k)
					nProgs[vu]++;
			}
		}

		const std::string path = Path::Combine(EmuFolders::Logs, "vu_probe.txt");
		// Rotación simple: los vuelcos INTERMEDIOS (cierre de VM o petición
		// manual con la sonda aun encendida) son instantaneas parciales y se
		// ANADEN; el vuelco FINAL (flanco ON->OFF, donde g_enabled ya esta a
		// false) contiene el total de la sesion y TRUNCA el archivo para que
		// el usuario siempre encuentre el informe completo arriba del todo...
		// es decir: OFF => "wb" (empezar limpio), ON => "wa" (adjuntar
		// instantaneas detras). Como los contadores NO se limpian al apagar,
		// el informe OFF es siempre el mas completo.
		std::FILE* fp = FileSystem::OpenCFile(path.c_str(), IsEnabled() ? "ab" : "wb");
		// Macro, no lambda: conserva el attribute format(printf) de ProbePrintf
		// para que cada cadena del informe se valide al compilar.
#define P(...) ProbePrintf(fp, __VA_ARGS__)

		P("Cenit VU Trace Probe — informe Fase 1\n");
		P("build=%s (%s)  motivo=%s  sonda=%s\n",
			BuildVersion::AppVersion, BuildVersion::GitShort, reason ? reason : "?",
			IsEnabled() ? "ON" : "OFF");
		P("juego=%s  EECycleRate=%d EECycleSkip=%d progCache=%d\n",
			serial.empty() ? "sin VM" : serial.c_str(),
			static_cast<int>(EmuConfig.Speedhacks.EECycleRate),
			static_cast<int>(EmuConfig.Speedhacks.EECycleSkip),
			static_cast<int>(EmuConfig.Cpu.Recompiler.EnableVUProgramCache));

		for (int vu = 0; vu < 2; vu++)
		{
			const auto& f = g_flow[vu];
			const u64 sc = f.stubCalls.load(std::memory_order_relaxed);
			const u64 sh = f.stubHits.load(std::memory_order_relaxed);
			const u64 fh = f.fastHits.load(std::memory_order_relaxed);
			const u64 sm = f.slowMiss.load(std::memory_order_relaxed);
			P("\n==== VU%d ====\n", vu);
			P("dispatcher (salidas a C++): stub=%llu hit=%llu | reintentos en mVUexecute rap=%llu lento=%llu\n",
				static_cast<unsigned long long>(sc), static_cast<unsigned long long>(sh),
				static_cast<unsigned long long>(fh), static_cast<unsigned long long>(sm));
			P("resueltos-sin-salida-a-C++ (inferido; solo vale en VU1, unicos bloques contados): %lld\n",
				(vu == 1)
					? static_cast<long long>(grand[vu]) - static_cast<long long>(
						sh + fh + sm + f.jumpEntries.load(std::memory_order_relaxed) -
						f.jumpCacheHits.load(std::memory_order_relaxed))
					: 0LL);
			P("programas: creados=%llu  entradas JR/JALR=%llu  jumpCache hit=%llu\n",
				static_cast<unsigned long long>(f.compiles.load(std::memory_order_relaxed)),
				static_cast<unsigned long long>(f.jumpEntries.load(std::memory_order_relaxed)),
				static_cast<unsigned long long>(f.jumpCacheHits.load(std::memory_order_relaxed)));
			P("micro: escrituras-invalidacion=%llu  ejecuciones-bloque=%llu  muestras-perdidas=%llu\n",
				static_cast<unsigned long long>(f.microWrites.load(std::memory_order_relaxed)),
				static_cast<unsigned long long>(grand[vu]),
				static_cast<unsigned long long>(g_dropped[vu].load(std::memory_order_relaxed)));

			P("bloques calientes (PC de entrada -> ejecuciones; %u del total):\n", nSlots[vu]);
			u64 shown = 0;
			for (u32 i = 0; i < nSlots[vu]; i++)
			{
				shown += topSlots[vu][i].execs;
				P("  PC=0x%04x  x%llu\n", topSlots[vu][i].pcBytes,
					static_cast<unsigned long long>(topSlots[vu][i].execs));
			}
			if (grand[vu] > shown)
				P("  (resto: %llu ejecuciones en PCs fuera del top)\n",
					static_cast<unsigned long long>(grand[vu] - shown));

			// Fase 1.5 — el ranking que elige candidatos de fusion: trabajo =
			// ejecuciones x instrucciones del bloque. reason: 1 rama, 2 M-bit,
			// 3 fin-microMem, 4 presupuesto-ciclos, 5 otro.
			P("bloques por TRABAJO (ejecuciones x ops; total=%llu; top %u = %llu):\n",
				static_cast<unsigned long long>(workTotal[vu]), nWork[vu],
				static_cast<unsigned long long>(workShown[vu]));
			for (u32 i = 0; i < nWork[vu]; i++)
				P("  PC=0x%04x  x%llu  ops=%u  ciclos=%u  r=%u  trabajo=%llu\n",
					topWork[vu][i].pcBytes,
					static_cast<unsigned long long>(topWork[vu][i].execs),
					topWork[vu][i].ops, topWork[vu][i].cycles, topWork[vu][i].reason,
					static_cast<unsigned long long>(topWork[vu][i].work));
			if (workTotal[vu] > workShown[vu])
				P("  (resto: %llu de trabajo fuera del top; PCs sin forma registrada quedan excluidos)\n",
					static_cast<unsigned long long>(workTotal[vu] - workShown[vu]));

			P("secuencias repetidas A->B (entre despachos; top %u):\n", nTrans[vu]);
			for (u32 i = 0; i < nTrans[vu]; i++)
				P("  0x%04x -> 0x%04x : x%u\n", topTrans[vu][i].pcs >> 16,
					topTrans[vu][i].pcs & 0xffffu, topTrans[vu][i].count);

			P("programas por despachos (top %u):\n", nProgs[vu]);
			for (u32 i = 0; i < nProgs[vu]; i++)
				P("  hash=%016llx  execs=%u  ultimo PC=0x%04x\n",
					static_cast<unsigned long long>(topProgs[vu][i].hashLo),
					topProgs[vu][i].execs, topProgs[vu][i].lastPC);
		}
#undef P

		if (fp)
		{
			std::fclose(fp);
			Console.WriteLn(Color_StrongGreen, "VUprobe: informe completo escrito en logs/vu_probe.txt (%s)",
				reason ? reason : "?");
		}
		else
		{
			Console.WriteLn(Color_Orange, "VUprobe: NO se pudo abrir logs/vu_probe.txt (%s)",
				reason ? reason : "?");
		}

		// Resumen al emulog: es lo que llega con el boton "Enviar registro"
		// sin pedirle al usuario ningun archivo extra. 10 PCs + 10 secuencias
		// por VU + la fila de flujo.
		for (int vu = 0; vu < 2; vu++)
		{
			const auto& f = g_flow[vu];
			Console.WriteLn("VUprobe VU%d: stub=%llu(hit %llu) rap=%llu lento=%llu bloques=%llu prog=%llu JR=%llu perd=%llu",
				vu,
				static_cast<unsigned long long>(f.stubCalls.load(std::memory_order_relaxed)),
				static_cast<unsigned long long>(f.stubHits.load(std::memory_order_relaxed)),
				static_cast<unsigned long long>(f.fastHits.load(std::memory_order_relaxed)),
				static_cast<unsigned long long>(f.slowMiss.load(std::memory_order_relaxed)),
				static_cast<unsigned long long>(grand[vu]),
				static_cast<unsigned long long>(f.compiles.load(std::memory_order_relaxed)),
				static_cast<unsigned long long>(f.jumpEntries.load(std::memory_order_relaxed)),
				static_cast<unsigned long long>(g_dropped[vu].load(std::memory_order_relaxed)));
			for (u32 i = 0; i < nSlots[vu] && i < 10; i++)
			{
				Console.WriteLn("VUprobe VU%d top%u PC=0x%04x x%llu", vu, i + 1,
					topSlots[vu][i].pcBytes, static_cast<unsigned long long>(topSlots[vu][i].execs));
			}
			// Fase 1.5: el top por trabajo es lo que decide la Fase 2, asi que
			// tambien viaja en el resumen (el boton "Enviar registro" no lleva
			// el vu_probe.txt completo).
			for (u32 i = 0; i < nWork[vu] && i < 10; i++)
			{
				Console.WriteLn("VUprobe VU%d wtop%u PC=0x%04x x%llu ops=%u ciclos=%u r=%u trab=%llu",
					vu, i + 1, topWork[vu][i].pcBytes,
					static_cast<unsigned long long>(topWork[vu][i].execs),
					topWork[vu][i].ops, topWork[vu][i].cycles, topWork[vu][i].reason,
					static_cast<unsigned long long>(topWork[vu][i].work));
			}
			for (u32 i = 0; i < nTrans[vu] && i < 10; i++)
			{
				Console.WriteLn("VUprobe VU%d seq%u 0x%04x->0x%04x x%u", vu, i + 1,
					topTrans[vu][i].pcs >> 16, topTrans[vu][i].pcs & 0xffffu, topTrans[vu][i].count);
			}
		}
	}
}
