// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// Harness12 — LA LOGICA DEL FAST PATH VU1 (MvuFastPath.h), probada en host contra
// un modelo lineal del search()/add() REAL.
//
// Existe por la misma razon que Harness11: hay piezas de este core que se pueden
// verificar SIN telefono, y dejarlas sin verificar es el defecto que ya costo dos
// ciclos (el «8 pares y ya» de 0.6.21 y el «sin divergencias = acelerado» de
// 0.6.25). Aqui la pieza es el memo de despacho: arm64/MvuFastPath.h.
//
// Por que hace falta un arnes y no basta «confiar en la optimizacion»: el memo
// responde, en CADA despacho de VU1, la pregunta que antes resolvia un barrido
// lineal. Un memo que se equivoca no cuesta velocidad — cuesta saltar a la entrada
// de OTRO bloque, o sea ejecutar microcodigo que nadie pidio. Un acelerador asi, o
// se demuestra equivalente al barrido, o no se enciende.
//
// Que comprueba:
//  1) Contrato del dominio: kSlots == 2048, IdxOf == (pc >> 3) & 2047 (la misma
//     formula que MvuSbIndex.h y la sonda), biyectividad sobre 0x0000..0x3ff8 — y
//     por que la comprobacion de PC dentro de Serve sigue haciendo falta aunque hoy
//     sea redundante (se fuerza con una casilla escrita a mano con otro PC).
//  2) Mascara: MaskOf(1) == ~0x3002 exacto y MaskOf(0) == ~0, con los bits borrados
//     fijados por escrito (1 = needExactMatch, 9 y 13 = flagInfo) y blockType (byte
//     6) FORZADO a no estar enmascarado: ahi es donde un memo con la mascara mal
//     puesta serviria la variante de un superbloque a un despacho normal.
//  3) Cada comprobacion de Serve, forzada una por una sobre entradas escritas a
//     mano: exact, empty, pc, vu, cacheEpoch, candidateEpoch, progId, mgrId,
//     flagHack/quickKey, bounds — cada una con su contador, y bounds ademas dejando
//     la casilla limpia (limpiar es parte del contrato: sin eso se reintentaria).
//  4) Bordes de la ventana: hostEntry == cacheStart vale, hostEntry == cacheEnd no
//     — la misma convencion [start, end) con la que mVUexecute comprueba limites.
//  5) Guardas de Record: exacto, VU distinto, barrido corto (< kMinRecordPos) y
//     puntero fuera de la ventana NO escriben nada (todo cae en recordSkipped) y la
//     casilla sigue vacia. Un miss de barrido jams se memoriza.
//  6) LEY DE COSTO como comportamiento, no como prosa: con la lista por debajo del
//     umbral el llamador compuesto NO pregunta a la tabla (consults no se mueve);
//     con la lista en el umbral, si. Y un acierto en posicion corta no se registra.
//  7) Invalidate() vs Reset(): el primero deja la tabla sin respuestas CONSERVANDO
//     las cuentas (la telemetria del cierre es de sesion entera y mVUreset no es
//     raro), el segundo borra las cuentas (solo arranque de VM).
//  8) El argumento (a) del encabezado, PROBADO y no afirmado: dos claves distintas
//     que coinciden bajo la mascara solo pueden coexistir si la mascara CAMBIO entre
//     las dos inserciones (add deduplica con la mascara vigente). Con las dos
//     inserciones convivindo, el memo tiene que seguir senalando el MISMO bloque que
//     el barrido senala (el insertado primero), y al voltear la mascara tiene que
//     RECHAZAR por flagHack antes que servir la respuesta del otro mundo.
//  9) EQUIVALENCIA DIFERENCIAL: 24000 pasos de anadir bloques (rapidos y exactos),
//     despachar por el camino del llamador real, voltear la mascara, escribir
//     microcodigo (candidateEpoch), resetear el cache (cacheEpoch + Invalidate),
//     DESTRUIR y REASIGNAR el gestor en la misma direccion, cambiar de programa y
//     mover la ventana sin subir la epoca. Despues de CADA paso se auditan TODAS las
//     casillas vivas: lo que el memo afirma tiene que ser exactamente lo que el
//     barrido del modelo afirma sobre la misma clave.
//
// Testigos: cada camino tiene su contador y el verde se declara invalido si alguno
// queda en cero (la regla de h11: diez mil comprobaciones que nunca tocaron el
// camino raro no son evidencia).
//
// Que NO comprueba, dicho en claro (para que nadie lea de mas en su verde):
//  - Que el LLAMADOR REAL pase la posicion que el barrido recorrio ni las guardas en
//    el orden exacto: aqui eso lo modela DispatchLikeTheCaller. Que
//    microBlockManager::search rellene outPos con la posicion verdadera vive en
//    microVU-arm64.h y lo cubre el CI.
//  - Que cacheStart/cacheEnd sean la ventana real del code cache: eso vive en
//    microVU-arm64.cpp (x86end + mVUcacheSafeZone*_1mb, la misma expresion de
//    mVUexecute) y lo cubre el CI.
//  - Que mVUreset/mVUclose suban codeCacheEpoch y que mVUclear suba
//    microMemWriteGen: aqui se prueba que CON ESAS EPOCAS SUBIDAS la tabla no sirve
//    nada viejo, no que los hooks esten puestos.
//  - Nada de emision JIT, VURegs, microcodigo ni timing: el memo no los toca.
//  - Nada de tasas de uso en juegos reales (cuantos PCs de un juego llegan al umbral
//    o cuanto se sirve de verdad). Eso solo se ve en el telefono — para eso esta la
//    linea de telemetria que el VM imprime al cerrarse.
//
// Lo corre tools/arneses/baseline.sh con g++ de host y el header DEL REPO (misma
// regla anti-copia-vieja que h5..h11).

#include "MvuFastPath.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <vector>

static int g_checks = 0;
static int g_fails = 0;

#define CHECK(cond, ...)                 \
	do                                   \
	{                                    \
		g_checks++;                    \
		if (!(cond))                   \
		{                                \
			g_fails++;               \
			std::printf("FALLO: ");      \
			std::printf(__VA_ARGS__);    \
			std::printf("\n");           \
		}                                \
	} while (0)

using mVUFastPath::Entry;
using mVUFastPath::Query;
using mVUFastPath::Stats;
using mVUFastPath::Table;
using mVUFastPath::kEmptyPc;
using mVUFastPath::kFlagMask;
using mVUFastPath::kMinRecordPos;
using mVUFastPath::kSlots;
using mVUFastPath::kVU1;

// u64h == uint64_t: los campos de Query/Stats del header son uint64_t, y en LP64 eso es
// unsigned long. Usar el mismo tipo evita conversiones invalidas al pasar &got a
// Serve() y al indexar punteros-miembro de Stats.
typedef uint64_t u64h;

// ------------------------------------------------------------------
// PRNG propio determinista (xorshift64*), sin <random>: la semilla fija hace que un
// fallo sea reproducible en otra maquina.
// ------------------------------------------------------------------
static u64h s_state = 0x9E3779B97F4A7C15ull;

static u64h xs(void)
{
	s_state ^= s_state >> 12;
	s_state ^= s_state << 25;
	s_state ^= s_state >> 27;
	return s_state * 0x2545F4914F6CDD1Dull;
}

static unsigned rndBelow(unsigned n) { return (unsigned)(xs() % n); }

// ------------------------------------------------------------------
// EL MODELO: copia lineal y honesta de microBlockManager en lo que al memo le
// importa. Reglas leidas del codigo real, no de la intuicion:
//   - add() (microVU-arm64.h:653-686) deduplica CON ESTE MISMO search() y despues
//     hace push_back en quickLookup, a TODOS los bloques incluidos los
//     needExactMatch. Por eso `lookup` del modelo lleva los dos tipos.
//   - Un bloque exacto se deduplica por compareState (aqui igualdad de estado
//     completo) y vive ademas en una lista MRU propia que SI se reordena al buscar.
//     El reordenado no toca quickLookup: el orden de insercion que ve el camino
//     rapido es inmutable, y eso es parte de lo que se quiere probar.
//   - search() rapido (microVU-arm64.h:729-762) recorre quickLookup en orden de
//     insercion y devuelve el PRIMER match bajo la mascara vigente.
// ------------------------------------------------------------------
struct MState
{
	u64h quick;                 // quick64[0] pelado, como lo guarda quickLookup
	unsigned extra0, extra1, extra2; // el resto del estado de 96 bytes
	unsigned needExact;              // byte 0 del estado
};

static bool StateEq(const MState& a, const MState& b)
{
	return a.quick == b.quick && a.extra0 == b.extra0 && a.extra1 == b.extra1 &&
		   a.extra2 == b.extra2 && a.needExact == b.needExact;
}

struct MBlock
{
	MState st;
	u64h host; // entrada compilada; unica por bloque en el modelo
	unsigned pc; // PC al que se anadio: la clave y el PC van juntos en el despacho
};

struct Model
{
	std::vector<MBlock> lookup; // quickLookup: append-only
	std::vector<int> fList;     // indices de los exactos, en orden MRU
	unsigned flagHack = 0;      // mVUsFlagHack vigente

	void clear()
	{
		lookup.clear();
		fList.clear();
	}

	// El barrido rapido de verdad: primer match bajo la mascara vigente, con
	// posicion — igual que search() con outPos.
	int searchQuick(u64h quick, unsigned* outPos) const
	{
		const u64h m = Table::MaskOf(flagHack);
		for (size_t i = 0; i < lookup.size(); i++)
		{
			if (outPos)
				*outPos = (unsigned)i;
			if ((lookup[i].st.quick & m) == (quick & m))
				return (int)i;
		}
		return -1;
	}

	// El barrido exacto de verdad: recorre fList, compara el estado completo y
	// promociona a la cabecera. El memo nunca responde esta rama; el modelo la tiene
	// para que add() de un bloque exacto deduplique como deduplica el codigo de
	// siempre, y para que un despacho exacto del diferencial se resuelva bien.
	int searchExact(const MState& st, unsigned* outPos)
	{
		for (size_t i = 0; i < fList.size(); i++)
		{
			if (StateEq(lookup[(size_t)fList[i]].st, st))
			{
				if (outPos)
					*outPos = (unsigned)i;
				if (i != 0)
				{
					const int hit = fList[i];
					fList.erase(fList.begin() + (long)i);
					fList.insert(fList.begin(), hit);
				}
				return fList[0];
			}
		}
		return -1;
	}

	// add() fiel: primero dedup, luego append. `pc` es el PC del bloque que se
	// compila; se guarda en el ref solo para que el despacho del modelo pregunte con
	// el par (pc, clave) que el dispatcher de verdad tendria en la mano.
	int add(const MState& st, u64h host, unsigned pc)
	{
		const int found = st.needExact ? searchExact(st, nullptr) : searchQuick(st.quick, nullptr);
		if (found >= 0)
			return found;
		lookup.push_back({st, host, pc});
		const int idx = (int)lookup.size() - 1;
		if (st.needExact)
			fList.push_back(idx);
		return idx;
	}
};

// ------------------------------------------------------------------
// EL MUNDO: lo vivo que el despacho lee al preguntar (los campos de Query que no
// dependen del PC ni de la clave).
// ------------------------------------------------------------------
struct World
{
	u64h cacheEpoch = 1;
	u64h candidateEpoch = 7;
	u64h progId = 0x1000;
	u64h mgrId = 0x2000;
	u64h cacheStart = 0x10000000ull;
	u64h cacheEnd = 0x12000000ull; // ventana [start, end)
	unsigned vuId = kVU1;
	unsigned hostSeq = 0;

	u64h newHost()
	{
		hostSeq++;
		return cacheStart + 0x40ull * hostSeq;
	}
};

static World g_world;
static Model g_model;

// FIDELIDAD: en el codigo real needExactMatch NO es un campo aparte — es el BIT 1 de
// quick64[0] (microRegInfo es una union: microVU-arm64.h:106-121). Por eso aqui se
// DERIVA del bit: un estado "exacto" lleva el bit puesto, y bajo flagHack ese bit se
// enmascara, que es precisamente como un despacho rapido puede resolver un bloque
// guardado como exacto. Modelarlo como campo aparte esconderia esa interaccion.
static MState MkState(u64h quick, unsigned needExact, unsigned extra = 0)
{
	MState st;
	st.quick = needExact ? (quick | 0x2ull) : (quick & ~0x2ull);
	st.extra0 = extra;
	st.extra1 = extra ^ 0x5a5a;
	st.extra2 = extra + 1u;
	st.needExact = (unsigned)((st.quick >> 1) & 1ull);
	return st;
}

// El mismo despacho pero partiendo de una clave ya guardada, pidiendo el camino
// RAPIDO: se limpia el bit 1 sin tocar nada mas, como haria lpState con
// needExactMatch a cero.
static MState AsQuickDispatch(const MState& stored)
{
	MState st = stored;
	st.quick &= ~0x2ull;
	st.needExact = 0;
	return st;
}

static u64h DifHost(void)
{
	const u64h h = g_world.cacheStart + 0x40ull * (g_world.hostSeq + 1u);
	g_world.hostSeq++;
	return h;
}

