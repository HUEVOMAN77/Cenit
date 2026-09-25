// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

// Cenit VU Superblock Engine — Fase 1 (sonda de trazas: medir antes de tocar).
//
// El plan del motor de superbloques exige, como Fase 1 y SIN optimización,
// exactamente tres cosas:
//   1. contador de entradas al dispatcher (por VU, desglosado en flujo
//      rápido mVUlookupProg / lento mVUsearchProg / entradas JR-JALR),
//   2. contador de ejecuciones de bloques VU1 — sonda de 3 instrucciones
//      (ldr/add/str) emitida por el propio JIT a la entrada de cada bloque,
//      indexada por PC de entrada en un array dentro del `microVU`,
//   3. identificación de secuencias repetidas — tabla de transiciones
//      (programa_prev,PC_prev) -> (programa_cur,PC_cur) alimentada desde
//      los puntos de resolución del dispatcher.
//
// Apagada (el default), NO cambia nada: ni el código emitido, ni el options
// sentinel, ni los hashes de programa, ni el disco. Encendida, solo añade
// incrementos — no fusiona bloques, no mueve ciclos, no altera el tiempo
// emulado. Medición pura.
//
// Isolación con la caché de programas en disco: los bloques compileados con
// la sonda ON llevan la instrumentación pegada, así que mientras está ON se
// corta la grabación de persistencia (mVUPersist) y se bloquean Init/Save/
// Hydrate del disco (guards en microVU_ProgCache-arm64.inl). El options
// sentinel NO se re-clave a propósito: la sonda cambia el código HOST, no el
// microcódigo que el contentHash identifica; re-clave habría vaciado la
// caché en disco de los usuarios por el solo hecho de medir. La inyección
// usa exclusivamente [x24, #inm] (el pin macFlag del dispatcher), sin
// materializar ninguna dirección absoluta, así que el recorder de
// persistencia no ve nada nuevo que clasificar.
//
// Threading: el estado por-VU lo escribe UN solo hilo (VU0: hilo EE; VU1:
// hilo MTVU con THREAD_VU1 — el dispatcher de VU1 corre íntegro allí). Los
// lectores (HUD en el hilo GS, volcado de informe) usan relaxed y aceptan
// desgarros: es telemetría, nunca entrada de simulación.
//
// El header es puro C++ (sin vixl, sin tipos de microVU): lo incluyen tanto
// microVU-arm64.cpp (dueño del layout; implementa SlotArray y los cortes de
// contadores) como la capa de UI/informe (ImGuiOverlays, native-lib).

#include "common/Pcsx2Defs.h"

#include <atomic>

namespace mVUTraceProbe
{
	// ------------------------------------------------------------------
	// Layout del array de contadores de bloques dentro de `microVU`.
	//
	// El pin x24 del dispatcher es &mVU.macFlag[0]. LDR/STR de 64 bits usan
	// el modo de offset inmediato ESCALADO (imm12 * 8), que alcanza
	// [pin, pin + 4095*8] = pin + 32760 — el mismo modo que ya emplea en
	// produccion el bloque de flags (neonBackup llega a +544 via
	// MemOperand(gprMVUFlag, 48 + neonReg*16), microVU_Misc-arm64.h). El
	// bloque de flags termina en +560 (statFlag[-16..0), macFlag[0..16),
	// clipFlag[16..32), neonCTemp[32..48), neonBackup[48..560)), asi que los
	// slots arrancan en +560 y 2048 u64 terminan en 560+16384 = 16944 <= 32760
	// y son todos multiplos de 8 (560 = 70*8), lo que el modo escalado exige.
	//
	// 2048 slots = exactamente los PCs de entrada posibles de VU1 (0x4000
	// bytes / 8). Índice = (pc >> 3) & 2047: CERO aliasing, offset resuelto
	// en tiempo de compilación (inm constante en el código emitido).
	// Coste por ejecución de bloque: 3 instrucciones
	//   ldr x8,[x24,#off] / add x8,x8,#1 / str x8,[x24,#off]
	// x8 = RXSCRATCH, fuera del pool del allocator (microVU_IR-arm64.h), y
	// la sonda se emite ANTES de mVUtestCycles, que re-materializa x8 para
	// lo suyo. Pinned contra offsetof reales por static_asserts en
	// microVU-arm64.cpp.
	// ------------------------------------------------------------------
	static constexpr u32 kProbeSlotBaseOff = 560;
	static constexpr u32 kProbeSlots = 2048;
	static constexpr u32 kProbeSlotMask = kProbeSlots - 1;
	static_assert(kProbeSlotBaseOff + kProbeSlots * 8 <= 4095u * 8u,
		"probe slot array out of [x24, #imm] scaled-x64 reach (imm12*8)");

	// Offset (desde el pin x24) del slot del PC de entrada, en bytes.
	__fi u32 SlotMemOffset(u32 startPC_bytes)
	{
		return kProbeSlotBaseOff + ((startPC_bytes >> 3) & kProbeSlotMask) * 8;
	}

	// Acceso al array de slots de cada VU (vive dentro del microVU; solo
	// microVU-arm64.cpp ve el struct — este getter se define allí).
	const u64* SlotArray(int vu);

