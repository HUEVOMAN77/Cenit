// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

// Cenit 0.6.37 — Decision de elegibilidad del motor de superbloques VU1, en un
// header puro y con una CUENTA EXACTA por causa.
//
// Que es esto. Las tres cadenas de decision del motor (armar un candidato,
// pedir una union durante el analisis, cerrar la compilacion variante) viven
// aqui como funciones sin dependencias: entran datos ya leidos, sale UN motivo.
// El motor (MvuSuperblock-arm64.cpp) deja de tener las condiciones escritas a
// mano y pasa a preguntar, contar por causa y ejecutar el efecto.
//
// Por que existe (el defecto que cierra, medido en el log de un HUAWEI NCO-LX3
// con God of War, 298.8 s, motor encendido):
//   vuSB: intentos=120542 construidos=0 ... rechazos{struct=47460 cold=21128
//         budget=1 ident=51953}
// Cuatro cubos para doce comprobaciones distintas. `ident=51953` no dice si el
// motor no encontraba hueco, si ya habia un candidato en ese PC, si el PC
// estaba quemado o si el indice se desincronizo — y esas cuatro respuestas
// piden cuatro arreglos distintos. `struct=47460` mezcla "este bloque no cae
// incondicionalmente sobre nadie" (que es microcodigo tal cual, no un bug) con
// "el destino nunca se analizo" (que si es cobertura). Con esa granularidad el
// log NO puede explicar un build=0: es exactamente la falta de denominador por
// la que 0.6.35 pago la leccion de `lookups` y 0.6.36 la del nombre `con-buffer`.
// Aqui cada comprobacion tiene su casilla y cada cadena cierra:
//   intentos == aceptados + Σ causas            (arm)
//   pedidos  == aceptadas + Σ causas            (union)
//   cierres  == adoptados + Σ causas            (cierre)
// El cierre se imprime firmado (ok / DESAJUSTE) en el informe: un desajuste en
// cualquier telefono es un defecto de la telemetria visible, no una inferencia.
//
// Las causas NO son una lista decorativa: el orden de las comprobaciones es el
// del codigo que reemplazan, y cambiar el orden cambia que se cuenta primero
// (y, con el, que se deja de contar). Harness14 fuerza la cadena completa contra
// la tabla de decisiones y el ejecutor de mutacion rompe cada comprobacion una
// por una y exige su rojo nombrado.
//
// Que NO cambia este header: NINGUN umbral. kHotEntries, kTargetMinEntries,
// kMinRatioPct, kMaxRatioPct, kMaxJunctions, kSbMaxOps, kSbMaxCycles son los
// mismos valores de siempre, que el motor pasa en una Policy — se copian, no se
// reinventan, y h10/h14 fijan los numeros. La politica de fusion del documento
// sigue intacta; lo unico que se agrega es la palabra de como se cuenta.
//
// Y que NO prueba esto en host, dicho para que nadie lea de mas en su verde:
// que el motor llame a estas funciones en el momento correcto, y que los datos
// que le pasa (aristas, formas, contadores de ejecucion) sean los vivos. Eso
// vive en microVU-arm64.cpp / MvuSuperblock-arm64.cpp, que un g++ de host no
// puede analizar (tira de <arm_neon.h> y de vixl); lo cubren el CI, las anclas
// de mutacion_h14.sh y —lo que este cambio necesita para dejar de ser una
// hipotesis— un log de telefono.

#include <cstdint>

namespace mVUSbElig
{
	// ------------------------------------------------------------------
	// Dominios de las cadenas.
	// ------------------------------------------------------------------

	// Politica de elegibilidad (los siete numeros de siempre, passed in). Vive
	// aqui como tipo para que el arnes pueda ensayar politicas que ningun
	// telefono del mundo va a ejecutar: lo que se prueba es la LEY, no el valor.
	struct Policy
	{
		uint32_t hot_entries;      // ejecuciones acumuladas exigidas al bloque origen
		uint32_t target_min;       // ejecuciones acumuladas exigidas al destino
		uint32_t min_ratio_pct;    // execB*100/execA >= esto: flujo estable
		uint32_t max_ratio_pct;    // y <= esto: B no lo domina otro predecesor
		uint32_t max_junctions;    // uniones por superbloque (tramos-1)
		uint32_t max_ops;          // instrucciones analizadas fundidas
		uint32_t max_cycles;       // ciclos cobrados fundidos
	};