// Un host dentro de la ventana vigente y NO reutilizable: dos bloques con la misma
// direccion de entrada harian indistinguible "sirvio el bloque A" de "sirvio el B".
// Mazo de CLAVES: bajo flagHack dos claves que difieren solo en bits enmascarados
// son la misma clave para add(), asi que barajar PCs no basta — hace falta barajar
// claves no equivalentes. Con claves distintas la posicion del acierto en quickLookup
// es la posicion de insercion, lo que hace verificable el walkPos del umbral.

static u64h g_keyDeck[kSlots];

// Posicion del mazo: explicita para poder reiniciarla al cambiar de ciclo.
static unsigned g_deckAt = kSlots;

static void DeckRestart(void)
{
	g_deckAt = kSlots; // fuerza el barajado en el proximo DeckKey
	// Los 2048 enteros del mazo viven en los bits 14..28: ninguno de los bits que
	// la mascara borra (1, 12 y 13) se usa, asi que dos naipes cualesquiera son
	// claves DISTINTAS incluso bajo ~0x3002 y add() los inserta los dos.
	for (unsigned i = 0; i < kSlots; i++)
	{
		const u64h lo = (u64h)(i & 0x3Fu) << 14;   // bits 14..19
		const u64h hi = (u64h)(i >> 6) << 20;      // bits 20..28
		g_keyDeck[i] = 0x0001000000000000ull | lo | hi;
	}
	for (unsigned i = kSlots - 1u; i > 0u; i--)
	{
		const unsigned j = rndBelow(i + 1u);
		const u64h t = g_keyDeck[i];
		g_keyDeck[i] = g_keyDeck[j];
		g_keyDeck[j] = t;
	}
}

static u64h DeckKey(void)
{
	if (g_deckAt >= kSlots)
	{
		DeckRestart();
		g_deckAt = 0;
	}
	return g_keyDeck[g_deckAt++];
}

// PC dentro del dominio de despacho de VU1 (0x0000..0x3ff8, de 8 en 8).
static unsigned DeckPc(void) { return rndBelow(kSlots) * 8u; }

// La Query que el despacho armaria HOY con este estado y este mundo. Los tests
// forzados la copian y le cambian UN campo: asi cada rechazo comprueba exactamente
// la comprobacion que dice comprobar.
static Query MakeQuery(unsigned pc, const MState& st)
{
	Query q;
	q.pc = pc;
	q.vuId = g_world.vuId;
	q.flagHack = g_model.flagHack ? 1u : 0u;
	q.exact = st.needExact ? 1u : 0u;
	q.cacheEpoch = g_world.cacheEpoch;
	q.candidateEpoch = g_world.candidateEpoch;
	q.progId = g_world.progId;
	q.mgrId = g_world.mgrId;
	q.quick64 = st.quick;
	q.cacheStart = g_world.cacheStart;
	q.cacheEnd = g_world.cacheEnd;
	return q;
}

// ------------------------------------------------------------------
// EL LLAMADOR COMPUESTO: replica de MvuFastPathServe + MvuFastPathRecord (guarda de
// exacto, guarda de tamano de lista, epoca leida antes, Serve, epoca re-leeida,
// barrido CON posicion y Record solo si la epoca no se movio). No es el codigo real
// — ese esta en microVU-arm64.cpp y lo compila el CI —, es el modelo contra el que
// se fuerza equivalencia.
//   path: 0 = miss (nada compilado, igual que hoy), 1 = sirvio el memo,
//         2 = resolvió el barrido.
// ------------------------------------------------------------------
struct Dispatch
{
	int memoIdx; // bloque que el memo afirma (-1 si no se sirvio)
	int walkIdx; // bloque que el modelo encuentra (-1 si miss)
	unsigned walkPos;
	bool asked;
	bool recorded; // el Record de ESTE paso escribio la tabla
	int path;
};

static unsigned g_wServed = 0, g_wWalkHit = 0, g_wWalkMiss = 0, g_wRecorded = 0;
static unsigned g_wSkippedShort = 0, g_wNoAsk = 0, g_wEpochMoved = 0;

// Cuentas del INVARIANTE por despacho (0.6.36). Se comparan como DELTAS de UN
// despacho y no como totales de final de sesion porque el arnes tambien llama a
// Serve/Record directamente (testServeFields, la auditoria del diferencial): esas
// llamadas suman consults sin decision previa, asi que un total absoluto de una tabla
// de pruebas no tiene por que cuadrar. Lo que SI tiene que cuadrar siempre es lo que
// movio UN despacho compuesto — que es exactamente la forma en que el telefono tiene
// que leer su linea de cierre. Cada rota se emite con CHECK, asi que ya suma en el
// recuento general; g_invChecks es solo el testigo de que el invariante se ejercito.
static unsigned g_invChecks = 0;
static unsigned g_wAskCounted = 0, g_wSkipShortCounted = 0, g_wSkipExactCounted = 0;

static Dispatch DispatchInner(unsigned pc, const MState& st, Table& tab)
{
	Dispatch d;
	d.memoIdx = -1;
	d.walkIdx = -1;
	d.walkPos = 0;
	d.asked = false;
	d.recorded = false;
	d.path = 0;
	uint64_t served = 0;

	// El llamador real llama a Table::AskCounted — la decision CONTADA — y no a
	// ShouldAsk (microVU-arm64.cpp:1839). El arnes replica eso, no la proyeccion, para
	// que los contadores se comprueben en el mismo camino que el telefono usa. Las
	// guardas siguen siendo metodos del encabezado: si la condicion viviera solo en el
	// cpp, mutacion_h12.sh no podria romperla — y una guarda que nadie puede romper es
	// una guarda que nadie prueba.
	//
	// El orden importa y por eso el VU se comprueba ANTES de llamar: en el motor la
	// condicion de VU vive en el `if constexpr (vuIndex == 1 && !doConstProp)` de la
	// plantilla MvuFastPathServe<vuIndex>, o sea para VU0 la tabla ni se llama y NI
	// UNA cuenta se suma. Poner el && despues de AskCounted contaria despachos de VU0
	// que la produccion no cuenta, y la linea del telefono dejaria de describir el
	// codigo que corre.
	const bool vu1 = (g_world.vuId == kVU1);
	const bool ask = vu1 && tab.AskCounted(st.needExact, (int)g_model.lookup.size());

	if (ask)
	{
		Query q = MakeQuery(pc, st);
		const u64h epochBefore = g_world.cacheEpoch;
		d.asked = true;
		if (tab.Serve(q, &served) && Table::EpochStable(epochBefore, g_world.cacheEpoch))
		{
			for (size_t i = 0; i < g_model.lookup.size(); i++)
				if (g_model.lookup[i].host == served)
					d.memoIdx = (int)i;
			d.path = 1;
			g_wServed++;
			return d;
		}
		if (g_world.cacheEpoch != epochBefore)
			g_wEpochMoved++;

		unsigned pos = 0;
		d.walkIdx = g_model.searchQuick(st.quick, &pos);
		d.walkPos = pos;
		if (d.walkIdx < 0)
		{
			d.path = 0;
			g_wWalkMiss++;
			return d;
		}
		g_wWalkHit++;
		d.path = 2;
		// Record solo con epoca estable de principio a fin del lookup. El arnes se
		// queda con si ESCRIBIO o no (d.recorded): esa es la unica verdad que la
		// sombra puede tener, porque la clase no expone sus entradas.
		if (g_world.cacheEpoch == epochBefore)
			d.recorded = tab.Record(q, g_model.lookup[(size_t)d.walkIdx].host, pos);
		if (d.recorded)
			g_wRecorded++;
		else if (pos < kMinRecordPos)
			g_wSkippedShort++;
		return d;
	}

	g_wNoAsk++;
	unsigned pos = 0;
	d.walkIdx = st.needExact ? g_model.searchExact(st, &pos) : g_model.searchQuick(st.quick, &pos);
	d.walkPos = pos;
	d.path = d.walkIdx < 0 ? 0 : 2;
	if (d.walkIdx < 0)
		g_wWalkMiss++;
	return d;
}

// El despacho completo, con el INVARIANTE comprobado SOBRE EL PASO ENTERO. Va aqui y no
// dentro de DispatchInner porque `consults` lo mueve Serve, que ocurre despues de la
// decision: medir entre las dos es medir la decision a medio cerrar. El contrato que el
// telefono imprime es
//     lookups == consults + skipExact + skipShort
// y por UN despacho eso significa: lookups +1, y exactamente uno de los otros tres +1 —
// el que corresponda al motivo, ninguno si pregunto y el memo no tuvo nada que ver... lo
// cual no existe: si pregunto, Serve suma consults. O sea la suma de los tres siempre
// vale 1.
static Dispatch DispatchLikeTheCaller(unsigned pc, const MState& st, Table& tab)
{
	const u64h l0 = tab.stats.lookups;
	const u64h c0 = tab.stats.consults;
	const u64h e0 = tab.stats.skipExact;
	const u64h s0 = tab.stats.skipShort;
	const u64h x0 = tab.stats.rejectExact;

	const Dispatch d = DispatchInner(pc, st, tab);

	const u64h dl = tab.stats.lookups - l0;
	const u64h dc = tab.stats.consults - c0;
	const u64h de = tab.stats.skipExact - e0;
	const u64h ds = tab.stats.skipShort - s0;
	const u64h dx = tab.stats.rejectExact - x0;

	// Fuera de VU1 no hay nada que cuadre: AskCounted no se llamo (el && corta antes),
	// o sea las cuatro cuentas valen 0 y exigir lookups==1 seria pedirle al arnes una
	// cuenta que la produccion tampoco hace. Queda el tripwire, que si vale para
	// cualquier VU: un exacto jamas tiene que llegar a Serve.
	if (g_world.vuId != kVU1)
	{
		CHECK(dx == 0ull, "pc=%04x en VU0: rejectExact subio %" PRIu64 " sin que nadie "
			"preguntara", pc, dx);
		return d;
	}

	g_invChecks++;
	// Todo por CHECK, no por printf suelto: el ejecutor de mutacion busca la linea
	// "^FALLO:" y el recuento de baseline busca ", N Fallos". Un rojo que no use esas
	// dos formas existe pero no se ve, y eso es peor que no comprobarlo.
	//
	// lookups tiene que haber subido justo 1: es el denominador, y un despacho que
	// llega a la decision y no se cuenta es el mismo defecto que no tener denominador.
	CHECK(dl == 1ull, "invariante del cierre: pc=%04x sumo %" PRIu64 " a lookups, queria 1",
		pc, dl);
	CHECK(dc + de + ds == 1ull, "invariante del cierre: pc=%04x dio consults=%" PRIu64
		" skip-exact=%" PRIu64 " skip-short=%" PRIu64 " (suman %" PRIu64 ", queria 1)",
		pc, dc, de, ds, dc + de + ds);
	// El motivo tiene que ser el que DecideAsk anuncia para este estado, no otro: los
	// tres se imprimen separados en el telefono, cambiarse entre si miente sobre POR QUE
	// el memo no se uso.
	if (d.asked)
	{
		CHECK(dc == 1ull && de == 0ull && ds == 0ull,
			"invariante del cierre: pc=%04x PREGUNTO y aun asi se sumo un salto "
			"(consults=%" PRIu64 " exact=%" PRIu64 " short=%" PRIu64 ")", pc, dc, de, ds);
		g_wAskCounted++;
	}
	else if (st.needExact != 0u)
	{
		CHECK(de == 1ull && dc == 0ull && ds == 0ull,
			"invariante del cierre: pc=%04x era EXACTO y no se conto como skip-exact "
			"(consults=%" PRIu64 " exact=%" PRIu64 " short=%" PRIu64 ")", pc, dc, de, ds);
		g_wSkipExactCounted++;
	}
	else if ((int)g_model.lookup.size() < (int)kMinRecordPos)
	{
		CHECK(ds == 1ull && dc == 0ull && de == 0ull,
			"invariante del cierre: pc=%04x tenia lista CORTA (%u refs) y no se conto como "
			"skip-short (consults=%" PRIu64 " exact=%" PRIu64 " short=%" PRIu64 ")",
			pc, (unsigned)g_model.lookup.size(), dc, de, ds);
		g_wSkipShortCounted++;
	}
	else
	{
		// No pregunto, no es exacto, y la lista es larga: no hay motivo que explique
		// el salto, o sea la decision del arnes y la del header ya no son la misma.
		CHECK(false, "invariante del cierre: pc=%04x salto sin motivo (exact=%u, %u refs, "
			"umbral %u): el arnes y DecideAsk ya no deciden lo mismo", pc,
			(unsigned)st.needExact, (unsigned)g_model.lookup.size(), (unsigned)kMinRecordPos);
	}
	// Tripwire del llamador: un exacto jamas tiene que llegar a Serve. rejectExact
	// moviendose aqui no es una medida, es la guarda rota (MvuFastPath.h lo dice).
	CHECK(dx == 0ull, "invariante del cierre: pc=%04x hizo mover rejectExact=%" PRIu64
		" — en produccion vale 0 y moverlo es la guarda del llamador rota", pc, dx);
	return d;
}

