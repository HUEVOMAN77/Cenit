// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

// Cenit VU Superblock Engine — Fases 2/3/4/5 del documento de arquitectura
// "VU Superblock Engine" (Profile → Record → Fuse → Replay → Enable).
//
// Qué hace: cuando la sonda Fase 1 demuestra que un bloque VU1 es caliente y
// cae ESTÁTICAMENTE e INCONDICIONALMENTE sobre otro (arista kind=1 de
// MvuTraceProbe), y la cadena normal ya compiló ambos, el motor arma UNA
// segunda compilación del PC de entrada en "modo variante": un superbloque
// que analiza y emite varios tramos como una sola unidad, eliminando la
// barrera de bloque (flushAll del allocator + intercambio de carriles P/Q +
// salto + búsqueda en el gestor) en cada unión. El presupuesto de ciclos NO
// se toca: la guarda de entrada que mVUtestCycles emite al principio del
// bloque compara el presupuesto contra los ciclos del área FUSA completa —
// si no alcanza, el despacho toma la cadena normal, que sí puede romper por
// presupuesto a mitad de camino. Por construcción un superbloque nunca se
// interrumpe a media fusión: o corre entero (con el presupuesto que la
// cadena normal habría consumido bloque a bloque, suma idéntica) o no corre.
//
// Identidad de la variante: bits de rasguño en el byte 6 de microRegInfo
// (blockType) SOLO en la clave con la que el gestor de bloques registra el
// superbloque. Ninguna otra copia del estado (mVUregs, lpState, pStateEnd)
// lleva nunca los bits, así que ningún despacho normal puede tocar la
// variante: las búsquedas del gestor comparan quick64[0] completo y el
// dispatch C++ entra con lpState limpio. El control de qué código corre
// vive 100% en C++ (SbResolve).
//
// Reglas del documento que este módulo hace cumplir (no negociables):
//  - Elegibilidad (Fase 2): flujo estable medido (umbral de ejecuciones del
//    origen + hotness del destino + ratio A/B en la malla de aristas), solo
//    ramas estáticas incondicionales B/BAL sin M/T/D/E-bit ni kick en la
//    rama ni en el delay slot, sin bad/evil branch, unión con el bloque
//    destino ya registrado y de lectura exacta de banderas limpia
//    (needExactMatch==0 y blockType sin bit E — la misma política que el
//    gestor usa para enlazar), sin envolturas, techos de uniones/
//    instrucciones/ciclos. Cualquier kick (isKick/doXGKICK) dentro del área
//    fusionada repele el superbloque entero: sus efectos en VIF1 son
//    observables por el host y esta fase no necesita asumir ese riesgo.
//    Las escrituras observables (memoria, banderas, Q/P, XGKICK) conservan
//    su emisión por instrucción intacta: la fusión solo elimina la barrera.
//  - Validación (Replay diferencial por parejas de despacho): ventana por
//    candidato — los despachos a la MISMA entrada y con el MISMO estado de
//    entrada (memcmp byte a byte del lpState de construcción) alternan
//    cadena-normal y superbloque. Al cerrar cada episodio (recMicroVU1::
//    Execute, tras volver startFunct) se hashkea el ESTADO DE SALIDA
//    completo (VI, VF, ACC, banderas macro/clip/status, TPC, cycle, flags,
//    pendings Q/P, anillos fmac/ialu/fdiv/efu, nextBlockCycles, respaldo
//    VI, contadores XGKICK y memoria de datos VU1). Dos episodios con
//    entrada idéntica (== la del candidato) forman un par: MATCH cuenta,
//    DIVERGENCE invalida la variante (fallback permanente), la reporta y,
//    a las kSbKills divergencias, apaga el motor por sesión. Sin kSbVerify-
//    Pares MATCH la ventana se agota -> candidato "unproven": se libera el
//    superbloque y el PC vuelve a la cadena normal para siempre. Con
//    TRUSTED el despacho usa la variante con solo un memcmp de entrada —
//    cero hashing en régimen estable. El interpretador de VU1 NO puede ser
//    oráculo: MTVU mantiene su imagen de registers desincronizada a
//    propósito; el par normal/SB con entrada idéntica sí mide exactamente
//    la equivalencia que se afirma.
//  - Regla de oro: nunca comprar FPS a cambio de ocultar un evento PS2.
//    E-bit, M-bit, T/D-bit, JR/JALR y salidas por presupuesto se conservan
//    (las salidas por presupuesto las absorbe siempre la cadena normal).
//  - Identidad persistente (Fase 3): con el motor ON la caché de programas
//    en disco queda EN PAUSA (ningún superbloque pisa el disco) y el
//    options sentinel incorpora un byte con el tier del dispositivo
//    (0 == motor OFF: sentinel bit-idéntico al de siempre; nadie pierde su
//    caché por existir esta función). kMvuCompilerAbiVersion sube a 8:
//    invalidate ante cambio de versión, como pide el documento.
//  - Fase 4 (host scheduling): prioridad del hilo MTVU (nice -2) mientras
//    el motor está ON. Cero alteración del timing emulado; WaitVU,
//    EECycleSkip, mVUcleanUp y mVUendProgram no se tocan.
//  - GATE: apagado por defecto. Activar solo con cero divergencias y mejora
//    sostenida medida en el dispositivo.
//
// Motor encendido implica sonda encendida: la elegibilidad vive de los
// contadores de la Fase 1. En mVUinit/mVUreset: probe :=
// (EnableVUTraceProbe || EnableVUSuperblock), grabación en disco forzada a
// OFF con cualquiera de los dos activa.
//
// Threading: todo el estado (tabla de candidatos, digests de ventana,
// contadores) lo escribe UN solo hilo — el dispatcher de VU1 (MTVU con
// THREAD_VU1); los flancos de SyncFromConfig corren en el hilo de settings
// con el VM ya parado o el cache recién vaciado. Los lectores (HUD,
// informe) usan relaxed: telemetría, nunca entrada de simulación. Header
// puro C++ (sin vixl, sin tipos de microVU).

