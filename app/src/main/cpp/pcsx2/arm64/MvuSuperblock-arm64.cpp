// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// Cenit VU Superblock Engine — tabla de candidatos, política de elegibilidad y
// replay diferencial por parejas de despacho. Ver MvuSuperblock-arm64.h para el
// diseño completo y las garantías que este archivo hace cumplir.
//
// Este TU NO incluye microVU-arm64.h: no necesita ver el struct (el digest de
// salida le llega por g_exitDigest, registrada por microVU-arm64.cpp en
// mVUinit), y así se mantiene libre de la superficie de vixl, igual que el
// cuerpo de la sonda.
//
// Cenit 0.6.32 (Fase 2 del prompt directo): el ciclo de vida de un candidato NO
// se decide aquí con ifs sueltos — vive en mVUSbLife::Machine, un header puro
// que Harness9 compila con g++ de host y fuerza transición por transición. Lo
// que este archivo hace es ALIMENTAR esa máquina con datos medidos (identidad,
// máscara del observador, pares, duraciones, presupuesto) y OBEDECERLA en el
// despacho. Si la máquina dice VALIDATE, el HUD dice VALIDATE y la variante no
// sirve el juego fuera de la ventana alternada; si dice UNSEEN, nadie la sirve.
// Las leyes de promoción (1000 pares en >1 ventana, ganancia medida con su n,
// P95 no peor, identidad completa) no son de este archivo y no se pueden
// parchear de este archivo sin que Harness9 lo grite.

#include "MvuSuperblock-arm64.h"

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

namespace mVUSuperblock
{
	std::atomic<bool> g_enabled{false};
	Stats g_stats[2];
	SbExitDigestFn g_exitDigest = nullptr;

	// ------------------------------------------------------------------
	// 0.6.32 — observadores registrados por el TU de microVU. Todo lo que esta
	// máquina necesita saber del estado de VU1 le llega por aquí, como dato, y
	// NADA de esto se asume: si el digest no registró su máscara, la máquina
	// rechaza los pares (Machine::PairMatched -> evidence.pairsMaskRejected) y
	// el informe lo cuenta. Un candidato sin observador no puede validarse, y
	// eso es un no dicho por el dato, no una opinión del motor.
	// ------------------------------------------------------------------
	SbAreaDigestFn g_areaDigest = nullptr;
	std::atomic<u32> g_exitMask{0};
	std::atomic<u32> g_engineAbi{0};
	std::atomic<bool> g_rollbackSurface{false};

	namespace
	{
		u64 s_profileHash = 0; // espejo de lo que Java publica (identidad juego+aparato)

		// Orden de "quemado" del HUD: el candidato mas adelantado es el que se
		// muestra. No es un ranking de merito, es el orden de la maquina — si se
		// reordena, el HUD mostraria el menos interesante y el usuario veria
		// UNSEEN mientras hay un TRUSTED trabajando.
		int RankOf(const mVUSbLife::Machine& m)
		{
			switch (m.state)
			{
			case mVUSbLife::State::Trusted:	 return 7;
			case mVUSbLife::State::Validated: return 6;
			case mVUSbLife::State::Shadowing: return 5;
			case mVUSbLife::State::Validating:	return 4;
			case mVUSbLife::State::Building:	return 3;
			case mVUSbLife::State::Invalidated: return 2;
			case mVUSbLife::State::Dead:		 return 1;
			default:							 return 0;
			}
		}
	}

	// Identidad de juego+dispositivo: la publica Java (es su FNV-1a, no la mía)
	// por el mismo canal del HUD del sintonizador. Se llama en el hilo de
	// settings/JNI, con la VM parada o con el motor sin candidatos vivos en su
	// mayoria; el escritor de la tabla es el hilo dispatcher, y aqui solo se
	// guarda un scalar de 64 bits que los candidatos leen al armar su identidad.
	void SetProfileHash(u64 hash)
	{
		// Escritura de 8 bytes sin atomicidad declarada: el peor resultado de una
		// carrera AQUI es un candidato armado con la identidad vieja, y ese
		// candidato se invalida solo en cuanto RebindIfChanged vea la nueva. No es
		// un riesgo de simulacion (no toca estado PS2), y por eso no merece un
		// mutex en el camino de despacho.
		s_profileHash = hash;
	}

	u64 ProfileHash()
	{
		return s_profileHash;
	}

	namespace
	{
		static constexpr u32 kKeyBytes = 96; // microRegInfo

		using mVUSbLife::Evidence;
		using mVUSbLife::Identity;
		using mVUSbLife::Machine;
		using mVUSbLife::Rules;
		using mVUSbLife::State;

		// Ventana de tiempos guardados para P95: 64 pares es kMinGainPairs, o sea
		// la muestra minima con la que la maquina acepta afirmar una ganancia.
		// Anillo (no acumulado) a proposito: el P95 de todo el historial esconderia
		// una regresion tardia, y el prompt veta la regresion, no el promedio.
		static constexpr u32 kTimingRing = 64;

		struct Candidate
		{
			bool inUse = false; // hueco asignado (la MAQUINA decide si ocupa: life.Occupies())
			u32 startPC = 0;	// bytes de microMem
			alignas(16) u8 key[kKeyBytes]; // lpState de construccion, tal cual

			void* entry = nullptr; // entrada host del superbloque
			u32 junctions = 0;
			u32 regionOps = 0;
			u32 regionCycles = 0;

			// --- LA MAQUINA: unica dueña del estado y de la evidencia ---
			Machine life;

			// --- contabilidad LOCAL del despacho (la maquina no conoce el
			// tamano de la ventana del motor) ---
			u32 epoch = 0;	  // id monotono de ventana de ESTE candidato
			u32 windowIn = 0; // despachos emparejables dentro de la ventana actual
			bool parity = false; // true => el proximo despacho emparejable va al SB

			// Episodios en vuelo. SbResolve pone *_Pending justo antes de que la
			// ruta elegida corra; SbFinish (fin del episodio) recoge el digest y la
			// DURACION, limpia *_Pending y pone *_Ready. ClosePair solo actua cuando
			// ambos lados estan Ready. Con *_Pending en vez de un "hasN" permanente,
			// un despacho que entra al PC pero NO fue emparejado por SbResolve (clave
			// distinta, presupuesto corto) no puede confundirse con el episodio de
			// la otra ruta.
			bool nPending = false;
			bool sPending = false;
			bool nReady = false;
			bool sReady = false;
			u64 pendN = 0;	// digest del episodio normal cerrado
			u64 pendS = 0;	// digest del episodio SB cerrado
			u64 pendNNs = 0;	// duracion real del episodio normal (0 = sin cronometrar)
			u64 pendSNS = 0;	// duracion real del episodio SB

			// Ganancia: sumas sobre TODOS los pares cronometrados (el ratio no es
			// de una pareja suelta) y anillo de los ultimos kTimingRing para P95.
			u64 sumBaseNs = 0;
			u64 sumSbNs = 0;
			u32 timedPairs = 0; // == life.evidence.timedPairs (se lee de la maquina)
			u64 baseRing[kTimingRing] = {};
			u64 sbRing[kTimingRing] = {};
			u32 ringPos = 0;
			u32 ringFill = 0;
		};

		Candidate s_tab[kSbMaxVariants];
		u32 s_live = 0; // huecos asignados (inUse), incluido maquinas ya muertas

		// PCs quemados: un bit por PC de entrada posible de VU1 (0x4000/8 = 2048,
		// mismo indice que los slots de la sonda). 0.6.32 aclara QUEMADO significa
		// exactamente, porque antes era la unica manera de decir "no" y por eso
		// castigaba de mas: aqui se quema solo (a) un rechazo DETERMINISTA de forma
		// (mismo PC + mismo microcodigo = mismo veredicto, reintentar es bucle de
		// compilaciones muertas), (b) un DEAD por divergencia y (c) un candidato
		// medido SIN ganancia. NO se quema el INVALIDATED por presupuesto ni por
		// cambio de contexto: esos son "no medi", y otro momento del juego si puede
		// medir. La maquina ya distingue los dos (Burn vs InvalidateContext); el
		// bit solo evita recompilar en bucle lo que la maquina ya rechazo.
		u64 s_burned[(mVUTraceProbe::kProbeSlots + 63) / 64] = {};
		u32 s_burnedCount = 0;

		// Total de divergencias vistas y CANDIDATOS distintos que divergieron. El
		// segundo es lo que permite el "DEAD por candidato" del prompt: con
		// superficie de rollback, apagar el proceso solo si el problema no es de un
		// candidato sino del motor (>=kSbBreakCandidates PCs distintos).
		u32 s_divergences = 0;
		u32 s_divergedPcs = 0;
		u32 s_lastDivergedIdx = 0xFFFFFFFFu;
		bool s_killed = false;

		std::mutex s_dumpMutex; // serializa DumpReport (toggle + JNI + close)

