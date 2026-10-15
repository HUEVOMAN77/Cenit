// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

// Cenit 0.6.35 — Fast Path VU1: memoria O(1) del barrido de despacho.
//
// Que es esto, dicho con la unidad correcta. NO es un segundo ejecutor de
// bloques, ni un candidato, ni un superbloque, ni una semantica nueva: es la
// memoria de LA UNICA pregunta que el despacho de VU1 hace muchisimas veces con
// la misma respuesta. El bloque que sirve ya existia y ya era el que el camino
// normal ejecutaba; lo unico que cambia es como se llega a el.
//
// El coste real, leido en el codigo (no supuesto):
//   mVUlookupProg<1> (microVU-arm64.cpp) por cada despacho hace
//     1) leer mVU.regs().start_pc y enmascarar,
//     2) cargar mVU.prog.quick[pc/8].prog          (salto de puntero #1),
//     3) cargar quick.prog->block[pc/8]            (salto de puntero #2),
//     4) microBlockManager::search() — que en la rama rapida RECORRE
//        quickLookup LINEALMENTE (microVU-arm64.h:731-762).
//   El paso 4 es lo que este archivo elimina de los despachos calientes. Los
//   pasos 1-3 se pagan igual: el fast path no finge ahorrarse los saltos de
//   puntero que la validacion necesita leer de todos modos, y no toca los
//   efectos secundarios del despacho (prog.cur, prog.isSame, quick[].block,
//   observed.record), que son parte de la semantica que el cache de saltos del
//   JIT lee despues (microVU_Branch-arm64.inl:493 lee quick[].prog como
//   identidad de programa).
//
// Por que memoizar la RESOLUCION es seguro (el argumento, no la esperanza):
//   search() en la rama rapida devuelve el PRIMER ref de quickLookup cuyo
//   `ref.quick` coincida con la clave del candidato bajo la mascara vigente
//   (exacta si !flagHack, `& ~0x3002` si flagHack — la misma expresion de
//   microVU-arm64.h:734-737). Dos cosas hacen que "el primer match" sea una
//   FUNCION de la clave y no del estado de la lista:
//     (a) add() (microVU-arm64.h:653-686) deduplica con ESTE mismo search()
//         antes de insertar, asi que NUNCA hay dos refs que coincidan con la
//         misma clave bajo la misma mascara (la comparacion es simetrica y
//         transitiva para una mascara fija). Por eso insertar un bloque nuevo
//         no puede cambiar la respuesta de una clave que ya tenia respuesta.
//     (b) quickLookup solo crece por append y solo se vacia destruyendo el
//         gestor (~microBlockManager -> reset()), y eso ocurre unicamente en
//         mVUdeleteProg, llamado solo desde mVUreset/mVUclose.
//   O sea: lo que puede invalidar una respuesta es (i) cambiar la clave, (ii)
//   cambiar la mascara, (iii) que el microcodigo que la genero ya no sea ese,
//   (iv) destruir programa/gestor/cache. Los cuatro se comprueban aqui: clave y
//   mascara como campos, microcodigo por la generacion de escrituras, y
//   destruccion por la epoca del code cache. NADA se asume.
//
// Epocas (la razon de que no existan punteros obsoletos):
//   cacheEpoch     — sube en mVUreset/mVUclose: el code cache se destruye y las
//                    opciones del compilador se rehacen ahi (mVUbuildOptionsSentinel),
//                    o sea cualquier entrada guardada apunta a codigo libre.
//   candidateEpoch — snapshot de mVU.microMemWriteGen, que mVUclear sube en CADA
//                    escritura a la imagen micro. Es la invalidacion inmediata del
//                    fast path ante cambio de microcodigo, y cuesta un `inc`.
//   Un rechazo por epoca no es un fallo: es volver al despachador de siempre, que
//                    re-registra con datos vivos. Su coste se mide (Stats), no se
//                    supone.
//
// Umbrales: kMinRecordPos existe porque el memo NO es gratis. Servir cuesta ~8
//   comparaciones sobre una linea de cache; el barrido cuesta una comparacion por
//   ref. Memorizar un acierto en posicion 3 cambiaria 4 comparaciones por 8 — una
//   perdida disfrazada de optimizacion. Por eso solo se registran barridos cuyo
//   acierto cayo en posicion >= kMinRecordPos: cada entrada viva representa un
//   barrido que YA costaba mas de lo que cuesta el memo. Lo que el telefono puede
//   observar (y lo que host NO puede afirmar) es cuantos PCs de un juego real
//   llegan a ese umbral; para eso estan los contadores de Stats, que se vuelcan en
//   el cierre del VM.
//
// Que NO toca esta fase, dicho explicitamente:
//   - Los despachos con needExactMatch != 0 (comparacion de 96 bytes sobre
//     fBlockList con promocion MRU). Reordenar listas ajenas para memoizarlas es
//     exactamente el efecto secundario que no se puede replicar con una copia
//     barata, y no es el caso caliente: el despacho de entrada normal lleva
//     lpState a cero (contrato del programa con E-bit, pinado por
//     mvu_lpstate_invariant_tests.cpp). Siguen por el camino de siempre.
//   - mVUentryGet/mVUsearchProg (la ruta lenta). Una caida de memo cuesta el
//     mismo barrido de hoy.
//   - SHADOW, snapshot/restore, promocion de candidatos, ejecucion duplicada,
//     superbloques, VU0, MTVU, WaitVU, EECycleRate/Skip, xgkick, sincronizacion
//     EE<->VU, timing, registros de VU, memoria de VU, Q/P, ACC, banderas,
//     latencias, VPU_STAT, camino de excepcion: este archivo no los mira y no
//     los cambia.
//
// Verificacion: Harness12 (tools/arneses/h12-src) compila ESTE archivo tal cual
//   con g++ de host y fuerza equivalencia contra un modelo lineal del search()
//   real (mismo dedup de add(), misma mascara, mismo orden de insercion), con un
//   testigo por cada comprobacion de la lista de arriba. Lo que el arnes NO puede
//   probar — y por eso no lo afirma — es que el llamador pase la posicion que el
//   barrido realmente recorrio, ni el ancho de la ventana del code cache: eso vive
//   en microVU-arm64.cpp y lo cubre el CI.