	// ------------------------------------------------------------------
	// Cadena 1 — armar la compilacion variante de un PC de despacho.
	// ------------------------------------------------------------------
	enum class ArmCause : uint8_t
	{
		kLive = 0,      // ya hay un candidato vivo en este PC (no se re-arma)
		kBurned,        // el motor ya juzgo este PC y lo tiene quemado
		kNoEdge,        // no hay arista incondicional medida (kind != 1)
		kNoShape,       // origen o destino sin forma registrada (reason == 0)
		kNoSlots,       // la sonda no tiene arreglo de ejecuciones: nada medido
		kFloor,         // hotness por debajo del piso (origen o destino)
		kRatio,         // ratio fuera de banda: B tiene otro predecesor dominante
		kPair,          // el par A+B ya pasa los techos: la primera union no paga
		kPolicyPass,    // veredicto transitorio: politica ok, sigue la asignacion
		kFull,          // tabla sin hueco (ni libre ni reasignable)
		kDesync,        // el hueco se enlazo a un PC que ya tenia otro (invariante)
		kArmed,         // armado: se devuelve hueco y el dispatcher reentra el compile
		kCount
	};

	static const uint32_t kArmCauseCount = static_cast<uint32_t>(ArmCause::kCount);

	// Un rechace es una causa terminal negativa. kPolicyPass no es terminal (es
	// "todavia no sé") y kArmed no es rechace: las dos se excluyen para que
	// `intentos == aceptados + Σ rechaces` no se cueza dos veces.
	inline bool IsArmRejection(ArmCause c)
	{
		return c != ArmCause::kPolicyPass && c != ArmCause::kArmed;
	}

	// Un RESULTADO terminal (lo que consume un intento): todo menos kPolicyPass.
	inline bool IsArmOutcome(ArmCause c)
	{
		return c != ArmCause::kPolicyPass;
	}

	// Datos YA leidos por el motor. Nada de punteros: lo que entra es lo que la
	// sonda y la tabla afirman, y el header no puede mirar nada mas (así la
	// decision es una funcion, no un recorrido con efectos).
	struct ArmInput
	{
		bool already_live;
		bool burned;
		uint8_t edge_kind;     // 0 ninguna, 1 incondicional, 2 condicional
		uint16_t shape_a_reason;
		uint16_t shape_b_reason;
		bool slots_present;
		uint64_t exec_a;
		uint64_t exec_b;
		uint16_t ops_a;
		uint16_t ops_b;
		uint16_t cycles_a;
		uint16_t cycles_b;
	};

	inline ArmCause DecideArm(const ArmInput& in, const Policy& p)
	{
		// Orden literal del codigo que reemplaza (MvuSuperblock-arm64.cpp
		// SbArmCompile 0.6.36): identidad, estructura, hotness, presupuesto.
		if (in.already_live)
			return ArmCause::kLive;
		if (in.burned)
			return ArmCause::kBurned;
		if (in.edge_kind != 1u)
			return ArmCause::kNoEdge;
		if (in.shape_a_reason == 0u || in.shape_b_reason == 0u)
			return ArmCause::kNoShape;
		if (!in.slots_present)
			return ArmCause::kNoSlots;
		if (in.exec_a < static_cast<uint64_t>(p.hot_entries) ||
			in.exec_b < static_cast<uint64_t>(p.target_min))
			return ArmCause::kFloor;
		// ratio = execB*100/execA. execA ya paso el piso de hotness (>0), pero la
		// division se protege igual: un 0 aqui seria un llamador roto, y un crash
		// en el despacho mas caliente del emulador no es una forma de decirlo.
		const uint64_t denom = in.exec_a ? in.exec_a : 1u;
		const uint64_t ratio = in.exec_b * 100ull / denom;
		if (ratio < static_cast<uint64_t>(p.min_ratio_pct) ||
			ratio > static_cast<uint64_t>(p.max_ratio_pct))
			return ArmCause::kRatio;
		// Overflow deliberado en u64: ops/cycles son u16 por construccion (BlockShape
		// es de 8 bytes), así que la suma maxima es 131070 y cabe. Cambiar BlockShape
		// sin cambiar esto no desborda, pero si deja de ser cierto el argumento — y
		// eso lo canta h14 con un static_assert sobre el tamano del input.
		if (static_cast<uint64_t>(in.ops_a) + in.ops_b > p.max_ops ||
			static_cast<uint64_t>(in.cycles_a) + in.cycles_b > p.max_cycles)
			return ArmCause::kPair;
		return ArmCause::kPolicyPass;
	}