#include "MvuSbLife.h"

#include "common/Pcsx2Defs.h"

#include <atomic>

namespace mVUSuperblock
{
	// ------------------------------------------------------------------
	// Activación — espejo del config bool
	// EmuCore/CPU/Recompiler/EnableVUSuperblock (apagado por defecto).
	// Llamado desde mVUinit/mVUreset junto con el de la sonda. Corte de
	// flanco: OFF->ON limpia la tabla y los contadores (el CheckForCPUConfig
	// Changes del toggle ya tiró el code cache, así que ninguna variante
	// fusionada sobrevive); ON->OFF vuelca el informe. El auto-apagado por
	// divergencias es POR PROCESO: SyncFromConfig no vuelve a encender.
	// ------------------------------------------------------------------
	extern std::atomic<bool> g_enabled;
	__forceinline_odr bool IsEnabled() { return g_enabled.load(std::memory_order_relaxed); }
	void SyncFromConfig(bool enabled);
	// Verdad si el motor se auto-apagó por divergencias (para el HUD/informe).
	bool WasKilled();

	// ------------------------------------------------------------------
	// ------------------------------------------------------------------
	// Cenit 0.6.32 (Fase 2 del prompt directo) — Observadores que el TU de
	// microVU REGISTRA en mVUinit. El motor no puede ver VURegs ni el ABI del
	// compilador desde este TU (vive libre de vixl a proposito), y la maquina de
	// estados necesita ambos como DATOS: la identidad del candidato
	// (SbAreaDigestFn + engineAbi) y la MASCARA de comparacion (exitMask), que
	// es lo unico que autoriza a contar un par. Si nadie registra la mascara, la
	// maquina RECHAZA los pares y el informe lo dice: una equivalencia afirmada
	// sobre un estado que no se miro entero no es evidencia.
	// ------------------------------------------------------------------
	// El area fusionada NO es contigua (cada tramo esta donde estaba su bloque),
	// asi que el digest se pide por LISTA de tramos: pcs[i] = PC de entrada del
	// tramo, ops[i] = instrucciones que cobra ese tramo segun la sonda. Quien ve
	// el microcodigo (el TU de microVU) decide como se hashkea, y el resultado es
	// parte de la IDENTIDAD: dos areas con el mismo PC pero microcodigo distinto
	// no comparten evidencia.
	using SbAreaDigestFn = u32 (*)(const u32* pcs, const u32* ops, u32 count);
	extern SbAreaDigestFn g_areaDigest;	// nullptr hasta mVUinit
	extern std::atomic<u32> g_exitMask;	// Regions:: que cubre g_exitDigest
	extern std::atomic<u32> g_engineAbi; // kMvuCompilerAbiVersion

