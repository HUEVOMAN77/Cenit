// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// Cenit VU Superblock Engine — tabla de candidatos, política de elegibilidad
// y replay diferencial por parejas de despacho. Ver MvuSuperblock-arm64.h para
// el diseño completo y las garantías que este archivo hace cumplir.
//
// Este TU NO incluye microVU-arm64.h: no necesita ver el struct (el digest de
// salida le llega por g_exitDigest, registrada por microVU-arm64.cpp en
// mVUinit), y así se mantiene libre de la superficie de vixl, igual que el
// cuerpo de la sonda.

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
	// Estados del candidato.
	//   FREE      - hueco disponible
	//   BUILDING  - SbArmCompile lo reservó; la compilación variante está
	//               en vuelo en este mismo hilo (no puede ser despachado)
	//   VERIFY    - superbloque emitido y adoptado; ventana de replay
	//               diferencial abierta (alternación normal/SB)
	//   TRUSTED   - kSbVerifyPairs MATCH: el despacho toma la variante con
	//               solo memcmp de entrada, cero hashing
	// DEAD/UNPROVEN no son estados persistentes en la tabla (el hueco se
	// libera para que otro PC lo use); lo permanente es el bit de PC quemado
	// en s_burned[], que prohíbe re-armar ese PC por resto de sesión. Sin él,
	// un candidato que nunca converge sería recompilado en bucle.
	// ------------------------------------------------------------------
	enum class SbState : u32
	{
		Free,
		Building,
		Verify,
		Trusted,
	};

	namespace
	{
		static constexpr u32 kKeyBytes = 96; // microRegInfo

		struct Candidate
		{
			SbState state = SbState::Free;
			u32 startPC = 0; // bytes de microMem
			alignas(16) u8 key[kKeyBytes]; // lpState de construcción, tal cual

			void* entry = nullptr; // entrada host del superbloque
			u32 junctions = 0;
			u32 regionOps = 0;
			u32 regionCycles = 0;

			// --- ventana de validación ---
			u32 window = 0;      // despachos emparejables consumidos (ambas rutas)
			u32 pairs = 0;       // MATCH acumulados
			bool parity = false; // true => el próximo despacho emparejable va al SB

			// Episodios en vuelo. SbResolve pone *_Pending justo antes de que
			// la ruta elegida corra; SbFinish (fin del episodio) recoge el
			// digest, limpia *_Pending y pone *_Ready. ClosePair solo actúa
			// cuando ambos lados están Ready. Con *_Pending en vez de un
			// "hasN" permanente, un despacho que entra al PC pero NO fue
			// emparejado por SbResolve (clave distinta, presupuesto corto) no
			// puede confundirse con el episodio de la otra ruta.
			bool nPending = false;
			bool sPending = false;
			bool nReady = false;
			bool sReady = false;
			u64 pendN = 0; // digest del episodio normal cerrado
			u64 pendS = 0; // digest del episodio SB cerrado
		};

		Candidate s_tab[kSbMaxVariants];
		u32 s_live = 0; // candidatos no-Free (incluye BUILDING)

		// PCs quemados (DEAD/UNPROVEN): un bit por PC de entrada posible de VU1
		// (0x4000/8 = 2048, mismo índice que los slots de la sonda).
		u64 s_burned[(mVUTraceProbe::kProbeSlots + 63) / 64] = {};
		u32 s_burnedCount = 0;

		// Total de divergencias vistas (el contador g_stats es telemetría;
		// este decide el auto-apagado y necesita leerse sin atomics).
		u32 s_divergences = 0;
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
				if (s_tab[i].state != SbState::Free && s_tab[i].startPC == startPC_bytes)
					return &s_tab[i];
			}
			return nullptr;
		}

		Candidate* AllocSlot()
		{
			if (s_live >= kSbMaxVariants)
				return nullptr;
			for (u32 i = 0; i < kSbMaxVariants; i++)
			{
				if (s_tab[i].state == SbState::Free)
				{
					std::memset(&s_tab[i], 0, sizeof(Candidate));
					s_tab[i].state = SbState::Building;
					s_live++;
					return &s_tab[i];
				}
			}
			return nullptr;
		}

		void Release(Candidate& c)
		{
			if (c.state != SbState::Free)
			{
				c.state = SbState::Free;
				if (s_live)
					s_live--;
			}
			c.entry = nullptr;
			c.nPending = c.sPending = false;
			c.nReady = c.sReady = false;
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
			using std::memory_order_relaxed;
			SbPrintf(f, "%s attempts=%" PRIu64 " built=%" PRIu64 " junctions=%" PRIu64
				" fused_ops=%" PRIu64 "\n",
				tag, s.attempts.load(relaxed), s.built.load(relaxed),
				s.junctions.load(relaxed), s.fused_ops.load(relaxed));
			SbPrintf(f, "%s rejected struct=%" PRIu64 " cold=%" PRIu64 " budget=%" PRIu64
				" identity=%" PRIu64 "\n",
				tag, s.rejected_struct.load(relaxed), s.rejected_cold.load(relaxed),
				s.rejected_budget.load(relaxed), s.rejected_identity.load(relaxed));
			SbPrintf(f, "%s replay normal=%" PRIu64 " sb=%" PRIu64 " unmatched=%" PRIu64
				" matched=%" PRIu64 " diverged=%" PRIu64 "\n",
				tag, s.verify_normal.load(relaxed), s.verify_sb.load(relaxed),
				s.unmatched.load(relaxed), s.matched.load(relaxed), s.diverged.load(relaxed));
			SbPrintf(f, "%s outcome promoted=%" PRIu64 " unproven=%" PRIu64 " killed=%" PRIu64 "\n",
				tag, s.promoted.load(relaxed), s.unproven.load(relaxed), s.killed.load(relaxed));
		}

		// ------------------------------------------------------------------
		// Cierre de pareja y agotamiento de ventana (llamadas solo desde
		// SbFinish, hilo dispatcher de VU1 — el único escritor de la tabla).
		// ------------------------------------------------------------------
		void ClosePair(Candidate& c, u32 idx)
		{
			if (!(c.nReady && c.sReady))
				return;

			const u64 a = c.pendN;
			const u64 b = c.pendS;
			c.nReady = c.sReady = false;
			Stats& st = g_stats[1];

			if (a != b)
			{
				st.diverged.fetch_add(1, std::memory_order_relaxed);
				s_divergences++;
				Console.Error("Cenit superblock: DIVERGENCE en PC=%04x (normal=%016" PRIx64
					" sb=%016" PRIx64 " spans=%u ops=%u) -> variante invalidada.",
					c.startPC, a, b, c.junctions + 1u, c.regionOps);
				Release(c);
				SetBurned(idx);

				if (s_divergences >= kSbKills && !s_killed)
				{
					// Auto-apagado POR PROCESO: SyncFromConfig ya no vuelve a
					// encender. El g_enabled se pone a false ANTES del vuelco
					// para que este informe sea el final y trunque el archivo
					// (misma convención de rotación que la sonda).
					s_killed = true;
					st.killed.fetch_add(1, std::memory_order_relaxed);
					for (u32 i = 0; i < kSbMaxVariants; i++)
						Release(s_tab[i]);
					s_live = 0;
					g_enabled.store(false, std::memory_order_relaxed);
					Console.Error("Cenit superblock: %u divergencias -> motor AUTO-APAGADO "
						"por sesión (config no lo vuelve a encender).", s_divergences);
					DumpReport("auto-apagado por divergencias");
				}
				return;
			}

			st.matched.fetch_add(1, std::memory_order_relaxed);
			c.pairs++;
			if (c.pairs >= kSbVerifyPairs)
			{
				c.state = SbState::Trusted;
				st.promoted.fetch_add(1, std::memory_order_relaxed);
				DevCon.WriteLn(Color_Yellow, "Cenit superblock: PC=%04x validado (%u/%u pares) "
					"-> confiable.", c.startPC, c.pairs, kSbVerifyPairs);
			}
		}

		void WindowExhausted(Candidate& c, u32 idx)
		{
			Stats& st = g_stats[1];
			const u32 pairsSeen = c.pairs;
			const u32 pc = c.startPC;
			Release(c);
			SetBurned(idx);
			st.unproven.fetch_add(1, std::memory_order_relaxed);
			DevCon.WriteLn(Color_Gray, "Cenit superblock: PC=%04x sin evidencia en %u despachos "
				"(%u pares) -> se queda con la cadena normal.",
				pc, kSbVerifyDispatches, pairsSeen);
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
			for (u32 i = 0; i < kSbMaxVariants; i++)
				std::memset(&s_tab[i], 0, sizeof(Candidate));
			s_live = 0;
			std::memset(s_burned, 0, sizeof(s_burned));
			s_burnedCount = 0;
			s_divergences = 0;
			for (int vu = 0; vu < 2; vu++)
			{
				Stats& s = g_stats[vu];
				s.attempts.store(0); s.built.store(0); s.junctions.store(0); s.fused_ops.store(0);
				s.rejected_struct.store(0); s.rejected_cold.store(0);
				s.rejected_budget.store(0); s.rejected_identity.store(0);
				s.verify_normal.store(0); s.verify_sb.store(0); s.unmatched.store(0);
				s.matched.store(0); s.diverged.store(0); s.promoted.store(0);
				s.unproven.store(0); s.killed.store(0);
			}
			g_enabled.store(true, std::memory_order_relaxed);
			Console.WriteLn(Color_StrongGreen, "vuSB: motor de superbloques ACTIVADO — validate-only "
				"(%u uniones/%u ops/%u ciclos por superbloque, %u pares de replay en ventana de %u). "
				"El informe sale al apagar el motor, al parar el juego o a petición.",
				kSbMaxJunctions, kSbMaxOps, kSbMaxCycles, kSbVerifyPairs, kSbVerifyDispatches);
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
				Release(s_tab[i]);
			s_live = 0;
		}
	}

	bool WasKilled()
	{
		return s_killed;
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
		return static_cast<int>(c - s_tab);
	}

	int SbAskJunction(int slot, u32 targetPC_bytes, bool branchStructClean,
		u32 regionOps, u32 regionCycles)
	{
		if (slot < 0 || slot >= static_cast<int>(kSbMaxVariants))
			return -1;

		Candidate& c = s_tab[slot];
		if (c.state != SbState::Building)
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
		if (c.state != SbState::Building)
			return;

		Stats& st = g_stats[1];

		// Barrido de los bits de rasguino en la clave de pareo (obligatorio en
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
		if (areaBad || junctions == 0 || !hostEntry || nOps == 0 ||
			nOps > kSbMaxOps || nCycles > kSbMaxCycles)
		{
			if (areaBad)
				st.rejected_struct.fetch_add(1, std::memory_order_relaxed);
			else
				st.rejected_budget.fetch_add(1, std::memory_order_relaxed);
			// Quema el PC: este rechazo es DETERMINISTA (la forma del area es
			// la que es — mismo PC, mismo microcodigo, mismo veredicto). Sin
			// quemar, cada despacho al PC re-armaria una compilacion variante
			// que vuelve a rechazar: compilaciones y enlaces muertos de forma
			// indefinida hasta agotar el code cache.
			Release(c);
			SetBurned(PcIdx(c.startPC));
			return;
		}

		c.entry = hostEntry;
		c.junctions = junctions;
		c.regionOps = nOps;
		c.regionCycles = nCycles;
		c.window = 0;
		c.pairs = 0;
		c.parity = false;
		c.state = SbState::Verify;
		st.built.fetch_add(1, std::memory_order_relaxed);
		st.fused_ops.fetch_add(nOps, std::memory_order_relaxed);

		DevCon.WriteLn(Color_Yellow, "Cenit superblock: built PC=%04x spans=%u ops=%u cycles=%u "
			"(validate-only, ventana %u despachos / %u pares).",
			startPC_bytes, junctions + 1u, nOps, nCycles, kSbVerifyDispatches, kSbVerifyPairs);
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
			if (c.state != SbState::Verify && c.state != SbState::Trusted)
				continue;
			if (c.startPC != startPC_bytes)
				continue;

			// Identidad: la variante solo vale para la clave de registers con
			// la que se construyó. Cualquier otra entrada es cadena normal.
			if (std::memcmp(c.key, pState96, kKeyBytes) != 0)
				continue;

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

			if (c.state == SbState::Trusted)
				return c.entry;

			// VERIFY: alternación estricta para que los episodios lleguen
			// emparejados por construcción.
			Stats& st = g_stats[1];
			c.window++;
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

	void SbFinish(u32 startPC_bytes, u32 consumedCycles)
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
			if (t.state == SbState::Verify && t.startPC == startPC_bytes)
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
			return;
		}

		if (!g_exitDigest)
			return;
		const u64 digest = g_exitDigest(consumedCycles);

		if (c->sPending)
		{
			c->sPending = false;
			c->pendS = digest;
			c->sReady = true;
		}
		else
		{
			c->nPending = false;
			c->pendN = digest;
			c->nReady = true;
		}

		ClosePair(*c, idx);

		// Ventana agotada sin los pares exigidos: "unproven" (Release + PC
		// quemado). Solo cuando no queda ningún episodio en vuelo — si lo
		// hubiera, se cierra primero y esta misma ruta lo juzga en su Finish.
		if (c->state == SbState::Verify && c->window >= kSbVerifyDispatches &&
			!c->nPending && !c->sPending)
		{
			WindowExhausted(*c, idx);
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
			if (s_tab[i].state == SbState::Free)
				continue;
			// Toda escritura a la imagen micro tira los programas afectados;
			// un superbloque construido sobre microcódigo que cambió no puede
			// quedar vivo ni una instrucción (regla del documento: cero trazas
			// vivas sobre código modificado). Los PCs quemados siguen quemados:
			// invalidar por escritura no borra una divergencia medida.
			Release(s_tab[i]);
		}
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

		u32 nVerify = 0, nTrusted = 0, nBuilding = 0;
		for (u32 i = 0; i < kSbMaxVariants; i++)
		{
			switch (s_tab[i].state)
			{
			case SbState::Building: nBuilding++; break;
			case SbState::Verify:   nVerify++;   break;
			case SbState::Trusted:  nTrusted++;  break;
			default: break;
			}
		}

		// Rotación igual que la sonda: los vuelcos INTERMEDIOS (motor aún ON)
		// se añaden; el vuelco FINAL (g_enabled ya en false) truncará para
		// que el informe completo esté arriba del todo.
		const std::string path = Path::Combine(EmuFolders::Logs, "vu_superblock.txt");
		std::FILE* fp = FileSystem::OpenCFile(path.c_str(), IsEnabled() ? "ab" : "wb");
		// Macro, no lambda: conserva el attribute format(printf) de SbPrintf.