		__fi u32 PcIdx(u32 startPC_bytes)
		{
			return (startPC_bytes >> 3) & (mVUTraceProbe::kProbeSlots - 1);
		}

		__fi bool IsBurned(u32 i) { return (s_burned[i >> 6] >> (i & 63)) & 1u; }

		void SetBurned(u32 i)
		{
			const u64 bit = 1ull << (i & 63);
			if (!(s_burned[i >> 6] & bit))
			{
				s_burned[i >> 6] |= bit;
				s_burnedCount++;
			}
		}

		Candidate* FindForPc(u32 startPC_bytes)
		{
			for (u32 i = 0; i < kSbMaxVariants; i++)
			{
				if (s_tab[i].inUse && s_tab[i].startPC == startPC_bytes)
					return &s_tab[i];
			}
			return nullptr;
		}

		Candidate* AllocSlot()
		{
			// Primera pasada: hueco libre de verdad. Segunda: un hueco cuya maquina
			// ya NO ocupa (DEAD / INVALIDATED / UNSEEN) — se le cambia de PC, porque
			// el castigo es del candidato, no del hueco. Antes de 0.6.32 no habia
			// segunda pasada: un candidato muerto se mordía la tabla para siempre y
			// 256 muertes dejaban el motor sin sitio, que se reportaba como
			// "rejected_identity" y parecia un problema de identidad cuando era
			// basura que no se barría.
			for (u32 pass = 0; pass < 2; pass++)
			{
				for (u32 i = 0; i < kSbMaxVariants; i++)
				{
					Candidate& c = s_tab[i];
					if (pass == 0)
					{
						if (c.inUse)
							continue;
					}
					else
					{
						if (c.inUse && !c.life.FreeForReassign())
							continue;
						if (!c.inUse)
							continue; // ya lo tomo la pasada 0
					}
					const bool wasOccupying = c.inUse && c.life.Occupies();
					std::memset(&c, 0, sizeof(Candidate));
					c.inUse = true;
					if (!wasOccupying)
						s_live++;
					return &c;
				}
			}
			return nullptr;
		}

		// El candidato deja de despachar. NO libera el hueco: `inUse` sigue diciendo
		// que ese PC ya fue juzgado (encontrarlo evita re-armar una compilacion que
		// la maquina ya rechazo), y AllocSlot si puede reasignar el hueco cuando la
		// maquina esta muerta. entry=null: ninguna ruta vuelve a tomar la variante.
		void Retire(Candidate& c)
		{
			c.entry = nullptr;
			c.nPending = c.sPending = false;
			c.nReady = c.sReady = false;
		}

		void ResetTableForEnable()
		{
			for (u32 i = 0; i < kSbMaxVariants; i++)
				std::memset(&s_tab[i], 0, sizeof(Candidate));
			s_live = 0;
			std::memset(s_burned, 0, sizeof(s_burned));
			s_burnedCount = 0;
			s_divergences = 0;
			s_divergedPcs = 0;
			s_lastDivergedIdx = 0xFFFFFFFFu;
			for (int vu = 0; vu < 2; vu++)
			{
				Stats& s = g_stats[vu];
				s.attempts.store(0); s.built.store(0); s.junctions.store(0); s.fused_ops.store(0);
				s.rejected_struct.store(0); s.rejected_cold.store(0);
				s.rejected_budget.store(0); s.rejected_identity.store(0);
				s.verify_normal.store(0); s.verify_sb.store(0); s.unmatched.store(0);
				s.matched.store(0); s.diverged.store(0); s.promoted.store(0);
				s.unproven.store(0); s.killed.store(0);
				s.mask_rejected.store(0); s.epochs.store(0); s.validated.store(0);
				s.dead_candidates.store(0); s.archived.store(0); s.trials.store(0);
				s.spot_checks.store(0); s.unprofitable.store(0);
			}
		}

		// Escritor de línea con chequeo de formato en tiempo de compilación
		// (mismo patrón que la sonda: un lambda con auto... reenviando a
		// fprintf no valida ninguna cadena y dispara -Wformat-security).
		void SbPrintf(std::FILE* fp, const char* fmt, ...)
#if defined(__GNUC__)
			__attribute__((format(printf, 2, 3)))
#endif
			;
		void SbPrintf(std::FILE* fp, const char* fmt, ...)
		{
			if (!fp)
				return;
			va_list ap;
			va_start(ap, fmt);
			std::vfprintf(fp, fmt, ap);
			va_end(ap);
		}

		void DumpStatsLine(std::FILE* f, const char* tag, const Stats& s)
		{
			SbPrintf(f, "%s attempts=%" PRIu64 " built=%" PRIu64 " junctions=%" PRIu64
				" fused_ops=%" PRIu64 "\n",
				tag, s.attempts.load(std::memory_order::relaxed), s.built.load(std::memory_order::relaxed),
				s.junctions.load(std::memory_order::relaxed), s.fused_ops.load(std::memory_order::relaxed));
			SbPrintf(f, "%s rejected struct=%" PRIu64 " cold=%" PRIu64 " budget=%" PRIu64
				" identity=%" PRIu64 "\n",
				tag, s.rejected_struct.load(std::memory_order::relaxed), s.rejected_cold.load(std::memory_order::relaxed),
				s.rejected_budget.load(std::memory_order::relaxed), s.rejected_identity.load(std::memory_order::relaxed));
			SbPrintf(f, "%s replay normal=%" PRIu64 " sb=%" PRIu64 " unmatched=%" PRIu64
				" matched=%" PRIu64 " diverged=%" PRIu64 "\n",
				tag, s.verify_normal.load(std::memory_order::relaxed), s.verify_sb.load(std::memory_order::relaxed),
				s.unmatched.load(std::memory_order::relaxed), s.matched.load(std::memory_order::relaxed), s.diverged.load(std::memory_order::relaxed));
			SbPrintf(f, "%s outcome promoted=%" PRIu64 " unproven=%" PRIu64 " killed=%" PRIu64 "\n",
				tag, s.promoted.load(std::memory_order::relaxed), s.unproven.load(std::memory_order::relaxed), s.killed.load(std::memory_order::relaxed));
			// 0.6.32 — la contabilidad de LA MAQUINA. Si algo de esto esta a cero
			// con el motor mucho rato, la evidencia no se esta formando y el motivo
			// esta en las lineas de arriba (mascara, presupuesto, identidad).
			SbPrintf(f, "%s maquina mask_rejected=%" PRIu64 " epochs=%" PRIu64 " validated=%" PRIu64
				" dead=%" PRIu64 " archived=%" PRIu64 " trials=%" PRIu64 " spot=%" PRIu64
				" unprofitable=%" PRIu64 "\n",
				tag, s.mask_rejected.load(std::memory_order::relaxed), s.epochs.load(std::memory_order::relaxed),
				s.validated.load(std::memory_order::relaxed), s.dead_candidates.load(std::memory_order::relaxed),
				s.archived.load(std::memory_order::relaxed), s.trials.load(std::memory_order::relaxed),
				s.spot_checks.load(std::memory_order::relaxed), s.unprofitable.load(std::memory_order::relaxed));
		}

		// ------------------------------------------------------------------
		// Identidad del candidato. Se construye con datos que existen o se
		// queda incompleta — y con identidad incompleta la maquina no promociona
		// nada (Rules::TrustedReady lo exige), asi que no hace falta un if de
		// favor en el camino de promocion.
		// ------------------------------------------------------------------
		// Tramos del area fusionada: PC de entrada de cada tramo + instrucciones que
		// cobra segun la sonda. El area NO es contigua, asi que un solo PC no la
		// describe; kSbMaxJunctions+1 es el techo que fija la politica de uniones.
		struct AreaSpec
		{
			u32 count = 0;
			u32 pcs[kSbMaxJunctions + 1] = {};
			u32 ops[kSbMaxJunctions + 1] = {};
		};

		Identity MakeIdentity(u32 startPC_bytes, const AreaSpec* area)
		{
			Identity id;
			id.startPc = startPC_bytes;
			// Digest del area: lo pone el TU que ve el microcodigo. Sin observador
			// registrado queda 0, y 0 = identidad incompleta = sin promocion.
			id.codeDigest = (g_areaDigest && area && area->count > 0) ?
				g_areaDigest(area->pcs, area->ops, area->count) : 0u;
			id.profileHash = s_profileHash;
			// THREAD_VU1 (Config.h) = REC_VU1 && Speedhacks.vuThread: es EXACTAMENTE
			// la condicion bajo la cual el E-bit hace fetch_or sobre
			// vu1Thread.mtvuInterrupts, o sea la condicion que hace inseguro
			// revertir un episodio. Se pasa como dato, no como opinion.
			id.mtvu = THREAD_VU1 ? 1u : 0u;
			id.cycleRate = static_cast<int32_t>(EmuConfig.Speedhacks.EECycleRate);
			id.cycleSkip = static_cast<int32_t>(EmuConfig.Speedhacks.EECycleSkip);
			id.engineAbi = g_engineAbi.load(std::memory_order_relaxed);
			return id;
		}