	// Superficie de snapshot/restore IMPLEMENTADA (encabezado de MvuSbLife.h,
	// punto 2): VURegs + 16 KB VU1.Mem + prog.lpState + microVU1.cycles +
	// VU0.VI[REG_VPU_STAT]. Vale false hasta que alguien la programe, y mientras
	// valga false NINGUN candidato entra en SHADOW aunque MTVU este apagado: cae
	// en VALIDATE_ONLY y el HUD lo dice. Es el estado honesto de esta version,
	// no un interruptor de favor.
	extern std::atomic<bool> g_rollbackSurface;

	// Identidad de juego+dispositivo: la publica Java por el mismo canal que el
	// HUD del sintonizador (native-lib la reenvia aqui), porque el FNV-1a de la
	// identidad lo construye Java y el C++ no lo recalcula. 0 = sin identidad.
	void SetProfileHash(u64 hash);
	u64 ProfileHash();

 prompt impone un solo dueño por intervalo: mientras el tuner CPU/VU mide,
	// el regidor se congela; la misma regla aplica al revés. Un superbloque en
	// VERIFY cambia CUANTAS VECES entra el recompilador y cómo se cobra el paso
	// (SbResolve alterna cadena normal / superbloque dentro de la misma ventana de
	// 8 samples). Dejar que el tuner ensaye MTVU o CycleRate encima de esa
	// alternación es medir dos cosas a la vez y no saber cuál ganó. Devuelve true
	// SOLO si hay candidatos en VERIFY; TRUSTED no cuenta (su camino ya no se
	// alterna: despacha directo a la variante) y un hueco libre tampoco.
	// s_live y SbState son internos al .cpp, así que la consulta vive allí.
	// ------------------------------------------------------------------
	bool Validating();

