// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// Harness11 — LA LOGICA DEL INDICE PC->hueco del motor de superbloques, probada
// en host contra un modelo lineal de referencia.
//
// Existe por la misma razon que Harness9: hay piezas de este motor que se pueden
// verificar SIN telefono, y dejarlas sin verificar es el defecto que ya costo dos
// ciclos (el «8 pares y ya» de 0.6.21 y el «sin divergencias = acelerado» de
// 0.6.25). Aqui la pieza es app/src/main/cpp/pcsx2/arm64/MvuSbIndex.h.
//
// Por que hace falta un arnes y no basta «confiar en el acelerador»: SbResolve
// corre en CADA despacho de VU1, y un indice que se equivoca no cuesta velocidad —
// cuesta servir un superbloque en un PC que no es el suyo, o sea ejecutar codigo
// que nadie valido. Un acelerador asi, o se demuestra equivalente al barrido, o no
// se enciende.
//
// Que comprueba:
//  1) Contrato del dominio: kSlots == 2048 e IdxOf == (pc >> 3) & 2047, la MISMA
//     formula que PcIdx() del motor y kProbeSlots de la sonda. Si las formulas se
//     separan, el Lookup mira en la casilla equivocada y todo lo demas es ruido.
//  2) Semantica de cada operacion aislada: Lookup en vacio, Bind nuevo /
//     idempotente / rechazado, Unbind dirigido y Unbind mal dirigido, Count,
//     ClearAll, y el truco id+1 (el hueco 0 NO puede confundirse con «vacio»).
//  3) Que ningun fallo silencioso es posible: un Bind sobre un PC ya enlazado a
//     otro hueco devuelve -2 y DEJA INTACTO el enlace viejo (pisar en silencio es
//     exactamente el defecto que hace impredecible un indice), y un Unbind con el
//     slotId equivocado devuelve false sin tocar el enlace ajeno.
//  4) REASIGNACION, que es el caso real que el motor produce (AllocSlot segunda
//     pasada: un hueco muerto cambia de PC): el PC viejo queda libre, el nuevo se
//     encuentra, y el vecino no se pisa.
//  5) EQUIVALENCIA DIFERENCIAL contra el modelo lineal: 20000 operaciones de
//     armar/reasignar/liberar desordenadas, comprobando DESPUES DE CADA PASO que
//     el indice dice lo mismo que el barrido de la tabla, casilla por casilla.
//     Semilla fija: un fallo es reproducible en otra maquina.
//  6) Tabla llena: los 256 huecos (kSbMaxVariants) conviven y todos se resuelven.
//
// Que NO comprueba, dicho en claro (para que nadie lea de mas en su verde):
//  - Que el MOTOR enlace en los momentos correctos (AllocSlot / SbArmCompile /
//    SbCloseCompile / SbFinish / ResetTableForEnable). Eso vive en
//    MvuSuperblock-arm64.cpp y lo cubren la AUTOCOMPROBACION de ese archivo (cada
//    kSbIndexAuditPeriod despachos SbResolve hace el barrido y lo confronta; si
//    difieren el indice se auto-destituye) y la auditoria de DumpReport. Un arnes
//    de host no ve esos llamadores: decir lo contrario seria mentir con un verde.
//  - Nada de emision JIT, VURegs ni microcodigo: el indice no los toca.
//
// Lo corre tools/arneses/baseline.sh con g++ de host y el header DEL REPO (misma
// regla anti-copia-vieja que h5..h10).

#include "MvuSbIndex.h"

#include <cstdio>

static int g_checks = 0;
static int g_fails = 0;

#define CHECK(cond, ...)             \
	do                           \
	{                            \
		g_checks++;          \
		if (!(cond))         \
		{                    \
			g_fails++;   \
			std::printf("FALLO: ");  \
			std::printf(__VA_ARGS__);\
			std::printf("\n"); \
		}                    \
	} while (0)