		// ------------------------------------------------------------------
		// Fin de un candidato, con las tres salidas distintas que la maquina
		// distingue (y el motor de 0.6.21 no podia): muerto por divergencia,
		// archivado sin evidencia, medido sin ganancia.
		// ------------------------------------------------------------------
		void FinishAsDead(Candidate& c, u32 idx, const char* why)
		{
			Stats& st = g_stats[1];
			Retire(c);
			SetBurned(idx); // no re-armar con ESTA identidad: la maquina ya lo juzgo
			st.dead_candidates.fetch_add(1, std::memory_order_relaxed);
			Console.Error("Cenit superblock: PC=%04x %s -> candidato DEAD "
				"(el motor sigue vivo: %u vivos, %u divergencias).",
				c.startPC, why, s_live, s_divergences);
		}

		void FinishAsArchived(Candidate& c, u32 idx, const char* why)
		{
			Stats& st = g_stats[1];
			(void)idx;
			Retire(c);
			st.archived.fetch_add(1, std::memory_order_relaxed);
			DevCon.WriteLn(Color_Gray, "Cenit superblock: PC=%04x %s -> archivado con su hueco "
				"liberable (no es un no de forma, es un «no medi»).", c.startPC, why);
		}

		// El breaker del proceso. Con superficie de rollback la divergencia se
		// revierte ANTES de llegar al juego, asi que un candidato malo no es razon
		// para apagar a los sanos: se exige ademas que sean PCs distintos. Sin
		// superficie, el episodio divergente YA corrio como autoritativo y el unico
		// escudo que queda es parar (kSbBreakNoSurface, igual que 0.6.21).
		bool BreakerTripped()
		{
			const bool surface = g_rollbackSurface.load(std::memory_order_relaxed);
			if (surface)
				return s_divergences >= kSbBreakWithSurface && s_divergedPcs >= kSbBreakCandidates;
			return s_divergences >= kSbBreakNoSurface;
		}

		void TripBreaker()
		{
			Stats& st = g_stats[1];
			s_killed = true;
			st.killed.fetch_add(1, std::memory_order_relaxed);
			for (u32 i = 0; i < kSbMaxVariants; i++)
				Retire(s_tab[i]);
			s_live = 0;
			g_enabled.store(false, std::memory_order_relaxed);
			Console.Error("Cenit superblock: %u divergencias en %u candidatos distintos "
				"(superficie=%s) -> motor AUTO-APAGADO por sesion (config no lo vuelve a "
				"encender).", s_divergences, s_divergedPcs,
				g_rollbackSurface.load(std::memory_order_relaxed) ? "SI" : "no");
			DumpReport("auto-apagado por divergencias");
		}

		// ------------------------------------------------------------------
		// Cierre de pareja: la UNICA fuente de evidencia de equivalencia. La
		// decision de promocionar NO se toma aqui — se le entrega el par a la
		// maquina y ella decide (PairMatched puede llevar a VALIDATED; Diverged
		// lleva a DEAD).
		// ------------------------------------------------------------------
		void ClosePair(Candidate& c, u32 idx)
		{
			if (!(c.nReady && c.sReady))
				return;

			const u64 a = c.pendN;
			const u64 b = c.pendS;
			const u64 nsN = c.pendNNs;
			const u64 nsS = c.pendSNS;
			c.nReady = c.sReady = false;
			Stats& st = g_stats[1];

			// El candidato murio mientras la otra pata estaba en vuelo (por ejemplo
			// SbKillAll a mitad de ventana). No hay a quien darle el par: se anota
			// como no emparejado y no se cuenta NUNCA como divergencia — comparar
			// contra un estado que ya no existe no es una divergencia, es ruido.
			if (!c.life.Occupies())
			{
				st.unmatched.fetch_add(1, std::memory_order_relaxed);
				return;
			}

			if (a != b)
			{
				st.diverged.fetch_add(1, std::memory_order_relaxed);
				s_divergences++;
				if (idx != s_lastDivergedIdx)
				{
					s_lastDivergedIdx = idx;
					s_divergedPcs++;
				}
				Console.Error("Cenit superblock: DIVERGENCE en PC=%04x (normal=%016" PRIx64
					" sb=%016" PRIx64 " spans=%u ops=%u) -> variante invalidada.",
					c.startPC, a, b, c.junctions + 1u, c.regionOps);
				const State before = c.life.state;
				c.life.Diverged(); // DEAD por candidato: la maquina, no este archivo
				FinishAsDead(c, idx, "divergencia (estado previo VALIDATE/SHADOW)");
				(void)before;
				if (BreakerTripped())
					TripBreaker();
				return;
			}

			st.matched.fetch_add(1, std::memory_order_relaxed);
			const u32 pairsBefore = c.life.evidence.pairs;
			const u32 rejectedBefore = c.life.evidence.pairsMaskRejected;
			c.life.PairMatched();
			if (c.life.evidence.pairsMaskRejected != rejectedBefore)
			{
				// La maquina RECHAZO el par porque el observador no declaro la mascara
				// completa. Se anota en el contador del proceso y NO se conto como match:
				// un informe no puede inflarse con comparaciones a ciegas.
				st.mask_rejected.fetch_add(c.life.evidence.pairsMaskRejected - rejectedBefore,
					std::memory_order_relaxed);
			}
			if (c.life.state == State::Validated && pairsBefore < mVUSbLife::kValidPairs)
			{
				st.validated.fetch_add(1, std::memory_order_relaxed);
				DevCon.WriteLn(Color_Yellow, "Cenit superblock: PC=%04x VALIDATED (%u pares en %u "
					"ventanas, 0 divergencias) — EQUIVALE, todavia sin ganancia medida.",
					c.startPC, c.life.evidence.pairs, c.life.evidence.windowsWithPairs);
			}

			// Ganancia: solo un par con LAS DOS duraciones medidas. Un 0 de reloj
			// monotono no es "0 ns", es "no cronometre", y contarlo como medicion
			// seria inventar el numero que la maquina exige para Trusted.
			if (nsN > 0 && nsS > 0)
			{
				c.sumBaseNs += nsN;
				c.sumSbNs += nsS;
				c.timedPairs++;
				const u32 slot = c.ringPos;
				c.baseRing[slot] = nsN;
				c.sbRing[slot] = nsS;
				c.ringPos = (c.ringPos + 1u) % kTimingRing;
				if (c.ringFill < kTimingRing)
					c.ringFill++;
			}
		}

		// P95 nearest-rank sobre el anillo (no sobre todo el historial: una
		// regresion tardia tiene que verse). order no altera los anillos.
		u64 PercentileNs(const u64* ring, u32 fill, u32 pct)
		{
			if (fill == 0)
				return 0;
			u64 tmp[kTimingRing];
			for (u32 i = 0; i < fill; i++)
				tmp[i] = ring[i];
			for (u32 i = 1; i < fill; i++) // insertion sort: 64 elementos, sin <algorithm> aqui
			{
				const u64 v = tmp[i];
				s32 j = (s32)i - 1;
				while (j >= 0 && tmp[j] > v)
				{
					tmp[j + 1] = tmp[j];
					j--;
				}
				tmp[j + 1] = v;
			}
			u32 rank = (fill * pct + 99) / 100; // ceil(p*fill/100), nearest-rank
			if (rank == 0)
				rank = 1;
			if (rank > fill)
				rank = fill;
			return tmp[rank - 1];
		}