	// ------------------------------------------------------------------
	// Política — constantes de elegibilidad (Fase 2) y validación (Replay).
	// Ajustadas para que un falso positivo de hotness cueste una
	// compilación, no la corrección: la corrección la decide el replay.
	// ------------------------------------------------------------------
	static constexpr u32 kSbHotEntries = 200000;   // ejecuciones acumuladas del bloque origen
	static constexpr u32 kSbTargetMinEntries = 1000; // el destino también debe haber corrido solo
	static constexpr u32 kSbMinRatioPct = 40;      // execB*100/execA >= 40: flujo estable
	static constexpr u32 kSbMaxRatioPct = 400;     // y <= 400: B no lo domina otro predecesor
	static constexpr u32 kSbMaxJunctions = 5;      // uniones por superbloque (tramos-1)
	static constexpr u32 kSbMaxOps = 192;          // instrucciones analizadas fundidas
	static constexpr u32 kSbMaxCycles = 384;       // ciclos cobrados fundidos
	// kSbVerifyDispatches: TAMANO DE LA VENTANA del motor (256 despachos
	// emparejables = UNA ventana; cada ventana sube el id monotono que la maquina
	// exige para el «>1 ventana»). LAS LEYES DE PROMOCION YA NO VIVEN AQUI: viven
	// en mVUSbLife (kValidPairs=1000, kValidWindows=2, kBudgetEpisodes=60000,
	// kMinGainPairs=64, kTrustedGainPermille=30), el header puro que Harness9
	// prueba en host. Antes (0.6.21..0.6.31) la promocion era «8 pares y ya», sin
	// umbral de ventanas ni de muestra: el prompt lo prohibe, y por eso se fue.
	static constexpr u32 kSbVerifyDispatches = 256; // despachos emparejables por ventana
	// Breaker del proceso. Sin superficie de rollback el episodio divergente YA
	// corrio como autoritativo (VALIDATE alternatea), asi que el unico escudo es
	// parar: 3 divergencias y fuera, como en 0.6.21. Con superficie (SHADOW de
	// verdad, donde la divergencia se revierte antes de llegar al juego) el
	// castigo global exige ADEMAS que sean al menos 3 CANDIDATOS DISTINTOS: un
	// candidato malo no apaga a los sanos — lo que el prompt pide como «DEAD por
	// candidato, no quemado de proceso» —, pero un motor que diverge en todas
	// partes esta roto y eso si es del proceso.
		// Quemado por PC (no por proceso): evita RECOMPILAR en bucle lo que la
	// maquina ya rechazo o ya no puede financiar. Se quema (a) rechazo
	// determinista de forma, (b) DEAD por divergencia, (c) medido sin ganancia y
	// (d) presupuesto agotado — los dos ultimos con el matiz de que la maquina
	// NO los considera un "no" de equivalencia (eso vive en sus estados, y el
	// informe los lista aparte: dead vs archived). El bit vive por identidad de
	// code cache: SbKillAll y el flanco OFF->ON de SyncFromConfig lo limpian.
	static constexpr u32 kSbBreakNoSurface = 3;
	static constexpr u32 kSbBreakWithSurface = 8;
	static constexpr u32 kSbBreakCandidates = 3;
	static constexpr u32 kSbMaxVariants = 256;     // superbloques vivos a la vez

	// Bits de rasguño de identidad de variante — byte 6 de microRegInfo
	// (blockType; los bits 0-1 son los blockType originales 0/1/2). Viven
	// SOLO en la clave de registro del gestor.
	static constexpr u32 kSbScratchFused = 0x40;
	static constexpr u32 kSbScratchVariant = 0x80;
	static constexpr u32 kSbScratchMask = kSbScratchFused | kSbScratchVariant;

	// Offset de blockType dentro de los 96 bytes de microRegInfo (los 7 bytes
	// del primer union: needExactMatch, flagInfo, q, p, xgkick, viBackUp,
	// blockType). El TU del motor NO ve el struct (vive libre de vixl/microVU);
	// microVU-arm64.cpp pinea este offset con un static_assert sobre
	// offsetof(microRegInfo, blockType) — si alguien reordena el struct, deja
	// de compilar antes de que el barrido de clave (SbCloseCompile) apuntase al
	// byte equivocado.
	static constexpr u32 kSbKeyBlockTypeOff = 6;

	// kSbMaxOps/kSbMaxCycles son POLITICA de fusion (techos del area); el
	// area puede terminar naturalmente mas alla (un tramo largo sin union
	// elegible). La tabla de slots de region del paso 1 tiene que cubrir el
	// peor caso de un bloque normal: microMemSize/8 = 2048 pares.
	static constexpr u32 kSbRegionCap = 2048;