using mVUSbIndex::Index;
using mVUSbIndex::kEmpty;
using mVUSbIndex::kSlots;
using mVUSbIndex::kMaxSlotId;

// ------------------------------------------------------------------
// El modelo de referencia: huecos con (inUse, pc), la forma de Candidate del
// motor en lo que al indice importa. «Resolver por barrido» es lo que el motor
// HACIA antes de 0.6.33: el indice tiene que dar siempre el mismo numero.
// ------------------------------------------------------------------
static constexpr int kModelSlots = 256; // == kSbMaxVariants del motor

struct ModelSlot
{
	bool inUse = false;
	unsigned pc = 0;
};

static ModelSlot g_model[kModelSlots];
static Index g_idx;

static void ModelClear(void)
{
	for (int i = 0; i < kModelSlots; i++)
	{
		g_model[i].inUse = false;
		g_model[i].pc = 0;
	}
	g_idx.ClearAll();
}

// El oraculo: barrido lineal de la tabla, como lo hacia SbResolve.
static int Sweep(int pc)
{
	for (int i = 0; i < kModelSlots; i++)
	{
		if (g_model[i].inUse && g_model[i].pc == static_cast<unsigned>(pc))
			return i;
	}
	return -1;
}

static unsigned ModelCount(void)
{
	unsigned n = 0;
	for (int i = 0; i < kModelSlots; i++)
		if (g_model[i].inUse)
			n++;
	return n;
}

// El modelo que la tabla IMPLICA para el indice, casilla por casilla.
static void BuildExpected(uint16_t* out)
{
	for (unsigned s = 0; s < kSlots; s++)
		out[s] = kEmpty;
	for (int i = 0; i < kModelSlots; i++)
	{
		if (g_model[i].inUse)
			out[Index::IdxOf(g_model[i].pc)] = static_cast<uint16_t>(i + 1);
	}
}

// Comparacion TOTAL indice vs modelo. Devuelve false y registra el primer
// desajuste (con nombres, no con numeros sueltos: un fallo tiene que decir que
// casilla, que PC y que esperaba cada uno).
static bool AgreeWithModel(const char* where, int step)
{
	uint16_t expected[kSlots];
	BuildExpected(expected);
	const uint16_t* live = g_idx.Raw();
	for (unsigned s = 0; s < kSlots; s++)
	{
		if (live[s] == expected[s])
			continue;
		CHECK(false,
			"%s (paso %d): casilla %u (PC %04x) indice=%d modelo=%d — el indice ya "
			"no es una copia de la tabla",
			where, step, s, s << 3,
			live[s] == kEmpty ? -1 : (int)live[s] - 1,
			expected[s] == kEmpty ? -1 : (int)expected[s] - 1);
		return false;
	}
	return true;
}

// ------------------------------------------------------------------
// PRNG propio determinista (xorshift64*), sin <random>: la semilla fija hace que
// un fallo sea reproducible en otra maquina. Un arnes que dependiera del reloj o
// del orden de un unordered_map no demostraria nada.
// ------------------------------------------------------------------
static unsigned long long s_state = 0x9E3779B97F4A7C15ull;

static unsigned long long xs(void)
{
	s_state ^= s_state >> 12;
	s_state ^= s_state << 25;
	s_state ^= s_state >> 27;
	return s_state * 0x2545F4914F6CDD1Dull;
}

static unsigned rndBelow(unsigned n) { return (unsigned)(xs() % n); }