	// ------------------------------------------------------------------
	// Cadena 2 — pedir una union a mitad de analisis (SbAskJunction).
	// ------------------------------------------------------------------
	enum class JunctionCause : uint8_t
	{
		kBadSlot = 0,   // id de hueco fuera de la tabla: llamador roto
		kNotBuilding,   // el candidato no esta BUILDING: no hay a quien unir
		kDirty,         // rama con M/T/D/E/I, bad/evil branch, kick o backupVI
		kCap,           // techo de uniones alcanzado
		kRegion,        // el area actual ya no deja hueco para el tramo destino
		kMisalign,      // PC destino no alineado a 8: no es un PC de entrada
		kNoShape,       // destino nunca analizado: fusionar sobre conjetura
		kSelf,          // destino == entrada: el superbloque se borra a si mismo
		kJoined,        // union concedida
		kCount
	};

	static const uint32_t kJunctionCauseCount = static_cast<uint32_t>(JunctionCause::kCount);

	inline bool IsJunctionRejection(JunctionCause c)
	{
		return c != JunctionCause::kJoined;
	}

	struct JunctionInput
	{
		bool slot_in_range;
		bool is_building;
		bool struct_clean;
		uint32_t junctions_so_far;
		uint32_t region_ops;
		uint32_t region_cycles;
		bool target_aligned;
		bool target_has_shape;
		bool target_is_self;
	};

	inline JunctionCause DecideJunction(const JunctionInput& in, const Policy& p)
	{
		if (!in.slot_in_range)
			return JunctionCause::kBadSlot;
		if (!in.is_building)
			return JunctionCause::kNotBuilding;
		if (!in.struct_clean)
			return JunctionCause::kDirty;
		if (in.junctions_so_far >= p.max_junctions)
			return JunctionCause::kCap;
		// `>=` y no `>`: es lo que hace el motor hoy. Con el area YA en el techo, la
		// union meteria el tramo destino encima y el area saldria fuera de politica
		// en el cierre — mejor no empezar la fusion.
		if (in.region_ops >= p.max_ops || in.region_cycles >= p.max_cycles)
			return JunctionCause::kRegion;
		if (!in.target_aligned)
			return JunctionCause::kMisalign;
		if (!in.target_has_shape)
			return JunctionCause::kNoShape;
		if (in.target_is_self)
			return JunctionCause::kSelf;
		return JunctionCause::kJoined;
	}

	// ------------------------------------------------------------------
	// Cadena 3 — cerrar la compilacion variante (SbCloseCompile).
	// ------------------------------------------------------------------
	enum class CloseCause : uint8_t
	{
		kBadSlot = 0,     // id fuera de tabla
		kNotBuilding,     // la maquina ya no esta en BUILDING
		kAreaBad,         // kick dentro del area: efectos observables por el host
		kNoJunction,      // el paso 1 no cruzo ninguna union: no es un superbloque
		kNoEntry,         // sin hostEntry: no hay nada que despachar
		kZeroOps,         // area de cero instrucciones medida
		kOverBudget,      // el area crecio tras la ultima union y ya no cabe
		kAdopted,         // adoptado -> VERIFY
		kCount
	};

	static const uint32_t kCloseCauseCount = static_cast<uint32_t>(CloseCause::kCount);