// ==================================================================
// 1) Contrato del dominio y de la mascara
// ==================================================================
static void testDomain(void)
{
	// -- Las dos guardas del llamador, pinchadas en sus dos bordes. No basta con que
	//    el differential las use: mutacion_h12.sh rompe ShouldAsk/EpochStable y aqui
	//    se fija el valor EXACTO en el borde, 7/8 y 8/9, para que un >= por un > o un
	//    bit olvidado no puedan sobrevivir. --
	CHECK(Table::ShouldAsk(0u, (int)kMinRecordPos) == true,
		"ShouldAsk(0, kMinRecordPos)=%d: la lista en el umbral debe preguntar",
		(int)Table::ShouldAsk(0u, (int)kMinRecordPos));
	CHECK(Table::ShouldAsk(0u, (int)kMinRecordPos - 1) == false,
		"ShouldAsk(0, umbral-1)=%d: por debajo del umbral preguntar es pagar por no "
		"ahorrar (ley de costo)", (int)Table::ShouldAsk(0u, (int)kMinRecordPos - 1));
	CHECK(Table::ShouldAsk(1u, 100000) == false,
		"ShouldAsk(exact, lista enorme)=%d: el dominio memoizado es la rama rapida de "
		"search(), los exactos no preguntan", (int)Table::ShouldAsk(1u, 100000));
	CHECK(Table::ShouldAsk(0u, 0) == false, "ShouldAsk(0, lista vacia)=%d",
		(int)Table::ShouldAsk(0u, 0));
	// Cenit 0.6.36 — DecideAsk es LA decision y ShouldAsk una proyeccion de ella, asi
	// que el borde se prueba dos veces y con el motivo explicito. No es redundancia: si
	// alguien vuelve a poner la condicion en dos sitios, el día que los dos difieran
	// este bloque lo dice (los dos motivos de no-preguntar son contadores DISTINTOS del
	// telefono, y un salto contado con el motivo equivocado se lee como una tasa de uso
	// que el juego no tuvo).
	CHECK(Table::DecideAsk(0u, (int)kMinRecordPos) == Table::kAsk,
		"DecideAsk(0, kMinRecordPos)=%d: queria kAsk", (int)Table::DecideAsk(0u, (int)kMinRecordPos));
	CHECK(Table::DecideAsk(0u, (int)kMinRecordPos - 1) == Table::kSkipShort,
		"DecideAsk(0, umbral-1)=%d: queria kSkipShort (ese contador es el que dice que "
		"los PCs de este juego no llegan al umbral)", (int)Table::DecideAsk(0u, (int)kMinRecordPos - 1));
	CHECK(Table::DecideAsk(1u, 100000) == Table::kSkipExact,
		"DecideAsk(exact, lista enorme)=%d: queria kSkipExact", (int)Table::DecideAsk(1u, 100000));
	CHECK(Table::DecideAsk(1u, 0) == Table::kSkipExact,
		"DecideAsk(exact, lista vacia)=%d: el motivo exacto tiene prioridad, o un juego "
		"que despacha con match exacto contaria como 'lista corta' y el log diria que el "
		"memo no aplica por tamano cuando no aplica por dominio",
		(int)Table::DecideAsk(1u, 0));
	CHECK(Table::ShouldAsk(1u, 100000) == (Table::DecideAsk(1u, 100000) == Table::kAsk) &&
			Table::ShouldAsk(0u, 3) == (Table::DecideAsk(0u, 3) == Table::kAsk),
		"ShouldAsk y DecideAsk ya no son la misma decision: hay dos copias de la ley de "
		"costo y estan divergiendo");
	CHECK(Table::EpochStable(5ull, 5ull) == true, "EpochStable con la misma epoca nego");
	CHECK(Table::EpochStable(5ull, 6ull) == false,
		"EpochStable dejo pasar una epoca movida: el seqlock del despacho ya no descarta "
		"un acierto a mitad de reset");
	CHECK(kSlots == 2048u,
		"kSlots=%u: el dominio de PCs de entrada de VU1 es 0x4000/8 = 2048 (la misma "
		"formula que MvuSbIndex.h y la sonda); si se separan, la casilla de un PC ya "
		"no es suya", (unsigned)kSlots);

	CHECK(Table::IdxOf(0x0000u) == 0u, "IdxOf(0)=%u queria 0", Table::IdxOf(0x0000u));
	CHECK(Table::IdxOf(0x0008u) == 1u, "IdxOf(8)=%u queria 1", Table::IdxOf(0x0008u));
	CHECK(Table::IdxOf(0x0010u) == 2u, "IdxOf(0x10)=%u queria 2", Table::IdxOf(0x0010u));
	CHECK(Table::IdxOf(0x3ff8u) == 2047u, "IdxOf(0x3ff8)=%u queria 2047 (ultima casilla)",
		Table::IdxOf(0x3ff8u));
	CHECK(Table::IdxOf(0x4000u) == 0u, "IdxOf(0x4000)=%u queria 0 (plegado por la "
		"mascara)", Table::IdxOf(0x4000u));

	// BIYECTIVIDAD sobre el dominio real. Es lo que hace que la comprobacion de PC de
	// Serve sea REDUNDANTE hoy. Si alguien mueve kSlots o el corrimiento y esto se
	// rompe, esa comprobacion pasa de redundante a la unica defensa contra servir el
	// bloque de otro PC — y por eso testServeFields la fuerza escribiendo a mano una
	// casilla con otro PC.
	{
		static bool seen[kSlots];
		for (unsigned i = 0; i < kSlots; i++)
			seen[i] = false;
		bool inyectiva = true;
		for (unsigned pc = 0; pc < 0x4000u; pc += 8u)
		{
			const unsigned idx = Table::IdxOf(pc);
			if (seen[idx])
			{
				inyectiva = false;
				break;
			}
			seen[idx] = true;
		}
		CHECK(inyectiva, "IdxOf no es inyectiva sobre el dominio de VU1: dos PCs "
			"comparten casilla y el memo puede servir el bloque de otro PC");
		unsigned cubiertas = 0;
		for (unsigned i = 0; i < kSlots; i++)
			if (seen[i])
				cubiertas++;
		CHECK(cubiertas == kSlots, "el dominio llena %u casillas de %u", cubiertas,
			(unsigned)kSlots);
	}

	// El marcador de vacio fuera del dominio: si no, "casilla libre" y "PC 0" serian
	// la misma cosa y el primer bloque del micro-programa no se podria memorizar.
	CHECK(kEmptyPc > 0x3ff8u, "kEmptyPc=%x esta dentro del dominio", kEmptyPc);

	// Mascara: el valor exacto de microVU-arm64.h:734-737.
	CHECK(kFlagMask == 0x3002ull, "kFlagMask=%" PRIx64 " queria 0x3002", (u64h)kFlagMask);
	CHECK(Table::MaskOf(1) == ~0x3002ull, "MaskOf(1)=%" PRIx64 " queria %" PRIx64 "",
		(u64h)Table::MaskOf(1), (u64h)~0x3002ull);
	CHECK(Table::MaskOf(0) == ~0ull, "MaskOf(0)=%" PRIx64 " queria todo-uno", (u64h)Table::MaskOf(0));
	CHECK(kMinRecordPos == 8u, "kMinRecordPos=%u: el umbral es la ley de costo (~8 "
		"comparaciones sobre una linea de cache); moverlo sin re-escribirla no vale",
		(unsigned)kMinRecordPos);
	CHECK(kVU1 == 1u, "kVU1=%u: el memo solo cubre VU1", (unsigned)kVU1);
	CHECK(sizeof(Entry) == 64u, "sizeof(Entry)=%u queria 64 (una linea de cache)",
		(unsigned)sizeof(Entry));

	// Bits que la mascara borra, fijados por escrito. 0x3002 = bit 1 + bits 12 y 13,
	// o sea needExactMatch (byte 0, bit 1) y los bits 4 y 5 de flagInfo (byte 1). Si
	// la mascara del header cambia, el modelo de este arnes y la tabla dejan de
	// significar lo mismo y esta linea tiene que ruidar.
	CHECK(kFlagMask == ((1ull << 1) | (1ull << 12) | (1ull << 13)),
		"la mascara dejo de borrar exactamente los bits 1/12/13 (needExactMatch y los "
		"dos bits de flagInfo que el speedhack ignora)");

	// Y por contraste: TODO lo demas se compara. Bytes 2..5 (flagInfo alto, q, p,
	// xgkick, viBackUp) tienen que estar intactos en la mascara — un memo que los
	// borrara serviria un bloque con los ciclos o el xgkick de otro estado.
	CHECK((kFlagMask & 0x000000ffff0000ull) == 0ull,
		"la mascara esta borrando los bytes 2..3 (q/p): el memo Serviria bloques con "
		"estado de ciclo distinto");
	CHECK((kFlagMask & 0x0000ffff00000000ull) == 0ull,
		"la mascara esta borrando los bytes 4..5 (xgkick/viBackUp)");

	// blockType (byte 6) NO puede estar enmascarado: es la invariante de identidad del
	// motor de superbloques (microVU-arm64.h:717-728). Si alguien la borrara, un
	// despacho normal resolveria la variante fusionada y este memo la memorizaria.
	CHECK((kFlagMask & (0xffull << (6 * 8))) == 0ull,
		"la mascara esta borrando el byte de blockType: un despacho normal podria "
		"servir la variante de un superbloque");
}

// ==================================================================
// Utilidades de las pruebas forzadas: una tabla aparte para no pisar g_vu1, y una
// entrada buena escrita con Record (el unico camino publico de llenado, para que lo
// que se prueba sea el contrato de la clase y no un struct armado a mano).
// ==================================================================
static Table g_tab;
static Stats g_prev;

static void Snapshot(void) { g_prev = g_tab.stats; }

static u64h Delta(uint64_t Stats::*f) { return (g_tab.stats.*f) - (g_prev.*f); }

static void ResetWorld(void)
{
	g_world = World();
	g_model.clear();
	g_model.flagHack = 0;
	g_tab.Reset();
}

// Una clave rapida tipica. Los CAMPOS van en bits disjuntos y la base fija vive
// arriba, en los bits 48..63: con una base que tocaba los bits 32..39 (el byte 0x44
// de 0x11223344..) el `|` del PC se ORaba contra ella y dos PCs distintos daban la
// MISMA clave (0x0300 y 0x0320 colapsaban), o sea las pruebas de tamano de lista
// mentian — de ahi "el modelo no tiene 8 refs (4)". Ahora: variante en 16..23, PC en 32..43 (los 12 bits del dominio,
// pc>>3 <= 0x7FF), base en 48..63. Ninguno toca el bit 1 (needExactMatch) ni los bits
// 12/13 (flagInfo), que son los que ~0x3002 borra: si los tocaran, dos claves
// "distintas" serian la misma bajo la mascara y el caso forzado no existiria.
static u64h KeyFor(unsigned pc, unsigned extraByte = 0)
{
	return 0xDEAD000000008000ull | ((u64h)(extraByte & 0xFFu) << 16) |
		   ((u64h)((pc >> 3) & 0xFFFu) << 32);
}

// Llena la casilla de `pc` por el camino legitimo y devuelve lo que memorizo.
static u64h ForgeGoodEntry(unsigned pc, unsigned needExact = 0, unsigned flagHack = 0)
{
	g_model.flagHack = flagHack;
	const MState st = MkState(KeyFor(pc), needExact, pc);
	const u64h host = g_world.newHost();
	const bool ok = g_tab.Record(MakeQuery(pc, st), host, 40u);
	CHECK(ok, "pc %04x: Record de una entrada valida devolvio false (exact=%u flagHack=%u)",
		pc, needExact, flagHack);
	return host;
}