#include <cstdint>
#include <cstring>

namespace mVUFastPath
{
	// Dominio de PCs de entrada de VU1: microMem 0x4000, una microinstruccion
	// cada 8 bytes, el dispatcher enmascara a 0x3ff8 -> 2048 entradas posibles.
	// Mismo dominio que MvuSbIndex.h y mVUTraceProbe::kProbeSlots por la misma
	// razon: es el numero de PCs de despacho que existen, no una tabla hash. Al
	// ser biyectivo, la casilla de un PC no puede colisionar con la de otro PC, y
	// por eso la comprobacion de PC que hace Serve es redundante... salvo por el
	// puntero: si IdxOf dejara de ser biyectivo (alguien cambia kSlots o el
	// corrimiento), esa comprobacion es la unica que evita servir el bloque de
	// otro PC. Se queda a proposito y Harness12 prueba precisamente eso.
	static constexpr uint32_t kSlots = 2048u;
	static constexpr uint32_t kSlotMask = kSlots - 1u;

	// Marcador de casilla vacia. 0xffffffff NO es un PC posible (el dominio es
	// 0x3ff8), asi que "vacia" no se confunde con "PC 0".
	static constexpr uint32_t kEmptyPc = 0xffffffffu;

	// Mascara del speedhack de banderas: la expresion literal de
	// microVU-arm64.h:734-737. Repetir el numero aqui es deliberado: el modelo de
	// Harness12 usa esta constante, y si alguien cambia la del header sin cambiar
	// esta, el verde del arnes no taparia la divergencia — la cubre la
	// comprobacion de abajo, que fija el valor exacto de la mascara negada.
	// Ojo al valor: ~0x3002 = 0xffffffffffffcffd. La version anterior de esta linea
	// tenia ...cfff, que es ~0x3000 — o sea DEJABA el bit 1 dentro de la mascara, y
	// el bit 1 es needExactMatch: un memo con esa mascara alzada habria tratado un
	// despacho exacto como rapido. La compilo g++ de host en Harness12 y ruidó, que
	// es exactamente para lo que existe el arnes.
	static constexpr uint64_t kFlagMask = 0x3002ull;
	static_assert(~kFlagMask == 0xffffffffffffcffdull,
		"la mascara del flaghack debe ser exactamente ~0x3002 (microVU-arm64.h:734-737)");

