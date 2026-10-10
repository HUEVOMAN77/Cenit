// Harness10 — ARNES DE SINTAXIS, no de logica.
//
// Existe por un fallo real y concreto: en 0.6.32 un insert a mano dejo una linea de
// prosa espanola FUERA de un comentario dentro de MvuSuperblock-arm64.h. El CI la
// tiro abajo ("unknown type name 'prompt'") despues de 25 minutos de compilacion.
// Ningun arnes de Java la cubria (h5..h8 solo compilan .java) y Harness9 solo cubre
// MvuSbLife.h. O sea: la cabecera publica del motor, que es la que microVU-arm64.cpp
// y MvuSuperblock-arm64.cpp incluyen, no la verificaba nadie en host.
//
// Que comprueba, en el unico sentido que un arnes de host puede:
//  1. Que cada cabecera del area arm64 es C++ VALIDO POR SI SOLA (autocontenida).
//     Sin el -I de pcsx2/arm64 en la linea de g++, cualquier cabecera que dependa de
//     que otro include aparezca antes se cae aqui, no en el CI del telefono.
//  2. Que la sintaxis es correcta (el bug de 0.6.32 es de esto).
//  3. Que los nombres/enumes que el contrato del HUD y del informe publican existen
//     con la forma que contrato y verificadores dicen — pero sin enlazar el motor:
//     -fsyntax-only, asi una cabecera que declare funciones no definidas NO es un
//     fallo de este arnes (eso lo cubre jnisig.py y el link del APK).
//
// No comprueba NADA de emision, ni vixl, ni tipos de VURegs: estas cabeceras viven
// a proposito libres de eso, y si algun dia dejan de estarlo este arnes ruidara
// porque el host no tiene los headers del toolchain de Android. Eso tambien es
// informacion.
//
// Versionado en tools/arneses/h10-src/. Las cabeceras se traen DEL REPO en cada
// corrida (misma regla anti-copia-vieja que h5..h9).

#include "MvuSbLife.h"
#include "MvuSuperblock-arm64.h"
#include "MvuTraceProbe-arm64.h"

#include <type_traits>

// --- Contrato de la maquina (lo que jnidict.py y el HUD leen) -------------------
static_assert(mVUSbLife::kStateCount == 8,
	"el contrato dice OCHO estados por candidato");
static_assert(static_cast<unsigned>(mVUSbLife::State::Unseen) == 0, "ordinal 0");
static_assert(static_cast<unsigned>(mVUSbLife::State::Building) == 1, "ordinal 1");
static_assert(static_cast<unsigned>(mVUSbLife::State::Validating) == 2, "ordinal 2");
static_assert(static_cast<unsigned>(mVUSbLife::State::Shadowing) == 3, "ordinal 3");
static_assert(static_cast<unsigned>(mVUSbLife::State::Validated) == 4, "ordinal 4");
static_assert(static_cast<unsigned>(mVUSbLife::State::Trusted) == 5, "ordinal 5");
static_assert(static_cast<unsigned>(mVUSbLife::State::Dead) == 6, "ordinal 6");
static_assert(static_cast<unsigned>(mVUSbLife::State::Invalidated) == 7, "ordinal 7");

// Las leyes de promocion que el contrato cita por nombre y numero.
static_assert(mVUSbLife::kValidPairs == 1000, "VALIDATED exige 1000 pares");
static_assert(mVUSbLife::kValidWindows == 2, "VALIDATED exige MAS DE UNA ventana");
static_assert(mVUSbLife::kMinGainPairs == 64, "la ganancia exige muestra");
static_assert(mVUSbLife::kTrustedGainPermille == 30, "TRUSTED exige >= +3% medido");
static_assert(mVUSbLife::kBudgetEpisodes == 60000, "presupuesto de validacion");