// ==================================================================
// 2) Cada comprobacion de Serve, forzada una por una
// ==================================================================
static void testServeFields(void)
{
	// -- vacio --
	ResetWorld();
	{
		const MState st = MkState(KeyFor(0x0010u), 0, 1);
		uint64_t dummy = 0x777;
		Snapshot();
		CHECK(!g_tab.Serve(MakeQuery(0x0010u, st), &dummy), "Serve en tabla vacia dijo true");
		CHECK(Delta(&Stats::consults) == 1ull, "consults subio %" PRIu64 ", queria 1", Delta(&Stats::consults));
		CHECK(Delta(&Stats::rejectEmpty) == 1ull, "rejectEmpty subio %" PRIu64 ", queria 1",
			Delta(&Stats::rejectEmpty));
		CHECK(Delta(&Stats::serves) == 0ull, "se sirvio desde una casilla vacia");
		CHECK(dummy == 0x777ull, "Serve escribio outHostEntry sin acertar: %" PRIx64 "", dummy);
	}

	// -- acierto limpio: el contrato completo, para que los rechazos de abajo se
	//    lean como "un campo distinto" y no como "una entrada mala de fabrica" --
	ResetWorld();
	{
		const unsigned pc = 0x0100u;
		const u64h host = ForgeGoodEntry(pc);
		Snapshot();
		uint64_t got = 0;
		CHECK(g_tab.Serve(MakeQuery(pc, MkState(KeyFor(pc), 0, pc)), &got), "una entrada "
			"recien registrada no se sirvio: Serve y Record no hablan en los mismos terminos");
		CHECK(got == host, "sirvio %" PRIx64 ", queria %" PRIx64 "", got, host);
		CHECK(Delta(&Stats::serves) == 1ull, "serves subio %" PRIu64 ", queria 1", Delta(&Stats::serves));
		CHECK(Delta(&Stats::rejectEmpty) == 0ull && Delta(&Stats::rejectPc) == 0ull &&
			Delta(&Stats::rejectVu) == 0ull && Delta(&Stats::rejectEpoch) == 0ull &&
			Delta(&Stats::rejectProg) == 0ull && Delta(&Stats::rejectMgr) == 0ull &&
			Delta(&Stats::rejectKey) == 0ull && Delta(&Stats::rejectBounds) == 0ull,
			"un acierto limpio conto rechazos: las cuentas no describen lo que pasa");
	}

	// -- needExactMatch: sale ANTES de tocar la casilla --
	{
		const unsigned pc = 0x0108u;
		const u64h host = ForgeGoodEntry(pc);
		MState st = MkState(KeyFor(pc), 0, pc);
		st.needExact = 1; // la clave tal cual, pero pidiendo comparacion exacta
		Query q = MakeQuery(pc, st);
		Snapshot();
		uint64_t got = 0;
		CHECK(!g_tab.Serve(q, &got), "Serve acepto un despacho needExactMatch: esa rama "
			"hace compareState de 96 bytes y promocion MRU, y no esta memoizada");
		CHECK(Delta(&Stats::rejectExact) == 1ull, "rejectExact subio %" PRIu64 ", queria 1",
			Delta(&Stats::rejectExact));
		CHECK(Delta(&Stats::rejectEmpty) == 0ull && Delta(&Stats::rejectPc) == 0ull &&
			Delta(&Stats::serves) == 0ull,
			"el rechazo por exacto leyo la casilla: tiene que salir antes de mirar nada");
		// Y la casilla sigue buena para su despacho rapido.
		Snapshot();
		CHECK(g_tab.Serve(MakeQuery(pc, MkState(KeyFor(pc), 0, pc)), &got),
			"el rechazo por exacto danio la casilla");
		CHECK(got == host, "tras el rechazo por exacto la casilla sirve %" PRIx64 " y no %" PRIx64 "", got, host);
	}

	// -- pc: redundante con IdxOf biyectivo, y por eso se fuerza --
	{
		ResetWorld();
		ForgeGoodEntry(0x0110u);
		const MState st = MkState(KeyFor(0x0110u), 0, 0x0110u);
		Query q = MakeQuery(0x0110u, st);
		// Misma casilla, otro PC: es lo que pasaria si IdxOf dejara de ser inyectivo.
		q.pc = 0x4110u; // IdxOf(0x4110) == IdxOf(0x0110) == 34
		CHECK(Table::IdxOf(0x4110u) == Table::IdxOf(0x0110u), "el alias que esta prueba "
			"usa dejo de ser alias: la comprobacion de pc ya no prueba nada");
		Snapshot();
		uint64_t got = 0;
		CHECK(!g_tab.Serve(q, &got), "Serve sirvio la entrada del PC de al lado: la "
			"comprobacion de pc no esta haciendo su trabajo");
		CHECK(Delta(&Stats::rejectPc) == 1ull, "rejectPc subio %" PRIu64 ", queria 1", Delta(&Stats::rejectPc));
		CHECK(Delta(&Stats::serves) == 0ull, "se sirvio con el pc rechazado");
	}

	// -- vuId: la casilla no puede contestar por otro VU --
	{
		ResetWorld();
		ForgeGoodEntry(0x0118u);
		const MState st = MkState(KeyFor(0x0118u), 0, 0x0118u);
		Query q = MakeQuery(0x0118u, st);
		q.vuId = 0; // VU0 preguntando con la tabla de VU1
		Snapshot();
		uint64_t got = 0;
		CHECK(!g_tab.Serve(q, &got), "Serve contesto a otro VU");
		CHECK(Delta(&Stats::rejectVu) == 1ull, "rejectVu subio %" PRIu64 ", queria 1", Delta(&Stats::rejectVu));
	}

	// -- cacheEpoch: el cache se destruyo (mVUreset/mVUclose) --
	{
		ResetWorld();
		ForgeGoodEntry(0x0120u);
		const MState st = MkState(KeyFor(0x0120u), 0, 0x0120u);
		Query q = MakeQuery(0x0120u, st);
		q.cacheEpoch += 1;
		Snapshot();
		uint64_t got = 0;
		CHECK(!g_tab.Serve(q, &got), "Serve sirvio codigo de una epoca de cache distinta");
		CHECK(Delta(&Stats::rejectEpoch) == 1ull, "rejectEpoch subio %" PRIu64 ", queria 1 (cache)",
			Delta(&Stats::rejectEpoch));
	}

	// -- candidateEpoch: escribieron microcodigo (mVUclear) --
	{
		ResetWorld();
		ForgeGoodEntry(0x0128u);
		const MState st = MkState(KeyFor(0x0128u), 0, 0x0128u);
		Query q = MakeQuery(0x0128u, st);
		q.candidateEpoch += 1;
		Snapshot();
		uint64_t got = 0;
		CHECK(!g_tab.Serve(q, &got), "Serve sirvio con microcodigo escrito despues de registrar");
		CHECK(Delta(&Stats::rejectEpoch) == 1ull, "rejectEpoch subio %" PRIu64 ", queria 1 (candidato)",
			Delta(&Stats::rejectEpoch));
	}

	// -- progId: otro programa en el mismo PC --
	{
		ResetWorld();
		ForgeGoodEntry(0x0130u);
		const MState st = MkState(KeyFor(0x0130u), 0, 0x0130u);
		Query q = MakeQuery(0x0130u, st);
		q.progId += 0x10;
		Snapshot();
		uint64_t got = 0;
		CHECK(!g_tab.Serve(q, &got), "Serve contesto con el programa equivocado");
		CHECK(Delta(&Stats::rejectProg) == 1ull, "rejectProg subio %" PRIu64 ", queria 1", Delta(&Stats::rejectProg));
	}

	// -- mgrId: el gestor se destruyo y el allocator reasigno otra direccion --
	{
		ResetWorld();
		ForgeGoodEntry(0x0138u);
		const MState st = MkState(KeyFor(0x0138u), 0, 0x0138u);
		Query q = MakeQuery(0x0138u, st);
		q.mgrId += 0x10;
		Snapshot();
		uint64_t got = 0;
		CHECK(!g_tab.Serve(q, &got), "Serve contesto con el gestor equivocado");
		CHECK(Delta(&Stats::rejectMgr) == 1ull, "rejectMgr subio %" PRIu64 ", queria 1", Delta(&Stats::rejectMgr));
	}

	// -- flagHack: la mascara vigente cambio --
	{
		ResetWorld();
		ForgeGoodEntry(0x0140u, 0, 1); // registrada CON flaghack
		const MState st = MkState(KeyFor(0x0140u), 0, 0x0140u);
		Query q = MakeQuery(0x0140u, st);
		q.flagHack = 0; // y ahora el despacho corre sin el speedhack
		Snapshot();
		uint64_t got = 0;
		CHECK(!g_tab.Serve(q, &got), "Serve uso una respuesta registrada con otra mascara");
		CHECK(Delta(&Stats::rejectKey) == 1ull, "rejectKey subio %" PRIu64 ", queria 1 (flagHack)",
			Delta(&Stats::rejectKey));
	}

	// -- quickKey: el microcodigo/estado bajo la mascara ya no es ese --
	{
		ResetWorld();
		const unsigned pc = 0x0148u;
		ForgeGoodEntry(pc);
		const MState st = MkState(KeyFor(pc, 0), 0, pc);
		MState st2 = st;
		st2.quick = KeyFor(pc, 1); // cambia el byte 0 mas alla de la mascara
		CHECK((st2.quick & Table::MaskOf(0)) != (st.quick & Table::MaskOf(0)),
			"las dos claves de la prueba son iguales sin mascara: la comprobacion no dice nada");
		Snapshot();
		uint64_t got = 0;
		CHECK(!g_tab.Serve(MakeQuery(pc, st2), &got), "Serve sirvio para una clave de estado distinta");
		CHECK(Delta(&Stats::rejectKey) == 1ull, "rejectKey subio %" PRIu64 ", queria 1 (quickKey)",
			Delta(&Stats::rejectKey));
	}

	// -- y con flagHack puesto, una clave que solo difiere en los bits ENMASCARADOS
	//    tiene que servirse: es el speedhack funcionando, no un bug --
	{
		ResetWorld();
		const unsigned pc = 0x0150u;
		const u64h host = ForgeGoodEntry(pc, 0, 1);
		const u64h base = KeyFor(pc);
		const u64h soloEnmascarados = base | 0x3002ull;
		CHECK((soloEnmascarados & Table::MaskOf(1)) == (base & Table::MaskOf(1)),
			"0x3002 dejo de ser el enmascarado: esta comprobacion ya no prueba el speedhack");
		const MState st = MkState(soloEnmascarados, 0, pc);
		Snapshot();
		uint64_t got = 0;
		CHECK(g_tab.Serve(MakeQuery(pc, st), &got), "el memo rechazo dos claves que el "
			"barrido del speedhack considera la misma: negaria el acierto que el barrido da");
		CHECK(got == host, "acojo una clave enmascarada distinta pero sirvio otra entrada");
		CHECK(Delta(&Stats::serves) == 1ull, "serves=%" PRIu64 " con el speedhack", Delta(&Stats::serves));

		// blockType, en cambio, NO se enmascara: cambiarlo tiene que rechazar.
		// Se da la vuelta con XOR y no con OR: la base de KeyFor vive en los bits
		// 48..63 y ya trae el bit 48 puesto, asi que un OR ahi no mueve nada y la
		// "variante" seria la misma clave — el test se confirmiria a si mismo sin
		// probar nada. Las dos comprobaciones de abajo fijan que la variante es de
		// verdad otra clave Y que sigue cayendo fuera de la mascara.
		Snapshot();
		got = 0;
		MState stVariant = st;
		stVariant.quick = base ^ (1ull << (6 * 8));
		CHECK(stVariant.quick != base, "el bit del blockType esta fijo en la codificacion "
			"de KeyFor: esta prueba no puede variarlo");
		CHECK((stVariant.quick & Table::MaskOf(1)) != (base & Table::MaskOf(1)),
			"el blockType cayo dentro de la mascara del speedhack: entonces SI se "
			"enmascara, y la invariante de identidad que se creia pinada es otra");
		CHECK(!g_tab.Serve(MakeQuery(pc, stVariant), &got), "un despacho normal se sirvio "
			"la variante de un superbloque: la invariante de identidad del motor se rompio");
		CHECK(Delta(&Stats::rejectKey) == 1ull, "la variante no se rechazo por clave sino por otra cosa");
	}

	// -- bounds: ventana [start, end) y limpieza de la casilla --
	{
		ResetWorld();
		const unsigned pc = 0x0158u;
		const u64h host = ForgeGoodEntry(pc);
		const MState st = MkState(KeyFor(pc), 0, pc);
		Query q = MakeQuery(pc, st);

		// Borde bajo INCLUSIVO: la primera direccion del cache es codigo valido.
		q.cacheStart = host;
		Snapshot();
		uint64_t got = 0;
		CHECK(g_tab.Serve(q, &got) && got == host, "hostEntry == cacheStart se rechazo: "
			"la ventana tiene que empezar en cacheStart");

		// Borde alto EXCLUSIVO.
		q = MakeQuery(pc, st);
		q.cacheEnd = host;
		Snapshot();
		got = 0;
		CHECK(!g_tab.Serve(q, &got), "hostEntry == cacheEnd se sirvio: la ventana es [start, end)");
		CHECK(Delta(&Stats::rejectBounds) == 1ull, "rejectBounds subio %" PRIu64 ", queria 1",
			Delta(&Stats::rejectBounds));
		// Limpiar es parte del contrato: si no, la casilla se reintentaria cada despacho.
		Snapshot();
		CHECK(!g_tab.Serve(MakeQuery(pc, st), &got), "la casilla sobrevivio al rechazo por bounds");
		CHECK(Delta(&Stats::rejectEmpty) == 1ull, "el bounds no limpia la casilla: "
			"subio %" PRIu64 " rejectEmpty en vez de 1", Delta(&Stats::rejectEmpty));
		CHECK(g_tab.Count() == 0u, "Count=%u tras limpiar por bounds", g_tab.Count());

		// Fuera por arriba tambien.
		ResetWorld();
		const u64h host2 = ForgeGoodEntry(0x0160u);
		const MState st2 = MkState(KeyFor(0x0160u), 0, 0x0160u);
		Query q2 = MakeQuery(0x0160u, st2);
		q2.cacheEnd = host2; // el cache "encogio" por debajo de la entrada
		uint64_t got2 = 0;
		CHECK(!g_tab.Serve(q2, &got2), "una entrada sobre el limite alto se sirvio");
	}
}