// ------------------------------------------------------------------
// 1) Contrato del dominio
// ------------------------------------------------------------------
static void testDomain(void)
{
	CHECK(kSlots == 2048u,
		"kSlots=%u: el dominio de PCs de entrada de VU1 es 0x4000/8 = 2048 (la "
		"misma formula que PcIdx del motor y kProbeSlots de la sonda). Si uno de "
		"los tres cambia, cambian los tres o el indice busca en otra casilla",
		(unsigned)kSlots);

	CHECK(Index::IdxOf(0x0000u) == 0u, "IdxOf(0)=%u queria 0", Index::IdxOf(0x0000u));
	CHECK(Index::IdxOf(0x0008u) == 1u, "IdxOf(8)=%u queria 1 (8 bytes por microinstruccion)",
		Index::IdxOf(0x0008u));
	CHECK(Index::IdxOf(0x0010u) == 2u, "IdxOf(0x10)=%u queria 2", Index::IdxOf(0x0010u));
	CHECK(Index::IdxOf(0x3ff8u) == 2047u, "IdxOf(0x3ff8)=%u queria 2047 (ultima casilla)",
		Index::IdxOf(0x3ff8u));
	// El plegado de la mascara, fijado por escrito: el motor pasa SIEMPRE PCs ya
	// enmascarados a 0x3ff8, asi que 0x4000 no deberia llegar nunca — pero un
	// indice no se construye sobre «nunca llega», se construye sobre lo que hace.
	CHECK(Index::IdxOf(0x4000u) == 0u, "IdxOf(0x4000)=%u queria 0 (plegado por la "
		"mascara); si esto cambia, el aliasing del dominio cambio y las demas "
		"comprobaciones ya no dicen lo que dicen",
		Index::IdxOf(0x4000u));
	CHECK(Index::IdxOf(0x0008u) == Index::IdxOf(0x4008u),
		"IdxOf(8) e IdxOf(0x4008) deberian coincidir (mascara de 2048)");

	// SlotOf == IdxOf ya enmascarado: si se separan, Bind y Lookup usan casillas
	// distintas y el indice no se parece en nada a la tabla.
	bool slotStable = true;
	for (unsigned pc = 0; pc < 0x4000u; pc += 8u)
	{
		if (Index::SlotOf(Index::IdxOf(pc)) != Index::IdxOf(pc))
		{
			slotStable = false;
			break;
		}
	}
	CHECK(slotStable, "SlotOf(IdxOf(pc)) != IdxOf(pc) para algun pc del dominio");
}