	// Umbral de registro, en posiciones del barrido (0-based). Ver comentario de
	// arriba: por debajo, el memo cuesta mas que lo que reemplaza.
	static constexpr uint32_t kMinRecordPos = 8u;

	// Id de VU que esta tabla sirve. El unico llamador es mVUlookupProg<1>; se
	// comprueba igual, para que "pertenece al mismo VU" sea codigo y no prosa.
	static constexpr uint32_t kVU1 = 1u;

	// Lo que el despacho sabe en el momento de preguntar. Todo son datos VIVOS
	// del dispatch normal — el fast path no inventa ninguna identidad.
	struct Query
	{
		uint32_t pc;         // PC de despacho YA enmascarado (& 0x3ff8)
		uint32_t vuId;       // mVU.index
		uint32_t flagHack;   // mVUsFlagHack vigente
		uint32_t exact;      // pState->needExactMatch != 0
		uint64_t cacheEpoch;     // epoca del code cache viva (mVUreset/mVUclose)
		uint64_t candidateEpoch; // mVU.microMemWriteGen vivo (mVUclear)
		uint64_t progId;         // microProgram* vivo (quick.prog)
		uint64_t mgrId;          // microBlockManager* vivo (quick.prog->block[pc/8])
		uint64_t quick64;        // pState->quick64[0], sin enmascarar
		uint64_t cacheStart;     // mVU.prog.x86start
		uint64_t cacheEnd;       // limite alto del cache (x86end + safe zone)
	};

	struct Entry
	{
		uint64_t cacheEpoch;     // epoca del cache al registrar
		uint64_t candidateEpoch; // microMemWriteGen al registrar
		uint32_t pc;             // kEmptyPc = casilla libre
		uint32_t vuId;
		uint64_t progId;         // identidad del programa del que se resolvio
		uint64_t mgrId;          // identidad del gestor que se recorrio
		uint64_t quickKey;       // ref.quick & mascara, tal como search compara
		uint64_t hostEntry;      // entrada del bloque YA compilado (no un duplicado)
		uint32_t flagHack;       // mascara con la que se registro
		uint32_t walkPos;        // posicion del acierto (telemetria de umbral)
	};
	static_assert(sizeof(Entry) == 64, "una entrada debe caber en una linea de cache");