// ==================================================================
// 3) Guardas de Record: lo que NO tiene que escribirse
// ==================================================================
static void testRecordGuards(void)
{
	// -- needExact: no se memoriza la rama que no se memoiza --
	ResetWorld();
	{
		const MState st = MkState(KeyFor(0x0200u), 1, 0x0200u);
		Snapshot();
		CHECK(!g_tab.Record(MakeQuery(0x0200u, st), g_world.cacheStart + 0x100, 40u),
			"Record acepto un despacho needExactMatch");
		CHECK(Delta(&Stats::recordSkipped) == 1ull, "recordSkipped=%" PRIu64 " queria 1 (exact)",
			Delta(&Stats::recordSkipped));
		CHECK(Delta(&Stats::records) == 0ull, "records subio con un despacho exacto");
		CHECK(g_tab.Count() == 0u, "Count=%u: el rechazo por exacto escribio la casilla",
			g_tab.Count());
		// Y la casilla esta realmente vacia: Serve tiene que decir rejectEmpty.
		MState stq = st;
		stq.needExact = 0;
		uint64_t got = 0;
		CHECK(!g_tab.Serve(MakeQuery(0x0200u, stq), &got), "la casilla 'rechazada' tiene contenido");
	}

	// -- VU distinto: esta tabla no contesta por VU0 aunque se lo pidan --
	{
		ResetWorld();
		g_world.vuId = 0;
		const MState st = MkState(KeyFor(0x0208u), 0, 0x0208u);
		Snapshot();
		CHECK(!g_tab.Record(MakeQuery(0x0208u, st), g_world.cacheStart + 0x100, 40u),
			"Record guardo una entrada de otro VU");
		CHECK(Delta(&Stats::recordSkipped) == 1ull, "recordSkipped=%" PRIu64 " queria 1 (vu)",
			Delta(&Stats::recordSkipped));
		CHECK(g_tab.Count() == 0u, "Count=%u tras el Record de VU0", g_tab.Count());
	}

	// -- barrido corto: memorizarlo costaria MAS que el barrido que reemplaza --
	{
		ResetWorld();
		const MState st = MkState(KeyFor(0x0210u), 0, 0x0210u);
		const Query q = MakeQuery(0x0210u, st);
		const u64h host = g_world.cacheStart + 0x400;
		for (unsigned pos = 0; pos < kMinRecordPos; pos++)
		{
			Snapshot();
			CHECK(!g_tab.Record(q, host, pos), "Record acepto un barrido de posicion %u "
				"(umbral %u): memorizar 1..%u comparaciones cambia comparaciones baratas "
				"por ~8 caras", pos, (unsigned)kMinRecordPos, (unsigned)(kMinRecordPos - 1));
			CHECK(Delta(&Stats::recordSkipped) == 1ull, "pos %u no conto recordSkipped", pos);
			CHECK(Delta(&Stats::records) == 0ull, "pos %u escribio la tabla", pos);
		}
		// Justo en el umbral SI: es el borde, y un umbral sin borde probado es prosa.
		Snapshot();
		CHECK(g_tab.Record(q, host, kMinRecordPos), "Record rechazo un barrido de posicion "
			"%u, exactamente el umbral", (unsigned)kMinRecordPos);
		CHECK(Delta(&Stats::records) == 1ull, "records=%" PRIu64 " en el umbral", Delta(&Stats::records));
		CHECK(g_tab.Count() == 1u, "Count=%u tras registrar en el umbral", g_tab.Count());
		// Y esa entrada se sirve.
		uint64_t got = 0;
		CHECK(g_tab.Serve(q, &got) && got == host, "lo registrado en el umbral no se sirve");
	}

	// -- puntero fuera de la ventana: no se escribe nada que no sea ejecutable --
	{
		ResetWorld();
		const MState st = MkState(KeyFor(0x0218u), 0, 0x0218u);
		const Query q = MakeQuery(0x0218u, st);
		Snapshot();
		CHECK(!g_tab.Record(q, g_world.cacheEnd, kMinRecordPos), "Record guardo una "
			"entrada justo en el limite alto de la ventana");
		CHECK(!g_tab.Record(q, g_world.cacheStart - 0x40, kMinRecordPos), "Record guardo "
			"una entrada antes del principio del cache");
		CHECK(Delta(&Stats::recordSkipped) == 2ull, "recordSkipped=%" PRIu64 " queria 2 (bounds)",
			Delta(&Stats::recordSkipped));
		CHECK(g_tab.Count() == 0u, "Count=%u: los bounds de Record escribieron la tabla",
			g_tab.Count());
		// En cacheStart si: es codigo valido del cache vivo.
		CHECK(g_tab.Record(q, g_world.cacheStart, kMinRecordPos), "Record rechazo el "
			"primer byte del cache");
	}

	// -- sobrescribir una casilla viva: la respuesta nueva es la ultima palabra --
	{
		ResetWorld();
		const unsigned pc = 0x0220u;
		const u64h h1 = ForgeGoodEntry(pc);
		const u64h h2 = ForgeGoodEntry(pc); // misma clave, otra entrada: re-Registro legitimo
		CHECK(h1 != h2, "el mundo no genera dos entradas distintas");
		uint64_t got = 0;
		CHECK(g_tab.Serve(MakeQuery(pc, MkState(KeyFor(pc), 0, pc)), &got) && got == h2,
			"tras re-Registrar la casilla sigue sirviendo la entrada vieja");
		CHECK(g_tab.Count() == 1u, "re-Registrar conto dos casillas vivas: Count=%u", g_tab.Count());
	}
}

// ==================================================================
// 4) Ley de costo, como comportamiento del llamador compuesto
// ==================================================================
static void testCostLaw(void)
{
	// La otra mitad de la ley de costo: con la lista corta el despacho NO pregunta, y
	// un acierto en posicion corta NO se registra. Las dos guardas juntas son lo que
	// hace que esta seccion no pueda ser mas lenta que el codigo que reemplaza.
	ResetWorld();

	// Ocho PCs distintos, ocho claves no equivalentes: add() inserta los ocho, asi que
	// la lista llega a tamano 8 y los aciertos caen en posiciones 0..7 — todas por
	// debajo del umbral salvo la ultima.
	static constexpr unsigned kPcs[8] = { 0x0300u, 0x0308u, 0x0310u, 0x0318u,
										  0x0320u, 0x0328u, 0x0330u, 0x0338u };
	for (unsigned i = 0; i < 8u; i++)
		g_model.add(MkState(KeyFor(kPcs[i]), 0, i), g_world.newHost(), kPcs[i]);
	CHECK(g_model.lookup.size() == 8u, "el modelo no tiene 8 refs (%u): las claves de "
		"KeyFor dejaron de ser distintas y esta prueba no llega al umbral",
		(unsigned)g_model.lookup.size());

	// -- lista POR DEBAJO del umbral: no se pregunta --
	{
		Model shortModel; // 7 refs
		shortModel.clear();
		for (unsigned i = 0; i + 1u < 8u; i++)
			shortModel.add(MkState(KeyFor(kPcs[i]), 0, i), g_world.newHost(), kPcs[i]);
		std::swap(g_model.lookup, shortModel.lookup); // el modelo pasa a tener 7
		Snapshot();
		for (unsigned i = 0; i < 7u; i++)
		{
			const Dispatch d = DispatchLikeTheCaller(kPcs[i], MkState(KeyFor(kPcs[i]), 0, i), g_tab);
			CHECK(!d.asked, "con %u refs el despacho pregunto al memo: la guarda de "
				"tamano no esta actuando", (unsigned)g_model.lookup.size());
			CHECK(d.path == 2, "lista corta: no se resolvio por el barrido (path=%d)", d.path);
		}
		CHECK(g_tab.stats.consults == 0ull, "el memo fue consultado con la lista corta: "
			"consults=%" PRIu64, g_tab.stats.consults);
		// Cenit 0.6.36 — y aqui esta la cuenta que antes no existia: los siete
		// despachos se contaron como skipShort, no desaparecieron. Sin estos tres
		// CHECK el log del telefono podria decir skip-short=0 con una lista corta y
		// nadie lo veria; con ellos, el denominador es una medida y no una esperanza.
		CHECK(g_tab.stats.lookups == 7ull, "lookups=%" PRIu64 " con 7 despachos de lista "
			"corta: el denominador no se sumo", g_tab.stats.lookups);
		CHECK(g_tab.stats.skipShort == 7ull, "skip-short=%" PRIu64 " queria 7",
			g_tab.stats.skipShort);
		CHECK(g_tab.stats.lookups == g_tab.stats.consults + g_tab.stats.skipExact +
			g_tab.stats.skipShort, "INVARIANTE roto en la lista corta: lookups=%" PRIu64
			" != consults+skip-exact+skip-short=%" PRIu64, g_tab.stats.lookups,
			g_tab.stats.consults + g_tab.stats.skipExact + g_tab.stats.skipShort);
	}

	// -- lista EN el umbral: se pregunta, y el acierto en posicion 7 (0-based) sigue
	//    siendo corto, o sea NO se registra --
	{
		Model tmp;
		tmp.clear();
		for (unsigned i = 0; i < 8u; i++)
			tmp.add(MkState(KeyFor(kPcs[i]), 0, i), g_world.newHost(), kPcs[i]);
		std::swap(g_model.lookup, tmp.lookup); // el modelo vuelve a tener 8
		Snapshot();
		const Dispatch d = DispatchLikeTheCaller(kPcs[7], MkState(KeyFor(kPcs[7]), 0, 7u), g_tab);
		CHECK(d.asked, "con 8 refs el despacho todavia no pregunta: el guard usa el "
			"borde equivocado");
		CHECK(Delta(&Stats::consults) == 1ull, "consults=%" PRIu64 " en el umbral",
			Delta(&Stats::consults));
		CHECK(d.walkPos == 7u, "el ultimo bloque cayo en posicion %u, queria 7", d.walkPos);
		CHECK(!d.recorded, "un acierto en posicion 7 se registro: el umbral esta un "
			"posicion mas abajo de lo que dice su comentario");
		CHECK(g_tab.stats.records == 0ull, "records=%" PRIu64 " con aciertos cortos",
			g_tab.stats.records);
		// La segunda vez tampoco sirve: nunca se memorizo.
		const Dispatch d2 = DispatchLikeTheCaller(kPcs[7], MkState(KeyFor(kPcs[7]), 0, 7u), g_tab);
		CHECK(d2.path != 1, "el memo sirvio algo que nunca registro");
	}

	// -- despacho EXACTO por el llamador compuesto: se cuenta en skipExact y ni se
	//    mira la tabla. Va por aqui y no solo por expectReject (que llama a Serve a
	//    mano) porque lo que el telefono imprime es la decision del llamador; un
	//    skipExact=0 con un juego que despacha exacto seria un informe mintiendo por
	//    omision. --
	{
		Snapshot();
		const unsigned nLookups = 3u;
		for (unsigned i = 0; i < nLookups; i++)
		{
			const Dispatch d = DispatchLikeTheCaller(kPcs[i], MkState(KeyFor(kPcs[i]), 1, i), g_tab);
			CHECK(!d.asked, "exacto %u pregunto al memo: la guarda de dominio no esta actuando", i);
			CHECK(d.path != 1, "exacto %u se sirvio del memo", i);
		}
		CHECK(Delta(&Stats::skipExact) == nLookups, "skip-exact=%" PRIu64 " queria %u",
			Delta(&Stats::skipExact), nLookups);
		CHECK(Delta(&Stats::skipShort) == 0ull, "skip-short=%" PRIu64 " con exactos: el "
			"motivo se eligo mal (un exacto con lista larga es kSkipExact, no kSkipShort)",
			Delta(&Stats::skipShort));
		CHECK(Delta(&Stats::consults) == 0ull, "consults subio con despachos exactos");
		CHECK(Delta(&Stats::rejectExact) == 0ull, "rejectExact subio %" PRIu64 " en el "
			"camino normal: en produccion vale 0 y moverlo es la guarda del llamador rota, "
			"no una medida", Delta(&Stats::rejectExact));
		CHECK(g_tab.stats.lookups ==
			g_tab.stats.consults + g_tab.stats.skipExact + g_tab.stats.skipShort,
			"INVARIANTE roto tras los exactos: lookups=%" PRIu64 " vs suma=%" PRIu64,
			g_tab.stats.lookups,
			g_tab.stats.consults + g_tab.stats.skipExact + g_tab.stats.skipShort);
	}

	// -- y con un bloque en posicion 8 (lista de 9) si se registra y si se sirve --
	{
		const unsigned pc = 0x0340u;
		g_model.add(MkState(KeyFor(pc), 0, 8u), g_world.newHost(), pc);
		CHECK(g_model.lookup.size() == 9u, "la lista quedo en %u, queria 9",
			(unsigned)g_model.lookup.size());
		Snapshot();
		const Dispatch d = DispatchLikeTheCaller(pc, MkState(KeyFor(pc), 0, 8u), g_tab);
		CHECK(d.walkPos == 8u, "el bloque nuevo cayo en %u, queria 8 (es el umbral)",
			d.walkPos);
		CHECK(d.recorded, "un acierto justo en el umbral no se registro");
		const Dispatch d2 = DispatchLikeTheCaller(pc, MkState(KeyFor(pc), 0, 8u), g_tab);
		CHECK(d2.path == 1, "la segunda vez no la sirvio el memo (path=%d)", d2.path);
		CHECK(d2.memoIdx == d.walkIdx, "el memo sirvio el bloque %d y el barrido habria "
			"devuelto el %d", d2.memoIdx, d.walkIdx);
	}
}