// ------------------------------------------------------------------
// 2) Semantica de cada operacion, aislada
// ------------------------------------------------------------------
static void testOps(void)
{
	ModelClear();

	CHECK(g_idx.Count() == 0u, "indice recien limpiado: Count=%u queria 0", g_idx.Count());
	CHECK(g_idx.Lookup(0u) == -1, "Lookup(0) en vacio queria -1");
	CHECK(g_idx.Lookup(Index::IdxOf(0x1230u)) == -1, "Lookup(0x1230) en vacio queria -1");

	// Bind nuevo.
	CHECK(g_idx.Bind(7u, 3u) == 1, "Bind(7,3) queria 1 (enlace nuevo)");
	CHECK(g_idx.Lookup(7u) == 3, "Lookup(7)=%d queria 3", g_idx.Lookup(7u));
	CHECK(g_idx.Count() == 1u, "Count=%u queria 1", g_idx.Count());

	// Re-bind del MISMO hueco: idempotente (no es enlace nuevo, no es fallo).
	CHECK(g_idx.Bind(7u, 3u) == 0, "re-Bind(7,3) queria 0 (mismo hueco)");
	CHECK(g_idx.Lookup(7u) == 3, "re-Bind cambio el enlace");
	CHECK(g_idx.Count() == 1u, "re-Bind subio Count a %u", g_idx.Count());

	// Bind de OTRO hueco al MISMO PC: invariante rota -> -2 y el viejo INTACTO.
	CHECK(g_idx.Bind(7u, 9u) == -2, "Bind(7,9) queria -2 (el PC ya era del hueco 3)");
	CHECK(g_idx.Lookup(7u) == 3, "el Bind rechazado pisó el enlace: Lookup=%d queria 3",
		g_idx.Lookup(7u));
	CHECK(g_idx.Count() == 1u, "el Bind rechazado movio Count=%u", g_idx.Count());

	// Unbind mal dirigido: no puede robarle el enlace a un hueco legitimo.
	CHECK(g_idx.Unbind(7u, 9u) == false, "Unbind(7,9) queria false (el enlace es de 3)");
	CHECK(g_idx.Lookup(7u) == 3, "un Unbind mal dirigido rompio el enlace ajeno");

	// Unbind correcto, y su segunda vez.
	CHECK(g_idx.Unbind(7u, 3u) == true, "Unbind(7,3) queria true");
	CHECK(g_idx.Lookup(7u) == -1, "tras Unbind Lookup=%d queria -1", g_idx.Lookup(7u));
	CHECK(g_idx.Unbind(7u, 3u) == false, "el segundo Unbind queria false (no habia nada)");
	CHECK(g_idx.Count() == 0u, "Count=%u queria 0 tras el ultimo Unbind", g_idx.Count());

	// El hueco id 0: el truco id+1 tiene que seguir distinguiendolo del vacio. Un
	// indice que guardase 0 por el hueco 0 «perderia» el primer candidato de la
	// tabla, que es justo el que el motor arma casi siempre primero.
	CHECK(g_idx.Bind(100u, 0u) == 1, "Bind(100,0) queria 1");
	CHECK(g_idx.Lookup(100u) == 0, "Lookup(100)=%d queria 0 (hueco 0, no vacio)",
		g_idx.Lookup(100u));
	CHECK(g_idx.Raw()[100u] != kEmpty, "el hueco 0 se guardo como vacio: el indice no "
		"distingue 'hueco 0' de 'nadie'");
	CHECK(g_idx.Unbind(100u, 0u) == true, "Unbind del hueco 0 queria true");
	CHECK(g_idx.Lookup(100u) == -1, "tras Unbind del hueco 0 quedan %d", g_idx.Lookup(100u));

	// ClearAll de verdad.
	g_idx.Bind(5u, 1u);
	g_idx.Bind(6u, 2u);
	g_idx.ClearAll();
	CHECK(g_idx.Count() == 0u, "Count tras ClearAll=%u queria 0", g_idx.Count());
	CHECK(g_idx.Lookup(5u) == -1 && g_idx.Lookup(6u) == -1, "ClearAll dejo enlaces");

	// Un idx FUERA de dominio tiene que plegar en la casilla enmascarada, no escribir
	// al otro lado del arreglo. El motor pasa SIEMPRE idx = IdxOf(pc), que ya viene
	// enmascarado, asi que hoy esto es defensividad pura — pero es la unica
	// comprobacion que ata SlotOf a su mascara. Sin ella se probo un mutante
	// (SlotOf devolviendo idx pelado) y el arnes quedo verde: un indice que escribe
	// fuera del arreglo no es un indice mas lento, es memoria corrupta, y eso tiene
	// que costar un rojo aqui aunque todavia nadie lo provoque.
	g_idx.Bind(kSlots + 9u, 4u);
	CHECK(g_idx.Raw()[9u] != kEmpty,
		"Bind con idx=%u no plego en la casilla 9: la mascara de SlotOf no esta "
		"actuando y el enlace se fue fuera del arreglo", kSlots + 9u);
	CHECK(g_idx.Lookup(9u) == 4, "Bind(idx fuera de dominio) y Lookup(9) no coinciden (%d)",
		g_idx.Lookup(9u));
	CHECK(g_idx.Lookup(kSlots + 9u) == 4, "Lookup con idx fuera de dominio no encontro el enlace");
	g_idx.ClearAll();
	CHECK(g_idx.Count() == 0u, "tras limpiar el enlace plegado Count=%u queria 0", g_idx.Count());

	// EL TECHO DEL ID, FORZADO Y NO AFIRMADO. Un `CHECK(kMaxSlotId >= 255)` seria
	// siempre verdadero con las dos partes escritas a mano: un chequeo decorativo,
	// la clase de verde que este proyecto ya cazó dos veces (el «criterio de audio
	// sin observador» de 0.6.31, y el filtro de rutas de h10 que no miraba nada).
	// Lo que si tiene consecuencia es el EXTREMO del dominio: que id+1 no se vuelva
	// el propio centinela del vacio. slotId = kMaxSlotId + 1 trunca a want = 0 ==
	// kEmpty, y sobre una casilla vacia la comparacion `cur == want` se cumple — o
	// sea Bind devolveria 0 («idempotente», «ya estaba enlazado») habiendo enlazado
	// NADA: un candidato perdido sin ruido. Igual Unbind devolveria true («lo
	// quite») sobre un enlace que nunca existio. Se forzo escribiendo el guard a
	// mano y el arnes lo caza; el motor NO puede llegar ahi porque sus candidatos
	// son < kSbMaxVariants y un static_assert lo ata, asi que esto es la puerta del
	// componente, no un escenario vivo — y por eso se dice aqui en vez de venderlo
	// como un bug que nadie tenia.
	{
		const uint32_t casilla = 3u;
		g_idx.ClearAll();
		CHECK(g_idx.Bind(casilla, kMaxSlotId) == 1, "Bind del id techo %u fallo", kMaxSlotId);
		CHECK(g_idx.Raw()[casilla] != kEmpty,
			"el id techo %u se guardo como VACIO: ese hueco no existiria para nadie", kMaxSlotId);
		CHECK(g_idx.Lookup(casilla) == (int)kMaxSlotId,
			"Lookup del id techo dijo %d, queria %d: la codificacion id+1 no es redonda",
			g_idx.Lookup(casilla), (int)kMaxSlotId);

		// Id fuera de dominio sobre casilla VACIA: el caso traicionero del truncado.
		g_idx.ClearAll();
		CHECK(g_idx.Bind(casilla, kMaxSlotId + 1u) == -2,
			"Bind con id fuera de dominio devolvio otro codigo: tenia que rechazarlo en la "
			"cara, no volverse 'idempotente' por truncar a vacio");
		CHECK(g_idx.Raw()[casilla] == kEmpty,
			"el Bind rechazado escribio %u en la casilla: tenia que dejarla vacia",
			(unsigned)g_idx.Raw()[casilla]);
		CHECK(g_idx.Count() == 0u, "Count=%u tras el Bind fuera de dominio: queria 0", g_idx.Count());

		// Id fuera de dominio sobre casilla OCUPADA: no pisa, y lo dice.
		CHECK(g_idx.Bind(casilla, 7u) == 1, "no se pudo enlazar 7 para la prueba del techo");
		CHECK(g_idx.Bind(casilla, kMaxSlotId + 1u) == -2, "el id fuera de dominio aceptado sobre enlace ajeno");
		CHECK(g_idx.Lookup(casilla) == 7, "el intento fuera de dominio perdio el enlace de 7: queda %d",
			g_idx.Lookup(casilla));

		// Y Unbind con id fuera de dominio no puede afirmar que quito algo — ni
		// sobre un enlace ajeno, NI sobre una casilla vacia (que es donde el
		// truncado miente: want = 0 coincide con el centinela y Unbind diria true
		// de un enlace que nunca existio).
		CHECK(g_idx.Unbind(casilla, kMaxSlotId + 1u) == false,
			"Unbind con id truncado a vacio dijo true sobre un enlace ajeno");
		CHECK(g_idx.Lookup(casilla) == 7, "el Unbind rechazado rompio el enlace de 7");
		g_idx.ClearAll();
		CHECK(g_idx.Unbind(casilla, kMaxSlotId + 1u) == false,
			"Unbind con id truncado a vacio dijo true sobre una casilla VACIA: afirmo "
			"quitar un enlace que nunca existio");
		g_idx.ClearAll();
	}
}