	inline bool IsCloseRejection(CloseCause c)
	{
		return c != CloseCause::kAdopted;
	}

	struct CloseInput
	{
		bool slot_in_range;
		bool is_building;
		bool area_bad;
		uint32_t junctions;
		bool has_entry;
		uint32_t ops;
		uint32_t cycles;
	};

	inline CloseCause DecideClose(const CloseInput& in, const Policy& p)
	{
		if (!in.slot_in_range)
			return CloseCause::kBadSlot;
		if (!in.is_building)
			return CloseCause::kNotBuilding;
		// Orden del codigo original: la forma del area se juzga antes que el
		// presupuesto, porque el quemado del PC es comun a las dos y la prosa del
		// informe tiene que poder decir CUAL rechazo.
		if (in.area_bad)
			return CloseCause::kAreaBad;
		if (in.junctions < 1u)
			return CloseCause::kNoJunction;
		if (!in.has_entry)
			return CloseCause::kNoEntry;
		if (in.ops == 0u)
			return CloseCause::kZeroOps;
		if (in.ops > p.max_ops || in.cycles > p.max_cycles)
			return CloseCause::kOverBudget;
		return CloseCause::kAdopted;
	}

	// ------------------------------------------------------------------
	// Nombre y cubo de cada causa. Una sola tabla: el informe imprime estas
	// palabras y el clasificador de cubos deriva de la misma lista, así el
	// numero que lee un usuario y el que agrega el informe no pueden divergir
	// (la leccion de DecideAsk en MvuFastPath.h: dos copias de una ley siempre
	// acaban diciendo cosas distintas).
	//
	// `Bucket` conserva los cuatro nombres del log de 0.6.36 (struct/cold/
	// budget/ident) para que una partida vieja y una nueva se puedan comparar,
	// pero ahora es DERIVADO: el informe lo suma de las causas, no de su propia
	// cuenta. Los cuatro contadores viejos salen del motor precisamente para que
	// no exista un segundo contador de la misma decision.
	// ------------------------------------------------------------------
	enum class Bucket : uint8_t
	{
		kStruct = 0,   // la estructura del microcodigo no permite la fusion
		kCold,         // hotness o flujo medido insuficiente
		kBudget,       // presupuesto de area/uniones
		kIdentity,     // estado de la tabla/huecos del motor
		kDispatch,     // filtro de presupuesto en el despacho (no es una de las 3 cadenas)
		kCount
	};

	static const uint32_t kBucketCount = static_cast<uint32_t>(Bucket::kCount);

	inline Bucket ArmBucketOf(ArmCause c)
	{
		switch (c)
		{
			case ArmCause::kLive:
			case ArmCause::kBurned:
			case ArmCause::kFull:
			case ArmCause::kDesync:
				return Bucket::kIdentity;
			case ArmCause::kNoEdge:
			case ArmCause::kNoShape:
				return Bucket::kStruct;
			case ArmCause::kNoSlots:
			case ArmCause::kFloor:
			case ArmCause::kRatio:
				return Bucket::kCold;
			case ArmCause::kPair:
				return Bucket::kBudget;
			case ArmCause::kPolicyPass:
			case ArmCause::kArmed:
			case ArmCause::kCount:
				break;
		}
		// kPolicyPass/kArmed no tienen cubo: no son rechace. Si alguien anade una
		// causa y no la mapa, esta linea la devuelve como kCold y el arnes la canta
		// (h14 exige que TODO rechace tenga cubo distinto de "sin cubo").
		return Bucket::kCold;
	}

	inline Bucket JunctionBucketOf(JunctionCause c)
	{
		switch (c)
		{
			case JunctionCause::kBadSlot:
			case JunctionCause::kNotBuilding:
				return Bucket::kIdentity;
			case JunctionCause::kDirty:
			case JunctionCause::kMisalign:
			case JunctionCause::kNoShape:
			case JunctionCause::kSelf:
				return Bucket::kStruct;
			case JunctionCause::kCap:
			case JunctionCause::kRegion:
				return Bucket::kBudget;
			case JunctionCause::kJoined:
			case JunctionCause::kCount:
				break;
		}
		return Bucket::kStruct;
	}