// ==================================================================
// 5) Invalidate vs Reset: la telemetria es de sesion entera
// ==================================================================
static void testInvalidateVsReset(void)
{
	ResetWorld();
	ForgeGoodEntry(0x0400u);
	ForgeGoodEntry(0x0408u);
	const u64h consultsAntes = g_tab.stats.consults;
	const u64h recordsAntes = g_tab.stats.records;
	const u64h invalidacionesAntes = g_tab.stats.invalidations;
	CHECK(g_tab.Count() == 2u, "Count=%u con dos entradas puestas", g_tab.Count());

	// Invalidate: las respuestas se van, las cuentas se quedan. mVUreset pasa por aqui
	// en CADA cambio de config y en cada carga de estado; borrar las cuentas ahi
	// dejaria el informe del cierre leyendo "casi nadie uso el memo".
	g_tab.Invalidate();
	CHECK(g_tab.Count() == 0u, "Invalidate dejo %u casillas vivas", g_tab.Count());
	CHECK(g_tab.stats.invalidations == invalidacionesAntes + 1ull, "invalidations no subio");
	CHECK(g_tab.stats.consults == consultsAntes && g_tab.stats.records == recordsAntes,
		"Invalidate borro contadores (consults=%" PRIu64 " records=%" PRIu64 "): la telemetria dejo "
		"de ser de sesion", g_tab.stats.consults, g_tab.stats.records);

	// Y tras invalidar no se sirve nada viejo: la casilla esta libre.
	uint64_t got = 0;
	const MState st = MkState(KeyFor(0x0400u), 0, 0x0400u);
	Snapshot();
	CHECK(!g_tab.Serve(MakeQuery(0x0400u, st), &got), "Invalidate no vacio la casilla de verdad");
	CHECK(Delta(&Stats::rejectEmpty) == 1ull, "tras Invalidate el rechazo no fue por vacio");

	// Reset: cuentas a cero (solo mVUinit).
	g_tab.Reset();
	CHECK(g_tab.stats.consults == 0ull && g_tab.stats.invalidations == 0ull &&
		g_tab.stats.records == 0ull && g_tab.stats.serves == 0ull,
		"Reset no limpio las cuentas: consults=%" PRIu64 " invalidations=%" PRIu64 "",
		g_tab.stats.consults, g_tab.stats.invalidations);
	CHECK(g_tab.Count() == 0u, "Reset dejo casillas vivas");
}

// ==================================================================
// 6) El argumento (a) del encabezado, FORZADO: dos claves que coinciden bajo una
//    mascara y no bajo la otra. add() las acepta SOLO si la mascara cambio entre
//    las dos inserciones (deduplica con la vigente), y eso es justo lo que la
//    comprobacion de flagHack tiene que cubrir.
// ==================================================================
static void testCoexistenciaMascara(void)
{
	// ---- caso 1: la mascara cambio entre dos inserciones, asi que dos refs
	// equivalentes bajo la mascara nueva conviven en quickLookup. Eso es lo que add()
	// produce de verdad (deduplica con la mascara VIGENTE), y es el unico modo de
	// tener dos respuestas posibles para una misma clave enmascarada. El memo tiene
	// que decir lo que dice el barrido: el PRIMER match, y al cambiar la mascara
	// tiene que CALLAR (flagHack en la clave) en vez de servir la del otro mundo.
	ResetWorld();
	g_model.flagHack = 1;
	const unsigned pc = 0x0500u;
	const u64h kX = KeyFor(pc);
	const u64h kY = kX | 0x1000ull; // flagInfo bit 12: ENMASCARADO por ~0x3002
	CHECK((kX & Table::MaskOf(1)) == (kY & Table::MaskOf(1)),
		"kX y kY dejaron de ser equivalentes bajo la mascara: esta prueba ya no "
		"reproduce la coexistencia");
	CHECK((kX & Table::MaskOf(0)) != (kY & Table::MaskOf(0)),
		"kX y kY son la misma clave exacta: sin ese contraste la prueba no distingue");
	const u64h hostX = DifHost();
	CHECK(g_model.add(MkState(kX, 0, 1), hostX, pc) == 0, "no se pudo anadir X");
	// Cambia la config del recompiler (mVUsFlagHack a 0) SIN reset de cache: la clave
	// de Y ya no coincide con la de X y add() la inserta.
	g_model.flagHack = 0;
	const u64h hostY = DifHost();
	CHECK(g_model.add(MkState(kY, 0, 2), hostY, pc) == 1, "Y debio insertarse sin mascara");
	CHECK(g_model.lookup.size() == 2u, "coexistencia no dada: %u refs",
		(unsigned)g_model.lookup.size());
	// Y ahora la mascara vuelve: los dos refs son equivalentes bajo ~0x3002.
	g_model.flagHack = 1;

	unsigned pos = 0;
	CHECK(g_model.searchQuick(kY, &pos) == 0 && pos == 0u,
		"el barrido con mascara no devolvio el PRIMERO (idx/pos %d/%u): el modelo ya no "
		"se parece a search()", g_model.searchQuick(kY, &pos), pos);

	Table& tab = g_tab;
	// El memo se llena con el mundo de la mascara puesta: respuesta = X.
	CHECK(tab.Record(MakeQuery(pc, MkState(kY, 0, 2)), g_model.lookup[0].host, 40u),
		"no se pudo registrar la respuesta del mundo con mascara");
	uint64_t got = 0;
	Snapshot();
	CHECK(tab.Serve(MakeQuery(pc, MkState(kY, 0, 2)), &got) && got == hostX,
		"con mascara el memo no reproduce al primer match del barrido");
	CHECK(Delta(&Stats::serves) == 1ull, "no conto el acierto con mascara");

	// El usuario apaga el speedhack sin reset: el barrido ahora elegiria Y. El memo
	// tiene que rechazar por flagHack, no servir la respuesta del mundo anterior.
	g_model.flagHack = 0;
	Snapshot();
	got = 0;
	CHECK(!tab.Serve(MakeQuery(pc, MkState(kY, 0, 2)), &got),
		"el memo sirvo X (respuesta del mundo con mascara) a un despacho cuyo barrido "
		"elige Y: saltaria a microcodigo que nadie pidio");
	CHECK(Delta(&Stats::rejectKey) == 1ull, "el cruce de mascaras no cayo en rejectKey");

	// Re-Registrado en el mundo nuevo, la casilla responde Y.
	CHECK(tab.Record(MakeQuery(pc, MkState(kY, 0, 2)), g_model.lookup[1].host, 40u),
		"re-Registro sin mascara rechazo");
	got = 0;
	CHECK(tab.Serve(MakeQuery(pc, MkState(kY, 0, 2)), &got) && got == hostY,
		"tras re-Registrar sin mascara sigue contestando con el bloque viejo");

	// ---- caso 2 (el subtley que hace que la clave se guarde ENMASCARADA): un bloque
	// almacenado como needExactMatch vive en quickLookup igual que los rapidos, y
	// bajo flagHack su clave enmascarada puede casar con un despacho RAPIDO. El memo
	// tiene que memorizar ESA respuesta, porque es la que el barrido da.
	ResetWorld();
	g_model.flagHack = 1;
	const unsigned pc2 = 0x0508u;
	const u64h kBase = KeyFor(pc2);
	MState exact = MkState(kBase, 1, 7u);
	CHECK(exact.needExact == 1u, "MkState no puso el bit 1: la fidelidad con la union "
		"microRegInfo se perdio");
	const u64h hostE = DifHost();
	CHECK(g_model.add(exact, hostE, pc) == 0, "no se pudo anadir el bloque exacto");
	CHECK(g_model.lookup.size() == 1u, "add() no mete los exactos en quickLookup: la "
		"premis del caso se cayo (%u refs)", (unsigned)g_model.lookup.size());
	// Despacho RAPIDO con la misma clave sin el bit 1: el barrido lo resuelve.
	const MState quickAsk = AsQuickDispatch(exact);
	pos = 0;
	CHECK(g_model.searchQuick(quickAsk.quick, &pos) == 0,
		"el barrido rapido NO resolvio el bloque exacto bajo flagHack: o la mascara "
		"dejo de cubrir el bit 1, o el modelo miente; en cualquier caso el memo no "
		"deberia memorizar esto");
	CHECK(tab.Record(MakeQuery(pc2, quickAsk), g_model.lookup[0].host, 40u),
		"el memo no memoriza una respuesta rapida que recae en un bloque exacto");
	got = 0;
	Snapshot();
	CHECK(tab.Serve(MakeQuery(pc2, quickAsk), &got) && got == hostE,
		"el memo no sirve el bloque exacto al que el barrido rapido llega");
	CHECK(Delta(&Stats::serves) == 1ull, "no conto el acierto sobre el bloque exacto");
	// Y en el mundo SIN mascara el mismo despacho no resuelve nada: el memo calla.
	g_model.flagHack = 0;
	pos = 0;
	CHECK(g_model.searchQuick(quickAsk.quick, &pos) == -1,
		"sin mascara el barrido encontro algo: la mascara dejo de importar");
	Snapshot();
	got = 0;
	CHECK(!tab.Serve(MakeQuery(pc2, quickAsk), &got),
		"el memo sirvio la respuesta del mundo con mascara a un despacho que hoy es miss");
	CHECK(Delta(&Stats::rejectKey) == 1ull, "el miss cruzado no cayo en rejectKey");
}

// ==================================================================
// 7) Invalidacion inmediata, con el MOTOR DEL RECHAZO NOMBRADO. Cada cambio que la
//    especificacion exige invalidar se fuerza sobre una entrada viva, y no basta con
//    que Serve diga false: tiene que decirlo POR LA RAZON QUE TOCA (el contador que
//    sube). Un memo que rechaza por el motivo equivocado esta dando aciertos por
//    accidente, y los aciertos por accidente se convierten en servidos cuando alguien
//    mueve el campo que casualidad los estaba parando.
// ==================================================================
static void expectReject(const char* what, unsigned pc, const MState& st, uint64_t Stats::*reason)
{
	Snapshot();
	uint64_t got = 0;
	CHECK(!g_tab.Serve(MakeQuery(pc, st), &got), "%s: Serve siguio dando una entrada", what);
	CHECK(Delta(reason) == 1ull, "%s: el rechazo no conto en la razon que le corresponde "
		"(subio %" PRIu64 " en la cuenta correcta)", what, Delta(reason));
	CHECK(Delta(&Stats::serves) == 0ull, "%s: y aun asi se sirvio", what);
}

static void testInvalidationReasons(void)
{
	// Cada invalidacion que pide la especificacion, forzada sobre una entrada viva y
	// verificada EN LA CUENTA QUE LE TOCA.
	const unsigned pc = 0x0700u;
	const unsigned idx = Table::IdxOf(pc);

	// -- control positivo: sin cambio alguno, Serve acierta. Sin esto, los rechazos de
	//    abajo no distinguirian "invalidado" de "nunca funciono". --
	ResetWorld();
	const u64h host = ForgeGoodEntry(pc);
	{
		Snapshot();
		uint64_t got = 0;
		CHECK(g_tab.Serve(MakeQuery(pc, MkState(KeyFor(pc), 0, pc)), &got) && got == host,
			"control: una entrada recien forjada no se sirvio");
		CHECK(Delta(&Stats::serves) == 1ull, "control: serves=%" PRIu64 ", queria 1",
			Delta(&Stats::serves));
	}

	// -- cambio de microcodigo: candidateEpoch (mVUclear) --
	{
		g_world.candidateEpoch++;
		expectReject("microcodigo escrito", pc, MkState(KeyFor(pc), 0, pc), &Stats::rejectEpoch);
	}

	// -- cambio de programa en el mismo PC --
	{
		ResetWorld();
		ForgeGoodEntry(pc);
		g_world.progId += 0x20;
		expectReject("programa distinto en el mismo PC", pc, MkState(KeyFor(pc), 0, pc),
			&Stats::rejectProg);
	}

	// -- gestor destruido y reasignado (mgrId cambia sin que cambie la epoca) --
	{
		ResetWorld();
		ForgeGoodEntry(pc);
		g_world.mgrId += 0x20;
		expectReject("gestor reasignado", pc, MkState(KeyFor(pc), 0, pc), &Stats::rejectMgr);
	}

	// -- configuracion del recompiler: el speedhack de banderas (mascara) --
	{
		ResetWorld();
		ForgeGoodEntry(pc, 0, 1);
		g_model.flagHack = 0; // el usuario apago el speedhack sin reset
		expectReject("mascara del flaghack volteada", pc, MkState(KeyFor(pc), 0, pc),
			&Stats::rejectKey);
	}

	// -- PC: la comprobacion que la especificacion pide aunque el indice sea biyectivo --
	{
		ResetWorld();
		ForgeGoodEntry(pc);
		MState st = MkState(KeyFor(pc), 0, pc);
		Query q = MakeQuery(pc, st);
		q.pc = 0x0708u; // casilla 34 -> 35: otro PC, otra casilla; se prueba el campo
		Snapshot();
		uint64_t got = 0;
		CHECK(!g_tab.Serve(q, &got), "un PC distinto recibio la entrada de su vecino");
		CHECK(Delta(&Stats::rejectEmpty) == 1ull, "el PC vecino no cayo en su casilla "
			"libre (subio %" PRIu64 " rejectEmpty)", Delta(&Stats::rejectEmpty));
		// Y el alias dentro de la misma casilla, que es lo que rejectPc protege:
		q.pc = 0x4700u; // IdxOf(0x4700) == IdxOf(0x0700)
		CHECK(Table::IdxOf(0x4700u) == idx, "el alias de esta prueba dejo de alias");
		Snapshot();
		CHECK(!g_tab.Serve(q, &got), "el alias pliego y recibo la entrada del otro PC");
		CHECK(Delta(&Stats::rejectPc) == 1ull, "rejectPc=%" PRIu64 ", queria 1", Delta(&Stats::rejectPc));
	}

	// -- cambio de MTVU / EECycleRate / EECycleSkip / reset de VU / destruccion del
	//    code cache: los seis pasan por mVUreset, y ahi sube la epoca del cache. Eso
	//    es lo que se modela aqui: epoca nueva => ninguna entrada vieja vale.
	for (unsigned i = 0; i < 6; i++)
	{
		ResetWorld();
		ForgeGoodEntry(pc);
		g_world.cacheEpoch += 1; // el bump de mVUreset/mVUclose
		expectReject("epoca de cache movida (camino de reset/config)", pc,
			MkState(KeyFor(pc), 0, pc), &Stats::rejectEpoch);
	}

	// -- y el Invalidate() real de mVUreset deja la casilla libre, no pendiente --
	{
		ResetWorld();
		ForgeGoodEntry(pc);
		g_world.cacheEpoch++;
		g_tab.Invalidate();
		expectReject("Invalidate() de mVUreset", pc, MkState(KeyFor(pc), 0, pc),
			&Stats::rejectEmpty);
	}

	// -- ramas: un despacho con needExactMatch jamas pregunta ni aprovecha --
	{
		ResetWorld();
		ForgeGoodEntry(pc);
		const MState st = MkState(KeyFor(pc), 1, pc);
		expectReject("despacho exacto", pc, st, &Stats::rejectExact);
		// y Record tampoco lo memoriza
		Snapshot();
		CHECK(!g_tab.Record(MakeQuery(pc, st), host, 40u), "Record memorizo un exacto");
		CHECK(Delta(&Stats::recordSkipped) == 1ull, "el exacto no conto recordSkipped");
	}

	// -- VU: la tabla no contesta por VU0 --
	{
		ResetWorld();
		ForgeGoodEntry(pc);
		const MState st = MkState(KeyFor(pc), 0, pc);
		Query q = MakeQuery(pc, st);
		q.vuId = 0;
		Snapshot();
		uint64_t got = 0;
		CHECK(!g_tab.Serve(q, &got), "VU0 recibio una respuesta de la tabla de VU1");
		CHECK(Delta(&Stats::rejectVu) == 1ull, "rejectVu=%" PRIu64 ", queria 1", Delta(&Stats::rejectVu));
	}
}