// ------------------------------------------------------------------
// 3) Reasignacion: el caso real del motor (AllocSlot segunda pasada — un hueco
//    cuya maquina ya NO ocupa cambia de PC). Aca es donde un indice mal mantenido
//    sirve la variante equivocada, asi que se fuerza sin azar.
// ------------------------------------------------------------------
static void testReassign(void)
{
	ModelClear();

	// hueco 5 en 0x0800 (casilla 256), hueco 6 en 0x0808 (casilla 257).
	g_model[5].inUse = true; g_model[5].pc = 0x0800;
	g_model[6].inUse = true; g_model[6].pc = 0x0808;
	g_idx.Bind(Index::IdxOf(0x0800u), 5u);
	g_idx.Bind(Index::IdxOf(0x0808u), 6u);
	CHECK(g_idx.Lookup(Index::IdxOf(0x0800u)) == 5, "hueco 5 no enlazado en 0x0800");
	CHECK(g_idx.Lookup(Index::IdxOf(0x0808u)) == 6, "hueco 6 no enlazado en 0x0808");
	CHECK(AgreeWithModel("vecinos enlazados", -1), "los dos enlaces vecinos no conviven");

	// El hueco 5 se reasigna a 0x0900: desligar del viejo, ligar al nuevo (el
	// orden exacto que cumplen UnlinkSlot en AllocSlot y LinkSlot en SbArmCompile).
	g_idx.Unbind(Index::IdxOf(0x0800u), 5u);
	g_model[5].pc = 0x0900;
	CHECK(g_idx.Bind(Index::IdxOf(0x0900u), 5u) == 1, "Bind del hueco reasignado fallo");
	CHECK(g_idx.Lookup(Index::IdxOf(0x0800u)) == -1,
		"el PC viejo 0x0800 sigue apuntando al hueco 5, que ya esta en 0x0900: un "
		"despacho a 0x0800 recibiria una variante que ya no existe ahi");
	CHECK(g_idx.Lookup(Index::IdxOf(0x0900u)) == 5, "reasignado y no se encuentra");
	CHECK(g_idx.Lookup(Index::IdxOf(0x0808u)) == 6, "el vecino se pille en el cambio");
	CHECK(AgreeWithModel("reasignacion", -1), "tras reasignar, indice y tabla difieren");

	// El orden inverso (ligar antes de desligar) TIENE que rechazarse, no ser
	// silencio. El motor no lo hace; si alguien lo hiciera, el indice lo avisa.
	CHECK(g_idx.Bind(Index::IdxOf(0x0900u), 6u) == -2,
		"deberia rechazar el segundo enlace de un PC ya ocupado");
	CHECK(g_idx.Lookup(Index::IdxOf(0x0900u)) == 5, "el rechazo piso el enlace bueno");

	// Aliasing del dominio: 0x0800 y 0x4800 pliegan a la MISMA casilla, asi que dos
	// huecos en esos PCs son la invariante rota y el indice tiene que rechazarlo.
	// Primero hay que devolver el hueco 5 a 0x0800 (arriba lo reasignamos a 0x0900)
	// HACIENDO EL ORDEN DEL MOTOR — desligar el viejo, ligar el nuevo — y verificar
	// que la tabla sigue siendo copia del indice: un Bind sobre una casilla vacia no
	// es un rechazo, es un enlace legitimo, y pretender lo contrario era un rojo del
	// propio arnes.
	CHECK(g_idx.Unbind(Index::IdxOf(0x0900u), 5u) == true,
		"no se pudo desligar 0x0900 para volver a enlazar 0x0800");
	g_model[5].pc = 0x0800;
	CHECK(g_idx.Bind(Index::IdxOf(0x0800u), 5u) == 1,
		"no se pudo re-enlazar 0x0800 para probar el aliasing");
	CHECK(AgreeWithModel("re-enlace para aliasing", -1),
		"tras volver a enlazar 0x0800, indice y tabla difieren");
	CHECK(Index::IdxOf(0x0800u) == Index::IdxOf(0x4800u),
		"el aliasing de la mascara cambio: esta comprobacion ya no dice lo que dice");
	CHECK(g_idx.Bind(Index::IdxOf(0x4800u), 7u) == -2,
		"aliasing aceptado en silencio: el hueco 7 se enlazo a una casilla que ya era "
		"del hueco 5, y a partir de ahi un despacho a 0x0800 y otro a 0x4800 pelean "
		"por el mismo enlace");
	CHECK(g_idx.Lookup(Index::IdxOf(0x0800u)) == 5,
		"el intento por el alias 0x4800 piso el enlace vivo de 0x0800 (queda %d, queria 5)",
		g_idx.Lookup(Index::IdxOf(0x0800u)));
	CHECK(g_idx.Unbind(Index::IdxOf(0x0800u), 5u) == true, "no se pudo limpiar el alias");
	g_model[5].inUse = false;
	CHECK(AgreeWithModel("cierre del alias", -1), "tras la prueba de aliasing, indice y tabla difieren");
}