	inline Bucket CloseBucketOf(CloseCause c)
	{
		switch (c)
		{
			case CloseCause::kBadSlot:
			case CloseCause::kNotBuilding:
				return Bucket::kIdentity;
			case CloseCause::kAreaBad:
				return Bucket::kStruct;
			case CloseCause::kNoJunction:
			case CloseCause::kNoEntry:
			case CloseCause::kZeroOps:
			case CloseCause::kOverBudget:
				return Bucket::kBudget;
			case CloseCause::kAdopted:
			case CloseCause::kCount:
				break;
		}
		return Bucket::kStruct;
	}

	inline const char* ArmCauseName(ArmCause c)
	{
		switch (c)
		{
			case ArmCause::kLive: return "vivo";
			case ArmCause::kBurned: return "quemado";
			case ArmCause::kNoEdge: return "sin-arista";
			case ArmCause::kNoShape: return "sin-forma";
			case ArmCause::kNoSlots: return "sin-contador";
			case ArmCause::kFloor: return "bajo-piso";
			case ArmCause::kRatio: return "ratio-fuera";
			case ArmCause::kPair: return "par-grande";
			case ArmCause::kPolicyPass: return "politica-ok";
			case ArmCause::kFull: return "tabla-llena";
			case ArmCause::kDesync: return "desenlace";
			case ArmCause::kArmed: return "armado";
			case ArmCause::kCount: break;
		}
		return "?";
	}

	inline const char* JunctionCauseName(JunctionCause c)
	{
		switch (c)
		{
			case JunctionCause::kBadSlot: return "hueco-fuera";
			case JunctionCause::kNotBuilding: return "no-construyendo";
			case JunctionCause::kDirty: return "rama-sucia";
			case JunctionCause::kCap: return "tope-uniones";
			case JunctionCause::kRegion: return "sin-hueco-de-area";
			case JunctionCause::kMisalign: return "destino-desalineado";
			case JunctionCause::kNoShape: return "destino-sin-forma";
			case JunctionCause::kSelf: return "destino-es-entrada";
			case JunctionCause::kJoined: return "unida";
			case JunctionCause::kCount: break;
		}
		return "?";
	}

	inline const char* CloseCauseName(CloseCause c)
	{
		switch (c)
		{
			case CloseCause::kBadSlot: return "hueco-fuera";
			case CloseCause::kNotBuilding: return "no-construyendo";
			case CloseCause::kAreaBad: return "area-con-kick";
			case CloseCause::kNoJunction: return "cero-uniones";
			case CloseCause::kNoEntry: return "sin-entrada";
			case CloseCause::kZeroOps: return "cero-ops";
			case CloseCause::kOverBudget: return "fuera-de-techos";
			case CloseCause::kAdopted: return "adoptado";
			case CloseCause::kCount: break;
		}
		return "?";
	}

	inline const char* BucketName(Bucket b)
	{
		switch (b)
		{
			case Bucket::kStruct: return "estructura";
			case Bucket::kCold: return "frio";
			case Bucket::kBudget: return "presupuesto";
			case Bucket::kIdentity: return "estado-tabla";
			case Bucket::kDispatch: return "despacho";
			case Bucket::kCount: break;
		}
		return "?";
	}