	// Contadores de la estructura. No son simulacion: no entran en ninguna
	// decision de servicio. Sirven para que el telefono pueda decir cuanta de
	// esta tabla se usa, que es justo lo que host no puede saber.
	//
	// Cenit 0.6.36 — los tres nuevos (`lookups`, `skipExact`, `skipShort`) no
	// estaban, y su ausencia es la razon por la que esta seccion no podia dejar
	// de ser experimental con el log que mandaste: `consults` cuenta los
	// despachos que PREGUNTARON, y preguntar esta filtrado por la ley de costo,
	// asi que de `served/consults` no se puede deducir nada sobre el juego
	// completo. Sin total no hay denominador: 30 000 aciertos pueden ser el 90 %
	// de los despachos o el 0.5 %, y el log no lo distingue. Con `lookups` la
	// cuenta se cierra sola y vale el INVARIANTE:
	//     lookups == consults + skipExact + skipShort
	// que el arnes comprueba casilla por casilla y que el telefono imprime. Si
	// ese invariante se rompe en un aparato, el defecto es de la telemetria y se
	// ve, no se infiere meses despues.
	//
	// Y `rejectExact` queda dicho de una vez, porque su nombre mentia: en
	// produccion tiene que valer SIEMPRE 0 — los despachos exactos se cuentan en
	// skipExact, antes de llegar aqui. Su comprobacion dentro de Serve sigue
	// puesta (es la defensa contra un llamador que deje de filtrar), asi que un
	// rejectExact > 0 en el log de un telefono no es una medida: es la senal de
	// que la guarda del llamador se rompio.
	struct Stats
	{
		uint64_t lookups;         // despachos de VU1 que llegaron a la decision
		uint64_t skipExact;       // decision: no preguntar, needExactMatch != 0
		uint64_t skipShort;       // decision: no preguntar, lista por debajo del umbral
		uint64_t consults;        // despachos que preguntaron
		uint64_t serves;          // aciertos servidos (barrido evitado)
		uint64_t records;         // entradas escritas
		uint64_t recordSkipped;   // barridos cortos / fuera de dominio: no se memoriza
		uint64_t rejectEmpty;   // casilla libre
		uint64_t rejectPc;      // casilla ocupada por OTRO PC (indice no biyectivo)
		uint64_t rejectVu;      // casilla de otro VU
		uint64_t rejectEpoch;   // cache destruido o microcodigo escrito
		uint64_t rejectProg;    // otro programa en el mismo PC
		uint64_t rejectMgr;     // otro gestor para el mismo programa
		uint64_t rejectKey;     // microcodigo/estado distinto bajo la mascara
		uint64_t rejectBounds;  // puntero fuera del cache vivo: se limpia la casilla
		uint64_t rejectExact;   // despachos needExactMatch: ni se pregunta
		uint64_t invalidations; // Reset() por destruccion del cache
	};

	// La tabla. Sin punteros propios, sin duenos. No es atomica ni compartida: la
	// escribe y lee solo el hilo dispatcher de VU1 (MTVU con THREAD_VU1, hilo EE
	// sin el), igual que quickLookup — la misma exponibilidad, no nueva.
	struct Table
	{
		Entry at[kSlots];
		Stats stats;

		Table() { Reset(); }

		// Dejar la tabla sin ninguna respuesta, CONSERVANDO los contadores: lo que
		// el telefono necesita saber es cuanto se uso la estructura en la sesion
		// entera, y mVUreset no es raro (cada cambio de config, cada carga de
		// estado). Borrar las cuentas ahi daria un informe por epoca que se lee
		// como "casi nadie usa el memo".
		void Invalidate()
		{
			for (uint32_t i = 0; i < kSlots; i++)
				at[i].pc = kEmptyPc;
			stats.invalidations++;
		}

		// Limpiado total: casillas y cuentas. Solo al arrancar el VM (mVUinit).
		void Reset()
		{
			for (uint32_t i = 0; i < kSlots; i++)
				at[i].pc = kEmptyPc;
			std::memset(&stats, 0, sizeof(stats));
		}

		static uint32_t IdxOf(uint32_t pcBytes) { return (pcBytes >> 3) & kSlotMask; }
		static uint64_t MaskOf(uint32_t flagHack)
		{
			return flagHack ? ~kFlagMask : ~0ull;
		}