	// ------------------------------------------------------------------
	// API del compilador (TU microVU-arm64.cpp — .inl compilados en él).
	// Modelo verificado contra el codigo: la variante es una SEGUNDA
	// mVUcompile del PC de entrada, armada por el hook de mVUexecute<1>
	// (SbArmCompile copia la clave de construccion ANTES de la re-entrada).
	// Su primer paso CONTINUA el analisis a traves de las uniones B
	// incondicionales elegibles en lugar de romper (SbAskJunction en el
	// corte rama>=2 de la delay slot, con rama B/BAL limpia): el AREA
	// COMPLETA se analiza y se emite como UN solo bloque — cero flushAll,
	// cero mVUsetupFlags, cero rotacion P/Q, cero salto y cero contador de
	// sonda en cada union. Eso ES la fusion: la pipeline de banderas (mFC,
	// xS/xM/xC, instancias fisicas F0-F3/macFlag/clipFlag y el carril Q/P)
	// cruza la union SIN normalizar, y el analisis la modela de forma
	// continua (findFlagInst resuelve la instancia exacta por ciclo de
	// escritura — estrictamente mas preciso que la rotacion del enlace
	// normal). Los contadores macro_flag[]/status_flag[] escalan con xPC
	// como en un bloque largo: la emision por op es 100% info[]-dirigida.
	//
	// Unicas dos piezas nuevas de emision/identidad:
	//  - mVUsetFlags/mVUstatusFlagOp caminan el bloque por PC (incPC2 +/-2)
	//    y un area fusionada NO es contigua: el paso 1 registra en
	//    mVU.sbOpSlot[] el slot info[] de cada op analizado (orden de
	//    region) y, con mVU.sbSlot >= 0, las caminatas avanzan por esa
	//    tabla. Coste: un store por op, solo en modo variante.
	//  - Identidad: los bits de rasguño (kSbScratchMask) se hornean en
	//    mVUblock.pState.blockType ANTES del add() del gestor (solo con
	//    frame variante) y quedan limpiados por la linea :774 que ya pone
	//    blockType=0. El gestor COPIA el pState en su microBlockLink, asi
	//    que el superbloque vive en su PROPIO enlace (pStateEnd propio —
	//    vital: el codigo M-bit/JR del area escribe pStateEnd de
	//    mVUpBlock, y compartir el enlace del bloque normal lo corromperia
	//    cruzado). Las busquedas normales comparan quick64/96B COMPLETOS:
	//    la variante es inalcanzable por enlace normal sin tocar search;
	//    needExactMatch del registro se deriva del blockType LIMPIO
	//    (& ~kSbScratchMask) para no forzar igualdad exacta artificial. La
	//    resolucion del superbloque vive 100% en C++ (SbResolve).
	//
	// El area termina dentro del propio paso 1: en la primera union no
	// elegible o al alcanzar techos; el corte del tramo final conserva su
	// semantica normal intacta (normBranch enlaza al bloque destino como
	// siempre, M-bit/E-bit/EOB cierran igual). Un area NO puede terminar
	// en JR/JALR ni en rama con pending-kick: los enlaces por jumpCache
	// escriben pStateEnd y el kick pending cambia la clave del destino;
	// SbAskJunction lo prohíbe (branchStructClean/kickSeen del llamador).
	// ------------------------------------------------------------------

	// Arma la compilacion variante para este PC de despacho (llamado desde
	// mVUexecute<1> con la resolucion normal ya en curso, pState96 =
	// &mVU.prog.lpState ANTES de que la variante la pise). Verifica la
	// politica de elegibilidad (hotness A y B, arista kind=1, ratio, sin
	// candidato vivo por este PC, tabla con hueco), copia la clave a la
	// sombra del slot y devuelve el id (>=0) — el llamador lo deposita en
	// mVU.sbSlot y reentra mVUcompile — o -1 si no hay que compilar nada.
	int SbArmCompile(u32 startPC_bytes, const void* pState96);

	// Pide una unión durante el análisis (corte rama>=2 de la variante).
	// targetPC_bytes = branchAddr; branchStructClean = rama B/BAL sin
	// M/T/D/E/I-bits en rama ni delay, sin bad/evil, sin backupVI en el
	// delay, sin kick pendiente (xgkickcycles==0); regionOps/regionCycles
	// = mVUcount/mVUcycles actuales. Devuelve el indice de union
	// (<kSbMaxJunctions) o -1: el llamador mantiene el corte normal.
	// El llamador excluye ADEMAS BAL: el enlace normal escribe la PC de
	// retorno en vi31 con su propia semantica (backupVI/constprop), que la
	// continue-analysis no reproduce a mitad de region — fusiona solo B.
	int SbAskJunction(int slot, u32 targetPC_bytes, bool branchStructClean,
		u32 regionOps, u32 regionCycles);