		// Cambio de ventana del candidato: es el momento de entregarle a la maquina
		// (a) el id monotono que exige para el ">1 ventana" y (b) la ganancia MEDIDA
		// de la ventana que cierra. Antes de 0.6.32 no habia ni una cosa ni la otra:
		// se promocionaba con 8 pares de una sola ventana y sin un solo numero de
		// velocidad — o sea "confiable" queria decir "no rompió 8 veces".
		void RollWindow(Candidate& c, u32 idx)
		{
			Stats& st = g_stats[1];
			(void)idx;
			const u32 newEpoch = c.epoch + 1u;
			c.epoch = newEpoch;
			c.windowIn = 0;
			c.life.StartWindow(newEpoch);
			st.epochs.fetch_add(1, std::memory_order_relaxed);

			if (c.life.state != State::Validated)
				return;
			if (c.timedPairs < mVUSbLife::kMinGainPairs)
				return; // sin muestra no hay numero: se espera a la siguiente ventana

			// permille de tiempo ahorrado sobre el total cronometrado (positivo =
			// la variante fue MAS RAPIDA). Y P95 por par: P95(sb) - P95(normal), en
			// microsegundos (un episodio es de microsegundos; en ms una regresion
			// real se leeria 0 y el veto de la maquina no vetaria nada).
			const u64 base = c.sumBaseNs ? c.sumBaseNs : 1;
			const s64 saved = (s64)c.sumBaseNs - (s64)c.sumSbNs;
			const s64 gain = saved * 1000 / (s64)base;
			const u64 p95N = PercentileNs(c.baseRing, c.ringFill, 95);
			const u64 p95S = PercentileNs(c.sbRing, c.ringFill, 95);
			const s64 deltaUs = ((s64)p95S - (s64)p95N) / 1000;
			const i32 clamp = (deltaUs > 1000000) ? 1000000 : (deltaUs < -1000000 ? -1000000 : (i32)deltaUs);

			const State before = c.life.state;
			c.life.ReportGain((i32)gain, clamp, c.timedPairs);
			DevCon.WriteLn(Color_Yellow, "Cenit superblock: PC=%04x ganancia medida %+d.%u%% "
				"(n=%u pares, P95 %+d us) -> %s.",
				c.startPC, (int)(gain / 10), (unsigned)(gain < 0 ? -((-gain) % 10) : gain % 10),
				c.timedPairs, (int)deltaUs, mVUSbLife::StateName(c.life.state));
			if (c.life.state == State::Trusted)
			{
				st.promoted.fetch_add(1, std::memory_order_relaxed);
			}
			else if (before == State::Validated)
			{
				// Medido y NO promocionado: equivale pero no vale la pena (o el P95
				// empeoro). Servirlo igual seria el error de 0.6.25 con otro nombre,
				// asi que se retira y se quema el PC para esta identidad.
				st.unprofitable.fetch_add(1, std::memory_order_relaxed);
				FinishAsDead(c, idx, "sin ganancia que valga el riesgo");
			}
		}

		// Ventana agotada sin la evidencia que la maquina pide: se le avisa a la
		// maquina (BudgetReached: INVALIDATED, no DEAD) y se libera el hueco.
		void WindowExhausted(Candidate& c, u32 idx)
		{
			Stats& st = g_stats[1];
			const u32 pairsSeen = c.life.evidence.pairs;
			const u32 pc = c.startPC;
			c.life.BudgetReached();
			Retire(c);
			st.unproven.fetch_add(1, std::memory_order_relaxed);
			DevCon.WriteLn(Color_Gray, "Cenit superblock: PC=%04x sin evidencia en %u ventanas "
				"(%u pares, %u episodios de %u) -> cadena normal.",
				pc, c.life.evidence.windows, pairsSeen, c.life.evidence.episodes,
				mVUSbLife::kBudgetEpisodes);
			// Presupuesto agotado: NO se quema el PC (es "no medi", la maquina lo
			// archiva y AllocSlot puede reusar el hueco), PERO se quema para no
			// recompilar en bucle un area que ya no tiene presupuesto: el bit de
			// quemado vive por identidad de cache y el proximo code cache lo limpia.
			SetBurned(idx);
		}

		// ¿Este candidato debe despachar la variante? LO DECIDE LA MAQUINA.
		__fi bool ShouldServe(const Candidate& c)
		{
			return c.life.DispatchesVariant();
		}
	}

	// ------------------------------------------------------------------
	// Activación
	// ------------------------------------------------------------------
	void SyncFromConfig(bool enabled)
	{
		const bool prev = g_enabled.load(std::memory_order_relaxed);

		// Auto-apagado por divergencias: por proceso. Un toggle del usuario no
		// re-enciende (el informe ya dijo por qué).
		if (enabled && s_killed)
		{
			if (prev)
			{
				g_enabled.store(false, std::memory_order_relaxed);
				DumpReport("config (motor bloqueado por divergencias)");
			}
			return;
		}

		if (enabled == prev)
			return;

		if (enabled)
		{
			// OFF->ON: tabla limpia. El CheckForCPUConfigChanges del toggle ya
			// tiró el code cache, así que ninguna variante fusionada sobrevive
			// a este punto; los PCs quemados de antes tampoco significan nada
			// con un cache nuevo, así que se reinician con todo lo demás.
			ResetTableForEnable();
			g_enabled.store(true, std::memory_order_relaxed);
			// Estado de la superficie, dicho en voz alta en cada arranque: es la
			// razon por la que el HUD de esta version dira VALIDATE y no SHADOW.
			const bool surface = g_rollbackSurface.load(std::memory_order_relaxed);
			Console.WriteLn(Color_StrongGreen, "vuSB: motor de superbloques ACTIVADO — %s "
				"(%u uniones/%u ops/%u ciclos por superbloque, ventana de %u despachos "
				"emparejables). Promocion: %u pares sin divergencia en >%u ventana, mas "
				"ganancia medida con n>=%u y P95 no peor.",
				surface ? "SHADOW disponible donde MTVU este apagado" : "VALIDATE_ONLY "
					"(superficie de snapshot/restore NO implementada: la sombra esta "
					"codificada como regla, no encendida de favor)",
				kSbMaxJunctions, kSbMaxOps, kSbMaxCycles, kSbVerifyDispatches,
				mVUSbLife::kValidPairs, mVUSbLife::kValidWindows - 1u, mVUSbLife::kMinGainPairs);
		}
		else
		{
			// ON->OFF: apagar PRIMERO y volcar después (el informe final
			// truncará el archivo; los instantáneas intermedias se habían
			// adjuntado). Con el motor OFF SbResolve devuelve nullptr, así que
			// soltar la tabla basta para que ninguna variante corra; el código
			// fusionado queda en el cache hasta el próximo reset.
			g_enabled.store(false, std::memory_order_relaxed);
			Console.WriteLn("vuSB: motor DESACTIVADO — volcando medición");
			DumpReport("config: motor apagado");
			for (u32 i = 0; i < kSbMaxVariants; i++)
				Retire(s_tab[i]);
			s_live = 0;
		}
	}

	bool WasKilled()
	{
		return s_killed;
	}

	// Fase 1 (0.6.31): ¿esta el motor ocupado? Ver el contrato en el header. El
	// bucle es sobre 256 entradas leidas como relaxed: corre una vez por segundo
	// (el tick del regidor), no por cuadro, asi que el costo es irrelevante.
	// 0.6.32: la puerta es la maquina — VALIDATE y SHADOW son los dos estados en
	// los que la variante se alterna con la cadena normal DENTRO de una ventana,
	// o sea los dos que cambian como se mide el intervalo que el tuner quiere
	// medir. TRUSTED no cuenta (despacha siempre la misma ruta) y VALIDATED
	// tampoco (no despacha: espera su numero de ganancia).
	bool Validating()
	{
		if (!IsEnabled() || s_killed)
			return false;
		for (u32 i = 0; i < kSbMaxVariants; i++)
		{
			const Machine& m = s_tab[i].life;
			if (m.state == State::Validating || m.state == State::Shadowing)
				return true;
		}
		return false;
	}