// ==================================================================
// 8) DIFERENCIAL por ciclos contra el modelo. Dos invariantes, en las dos
//    direcciones:
//      (i) NINGUN ACIERTO DEL MEMO PUEDE SER FALSO: si Serve(q) dice true, el host
//          que da es el que el barrido lineal del modelo da para esa clave bajo esa
//          mascara. Un memo que acierta donde el barrido elegiria OTRO bloque salta a
//          codigo que nadie pidio: este es el rojo que importa.
//     (ii) CON EL CONTEXTO SIN MOVERSE EL MEMO TIENE QUE ACERTAR: lo que se registro
//          y cuyo contexto (epocas, programa, gestor, mascara, ventana) sigue igual
//          tiene que servirse. Si no, el memo es solo un costo y el verde del arnes
//          estaria tapando una optimizacion muerta.
//    Despues de cada despacho se auditan la casilla tocada y un muestreo; al cerrar
//    cada ciclo se auditan las 2048. Los eventos hostiles (mascara, microcodigo,
//    ventana, programa, reset) desincronizan la sombra ANTES de auditar: lo que el
//    arnes ya no puede afirmar no se le exige a la tabla — y la validacion de que el
//    rechazo llega POR LA RAZON CORRECTA es la seccion de arriba, dirigida.
// ==================================================================
struct Shadow
{
	bool valid = false;
	unsigned pc = 0;
	MState st;          // el estado con el que se despacho y se resolvio
	u64h host = 0;
	// Contexto CONGELADO al registrar: es lo que ShadowLive compara contra el mundo
	// vivo para saber si la casilla todavia debe servir o ya debe callar.
	u64h recordEpoch = 0, recordCandidate = 0, recordProg = 0, recordMgr = 0;
	u64h recordCacheStart = 0, recordCacheEnd = 0;
	unsigned recordFlagHack = 0;
};

static Shadow g_shadow[kSlots];
static Table g_difTab; // la tabla del diferencial, separada de la de las pruebas

static void ShadowClearAll(void)
{
	for (unsigned i = 0; i < kSlots; i++)
	{
		g_shadow[i].valid = false;
		g_shadow[i].host = 0;
	}
}



static Query ShadowQuery(const Shadow& sh) { return MakeQuery(sh.pc, sh.st); }

// Si el contexto en el que se registro la respuesta sigue siendo el contexto vivo, la
// casilla tiene que servir. Si algo se movio, la auditoria espera un rechazo.
static bool ShadowLive(const Shadow& sh)
{
	const Query q = ShadowQuery(sh);
	return q.cacheEpoch == sh.recordEpoch &&
		   q.candidateEpoch == sh.recordCandidate &&
		   q.progId == sh.recordProg &&
		   q.mgrId == sh.recordMgr &&
		   q.flagHack == sh.recordFlagHack &&
		   q.cacheStart == sh.recordCacheStart &&
		   q.cacheEnd == sh.recordCacheEnd;
}

// El oraculo: lo que el barrido lineal de verdad responderia para esa clave.
static u64h SweepAnswer(const Query& q, unsigned* outPos)
{
	const int idx = g_model.searchQuick(q.quick64, outPos);
	if (idx < 0)
		return 0;
	return g_model.lookup[(size_t)idx].host;
}

static unsigned g_wDifServe = 0, g_wDifDeclineSynced = 0, g_wDifLegitReject = 0;
static unsigned g_wDifAdds = 0, g_wDifExactAdds = 0, g_wDifMaskFlip = 0, g_wDifMicroWrite = 0;
static unsigned g_wDifCacheReset = 0, g_wDifProgChange = 0, g_wDifWindowMove = 0;
static unsigned g_wDifMgrReassign = 0;
static unsigned g_wDifShortWalk = 0, g_wDifMiss = 0, g_wDifDesync = 0, g_wDifAudits = 0;
static bool g_difBroken = false;

static void AuditSlot(const char* where, int step, unsigned slot)
{
	if (g_difBroken)
		return;
	const Shadow& sh = g_shadow[slot];
	if (!sh.valid)
		return;

	// La auditoria RE-despacha con el contexto VIVO: MakeQuery lee epocas, programa,
	// gestor, mascara y ventana de hoy, igual que haria el dispatcher. Congelar la
	// Query del registro seria auditar una foto vieja y no probar la invalidacion.
	const Query q = ShadowQuery(sh);
	unsigned pos = 0;
	const u64h expected = SweepAnswer(q, &pos);
	const bool live = ShadowLive(sh);
	const bool hostInWindow = sh.host >= q.cacheStart && sh.host < q.cacheEnd;

	const u64h servesAntes = g_difTab.stats.serves;
	uint64_t got = 0;
	const u64h antesEmpty = g_difTab.stats.rejectEmpty;
	const u64h antesPc = g_difTab.stats.rejectPc;
	const u64h antesVu = g_difTab.stats.rejectVu;
	const u64h antesEpoch = g_difTab.stats.rejectEpoch;
	const u64h antesProg = g_difTab.stats.rejectProg;
	const u64h antesMgr = g_difTab.stats.rejectMgr;
	const u64h antesKey = g_difTab.stats.rejectKey;
	const u64h antesBounds = g_difTab.stats.rejectBounds;
	const u64h antesExact = g_difTab.stats.rejectExact;
	const bool served = g_difTab.Serve(q, &got);
	const u64h rechazos = (g_difTab.stats.rejectEmpty - antesEmpty) +
						   (g_difTab.stats.rejectPc - antesPc) +
						   (g_difTab.stats.rejectVu - antesVu) +
						   (g_difTab.stats.rejectEpoch - antesEpoch) +
						   (g_difTab.stats.rejectProg - antesProg) +
						   (g_difTab.stats.rejectMgr - antesMgr) +
						   (g_difTab.stats.rejectKey - antesKey) +
						   (g_difTab.stats.rejectBounds - antesBounds) +
						   (g_difTab.stats.rejectExact - antesExact);

	if (served)
	{
		if (g_difTab.stats.serves != servesAntes + 1ull)
		{
			CHECK(false, "%s (paso %d): Serve dio true sin contar el acierto", where, step);
			g_difBroken = true;
			return;
		}
		// (i) NINGUN ACIERTO PUEDE SER FALSO.
		if (got != expected)
		{
			CHECK(false, "%s (paso %d): PC %04x — el memo sirve %" PRIx64 " y el barrido "
				"del modelo responde %" PRIx64 " (posicion %u, %u refs, mascara %s, "
				"contexto %s). Servir otro bloque es ejecutar microcodigo que nadie pidio.",
				where, step, sh.pc, got, expected, pos, (unsigned)g_model.lookup.size(),
				q.flagHack ? "~0x3002" : "exacta", live ? "vivo" : "MOVIDO");
			g_difBroken = true;
			return;
		}
		if (got != sh.host)
		{
			CHECK(false, "%s (paso %d): PC %04x — Serve contesto %" PRIx64 " y la sombra "
				"(lo que Record escribio) dice %" PRIx64, where, step, sh.pc, got, sh.host);
			g_difBroken = true;
			return;
		}
		g_wDifServe++;
		return;
	}

	// Rechazo. Dos cosas se exigen: que no se sirva (ya se cumplio) y que haya una
	// RAZON — exactamente un contador de rechazo tiene que haber subido. Un false sin
	// cuenta es un camino que la telemetria del telefono no puede explicar.
	if (rechazos != 1ull)
	{
		CHECK(false, "%s (paso %d): PC %04x — Serve rechazo sin contar NINGUN motivo "
			"(subieron %" PRIu64 "): la telemetria y el control se separaron",
			where, step, sh.pc, rechazos);
		g_difBroken = true;
		return;
	}

	// (ii) CON TODO EL CONTEXTO VIVO Y EL BARRIDO RESPONDIENDO DENTRO DE LA VENTANA,
	// el memo tiene que servir. Si no, es solo un costo.
	if (live && expected != 0 && hostInWindow)
	{
		g_wDifDeclineSynced++;
		CHECK(false, "%s (paso %d): PC %04x — el memo nego su propia respuesta con el "
			"contexto vivo y el barrido dando %" PRIx64 " en pos %u. Sin servir, el memo "
			"es solo costo.", where, step, sh.pc, expected, pos);
		g_difBroken = true;
		return;
	}
	g_wDifLegitReject++;

	// Un rechazo por bounds LIMPIA la casilla (contrato de Serve): la sombra ya no
	// describe nada. Los demas rechazos no: la entrada sigue ahi y puede volver a ser
	// servible si el contexto regresa (la mascara del speedhack es exactamente ese
	// caso, y retirar la sombra ahi seria dejar de auditar lo interesante).
	if (g_difTab.stats.rejectBounds != antesBounds)
	{
		g_shadow[slot].valid = false;
		g_wDifDesync++;
	}
}

// Auditoria completa del ciclo: las 2048 casillas. Cara, pero es la que garantiza
// que ninguna entrada quedada se sirva falsa. Se hace una vez por ciclo (el barrido
// del modelo es lineal, 2048 x refs por ciclo) y una casilla por despacho.
static void AuditAll(const char* where, int step)
{
	if (g_difBroken)
		return;
	for (unsigned slot = 0; slot < kSlots; slot++)
		AuditSlot(where, step, slot);
	g_wDifAudits++;
}