// ------------------------------------------------------------------
// 4) Diferencial contra el modelo. El modelo replica LAS REGLAS DEL MOTOR, no la
//    implementacion del indice: por eso la equivalencia dice algo. Reglas:
//      - «un hueco por PC» (SbArmCompile rechaza armar si FindForPc encuentra);
//      - reasignar = desligar el PC viejo y ligar el nuevo;
//      - liberar = desligar.
//    Con esas reglas el Bind rechazado no debe ocurrir nunca; si ocurre, el
//    modelo y el indice acaban de desmentirse y el paso se registra como fallo.
// ------------------------------------------------------------------
static void testDifferential(void)
{
	ModelClear();

	unsigned armed = 0, reassigned = 0, freed = 0, refused = 0, idempotent = 0;
	int mismatchStep = -1;

	for (int step = 0; step < 20000; step++)
	{
		const int slot = (int)rndBelow(kModelSlots);
		// El PC se elige de dos maneras a proposito. Tres de cada cuatro veces al
		// azar, que es lo que hace el motor: llega un bloque y se arma en su PC.
		// Una de cada cuatro, el PC que ESE hueco ya tiene si lo tiene — que es el
		// re-enlace idempotente (pasaria si el hook de arm se disparase dos veces
		// sobre el mismo candidato). Sin esto el camino idempotente sale por azar
		// 1 de cada 2048 pasos y el arnes puede quedarse verde sin haberlo tocado
		// nunca: el verde vacio que esta casa declara que no vale.
		unsigned pc;
		if (g_model[slot].inUse && (xs() & 3u) == 0u)
			pc = g_model[slot].pc;
		else
			pc = rndBelow(kSlots) * 8u; // PCs validos de VU1
		const unsigned op = rndBelow(10u);

		if (op < 4u)
		{
			// armar: la regla del motor es mirar el PC ANTES de tocar la tabla.
			const int owner = Sweep((int)pc);
			if (owner >= 0 && owner != slot)
			{
				refused++; // SbArmCompile devolveria -1 (rejected_identity)
			}
			else if (!g_model[slot].inUse)
			{
				g_model[slot].inUse = true;
				g_model[slot].pc = pc;
				const int r = g_idx.Bind(Index::IdxOf(pc), (unsigned)slot);
				CHECK(r == 1, "paso %d: Bind nuevo (%04x,%d) devolvio %d", step, pc, slot, r);
				armed++;
			}
			else if (g_model[slot].pc == pc)
			{
				const int r = g_idx.Bind(Index::IdxOf(pc), (unsigned)slot);
				CHECK(r == 0, "paso %d: re-Bind idempotente devolvio %d", step, r);
				idempotent++;
			}
			else
			{
				// reasignacion
				const unsigned old = g_model[slot].pc;
				CHECK(g_idx.Unbind(Index::IdxOf(old), (unsigned)slot) == true,
					"paso %d: Unbind del PC viejo %04x del hueco %d dijo false", step, old, slot);
				g_model[slot].pc = pc;
				const int r = g_idx.Bind(Index::IdxOf(pc), (unsigned)slot);
				CHECK(r == 1, "paso %d: Bind de reasignacion (%04x,%d) devolvio %d", step, pc, slot, r);
				reassigned++;
			}
		}
		else if (op < 7u)
		{
			// liberar el hueco
			if (g_model[slot].inUse)
			{
				CHECK(g_idx.Unbind(Index::IdxOf(g_model[slot].pc), (unsigned)slot) == true,
					"paso %d: Unbind(%04x,%d) dijo false con el enlace puesto", step,
					g_model[slot].pc, slot);
				g_model[slot].inUse = false;
				freed++;
			}
		}
		else
			continue; // paso sin operacion: deja pasar la comprobacion igual

		// Despues de CADA operacion: el indice tiene que decir lo mismo que el
		// barrido de la tabla, en TODAS las casillas.
		if (g_idx.Count() != ModelCount())
		{
			CHECK(false, "paso %d: Count=%u pero la tabla tiene %u huecos enlazados",
				step, g_idx.Count(), ModelCount());
			mismatchStep = step;
			break;
		}
		if (!AgreeWithModel("diferencial", step))
		{
			mismatchStep = step;
			break;
		}
	}

	CHECK(mismatchStep < 0, "el diferencial se corto en el paso %d", mismatchStep);
	// Un verde que no paso por los cuatro caminos no prueba nada: hay que poder
	// ver que la secuencia realmente los ejercito.
	CHECK(armed > 0 && reassigned > 0 && freed > 0 && refused > 0 && idempotent > 0,
		"la secuencia no ejercito los cinco caminos (armados=%u reasignados=%u "
		"liberados=%u rechazados=%u idempotentes=%u): un verde asi no vale",
		armed, reassigned, freed, refused, idempotent);
	CHECK(AgreeWithModel("cierre del diferencial", -1),
		"al cerrar el diferencial, el indice no coincide casilla por casilla con la tabla");
}