	// Cierre de la compilación variante (perf_and_return): junctions>=1 y
	// area limpia -> adopcion interna (guarda hostEntry + metricas del area,
	// estado VERIFY); si no, libera el slot y QUEMA el PC. Antes de ambos
	// caminos barre los bits de rasguino del byte blockType del key de pareo
	// (el registro del gestor los conserva a proposito: son lo que hace la
	// variante inalcanzable por search normal — el GATE de validacion). areaBad: kick (isKick/doXGKICK)
	// en el area — sus efectos en VIF1 son observables por el host y la
	// fusion no debe moverlos. El T/D-bit NO entra aqui: dentro del area se
	// analiza y se emite op a op con su semantica normal intacta (mVUDoTBit/
	// mVUDoDBit, igual que en cualquier bloque de la cadena), y en el corte
	// terminal lo maneja normBranch con la semantica completa. nOps/nCycles:
	// totales del area fusionada (mVUcount/mVUcycles finales); por encima de
	// los techos de politica el candidato se descarta igual (el area crecio
	// tras la ultima union).
	void SbCloseCompile(int slot, u32 startPC_bytes, void* hostEntry,
		u32 junctions, bool areaBad, u32 nOps, u32 nCycles);

	// ------------------------------------------------------------------
	// API del dispatcher (C++ — todos los caminos de resolución pasan por
	// aquí: stub emitido -> mVUlookupProg_VU1, lento -> mVUsearchProg/
	// mVUentryGet, y mVUcompileJIT; ninguno resuelve programas desde
	// código generado, así que no hay rutas que se escapen el control).
	// ------------------------------------------------------------------

	// Resuelve el despacho contra la tabla de candidatos vivos: si hay
	// variante VERIFY/TRUSTED para este PC, con la clave de entrada byte-
	// idéntica a la de construcción y presupuesto (mVU.cycles del episodio
	// >= ciclos del área fusionada) — con alternación de paridad por
	// ventana en VERIFY — devuelve la entrada del superbloque y registra el
	// episodio para SbFinish; si no, nullptr (el llamador sigue la cadena
	// normal; si el camino elegido fue el normal DENTRO de la ventana, el
	// episodio también queda registrado para el emparejamiento). El filtro
	// de presupuesto hace que la guarda de entrada del superbloque (la
	// misma que emite mVUtestCycles para cualquier bloque, sobre los
	// ciclos del AREA COMPLETA) nunca rompa a media fusion con presupuesto
	// suficiente: o corre entera, o no corre; con presupuesto corto manda
	// la cadena normal, que si rompe bloque a bloque como siempre.
	// pState96 = &mVU.prog.lpState (clave con la que el dispatcher resuelve
	// esta entrada). Llamado SIEMPRE con el motor ON (el escaneo interno
	// es O(vivos) y caro-cero cuando no hay candidatos).
	void* SbResolve(u32 startPC_bytes, const void* pState96, s32 budget);

	// Cierre del episodio (recMicroVU1::Execute, tras volver startFunct):
	// consumedCycles = regs().cycle ganado por el episodio (delta — el
	// absoluto depende del presupuesto y no puede entrar en el par). El
	// motor pide el digest de SALIDA a g_exitDigest (definida en
	// microVU-arm64.cpp, que es el TU que ve VURegs/VU1; el motor solo lo
	// compara) y empareja con el episodio de la otra ruta que entro con la
	// misma clave: MATCH cuenta, DIVERGENCE invalida la variante + fallback
	// + reporte, kSbKills divergencias auto-apagan el motor por proceso,
	// ventana agotada sin los pares -> candidato "unproven" (superbloque
	// liberado). Solo VU1, solo con motor ON. Llamable SIEMPRE (el motor
	// decide si hay episodio abierto).
	// episodeNs: duracion REAL del episodio (el llamador la toma con
	// Common::Timer, monotonia). Con las DOS patas de un par medidas se forma la
	// GANANCIA: sin numero no hay TRUSTED, y con numero pero sin su n tampoco.
	// 0 = «este episodio no se cronometro»: ese par no cuenta como cronometrado
	// (se anota aparte, no se silencia).
	void SbFinish(u32 startPC_bytes, u32 consumedCycles, u64 episodeNs);