static void testDifferential(void)
{
	ResetWorld();
	g_difTab.Reset();
	ShadowClearAll();
	g_difBroken = false;

	static constexpr unsigned kCycles = 14u;
	static constexpr unsigned kDispatches = 420u;

	for (unsigned cycle = 0; cycle < kCycles && !g_difBroken; cycle++)
	{
		// ---- fase 1: la compilacion va llenando el gestor. El memo solo entra en
		// juego cuando la lista pasa el umbral, asi que se llena siempre por encima, y
		// con PCs del mazo (distintos entre si) para que add() inserte de verdad.
		DeckRestart();
		const unsigned target = kMinRecordPos + 6u + rndBelow(40u);
		while (g_model.lookup.size() < target)
		{
			const unsigned pc = DeckPc();
			const MState st = MkState(DeckKey(), 0,
				rndBelow(4000u) + (unsigned)g_model.lookup.size());
			const int before = (int)g_model.lookup.size();
			g_model.add(st, DifHost(), pc);
			CHECK((int)g_model.lookup.size() > before, "ciclo %u: add() de un PC %04x "
				"nuevo no inserto — el mazo esta dando PCs repetidos y la lista no crece",
				cycle, pc);
			g_wDifAdds++;
		}

		// Algunos bloques exactos conviviendo: add() los mete en quickLookup igual,
		// asi que bajo flagHack pueden ser el primer match de una clave rapida.
		for (unsigned i = 0; i < 6u; i++)
		{
			const MState st = MkState(DeckKey(), 1, rndBelow(4000u));
			const int before = (int)g_model.lookup.size();
			g_model.add(st, DifHost(), DeckPc());
			if ((int)g_model.lookup.size() > before)
				g_wDifExactAdds++;
		}

		// ---- fase 2: despachos, con eventos hostiles intercalados
		for (unsigned k = 0; k < kDispatches && !g_difBroken; k++)
		{
			const unsigned roll = rndBelow(100u);

			if (roll == 0u)
			{
				g_model.flagHack = g_model.flagHack ? 0u : 1u;
				g_wDifMaskFlip++;
				// NO se retiran las sombras al voltear la mascara. Retirarlas seria
				// taparse los ojos: la mascara es justamente una de las cosas que la
				// especificacion exige invalidar, y la unica forma de probarlo es
				// AUDITAR con el contexto cambiado. La auditoria re-pregunta con la
				// mascara de hoy y exige que el rechazo suba rejectKey.
				continue;
			}
			if (roll == 1u)
			{
				g_world.candidateEpoch++; // mVUclear
				g_wDifMicroWrite++;
				continue; // se audita, no se retira: rejectEpoch es la razon que se espera
			}
			if (roll == 2u)
			{
				g_world.progId += 0x100; // otro programa en el mismo PC
				g_wDifProgChange++;
				continue; // rejectProg
			}
			if (roll == 5u)
			{
				// El caso que NO puede faltar: programa destruido y reasignado por el
				// allocator, o sea la MISMA direccion con otro mundo detras, sin que se
				// mueva ninguna epoca ni la mascara. reset() del gestor es vaciar
				// quickLookup, y aqui solo se resetea el gestor del programa viejo; al
				// reasignar la direccion, la unica cosa que distingue la entrada memoizada
				// de un gestor nuevo es el campo mgrId. Si alguien borra esa comprobacion
				// de Serve, este evento es el que la caza.
				g_world.mgrId += 0x40;
				g_model.clear();
				g_wDifMgrReassign++;
				continue; // rejectMgr
			}
			if (roll == 3u)
			{
				// la ventana se mueve SIN subir la epoca: el bounds de Serve es lo
				// unico que queda, y por eso se prueba aqui y no solo arriba
				g_world.cacheStart += 0x100000;
				g_world.cacheEnd = g_world.cacheStart + 0x2000000;
				g_wDifWindowMove++;
				continue; // rejectBounds; y la auditoria limpia la casilla por su cuenta
			}
			if (roll == 4u)
			{
				// reset del code cache: epoca, Invalidate y gestor nuevo (mVUreset)
				g_world.cacheEpoch++;
				g_difTab.Invalidate();
				ShadowClearAll();
				g_model.clear();
				g_wDifCacheReset++;
				continue;
			}

			// despacho por el camino del llamador
			unsigned pc;
			MState st;
			if (k % 40u == 7u)
			{
				// clave imposible (quick64 = 0 no coincide con ninguna KeyFor ni bajo
				// la mascara exacta ni bajo ~0x3002): fuerza el MISS, que es el camino
				// "no esta compilado todavia" y tiene que seguir yendo al barrido.
				pc = rndBelow(kSlots) * 8u;
				st = MkState(0ull, 0, 0u);
			}
			else if (!g_model.lookup.empty() && (xs() & 3u) != 0u)
			{
				// una clave ya compilada: el caso caliente de verdad
				const size_t pick = (size_t)rndBelow((unsigned)g_model.lookup.size());
				st = g_model.lookup[pick].st;
				st.needExact = 0; // el despacho de entrada no pide match exacto
				// EL PC DE LA BLOQUE: la clave del memo es (slot(pc), maskedKey). Un
				// PC distinto mira otro slot y el memo no puede servir, aunque la
				// clave sea la misma. Este era el fallo que dejaba servidos=0.
				pc = g_model.lookup[pick].pc;
			}
			else
			{
				pc = rndBelow(kSlots) * 8u;
				st = MkState(KeyFor(pc, rndBelow(256u)), 0, rndBelow(4000u));
			}

			const Dispatch d = DispatchLikeTheCaller(pc, st, g_difTab);
			const unsigned slot = Table::IdxOf(pc);

			if (d.path == 2 && d.walkIdx >= 0)
			{
				if (d.walkPos < kMinRecordPos)
					g_wDifShortWalk++;
				if (d.recorded)
				{
					// Record PISO la casilla: la sombra tiene que decir lo que la tabla
					// dice ahora. Si se quedara la anterior, la auditoria llamaria al
					// memo por una respuesta que ya no esta ahi y el arnes acusaria al
					// memo de un rechazo que se provoco el propio arnes.
					//
					// La sombra NO guarda una Query congelada: guarda (pc, estado) y el
					// CONTEXTO en el que se registro. Auditar con la foto del registro
					// seria taparse los ojos — un cambio de mascara o de epoca no se
					// podria distinguir de "el memo nunca fallo". Con el contexto vivo se
					// vuelve a preguntar, y ahi es donde la invalidacion se prueba.
					const Query rq = MakeQuery(pc, st); // contexto de ESTE paso
					Shadow& sh = g_shadow[slot];
					sh.valid = true;
					sh.pc = pc;
					sh.st = st;
					sh.host = g_model.lookup[(size_t)d.walkIdx].host;
					sh.recordEpoch = rq.cacheEpoch;
					sh.recordCandidate = rq.candidateEpoch;
					sh.recordProg = rq.progId;
					sh.recordMgr = rq.mgrId;
					sh.recordCacheStart = rq.cacheStart;
					sh.recordCacheEnd = rq.cacheEnd;
					sh.recordFlagHack = rq.flagHack;
				}
				// Si NO recordo (barrido corto o fuera de ventana) la tabla no cambio:
				// la sombra vieja, si existe, sigue describiendo la entrada viva.
			}
			else if (d.path == 0)
			{
				g_wDifMiss++;
			}

			// auditoria: la casilla tocada y un muestreo
			AuditSlot("diferencial", (int)(cycle * kDispatches + k), slot);
			for (unsigned s = 0; s < 6u; s++)
				AuditSlot("diferencial-muestreo", (int)(cycle * kDispatches + k),
					rndBelow(kSlots));
		}

		if (!g_difBroken)
			AuditAll("fin de ciclo", (int)cycle);
	}

	CHECK(!g_difBroken, "el diferencial se rompio (ver los fallos de arriba)");
	CHECK(g_wDifServe > 0, "el diferencial nunca audito un acierto servido (%u): la "
		"invariante (i) no se ejercito", g_wDifServe);
	CHECK(g_difTab.stats.serves > 0, "el memo no sirvio nada en el diferencial "
		"(serves=%" PRIu64 "): la invariante (ii) no se ejercito", g_difTab.stats.serves);
	CHECK(g_wDifDeclineSynced == 0, "%u negaciones con el contexto vivo", g_wDifDeclineSynced);
	CHECK(g_wDifLegitReject > 0, "%u rechazos legitimos: la invalidacion no llego a "
		"auditoria", g_wDifLegitReject);
	CHECK(g_wDifAdds > 0 && g_wDifExactAdds > 0 && g_wDifMaskFlip > 0 &&
			g_wDifMicroWrite > 0 && g_wDifCacheReset > 0 && g_wDifProgChange > 0 &&
			g_wDifWindowMove > 0 && g_wDifMgrReassign > 0 && g_wDifShortWalk > 0 &&
			g_wDifMiss > 0 && g_wDifAudits >= kCycles,
		"la secuencia no ejercito todos los caminos (adds=%u exact=%u mascaras=%u "
		"micro=%u resets=%u programas=%u ventanas=%u gestores-reasignados=%u "
		"barridos-cortos=%u misses=%u "
		"auditorias-completas=%u): un verde asi no vale",
		g_wDifAdds, g_wDifExactAdds, g_wDifMaskFlip, g_wDifMicroWrite, g_wDifCacheReset,
		g_wDifProgChange, g_wDifWindowMove, g_wDifMgrReassign, g_wDifShortWalk, g_wDifMiss, g_wDifAudits);
}


// ==================================================================
// 8) Que la instancia global sea UNA sola: microVU-arm6.cpp la usa desde su TU y
//    el arnes desde el suyo. Si fueran dos, la telemetria del cierre contaria otra
//    cosa que la que el despacho escribe.
// ==================================================================
static void testSingleton(void)
{
	mVUFastPath::g_vu1.Reset();
	const u64h antes = mVUFastPath::g_vu1.stats.consults;
	ResetWorld();
	const unsigned pc = 0x0600u;
	const MState st = MkState(KeyFor(pc), 0, pc);
	const u64h host = g_world.newHost();
	CHECK(mVUFastPath::g_vu1.Record(MakeQuery(pc, st), host, 40u), "Record en g_vu1 rechazo");
	uint64_t got = 0;
	CHECK(mVUFastPath::g_vu1.Serve(MakeQuery(pc, st), &got) && got == host,
		"g_vu1 no se comporta como Table");
	CHECK(mVUFastPath::g_vu1.stats.consults == antes + (u64h)1,
		"consults de g_vu1=%" PRIu64 ", queria %" PRIu64 ": la instancia no es la que el despacho escribe",
		mVUFastPath::g_vu1.stats.consults, antes + (u64h)1);
	mVUFastPath::g_vu1.Reset();
}

int main(void)
{
	testDomain();
	testServeFields();
	testRecordGuards();
	testCostLaw();
	testInvalidateVsReset();
	testInvalidationReasons();
	testCoexistenciaMascara();
	testDifferential();
	testSingleton();

	// Testigos de los caminos del despacho: sin esto el verde podria venir de un arnes
	// que nunca pregunto, nunca registro o nunca vio un barrido corto.
	CHECK(g_wServed > 0 && g_wWalkHit > 0 && g_wWalkMiss > 0 && g_wRecorded > 0 &&
			g_wSkippedShort > 0 && g_wNoAsk > 0,
		"caminos del despacho sin ejercitar: servidos=%u aciertos-de-barrido=%u "
		"miss=%u registrados=%u cortos=%u sin-preguntar=%u", g_wServed, g_wWalkHit,
		g_wWalkMiss, g_wRecorded, g_wSkippedShort, g_wNoAsk);

	// Cenit 0.6.36 — el INVARIANTE tiene que haberse COMPROBADO y tiene que haber visto
	// los TRES motivos. Las rotas ya sumaron en g_fails por su propio CHECK; lo que va
	// aqui es que el verde no pueda venir de un arnes que nunca despacho, o que despacho
	// siempre por el mismo motivo. Sin los tres testigos, un log con skip-short=0 y
	// skip-exact=0 no se puede distinguir de un contador que nadie llama.
	CHECK(g_invChecks > 0u, "el invariante no se compruebo en NINGUN despacho "
		"(checks=%u): la telemetria nueva no esta ejercida", g_invChecks);
	CHECK(g_wAskCounted > 0u && g_wSkipShortCounted > 0u && g_wSkipExactCounted > 0u,
		"motivos de AskCounted sin ejercitar: pregunto=%u salto-corto=%u salto-exacto=%u "
		"— los tres se imprimen en el cierre, los tres tienen que haberse contado aqui",
		g_wAskCounted, g_wSkipShortCounted, g_wSkipExactCounted);

	std::printf("Harness12: %d Comprobaciones, %d Fallos\n", g_checks, g_fails);
	// Se imprime con la palabra "Comprobaciones" para que baseline.sh (que filtra por
	// "Comprobaciones|FALLO") muestre la cuenta del denominador en la linea base y no la
	// deje enterrada. Y deliberadamente NO lleva ", N Fallos": mutacion_h12.sh busca esa
	// cadena para su control, y una segunda linea con el mismo patron podria dar verde
	// mientras la primera esta roja. Las rotas del invariante ya estan contadas arriba,
	// en g_fails, porque se emiten con CHECK.
	std::printf("  invariante del cierre: %d Comprobaciones de despacho | AskCounted: "
		"pregunto=%u salto-corto=%u salto-exacto=%u\n",
		g_invChecks, g_wAskCounted, g_wSkipShortCounted, g_wSkipExactCounted);
	std::printf("  testigos: servidos=%u aciertos-barrido=%u miss=%u registrados=%u "
		"cortos=%u no-pregunto=%u | dif: aciertos=%u rechazos=%u adds=%u exact=%u "
		"mascaras=%u micro=%u resets=%u programas=%u ventanas=%u gestores=%u cortos=%u\n",
		g_wServed, g_wWalkHit, g_wWalkMiss, g_wRecorded, g_wSkippedShort, g_wNoAsk,
		g_wDifServe, g_wDifLegitReject, g_wDifAdds, g_wDifExactAdds, g_wDifMaskFlip,
		g_wDifMicroWrite, g_wDifCacheReset, g_wDifProgChange,
		g_wDifWindowMove, g_wDifMgrReassign, g_wDifShortWalk);
	return g_fails == 0 ? 0 : 1;
}