#define P(...) SbPrintf(fp, __VA_ARGS__)

		P("Cenit VU Superblock Engine — informe Fases 2-5\n");
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
		P("validacion: %u pares MATCH en ventana de %u despachos; %u divergencias -> OFF por sesion\n",
			kSbVerifyPairs, kSbVerifyDispatches, kSbKills);
		P("\n");
		DumpStatsLine(fp, "[VU1]", g_stats[1]);
		P("\n");
		P("tabla: %u vivos (building=%u verify=%u trusted=%u)  PCs quemados=%u  "
			"divergencias=%u\n",
			s_live, nBuilding, nVerify, nTrusted, s_burnedCount, s_divergences);
		P("\n");
		P("PC        estado    uniones  ops  ciclos  ventana  pares  entrada\n");
		for (u32 i = 0; i < kSbMaxVariants; i++)
		{
			const Candidate& c = s_tab[i];
			if (c.state == SbState::Free)
				continue;
			const char* s = (c.state == SbState::Building) ? "building" :
			                (c.state == SbState::Verify)   ? "verify"   : "trusted";
			P("%08x  %-9s %7u %5u %6u %8u %6u  %p\n",
				c.startPC, s, c.junctions, c.regionOps, c.regionCycles,
				c.window, c.pairs, c.entry);
		}
		P("\n");
		P("GATE: el documento exige cero divergencias Y mejora sostenida medida\n");
		P("en el dispositivo antes de considerar esta ruta lista. Con 0 pares o\n");
		P("candidatos unproven no hay evidencia de nada: es un no, no un casi.\n");
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
		Console.WriteLn("vuSB: [%s] vivos=%u building=%u verify=%u trusted=%u quemados=%u "
			"divergencias=%u auto-off=%u",
			reason ? reason : "?", s_live, nBuilding, nVerify, nTrusted, s_burnedCount,
			s_divergences, s_killed ? 1u : 0u);
		Console.WriteLn("vuSB: intentos=%" PRIu64 " construidos=%" PRIu64 " uniones=%" PRIu64
			" ops=%" PRIu64 " rechazos{struct=%" PRIu64 " cold=%" PRIu64 " budget=%" PRIu64
			" ident=%" PRIu64 "}",
			s.attempts.load(std::memory_order_relaxed), s.built.load(std::memory_order_relaxed),
			s.junctions.load(std::memory_order_relaxed), s.fused_ops.load(std::memory_order_relaxed),
			s.rejected_struct.load(std::memory_order_relaxed),
			s.rejected_cold.load(std::memory_order_relaxed),
			s.rejected_budget.load(std::memory_order_relaxed),
			s.rejected_identity.load(std::memory_order_relaxed));
		Console.WriteLn("vuSB: replay normal=%" PRIu64 " sb=%" PRIu64 " matched=%" PRIu64
			" diverged=%" PRIu64 " promoted=%" PRIu64 " unproven=%" PRIu64 " killed=%" PRIu64,
			s.verify_normal.load(std::memory_order_relaxed),
			s.verify_sb.load(std::memory_order_relaxed),
			s.matched.load(std::memory_order_relaxed),
			s.diverged.load(std::memory_order_relaxed),
			s.promoted.load(std::memory_order_relaxed),
			s.unproven.load(std::memory_order_relaxed),
			s.killed.load(std::memory_order_relaxed));
	}
}