// La mascara: 14 regiones declaradas, todas exigidas. Si alguien anade una region y
// no la exige, el par se contaria sobre estado no mirado; si alguien quita un bit
// exigido, el digest puede decir que cubre lo que no cubre. Las dos cosas se ven aqui.
static_assert(mVUSbLife::Regions::EpisodeCycles == (1u << 13), "14 regiones = bits 0..13");
static_assert(mVUSbLife::kRequiredRegions ==
		(mVUSbLife::Regions::Vf | mVUSbLife::Regions::Vi | mVUSbLife::Regions::Acc |
		 mVUSbLife::Regions::Qp | mVUSbLife::Regions::Cycle | mVUSbLife::Regions::Flags |
		 mVUSbLife::Regions::Pendings | mVUSbLife::Regions::MacroClipStatus |
		 mVUSbLife::Regions::NextBlockCycles | mVUSbLife::Regions::Xgkick |
		 mVUSbLife::Regions::ViBackup | mVUSbLife::Regions::LatencyRings |
		 mVUSbLife::Regions::DataMem | mVUSbLife::Regions::EpisodeCycles),
	"la maquina exige TODAS las regiones que el digest declara (jnidict lo cruza "
	"contra g_exitMask en microVU-arm64.cpp; aqui se fija el valor de la exigencia)");

// --- Contrato del motor (lo que microVU-arm64.cpp registra) ---------------------
namespace mVUSuperblock
{
	// Firmas exactas: cambiar una sin cambiar el llamador es justo lo que el CI
	// descubria a las 25 minutos. Estas lineas lo descubren en host en 2 segundos.
	void (*const s_fnSync)(bool) = &SyncFromConfig;
	bool (*const s_fnKilled)() = &WasKilled;
	bool (*const s_fnValidating)() = &Validating;
	void (*const s_fnProfile)(u64) = &SetProfileHash;
	u64 (*const s_fnGetProfile)() = &ProfileHash;
	void (*const s_fnHud)(char*, u32) = &HudSummary;
	void (*const s_fnFinish)(u32, u32, u64) = &SbFinish;
	void* (*const s_fnResolve)(u32, const void*, s32) = &SbResolve;

	// El observador de area es una funcion de punteros COUNTADOS (la zona no es
	// contigua), y esa forma es parte del contrato con el TU de microVU.
	u32 (*const s_fnArea)(const u32*, const u32*, u32) = g_areaDigest;

	// Tipos del puente: si el ABI o la mascara dejan de ser atomicos, el hilo de
	// settings y el dispatcher escriben en competencia sin que nadie lo vea.
	static_assert(std::is_same<decltype(g_exitMask), const std::atomic<u32>&>::value ||
		std::is_same<decltype(g_exitMask), std::atomic<u32>>::value,
		"g_exitMask debe ser atomic<u32>");
	static_assert(std::is_same<decltype(g_engineAbi), std::atomic<u32>>::value,
		"g_engineAbi debe ser atomic<u32>");
	static_assert(std::is_same<decltype(g_rollbackSurface), std::atomic<bool>>::value,
		"g_rollbackSurface debe ser atomic<bool>: es dato, no prosa");
	static_assert(std::is_same<decltype(g_enabled), std::atomic<bool>>::value,
		"g_enabled debe ser atomic<bool>");

	// El interruptor de la superficie de rollback tiene que seguir siendo un bool
	// que se ESCRIBE (false hoy). Si alguien lo deja en un literal true sin programar
	// la superficie, esto no lo detecta — lo detecta Harness9 (Rules::ShadowSafe) y el
	// aviso de consola. Aqui se fija que SIGA siendo un dato legible desde el motor.
	static_assert(sizeof(g_rollbackSurface) == sizeof(bool),
		"g_rollbackSurface sigue siendo un flag de un byte");
}

// Las variables que el arnes "toca" para forzar la comprobacion de tipos existen en
// la cabecera como declaraciones: con -fsyntax-only no se enlazan, y no hace falta.
// Este main no corre; el arnes es el codigo de retorno del compilador.
int main()
{
	return 0;
}