	// ------------------------------------------------------------------
	// Política de elegibilidad — Fase 2
	// ------------------------------------------------------------------
	int SbArmCompile(u32 startPC_bytes, const void* pState96)
	{
		if (!IsEnabled() || s_killed)
			return -1;

		Stats& st = g_stats[1];
		st.attempts.fetch_add(1, std::memory_order_relaxed);

		// Un solo candidato vivo por PC de entrada (y no re-armar mientras la
		// compilación variante está en vuelo — mismo hilo, pero el hook puede
		// dispararse otra vez antes de SbCloseCompile si el dispatcher entra
		// dos veces seguidas a este PC).
		if (FindForPc(startPC_bytes))
		{
			st.rejected_identity.fetch_add(1, std::memory_order_relaxed);
			return -1;
		}

		const u32 idx = PcIdx(startPC_bytes);
		if (IsBurned(idx))
		{
			st.rejected_identity.fetch_add(1, std::memory_order_relaxed);
			return -1;
		}

		// La arista incondicional A->B tiene que existir y B tiene que estar
		// compilado (forma registrada): sin ambos datos medidos no hay nada
		// que fusionar, solo conjetura.
		const mVUTraceProbe::BlockEdge& e = mVUTraceProbe::g_edges[1][idx];
		if (e.kind != 1)
		{
			st.rejected_struct.fetch_add(1, std::memory_order_relaxed);
			return -1;
		}
		const mVUTraceProbe::BlockShape& shapeA = mVUTraceProbe::g_shape[1][idx];
		const mVUTraceProbe::BlockShape& shapeB = mVUTraceProbe::g_shape[1][e.succIdx];
		if (shapeA.reason == 0 || shapeB.reason == 0)
		{
			st.rejected_struct.fetch_add(1, std::memory_order_relaxed);
			return -1;
		}

		// Hotness: A tiene que ser un bloque caliente medido, B tiene que
		// haber corrido solo un montón de veces, y el ratio tiene que decir
		// que A cae de forma estable sobre B (no que B tenga otro predecesor
		// dominante).
		const u64* slots = mVUTraceProbe::SlotArray(1);
		if (!slots)
		{
			st.rejected_cold.fetch_add(1, std::memory_order_relaxed);
			return -1;
		}
		const u64 execA = slots[idx];
		const u64 execB = slots[e.succIdx];
		if (execA < kSbHotEntries || execB < kSbTargetMinEntries)
		{
			st.rejected_cold.fetch_add(1, std::memory_order_relaxed);
			return -1;
		}
		const u64 ratioPct = execB * 100ull / (execA ? execA : 1ull);
		if (ratioPct < kSbMinRatioPct || ratioPct > kSbMaxRatioPct)
		{
			st.rejected_cold.fetch_add(1, std::memory_order_relaxed);
			return -1;
		}

		// Tamaño del par: si A+B ya pasa los techos del área, la primera
		// unión sería la última y no paga la compilación extra.
		if (static_cast<u64>(shapeA.ops) + shapeB.ops > kSbMaxOps ||
			static_cast<u64>(shapeA.cycles) + shapeB.cycles > kSbMaxCycles)
		{
			st.rejected_budget.fetch_add(1, std::memory_order_relaxed);
			return -1;
		}

		Candidate* c = AllocSlot();
		if (!c)
		{
			st.rejected_identity.fetch_add(1, std::memory_order_relaxed);
			return -1;
		}

		c->startPC = startPC_bytes;
		std::memcpy(c->key, pState96, kKeyBytes);

		// Identidad ANTES de armar: si el hueco traia otra identidad (reasignado
		// de un candidato muerto), RebindIfChanged limpia y deja la maquina en
		// UNSEEN, que es exactamente desde donde StartBuild es legal.
		AreaSpec seed;
		seed.count = 1;
		seed.pcs[0] = startPC_bytes;
		seed.ops[0] = mVUTraceProbe::g_shape[1][idx].ops;
		c->life.RebindIfChanged(MakeIdentity(startPC_bytes, &seed));
		c->life.SetObserver(g_exitMask.load(std::memory_order_relaxed));
		c->life.StartBuild();
		c->epoch = 1;
		c->windowIn = 0;
		c->parity = false;
		c->sumBaseNs = 0;
		c->sumSbNs = 0;
		c->timedPairs = 0;
		c->ringPos = 0;
		c->ringFill = 0;
		return static_cast<int>(c - s_tab);
	}

	int SbAskJunction(int slot, u32 targetPC_bytes, bool branchStructClean,
		u32 regionOps, u32 regionCycles)
	{
		if (slot < 0 || slot >= static_cast<int>(kSbMaxVariants))
			return -1;

		Candidate& c = s_tab[slot];
		if (c.life.state != State::Building)
			return -1;

		Stats& st = g_stats[1];

		if (!branchStructClean)
		{
			st.rejected_struct.fetch_add(1, std::memory_order_relaxed);
			return -1;
		}

		if (c.junctions >= kSbMaxJunctions)
		{
			st.rejected_budget.fetch_add(1, std::memory_order_relaxed);
			return -1;
		}

		// Techos del área: regionOps/regionCycles son los contadores del
		// análisis en el punto de la unión. La unión mete además el tramo
		// destino, así que se exige que el área ACTUAL deje hueco para él.
		if (regionOps >= kSbMaxOps || regionCycles >= kSbMaxCycles)
		{
			st.rejected_budget.fetch_add(1, std::memory_order_relaxed);
			return -1;
		}

		// El destino tiene que ser un PC de entrada real (8-byte alineado,
		// dentro de la imagen) y tiene que haber sido compilado: fusionar
		// hacia código que nadie analizó no es una fusión, es una conjetura.
		const u32 tidx = PcIdx(targetPC_bytes);
		if ((targetPC_bytes & 7u) != 0)
		{
			st.rejected_struct.fetch_add(1, std::memory_order_relaxed);
			return -1;
		}
		if (mVUTraceProbe::g_shape[1][tidx].reason == 0)
		{
			st.rejected_struct.fetch_add(1, std::memory_order_relaxed);
			return -1;
		}

		// No se fusiona hacia el propio PC de entrada (bucle de un solo
		// tramo): el superbloque no elimina la barrera, se elimina a sí
		// mismo y el area no termina nunca en el paso 1.
		if (targetPC_bytes == c.startPC)
		{
			st.rejected_struct.fetch_add(1, std::memory_order_relaxed);
			return -1;
		}

		const u32 j = c.junctions;
		c.junctions = j + 1;
		st.junctions.fetch_add(1, std::memory_order_relaxed);
		return static_cast<int>(j);
	}

	void SbCloseCompile(int slot, u32 startPC_bytes, void* hostEntry,
		u32 junctions, bool areaBad, u32 nOps, u32 nCycles)
	{
		if (slot < 0 || slot >= static_cast<int>(kSbMaxVariants))
			return;

		Candidate& c = s_tab[slot];
		if (c.life.state != State::Building)
			return;

		Stats& st = g_stats[1];

		// Barrido de los bits de rasguño en la clave de pareo (obligatorio en
		// AMBOS caminos, antes de cualquier decisión). La variante se registró
		// con kSbScratchVariant horneado en blockType — pero solo en la copia
		// del gestor (add() memcpy), NUNCA en lpState: mVUinitFirstPass copia
		// pState a lpState antes del horneado. Queda un vector real: la guarda
		// de presupuesto del superbloque, si llegara a romperse a mitad de
		// fusion, ejecuta copyPLState(&mVUpBlock->pState) y hornearia el bit en
		// lpState para el proximo despacho. SbResolve exige igualdad byte-a-
		// byte contra este key; con los bits barridos aqui, un lpState horneado
		// simplemente NO parea (despacho -> cadena normal, telemetria
		// unmatched) en lugar de envenenar el emparejamiento con una divergencia
		// falsa — o peor, hacer que una busqueda normal de la cadena no
		// encuentre su propio bloque limpio. El registro del gestor conserva
		// sus bits a proposito: una clave de gestor barrida haria la variante
		// alcanzable por search() normal y romperia el GATE de validacion.
		c.key[kSbKeyBlockTypeOff] &= static_cast<u8>(~kSbScratchMask);

		// Un área con kick o con bits T/D no se adopta nunca (el documento:
		// los efectos en VIF1 y las señales al EE son observables por el host;
		// los cortocircuitos de interrupción VU→EE de T/D dentro de una fusión
		// son exactamente el tipo de evento que la regla de oro prohíbe mover).
		// junctions==0 significa que el paso 1 no cruzó ninguna unión: no hay
		// superbloque, solo una compilación duplicada que se tira. nOps/nCycles
		// por encima de los techos: el área creció tras la última unión (un
		// tramo largo hasta el corte natural) y ya no es un superbloque
		// medido contra presupuesto — fuera de política.
		const bool shapeOk = !areaBad && junctions >= 1 && hostEntry && nOps != 0 &&
			nOps <= kSbMaxOps && nCycles <= kSbMaxCycles;
		if (!shapeOk)
		{
			if (areaBad)
				st.rejected_struct.fetch_add(1, std::memory_order_relaxed);
			else
				st.rejected_budget.fetch_add(1, std::memory_order_relaxed);
			// Quema el PC: este rechazo es DETERMINISTA (la forma del area es
			// la que es — mismo PC, mismo microcodigo, mismo veredicto). Sin
			// quemar, cada despacho al PC re-armaria una compilacion variante
			// que vuelve a rechazar: compilaciones y enlaces muertos de forma
			// indefinida hasta agotar el code cache. La maqueda NO es la del
			// prompt (un "no" medido); la maquina queda en UNSEEN y el hueco es
			// reasignable: quien decide el "no permanente" aqui es la forma, y
			// la forma no cambia sin reescribir microcodigo (SbKillAll).
			c.entry = nullptr;
			c.inUse = false;
			if (s_live)
				s_live--;
			SetBurned(PcIdx(c.startPC));
			return;
		}

		c.entry = hostEntry;
		c.junctions = junctions;
		c.regionOps = nOps;
		c.regionCycles = nCycles;
		c.windowIn = 0;
		c.parity = false;

		// Identidad con el tramo de ENTRADA ya medido. El area completa no es contigua
		// y el motor no guardo la lista de destinos (no la necesita para despachar),
		// asi que el digest que forma la identidad es el del tramo de entrada mas lo
		// que el observador decida mirar de los siguientes: Refinar la etiqueta con
		// RelabelWhileBuilding es legal porque AUN no hay evidencia que romper — y si
		// alguien cambia el microcodigo, SbKillAll invalida el candidato igual que a
		// cualquier otro.
		AreaSpec area;
		area.count = 1;
		area.pcs[0] = startPC_bytes;
		area.ops[0] = mVUTraceProbe::g_shape[1][PcIdx(startPC_bytes)].ops;
		c.life.RelabelWhileBuilding(MakeIdentity(startPC_bytes, &area));
		c.life.SetObserver(g_exitMask.load(std::memory_order_relaxed));
		c.life.FinishBuild(true, !Rules::OutOfBudget(c.life.evidence),
			g_rollbackSurface.load(std::memory_order_relaxed));
		st.built.fetch_add(1, std::memory_order_relaxed);
		st.fused_ops.fetch_add(nOps, std::memory_order_relaxed);

		DevCon.WriteLn(Color_Yellow, "Cenit superblock: built PC=%04x spans=%u ops=%u cycles=%u "
			"-> %s (identidad %s, digest=%08x, ventana %u despachos).",
			startPC_bytes, junctions + 1u, nOps, nCycles, mVUSbLife::StateName(c.life.state),
			c.life.identity.Complete() ? "completa" : "INCOMPLETA: sin promocion",
			c.life.identity.codeDigest, kSbVerifyDispatches);
	}