		// Cenit 0.6.36 — LA decision vive aqui y en ningun otro sitio, en forma de
		// motivo. Antes era un bool (ShouldAsk) y anadir contadores pedia repetir
		// las dos condiciones en otra funcion: dos copias de la ley de costo que
		// pueden divergir en el proximo retoque, y una divergencia entre lo que se
		// ejecuta y lo que se mide es peor que no medir — es medir otra cosa. Con
		// un unico DecideAsk, ShouldAsk es una proyeccion y AskCounted otra;
		// romper el nucleo pone rojas las dos, que es lo que quiere el ejecutor de
		// mutacion. Dos condiciones, ninguna negociable:
		//   - needExactMatch == 0: el dominio memoizado es la rama rapida de search().
		//   - lookupCount >= kMinRecordPos: con la lista corta el barrido cuesta menos
		//     lecturas que el memo; preguntar seria pagar por no ahorrar (ley de
		//     costo, medida en tools/medicion/dispatch_cost_bench.cpp). `lookupCount`
		//     es quickLookup.size() leido del gestor VIVO, no contado a mano.
		enum AskReason
		{
			kAsk = 0,        // preguntar
			kSkipExact,      // no preguntar: dominio exacto (efectos que la copia no tiene)
			kSkipShort       // no preguntar: la lista es corta, preguntar perderia
		};

		static AskReason DecideAsk(uint32_t needExactMatch, int lookupCount)
		{
			if (needExactMatch != 0u)
				return kSkipExact;
			if (lookupCount < static_cast<int>(kMinRecordPos))
				return kSkipShort;
			return kAsk;
		}

		static bool ShouldAsk(uint32_t needExactMatch, int lookupCount)
		{
			return DecideAsk(needExactMatch, lookupCount) == kAsk;
		}

		// La decision, contada: la que llama el motor. `lookups` se suma PRIMERO y
		// se sale por el motivo que sea; `consults` NO se suma aqui — lo suma
		// Serve — porque si no cada pregunta valdria dos y el invariante
		//     lookups == consults + skipExact + skipShort
		// se romperia justo en el caso que mas importa medir. Los dos no-preguntar
		// se cuentan SEPARADOS porque se leen distinto: skipShort alto dice "los
		// PCs calientes de este juego nunca llegan al umbral de 8" — el memo no
		// aplica, y eso hay que decirlo en vez de callarlo; skipExact alto dice
		// "el juego despacha con match exacto", otro dominio. La ausencia de estos
		// dos contadores es lo que dejo a esta seccion sin poder salir de
		// experimental con el log de God of War que mandaste: consults es un filtro
		// y un filtro no es un denominador.
		bool AskCounted(uint32_t needExactMatch, int lookupCount)
		{
			stats.lookups++;
			// switch sobre el motivo, con break y un unico return false al final:
			// sin default el compilador exige salida de la funcion de todos modos
			// (-Wreturn-type) y un default silenciaria la advertencia util si
			// alguien anade un AskReason nuevo. Con esta forma, anadir un motivo sin
			// contarlo deja de ser un error de compilacion pero lo pilla el arnes,
			// que comprueba el invariante lookups == consults + skipExact + skipShort
			// en cada despacho.
			switch (DecideAsk(needExactMatch, lookupCount))
			{
				case kAsk:
					return true;
				case kSkipExact:
					stats.skipExact++;
					break;
				case kSkipShort:
					stats.skipShort++;
					break;
			}
			return false;
		}

		// Seqlock, en una linea y por el mismo motivo: un acierto del memo es acierto
		// solo si la epoca del cache NO se movio entre leer los datos y usar la
		// respuesta (mVUreset pudo haber liberado el code cache en medio del lookup;
		// la casilla seguira "coherente" consigo misma porque Invalidate() aun no
		// habia corrido cuando se leyo). El llamador pasa la epoca que anoto al
		// preguntar y la que lee despues de Serve; si no coinciden, el acierto se
		// descarta y se corre el barrido de siempre.
		static bool EpochStable(uint64_t epochAtAsk, uint64_t epochNow)
		{
			return epochNow == epochAtAsk;
		}