	// ------------------------------------------------------------------
	// Activación — espejo del config bool
	// EmuCore/CPU/Recompiler/EnableVUTraceProbe.
	//
	// SyncFromConfig() se llama desde mVUinit/mVUreset ANTES de
	// mVUPersist::SyncRecordingFromConfig: el reset del recompiler es el
	// único punto que ve la cascada completa de settings (perfil de
	// hardware -> INI global -> capa por-juego) y el único momento seguro
	// para roturar el code cache. CORTE DE FLANCO (idempotente: mVUreset
	// corre por VU0 y por VU1 con la misma entrada):
	//   OFF->ON:  limpia TODO (tablas, flujo y los dos arrays de slots que
	//             el llamador pasa por puntero — el probe no ve microVU).
	//   ON->OFF:  vuelca el informe (los contadores quedan legibles hasta
	//             el próximo encendido; el HUD etiqueta "sonda apagada").
	// ------------------------------------------------------------------
	extern std::atomic<bool> g_enabled;
	__fi bool IsEnabled() { return g_enabled.load(std::memory_order_relaxed); }
	void SyncFromConfig(bool enabled, u64* slots0, u64* slots1);

	// ------------------------------------------------------------------
	// Contadores de flujo — los incrementa microVU-arm64.cpp en los
	// puntos C++ de resolución del dispatcher; nunca desde código emitido.
	//
	// Ojo con la cobertura real, porque determina cómo se lee el informe:
	// el dispatcher JIT tiene DOS salidas C++ y una ruta que NUNCA sale a
	// C++. Cada despacho intenta primero el stub emitido
	// mVUlookupProg_VUx (BL desde código generado): si resuelve, ejecuta el
	// bloque sin volver a C++ — eso solo lo ve el contador de bloques (los
	// slots por PC que incrementa la sonda JIT). Si falla, BL a
	// mVUexecuteVUx, que reintenta en C++ (stubHits) y si todavía falla cae
	// a mVUsearchProg (slowMiss). Las entradas dinámicas JR/JALR tienen su
	// propio camino (mVUcompileJIT). Por eso "entradas al dispatcher" se
	// desglosan en los tres puntos observables y la cifra de despachos
	// resueltos 100% dentro del JIT se INFERIR por diferencia con las
	// ejecuciones de bloque, no se cuenta.
	// ------------------------------------------------------------------
	struct VuFlow
	{
		std::atomic<u64> stubCalls{0};     // BLs del dispatcher al stub rapido emitido (cada despacho JIT pasa por aqui)
		std::atomic<u64> stubHits{0};      // de esos, resueltos alli mismo (sin recompilar ni abrir code cache)
		std::atomic<u64> fastHits{0};      // despachos ya dentro de mVUexecute que resolvio mVUlookupProg (sin abrir code cache)
		std::atomic<u64> slowMiss{0};      // despachos que cayeron a mVUsearchProg
		std::atomic<u64> compiles{0};      // programas creados from-scratch (mVUcreateProg)
		std::atomic<u64> jumpEntries{0};   // entradas dinamicas JR/JALR (mVUcompileJIT)
		std::atomic<u64> jumpCacheHits{0}; // de esas, resueltas por jumpCache sin re-search
		std::atomic<u64> microWrites{0};   // mVUclear (escrituras a la imagen micro -> invalidaciones)
	};
	extern VuFlow g_flow[2];

	// ------------------------------------------------------------------
	// Secuencias repetidas: transiciones entre despachos consecutivos del
	// MISMO VU. Clave abierta de 16384 entradas sobre una firma de 64 bits.
	// Riesgo documentado del prototipo: una colisión de 64 bits fusiona dos
	// secuencias en el conteo; odds ~n²/2⁶⁴ con n <= decenas de miles —
	// despreciable para rankear candidatas a fusión en Fase 2.
	// ------------------------------------------------------------------
	static constexpr u32 kTransitionCap = 16384;
	static constexpr u32 kTransitionMask = kTransitionCap - 1;
	static constexpr u32 kProgramCap = 512;
	static constexpr u32 kProgramMask = kProgramCap - 1;

	struct TransitionEntry
	{
		u64  key;   // firma mixta (prevHash, curHash, prevPC, curPC); 0 = vacía
		u32  pcs;   // (prevPC << 16) | curPC — PCs en bytes de microMem
		u32  count; // 0 = vacía (tras inserción siempre > 0)
	};

	struct ProgramEntry
	{
		u64  hashLo; // half bajo del contentHash; 0 = vacía
		u32  execs;  // despachos resueltos a este programa
		u32  lastPC; // último PC de entrada observado
	};

	extern TransitionEntry g_transitions[2][kTransitionCap];
	extern ProgramEntry g_programs[2][kProgramCap];
	extern std::atomic<u32> g_usedTransitions[2];
	extern std::atomic<u32> g_usedPrograms[2];
	extern std::atomic<u64> g_dropped[2]; // inserciones perdidas por saturación

	// Observa un despacho resuelto (programa hashLo entrado por
	// startPC_bytes). Actualiza la tabla de programas y la transición desde
	// el despacho anterior del mismo VU. Llamado UNA vez por despacho,
	// desde el hilo dispatcher de ese VU, solo con la sonda ON.
	void ObserveDispatch(int vu, u32 startPC_bytes, u64 hashLo);

	// Suma del array de slots de un VU (0 si sonda nunca activada para ese
	// VU). O(2048) — llamar desde el HUD/informe, no desde el emulador.
	u64 BlockExecTotal(int vu);

	// ------------------------------------------------------------------
	// Informe: ranking de bloques calientes (top slots), top transiciones,
	// contadores de flujo y metadatos. Se imprime un resumen por
	// Console.WriteLn (viaja en emulog.txt — es lo que llega con el botón
	// "Enviar registro") y el detalle completo se escribe a
	// logs/vu_probe.txt ("wb"). Llamable desde cualquier hilo; se invoca
	// en la transición ON->OFF, en mVUclose con sonda ON, y vía JNI a
	// petición del usuario.
	// ------------------------------------------------------------------
	void DumpReport(const char* reason);
}