	// Etiqueta de LA MAQUINA para la linea VU-SB del HUD, con recuentos. En
	// VALIDATE y SHADOW nunca dice «acelerado»: esa regla esta codificada en
	// mVUSbLife::Machine::HudLabel, no en quien pinta. outSize >= 96.
	void HudSummary(char* out, u32 outSize);

	// Digest de salida del estado observable de VU1 (VF/VI/ACC, q/p,
	// flags, pendings, micro_*flags, mac/status/clipflag, nextBlockCycles,
	// respaldo VI, contadores XGKICK, anillos fmac/fdiv/efu/ialu +
	// posiciones, y los 16 KB de memoria de datos). Definida y registrada
	// por microVU-arm64.cpp en mVUinit; nullptr hasta entonces.
	using SbExitDigestFn = u64 (*)(u32 consumedCycles);
	extern SbExitDigestFn g_exitDigest;

	// Invalidación masiva: toda escritura a la imagen micro invalida las
	// variantes (cero trazas vivas sobre microcódigo que cambió, pide el
	// documento). Barato: solo hace algo si hay variantes vivas.
	void SbKillAll(u32 gen);

	// ------------------------------------------------------------------
	// Contadores — vuelco a logs/vu_superblock.txt + resumen en emulog.
	// ------------------------------------------------------------------
	struct Stats
	{
		std::atomic<u64> attempts{0};      // PCs evaluados por la política
		std::atomic<u64> built{0};         // superbloques adoptados
		std::atomic<u64> junctions{0};     // uniones fundidas (barreras eliminadas)
		std::atomic<u64> fused_ops{0};     // instrucciones dentro de superbloques
		std::atomic<u64> rejected_struct{0};   // rama no fundible (bits/kick/cond/evil/wrap)
		std::atomic<u64> rejected_cold{0};     // sin hotness / flujo inestable
		std::atomic<u64> rejected_budget{0};   // techo de ops/ciclos/uniones o región sin presupuesto
		std::atomic<u64> rejected_identity{0}; // clave de unión sucia o tabla llena
		std::atomic<u64> verify_normal{0};   // despachos de la ventana por la cadena normal
		std::atomic<u64> verify_sb{0};       // despachos de la ventana por el superbloque
		std::atomic<u64> unmatched{0};       // episodios verificados sin par de entrada idéntica
		std::atomic<u64> matched{0};         // pares con hash de salida idéntico
		std::atomic<u64> diverged{0};        // pares distintos -> invalidar + fallback
		std::atomic<u64> promoted{0};        // variantes VERIFY -> TRUSTED
		std::atomic<u64> unproven{0};        // ventana agotada sin evidencia -> cadena normal
		std::atomic<u64> killed{0};          // veces que el motor se auto-apagó
		// 0.6.32 — contabilidad de LA MAQUINA por candidato.
		std::atomic<u64> mask_rejected{0};   // pares NO contados por mascara incompleta
		std::atomic<u64> epochs{0};          // ventanas (ids monotonicos) abiertas
		std::atomic<u64> validated{0};       // VALIDATED (equivale) — NO es TRUSTED
		std::atomic<u64> dead_candidates{0}; // DEAD por candidato (el proceso sigue vivo)
		std::atomic<u64> archived{0};        // INVALIDATED por presupuesto/contexto
		std::atomic<u64> trials{0};          // ventanas de prueba declaradas
		std::atomic<u64> spot_checks{0};     // comprobaciones puntuales de un TRUSTED
		std::atomic<u64> unprofitable{0};    // VALIDATED sin ganancia: equivale pero no vale la pena
	};
	extern Stats g_stats[2];

	// ------------------------------------------------------------------
	// Informe — igual que el probe: detalle a logs/vu_superblock.txt
	// ("wb") + resumen por Console.WriteLn (viaja en emulog.txt).
	// ------------------------------------------------------------------
	void DumpReport(const char* reason);
}