		// Servir. Devuelve true SOLO si la casilla afirma exactamente la misma
		// respuesta que el barrido lineal daria con estas entradas vivas. Un
		// false no es un error: es "usa el despachador normal", que es el codigo
		// de siempre sin modificar.
		bool Serve(const Query& q, uint64_t* outHostEntry)
		{
			stats.consults++;

			// Fuera del dominio memoizado: ni se toca la tabla.
			if (q.exact)
			{
				stats.rejectExact++;
				return false;
			}

			Entry& e = at[IdxOf(q.pc)];

			if (e.pc == kEmptyPc)
			{
				stats.rejectEmpty++;
				return false;
			}
			if (e.pc != q.pc)
			{
				stats.rejectPc++;
				return false;
			}
			if (e.vuId != q.vuId)
			{
				stats.rejectVu++;
				return false;
			}
			if (e.cacheEpoch != q.cacheEpoch || e.candidateEpoch != q.candidateEpoch)
			{
				stats.rejectEpoch++;
				return false;
			}
			if (e.progId != q.progId)
			{
				stats.rejectProg++;
				return false;
			}
			if (e.mgrId != q.mgrId)
			{
				stats.rejectMgr++;
				return false;
			}
			if (e.flagHack != q.flagHack ||
				e.quickKey != (q.quick64 & MaskOf(q.flagHack)))
			{
				stats.rejectKey++;
				return false;
			}
			if (e.hostEntry < q.cacheStart || e.hostEntry >= q.cacheEnd)
			{
				// El puntero no esta dentro del cache vivo: la casilla ya no
				// describe nada ejecutable. Se limpia ahi mismo para que no se
				// vuelva a intentar con el mismo contenido.
				stats.rejectBounds++;
				e.pc = kEmptyPc;
				return false;
			}

			*outHostEntry = e.hostEntry;
			stats.serves++;
			return true;
		}

		// Registrar la respuesta de un barrido REAL. `hostEntry` es la entrada del
		// bloque que ESE barrido devolvio — el mismo bloque que el despacho normal
		// ejecuta — asi que no hay codigo nuevo: solo se recuerda donde estaba.
		bool Record(const Query& q, uint64_t hostEntry, uint32_t walkPos)
		{
			if (q.exact || q.vuId != kVU1 || walkPos < kMinRecordPos)
			{
				stats.recordSkipped++;
				return false;
			}
			if (hostEntry < q.cacheStart || hostEntry >= q.cacheEnd)
			{
				stats.recordSkipped++;
				return false;
			}

			Entry& e = at[IdxOf(q.pc)];
			// `pc` se escribe al ultimo, y no por elegancia: es la marca de
			// validez, asi que una casilla nunca queda "anunciada" con el resto
			// de los campos a medio poner. Ademas el unico escritor es el hilo
			// dispatcher, que es tambien el unico lector — no hay ventana
			// parcial que cerrar. kEmptyPc sigue mandando: una casilla con
			// pc == kEmptyPc cae por rejectEmpty antes que por cualquier campo.
			e.cacheEpoch = q.cacheEpoch;
			e.candidateEpoch = q.candidateEpoch;
			e.vuId = q.vuId;
			e.progId = q.progId;
			e.mgrId = q.mgrId;
			e.quickKey = q.quick64 & MaskOf(q.flagHack);
			e.hostEntry = hostEntry;
			e.flagHack = q.flagHack;
			e.walkPos = walkPos;
			e.pc = q.pc;
			stats.records++;
			return true;
		}

		// Cuantas casillas estan ocupadas (para el informe).
		uint32_t Count() const
		{
			uint32_t n = 0;
			for (uint32_t i = 0; i < kSlots; i++)
				if (at[i].pc != kEmptyPc)
					n++;
			return n;
		}
	};

	// Instancia unica del dispatcher de VU1. C++17 inline variable: una sola
	// definicion aunque la incluyan microVU-arm64.cpp y el arnes de host.
	inline Table g_vu1;
}