	// ------------------------------------------------------------------
	// Despacho — resolución y ventana de replay
	// ------------------------------------------------------------------
	void* SbResolve(u32 startPC_bytes, const void* pState96, s32 budget)
	{
		if (!IsEnabled() || s_killed)
			return nullptr;

		// Escaneo lineal: con la tabla casi vacía (el caso común) esto es una
		// comprobación de estado fallida por hueco; el hook del dispatcher
		// solo llama aquí cuando el motor está ON.
		for (u32 i = 0; i < kSbMaxVariants; i++)
		{
			Candidate& c = s_tab[i];
			if (!c.inUse || c.startPC != startPC_bytes)
				continue;

			// La maquina manda, no el deseo: si no esta en un estado que despacha
			// la variante (VALIDATE, SHADOW/TRIAL, VALIDATED/TRIAL, TRUSTED o
			// vigilancia), ni se mira la clave. Un DEAD o un INVALIDATED que aun
			// tiene el hueco asignado queda aqui mismo descartado.
			if (!ShouldServe(c))
				return nullptr;

			// Identidad: la variante solo vale para la clave de registers con
			// la que se construyó. Cualquier otra entrada es cadena normal.
			if (std::memcmp(c.key, pState96, kKeyBytes) != 0)
				return nullptr;

			// Presupuesto: el superbloque cobra los ciclos del área completa
			// (S) en su guarda de entrada. El filtro exige budget >= S+1 y la
			// razón es el PUNTO DE CORTE: con budget == S exacto, la cadena
			// normal rompe justo al terminar el último tramo (su guarda por
			// bloque chequea budget - s_k - 1 < 0) mientras el superbloque no
			// rompe ahí y sigue hacia el bloque siguiente de la cadena; los dos
			// episodios terminarian en puntos distintos con memoria y estado
			// legitimately distintos -> divergencia falsa. Con budget >= S+1
			// ninguna guarda de bloque dispara DENTRO del area en ninguna de las
			// dos rutas (restante al inicio de cada tramo k >= (S - s_{k-1}) + 1
			// > s_k - s_{k-1} = ciclos del tramo). Mas alla del area ambas rutas
			// llegan al mismo punto con el mismo mVU.cycles (el modelo de
			// reposicion del paso 2 lo garantiza op a op), asi que un break
			// posterior ocurre igual en las dos. O corre el area entera en
			// ambas, o no se empareja: cero cortes a media fusion.
			if (budget < static_cast<s32>(c.regionCycles) + 1)
			{
				g_stats[1].rejected_budget.fetch_add(1, std::memory_order_relaxed);
				return nullptr;
			}

			// TRUSTED: despacha siempre, con su spot-check periodico. La
			// vigilancia NO es "una comprobacion suelta": reabre la ventana
			// alternada, porque sin superficie de snapshot/restore no se puede
			// correr la variante y la normal sobre el MISMO estado dentro de un
			// episodio. Es la unica manera honesta de seguir comprobando a un
			// confiable, y la maquina la implementa (BeginSpotCheck).
			if (c.life.state == State::Trusted)
			{
				if (Rules::ShouldSpotCheck(c.life.dispatches))
				{
					if (c.life.BeginSpotCheck())
					{
						g_stats[1].spot_checks.fetch_add(1, std::memory_order_relaxed);
						DevCon.WriteLn(Color_Gray, "Cenit superblock: PC=%04x TRUSTED -> "
							"ventana de vigilancia (replay reabierto).", c.startPC);
					}
					else
					{
						// BeginSpotCheck dijo no: o ya esta abierta la ventana (no
						// puede pasar con Trusted) o el candidato vive en modo sombra,
						// y en sombra la vigilancia se hace con snapshot/restore — el
						// mecanismo que g_rollbackSurface declara si existe. Mientras
						// no exista, un Trusted en sombra NO se reabre por replay y
						// tampoco se sirve a ciegas: se queda en la cadena normal.
						if (c.life.shadowMode)
							return nullptr;
						return c.entry;
					}
				}
				else
				{
					return c.entry;
				}
			}

			// Ventana de PRUEBA declarada (SHADOW/VALIDATED con trialLeg): la
			// variante sirve el juego ENTERO y no hay par que formar. El motor de
			// esta version no declara pruebas — la ganancia se mide con los pares
			// del replay, que es una comparacion manzana con manzana mejor que un
			// A/B de FPS —, pero el camino esta escrito para que la maquina no
			// sea la que mienta si alguien lo declara despues.
			if (c.life.trialLeg)
			{
				c.windowIn++;
				return c.entry;
			}

			// SHADOW sin ventana de prueba: el autoritativo es la cadena normal y
			// la variante solo correria dentro del snapshot/restore — que es justo
			// lo que g_rollbackSurface dice si existe o no. Si por un error de
			// llamador llegara aqui con la maquina en SHADOW, NO se sirve la
			// variante y no se declara episodio: sin superficie no hay sombra.
			if (c.life.state == State::Shadowing)
				return nullptr;

			// VALIDATED fuera de prueba: espera su numero de ganancia sin tocar el
			// juego (RollWindow se lo da con los pares ya medidos).
			if (c.life.state == State::Validated)
				return nullptr;

			// --- replay en curso (VALIDATE, o vigilancia de un TRUSTED) ---
			// Alternacion estricta para que los episodios lleguen emparejados por
			// construccion.
			Stats& st = g_stats[1];
			c.windowIn++;
			if (c.parity)
			{
				c.parity = false;
				c.sPending = true;
				st.verify_sb.fetch_add(1, std::memory_order_relaxed);
				return c.entry;
			}
			c.parity = true;
			c.nPending = true;
			st.verify_normal.fetch_add(1, std::memory_order_relaxed);

			// Ruta normal dentro de la ventana: el episodio se cierra en
			// SbFinish, así que se devuelve nullptr (el dispatcher sigue su
			// cadena resuelta) pero el episodio queda declarado pendiente.
			return nullptr;
		}

		return nullptr;
	}