// ------------------------------------------------------------------
// 5) Tabla llena: los 256 huecos conviven y cada PC se resuelve en su hueco.
// ------------------------------------------------------------------
static void testFullTable(void)
{
	ModelClear();
	for (int i = 0; i < kModelSlots; i++)
	{
		const unsigned pc = (unsigned)i * 8u;
		g_model[i].inUse = true;
		g_model[i].pc = pc;
		CHECK(g_idx.Bind(Index::IdxOf(pc), (unsigned)i) == 1,
			"hueco %d en PC %04x no se enlazo (tabla llena prematuramente)", i, pc);
	}
	CHECK(g_idx.Count() == (unsigned)kModelSlots, "Count=%u con %d huecos enlazados",
		g_idx.Count(), kModelSlots);

	bool allResolve = true;
	for (int i = 0; i < kModelSlots; i++)
	{
		const unsigned pc = (unsigned)i * 8u;
		if (g_idx.Lookup(Index::IdxOf(pc)) != i)
		{
			allResolve = false;
			break;
		}
	}
	CHECK(allResolve, "con la tabla llena, algun PC no resolvio en su hueco");
	CHECK(AgreeWithModel("tabla llena", -1), "tabla llena: indice y tabla difieren");

	// Y un hueco fantasma (id >= kSbMaxVariants) no lo puede crear el indice por
	// si solo: el limite es del llamador, pero el dominio debe seguir cabiendo.
	CHECK((unsigned)kModelSlots - 1u + 1u <= (unsigned)kMaxSlotId,
		"el ultimo hueco de la tabla no cabe en el dominio del indice");
}

int main(void)
{
	testDomain();
	testOps();
	testReassign();
	testDifferential();
	testFullTable();

	std::printf("Harness11: %d Comprobaciones, %d Fallos\n", g_checks, g_fails);
	return g_fails == 0 ? 0 : 1;
}