	// ------------------------------------------------------------------
	// Puerta de re-evaluacion (ArmGate) — la segunda pata del defecto.
	//
	// Por que hace falta: el armado estaba colgado SOLO del camino lento de
	// despacho. En el log del telefono eso es literalmente aritmetica:
	//   stub=13110338  hit=12989796  lento=120542
	// o sea el motor solo podia considerar 120542 de 13110338 despachos = 0.9 %.
	// Los bloques calientes (0x0378 con 44 486 376 ejecuciones, 0x0cd8 con
	// 123 013 572) se resuelven por el stub emitido y NUNCA se arman: el motor
	// puede servir superbloques pero casi no puede construirlos. Y esa es la
	// causa de build=0 que este ciclo arregla, no una conjetura.
	//
	// Pero armar desde el stub significa preguntar en CADA despacho, y preguntar
	// 13 millones de veces por partida por un candidato que puede tardar minutos
	// en madurar no es medir: es molestar. La puerta da la cobertura sin el ruido:
	// un PC se re-julga como mucho una vez cada kGatePeriodConsiderations
	// CONSIDERACIONES (no cuadros, no segundos: consideraciones, que es la unidad
	// que este contador ve). Con la tasa del log —44 000 despachos de VU1 por
	// segundo— un periodo de 1024 re-julga cada PC ~43 veces por segundo, y el
	// piso de hotness (200 000 ejecuciones) se cruza con cientos de oportunidades
	// de juicio por delante. El costo que se agrega es el de la propia puerta: una
	// lectura y una comparacion por despacho, del orden del tally que 0.6.36 midio
	// en 0.65 ns. La cuenta que la puerta paga va dicha en el cierre:
	// `considerados == intentos + puerta-cerrada`, donde `considerados` es la
	// SUMA de las dos llegadas (stub y lento) y no un contador propio que pueda
	// contradecirla, y `intentos` —la cuenta que el log de 0.6.36 ya traia— es lo
	// que PASA por la puerta, no lo que llega a ella. Sin esa fila el log nuevo
	// mostraria intentos bajos sin poder explicar que la mayoria de los despachos
	// ni se evaluaron.
	//
	// La puerta es mecanica, no semantica: NUNCA cambia un veredicto. Cambiar su
	// periodo cambia cuantas veces se pregunta, no lo que se responde. Por eso
	// puede probarse en host completa (h14) y por eso un verde aqui no afirma que
	// un telefono vaya a construir superbloques: eso solo lo afirma el log.
	// ------------------------------------------------------------------
	static const uint32_t kGateSlots = 2048u;
	static const uint32_t kGateSlotMask = kGateSlots - 1u;
	static const uint32_t kGatePeriodConsiderations = 1024u;

	struct ArmGate
	{
		// `next[i]` = numero de consideracion a partir del cual el PC i vuelve a
		// ser juzgable. Cero al arrancar: todo PC se juzga la primera vez que se
		// ve (sin esto, un arranque con puerta cerrada no construiria nada nunca).
		uint32_t next[kGateSlots];
		uint32_t considered;

		ArmGate() { Clear(); }

		void Clear()
		{
			for (uint32_t i = 0; i < kGateSlots; i++)
				next[i] = 0u;
			considered = 0u;
		}

		static uint32_t IdxOf(uint32_t pc_bytes)
		{
			return (pc_bytes >> 3) & kGateSlotMask;
		}

		// Consulta + consume: sube el contador de consideraciones y devuelve true
		// si este PC se juzga AHORA (y re-arma su proxima ventana). Compara
		// envoltura-arriba: con u32 el contador da vuelta tras ~4e9
		// consideraciones —unas 26 horas de partida a 44 000/s— y una resta
		// convertida a con signo sigue dando el orden correcto en el vuelco.
		// Sin esta forma, al volver el contador se cerraria para los 2048 PCs a la
		// vez y el motor dejari de armar candidatos en una sesion larga, en silencio.
		bool Open(uint32_t pc_bytes)
		{
			const uint32_t i = IdxOf(pc_bytes);
			considered++;
			// `(int32_t)(considered - next[i]) >= 0` es "considered >= next[i]"
			// modulo envoltura. Solo para el primer juicio de un PC esto es
			// trivialmente cierto (next=0); despues importa exactamente lo de arriba.
			const int32_t delta = static_cast<int32_t>(considered - next[i]);
			if (delta < 0)
				return false;
			next[i] = considered + kGatePeriodConsiderations;
			return true;
		}

		// Cuantas casillas tienen una puerta abierta en este momento. Diagnostico
		// puro del arnes; el informe del motor no lo imprime (no es evidencia de
		// nada que pase en un telefono).
		uint32_t ReadyCount() const
		{
			uint32_t n = 0;
			for (uint32_t i = 0; i < kGateSlots; i++)
				if (static_cast<int32_t>(considered - next[i]) >= 0)
					n++;
			return n;
		}
	};
}