	void SbFinish(u32 startPC_bytes, u32 consumedCycles, u64 episodeNs)
	{
		if (!IsEnabled() || s_killed)
			return;

		// Filtro barato ANTES de pedir el digest: el hash de salida cuesta
		// (incluye los 16 KB de memoria de datos de VU1) y solo hace falta
		// para un episodio que SbResolve declaró pendiente. Un despacho a un
		// PC con candidato VERIFY pero sin episodio emparejado (clave distinta
		// o presupuesto corto) es telemetría, no evidencia.
		Candidate* c = nullptr;
		for (u32 i = 0; i < kSbMaxVariants; i++)
		{
			Candidate& t = s_tab[i];
			if (t.inUse && t.startPC == startPC_bytes)
			{
				c = &t;
				break;
			}
		}
		if (!c)
			return;

		const u32 idx = PcIdx(c->startPC);

		if (!c->nPending && !c->sPending)
		{
			g_stats[1].unmatched.fetch_add(1, std::memory_order_relaxed);
			// Un TRUSTED que despacha fuera de su ventana de vigilancia NO forma par
			// (no hay episodio al otro lado), pero SU despacho si cuenta: la vigilancia
			// se dispara por numero de despachos (Rules::ShouldSpotCheck sobre
			// life.dispatches). Sin esta linea un Trusted serviria la variante para
			// siempre sin volver a comprobarse nunca — que es exactamente la confianza
			// no verificable que el prompt llama vender como activo lo que no se mide.
			// Solo se llama a EpisodeDone en este estado: para un VALIDATE sin par, el
			// presupuesto mide el trabajo REAL (episodios con digest), y aqui no hubo
			// digest — cobrarlo inflaria el gasto y archivaria candidatos sanos.
			if (c->life.state == State::Trusted)
				c->life.EpisodeDone();
			return;
		}

		if (!g_exitDigest)
			return;
		const u64 digest = g_exitDigest(consumedCycles);

		const bool wasSpot = c->life.spotLeg;
		if (c->sPending)
		{
			c->sPending = false;
			c->pendS = digest;
			c->pendSNS = episodeNs;
			c->sReady = true;
		}
		else
		{
			c->nPending = false;
			c->pendN = digest;
			c->pendNNs = episodeNs;
			c->nReady = true;
		}

		// Trabajo real hecho: el presupuesto de validacion mide EPISODIOS (cada
		// uno cuesta su digest, y con superficie costaria tambien un
		// snapshot/restore de ~21 KB), no pares — un par son dos episodios.
		c->life.EpisodeDone();

		ClosePair(*c, idx);

		// Vigilancia cerrada limpia: la maquina vuelve a TRUSTED con la evidencia
		// que ya tenia (no se re-midio la ganancia, se volvio a comprobar la
		// equivalencia). Si la ventana hubiera divergido, ClosePair ya lo mato y
		// EndSpotCheck devuelve false.
		if (wasSpot && !c->nPending && !c->sPending &&
			(c->life.state == State::Validating || c->life.state == State::Shadowing))
		{
			// clean = la evidencia sigue sin divergencias (si hubiera divergido,
			// ClosePair ya lo hubiera matado y EndSpotCheck responde false).
			if (c->life.EndSpotCheck(c->life.evidence.divergences == 0))
			{
				DevCon.WriteLn(Color_Yellow, "Cenit superblock: PC=%04x vigilancia limpia -> "
					"sigue TRUSTED.", c->startPC);
			}
		}

		// Presupuesto de trabajo agotado: la maquina decide si promociona o
		// archiva (nunca quema por presupuesto).
		if (Rules::OutOfBudget(c->life.evidence))
		{
			const State before = c->life.state;
			c->life.BudgetReached();
			if (before != c->life.state && c->life.state == State::Invalidated)
			{
				WindowExhausted(*c, idx);
				return;
			}
		}

		// Cambio de ventana (id monotono para la maquina) y entrega de la ganancia
		// medida. Se hace con la ventana limpia — sin episodios en vuelo — para no
		// mezclar despachos de dos ventanas en un mismo id.
		if (c->inUse && c->life.Occupies() && c->windowIn >= kSbVerifyDispatches &&
			!c->nPending && !c->sPending)
		{
			RollWindow(*c, idx);
			if (c->life.state == State::Validated || c->life.state == State::Trusted)
			{
				// El candidato ya no alternando: si quedo VALIDATED sin promocionar,
				// RollWindow lo retiro; si quedo TRUSTED, sigue despachando.
			}
			if (!c->life.Occupies())
			{
				Retire(*c);
				c->inUse = false;
				if (s_live)
					s_live--;
			}
		}
	}

	// ------------------------------------------------------------------
	// Invalidación masiva
	// ------------------------------------------------------------------
	void SbKillAll(u32 gen)
	{
		// gen: generación de invalidación (reservado para el informe — la
		// política actual no lo necesita: todo vuelco vale sin él).
		(void)gen;

		if (!IsEnabled())
			return;

		for (u32 i = 0; i < kSbMaxVariants; i++)
		{
			Candidate& c = s_tab[i];
			if (!c.inUse)
				continue;
			// Toda escritura a la imagen micro tira los programas afectados;
			// un superbloque construido sobre microcódigo que cambió no puede
			// quedar vivo ni una instrucción (regla del documento: cero trazas
			// vivas sobre código modificado). La maquina lo archiva — INVALIDATED,
			// que no es un "no" sino un "no vale esta evidencia" — y el hueco pasa
			// a ser reasignable. Los PCs quemados por FORMA siguen quemados:
			// invalidar por escritura no borra una divergencia medida.
			if (c.life.Occupies())
				c.life.InvalidateContext();
			Retire(c);
		}
	}

	// ------------------------------------------------------------------
	// HUD — una linea, dicha por la maquina
	// ------------------------------------------------------------------
	void HudSummary(char* out, u32 outSize)
	{
		if (!out || outSize < 192)
			return;
		out[0] = '\0';

		if (!IsEnabled() && !s_killed)
			return;

		const Candidate* best = nullptr;
		int bestRank = 0;
		u32 nValidate = 0, nShadow = 0, nValidated = 0, nTrusted = 0, nDead = 0, nArchived = 0;
		u32 maskRejected = 0;
		for (u32 i = 0; i < kSbMaxVariants; i++)
		{
			const Candidate& c = s_tab[i];
			if (!c.inUse)
				continue;
			switch (c.life.state)
			{
			case State::Validating:	nValidate++; break;
			case State::Shadowing:	nShadow++;   break;
			case State::Validated:	nValidated++; break;
			case State::Trusted:	nTrusted++;  break;
			case State::Dead:		nDead++;     break;
			case State::Invalidated: nArchived++; break;
			default: break;
			}
			maskRejected += c.life.evidence.pairsMaskRejected;
			const int r = RankOf(c.life);
			if (!best || r > bestRank)
			{
				bestRank = r;
				best = &c;
			}
		}

		// La etiqueta sin candidatos NO puede decir OFF: el motor esta encendido y
		// OFF se leeria como "no lo active", que es distinto de "no hay nada
		// fusionable aun". Y si el breaker paro el proceso, eso tambien se dice.
		const char* label = best ? best->life.HudLabel() :
			(s_killed ? "VU-SB: PARADO" : "VU-SB: ON/SIN CANDIDATO");
		// Los recuentos que se pintan son los de LA MAQUINA (pares y ventanas del
		// candidato mas adelantado), no los acumulados del proceso: un "match 4000"
		// global junto a un candidato con 3 pares haria parecer que hay evidencia
		// donde la hay de un solo bloque. El acumulado completo esta en el informe.
		const Evidence ev = best ? best->life.evidence : Evidence();
		std::snprintf(out, outSize,
			"%s pares=%u/%u ventana=%u | val=%u sh=%u vtd=%u trs=%u dead=%u arch=%u rej=%u",
			label, ev.pairs, mVUSbLife::kValidPairs, ev.windowsWithPairs,
			nValidate, nShadow, nValidated, nTrusted, nDead, nArchived, maskRejected);
		out[outSize - 1] = '\0';
	}

	// ------------------------------------------------------------------
	// Informe — igual que la sonda: detalle a logs/vu_superblock.txt +
	// resumen por Console (viaja en emulog.txt).
	// ------------------------------------------------------------------
	void DumpReport(const char* reason)
	{
		// Orden de locks: el serial ANTES de s_dumpMutex (inversión ABBA
		// real al revés, igual que en la sonda: la ruta JNI toma el mutex y
		// luego pide el serial).
		const std::string serial = VMManager::HasValidVM() ? VMManager::GetDiscSerial() : std::string();

		std::lock_guard<std::mutex> lock(s_dumpMutex);

		u32 nValidate = 0, nShadow = 0, nValidated = 0, nTrusted = 0, nDead = 0, nArchived = 0, nBuilding = 0;
		u32 maskRejected = 0, episodesMax = 0;
		for (u32 i = 0; i < kSbMaxVariants; i++)
		{
			const Candidate& c = s_tab[i];
			if (!c.inUse)
				continue;
			switch (c.life.state)
			{
			case State::Building:	 nBuilding++;	break;
			case State::Validating:	 nValidate++;	break;
			case State::Shadowing:	 nShadow++;		break;
			case State::Validated:	 nValidated++;	break;
			case State::Trusted:	 nTrusted++;	break;
			case State::Dead:		 nDead++;		break;
			case State::Invalidated: nArchived++;	break;
			default: break;
			}
			maskRejected += c.life.evidence.pairsMaskRejected;
			if (c.life.evidence.episodes > episodesMax)
				episodesMax = c.life.evidence.episodes;
		}

		const u32 declaredMask = g_exitMask.load(std::memory_order_relaxed);
		const bool maskOk = Rules::MaskComplete(declaredMask);

		// Rotación igual que la sonda: los vuelcos INTERMEDIOS (motor aún ON)
		// se añaden; el vuelco FINAL (g_enabled ya en false) truncará para
		// que el informe completo esté arriba del todo.
		const std::string path = Path::Combine(EmuFolders::Logs, "vu_superblock.txt");
		std::FILE* fp = FileSystem::OpenCFile(path.c_str(), IsEnabled() ? "ab" : "wb");
		// Macro, no lambda: conserva el attribute format(printf) de SbPrintf.
#define P(...) SbPrintf(fp, __VA_ARGS__)

		P("Cenit VU Superblock Engine — informe Fases 2-5 (0.6.32: maquina por candidato)\n");
		P("build=%s (%s)  motivo=%s  motor=%s\n",
			BuildVersion::AppVersion, BuildVersion::GitShort, reason ? reason : "?",
			IsEnabled() ? "ON" : "OFF");
		P("juego=%s  EECycleRate=%d EECycleSkip=%d progCache=%d\n",
			serial.empty() ? "sin VM" : serial.c_str(),
			static_cast<int>(EmuConfig.Speedhacks.EECycleRate),
			static_cast<int>(EmuConfig.Speedhacks.EECycleSkip),
			static_cast<int>(EmuConfig.Cpu.Recompiler.EnableVUProgramCache));
		P("estado: auto-apagado=%s\n", s_killed ? "SI" : "no");
		P("\n");
		P("politica: hot=%u target_min=%u ratio=[%u,%u]%% max{uniones=%u,ops=%u,ciclos=%u}\n",
			kSbHotEntries, kSbTargetMinEntries, kSbMinRatioPct, kSbMaxRatioPct,
			kSbMaxJunctions, kSbMaxOps, kSbMaxCycles);
		// Las leyes de promocion viven en MvuSbLife.h (Harness9 las prueba); este
		// bloque las COPIA para que el informe se pueda leer sin el codigo. Si
		// alguien cambia la maquina sin cambiar aqui, jnidict.py lo canta.
		P("validacion (maquina por candidato): %u pares MATCH sin divergencia en >%u ventana; "
			"presupuesto %u episodios\n",
			mVUSbLife::kValidPairs, mVUSbLife::kValidWindows - 1u, mVUSbLife::kBudgetEpisodes);
		P("trusted: identidad completa + ganancia >= %+d.%u%% medida con n>=%u pares cronometrados "
			"+ P95 no peor; vigilancia cada %u despachos\n",
			mVUSbLife::kTrustedGainPermille / 10, mVUSbLife::kTrustedGainPermille % 10,
			mVUSbLife::kMinGainPairs, mVUSbLife::kTrustedSpotPeriod);
		P("ventana del motor: %u despachos emparejables = 1 ventana (id monotono a la maquina)\n",
			kSbVerifyDispatches);
		P("breaker: sin superficie=%u divergencias; con superficie=%u en >=%u candidatos\n",
			kSbBreakNoSurface, kSbBreakWithSurface, kSbBreakCandidates);
		P("\n");
		P("observadores: digest=%s mascara=0x%x (%s) requerida=0x%x area=%s abi=%u "
			"rollback=%s perfil=%016llx\n",
			g_exitDigest ? "registrado" : "AUSENTE", declaredMask,
			maskOk ? "COMPLETA" : "INCOMPLETA — sin evidencia posible",
			mVUSbLife::kRequiredRegions, g_areaDigest ? "si" : "NO",
			g_engineAbi.load(std::memory_order_relaxed),
			g_rollbackSurface.load(std::memory_order_relaxed) ? "IMPLEMENTADO" : "NO (VALIDATE_ONLY)",
			(unsigned long long)s_profileHash);
		if (!maskOk)
			P("AVISO: la mascara declarada NO cubre todo lo requerido: %u pares rechazados. "
			  "Nada de esto se promociona, y no es un bug del candidato — es el observador.\n",
				maskRejected);
		P("modo efectivo: %s\n",
			g_rollbackSurface.load(std::memory_order_relaxed)
				? (THREAD_VU1 ? "MTVU encendido -> VALIDATE_ONLY (la sombra no es segura: el "
					  "fetch_or de mtvuInterrupts lo lee el hilo EE y ningun rollback lo deshace)"
					: "SHADOW habilitable (superficie + sin MTVU)")
				: "VALIDATE_ONLY: la superficie de snapshot/restore no esta implementada. "
				  "El HUD nunca dira SHADOW ni 'acelerado' en este estado, y la ganancia se "
				  "mide aun asi por pares de duracion (no por FPS).");
		P("\n");
		DumpStatsLine(fp, "[VU1]", g_stats[1]);
		P("\n");
		P("tabla: %u huecos asignados (building=%u validate=%u shadow=%u validated=%u trusted=%u "
			"dead=%u archived=%u)  PCs quemados=%u  divergencias=%u en %u candidatos  "
			"episodios max=%u\n",
			s_live, nBuilding, nValidate, nShadow, nValidated, nTrusted, nDead, nArchived,
			s_burnedCount, s_divergences, s_divergedPcs, episodesMax);
		P("\n");
		P("PC        estado       uniones  ops  ciclos  ventana  pares  ventanas  diverg  n(gan)  ganancia  P95us  mtvu  digest    entrada\n");
		for (u32 i = 0; i < kSbMaxVariants; i++)
		{
			const Candidate& c = s_tab[i];
			if (!c.inUse)
				continue;
			const Machine& m = c.life;
			char gain[24];
			if (m.evidence.HasGain())
				std::snprintf(gain, sizeof(gain), "%+d.%u%%", (int)(m.evidence.gainPermille / 10),
					(unsigned)(m.evidence.gainPermille < 0 ? -((-m.evidence.gainPermille) % 10) :
						m.evidence.gainPermille % 10));
			else
				std::snprintf(gain, sizeof(gain), "(sin medir)");
			gain[sizeof(gain) - 1] = '\0';
			P("%08x  %-11s %7u %5u %6u %8u %6u %9u %7u %8u %11s %7d %4u  %08x  %p\n",
				c.startPC, mVUSbLife::StateName(m.state), c.junctions, c.regionOps,
				c.regionCycles, c.epoch, m.evidence.pairs, m.evidence.windowsWithPairs,
				m.evidence.divergences, m.evidence.timedPairs, gain,
				m.evidence.p95DeltaUs, m.identity.mtvu, m.identity.codeDigest, c.entry);
		}
		P("\n");
		P("GATE: el documento exige cero divergencias Y mejora sostenida medida\n");
		P("en el dispositivo antes de considerar esta ruta lista. Con 0 pares o\n");
		P("candidatos unproven no hay evidencia de nada: es un no, no un casi.\n");
		P("La maquina de estados de este informe es la misma que Harness9 prueba en\n");
		P("host (tools/arneses/h9-src, linea base de baseline.sh): transiciones, umbrales,\n");
		P("presupuesto, identidad, mascaras y honestidad del HUD. Lo que NO prueba es que un\n");
		P("superbloque real no diverja ni que acelere: eso solo se ve aqui, en el\n");
		P("telefono, con el motor encendido.\n");
#undef P

		if (fp)
		{
			std::fclose(fp);
			Console.WriteLn(Color_StrongGreen, "vuSB: informe completo escrito en logs/vu_superblock.txt (%s)",
				reason ? reason : "?");
		}
		else
		{
			Console.WriteLn(Color_Orange, "vuSB: NO se pudo abrir logs/vu_superblock.txt (%s)",
				reason ? reason : "?");
		}

		// Resumen al emulog: es lo que llega con el botón "Enviar registro".
		const Stats& s = g_stats[1];
		Console.WriteLn("vuSB: [%s] huecos=%u build=%u validate=%u shadow=%u validated=%u trusted=%u "
			"dead=%u archived=%u quemados=%u divergencias=%u/%u candidatos auto-off=%u",
			reason ? reason : "?", s_live, nBuilding, nValidate, nShadow, nValidated, nTrusted,
			nDead, nArchived, s_burnedCount, s_divergences, s_divergedPcs, s_killed ? 1u : 0u);
		Console.WriteLn("vuSB: observadores{digest=%s mascara=0x%x completa=%s rollback=%s} "
			"paresRechazadosPorMascara=%u",
			g_exitDigest ? "si" : "NO", declaredMask, maskOk ? "si" : "NO",
			g_rollbackSurface.load(std::memory_order_relaxed) ? "si" : "NO",
			(unsigned)maskRejected);
		Console.WriteLn("vuSB: intentos=%" PRIu64 " construidos=%" PRIu64 " uniones=%" PRIu64
			" ops=%" PRIu64 " rechazos{struct=%" PRIu64 " cold=%" PRIu64 " budget=%" PRIu64
			" ident=%" PRIu64 "}",
			s.attempts.load(std::memory_order_relaxed), s.built.load(std::memory_order_relaxed),
			s.junctions.load(std::memory_order_relaxed), s.fused_ops.load(std::memory_order_relaxed),
			s.rejected_struct.load(std::memory_order_relaxed),
			s.rejected_cold.load(std::memory_order_relaxed),
			s.rejected_budget.load(std::memory_order_relaxed), s.rejected_identity.load(std::memory_order_relaxed));
		Console.WriteLn("vuSB: replay normal=%" PRIu64 " sb=%" PRIu64 " matched=%" PRIu64
			" diverged=%" PRIu64 " promoted=%" PRIu64 " unproven=%" PRIu64 " killed=%" PRIu64
			" ventanas=%" PRIu64 " spot=%" PRIu64 " sinGanancia=%" PRIu64,
			s.verify_normal.load(std::memory_order_relaxed),
			s.verify_sb.load(std::memory_order_relaxed),
			s.matched.load(std::memory_order_relaxed), s.diverged.load(std::memory_order_relaxed),
			s.promoted.load(std::memory_order_relaxed), s.unproven.load(std::memory_order_relaxed),
			s.killed.load(std::memory_order_relaxed), s.epochs.load(std::memory_order_relaxed),
			s.spot_checks.load(std::memory_order_relaxed), s.unprofitable.load(std::memory_order_relaxed));
	}
}
