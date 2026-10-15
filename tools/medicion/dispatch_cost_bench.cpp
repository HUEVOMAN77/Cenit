// Medicion del coste de RESOLUCION de despacho de VU1: barrido lineal de quickLookup
// vs memo O(1).
//
// Que es y que NO es: es un MICROBENCHMARK de host (el x86 de esta sandbox) sobre las
// dos maneras de resolver «que bloque compilado corresponde a este (PC, estado)». No es
// un numero de FPS, no es del NCO-LX3, y no dice cuanto acelera un juego: dice cuanto
// cuesta, en nanosegundos de CPU de escritorio, el barrido que el fast path deja de
// pagar cuando acierta. El numero que importa en el telefono llega con la linea de
// telemetria del cierre del VM (consults/served por sesion) y no se inventa aqui.
//
// Que NO mide, dicho para que nadie lea de mas:
//   - No mide el despacho REAL de PCSX2 (que paga ademas leer start_pc, dos saltos de
//     puntero por quick[], el bookkeeping prog.cur/isSame/quick.block y el salto al
//     JIT). Se cronometra SOLO la resolucion, que es lo unico que el memo cambia.
//   - No mide un game profile real: las longitudes de lista y la distribucion de
//     aciertos de un juego viven en el telefono.
//   - No dice «x% mas FPS». Una resolucion de N ns es N ns por DESPACHO, y cuantos
//     despachos por frame hay en un juego concreto no se sabe aqui.
//   - El bucle cronometrado pregunta siempre por la misma clave (el caso caliente), asi
//     que la rama esta perfectamente predicha. Eso favorece a los dos metodos por igual
//     y es el escenario para el que el memo existe; no es el promedio de un juego.
//
// La regla que hace que esto sea una medicion y no una opinion: antes de cronometrar se
// comprueba que los dos metodos dan la MISMA respuesta para la misma clave (el mismo
// bloque), igual que en sb_resolve_bench.cpp. Cronometrar un metodo que no resuelve lo
// mismo es ruido. Y el memo se llena POR EL CAMINO LEGITIMO (Record), no escribiendo la
// entrada a mano: si Record se niega (por el umbral), el memo no existe en esa fila y se
// dice, en vez de inventar un numero.
//
// Como se corre (misma regla anti-copia-vieja que h9..h12: lee el header DEL REPO):
//   g++ -O2 -std=c++17 -Wall -Wextra -I app/src/main/cpp/pcsx2/arm64
//   -o /tmp/dispbench tools/medicion/dispatch_cost_bench.cpp
//   /tmp/dispbench
//
// Salida de referencia (g++ 13 de host x86 de esta sandbox, -O2, mejor y peor de siete
// pasadas por fila; se cita el RANGO porque un numero suelto de un microbenchmark se
// mueve solo, y eso es exactamente lo que este proyecto hizo mal en 0.6.25):
//   4 refs (pos 3, bajo umbral)   barrido 2.2-2.3 ns    memo NO APLICABLE
//   8 refs (pos 7, bajo umbral)   barrido 4.7-4.8 ns    memo NO APLICABLE
//   9 refs (pos 8, EL umbral)     barrido 4.2-4.4 ns    memo 2.6-2.8 ns  (1.5x)
//   48 refs                       barrido 21.4-34.6 ns  memo 2.6-3.9 ns  (5.5x)
//   192 refs                      barrido 84.7-91.1 ns  memo 2.6-2.9 ns  (29.3x)
// Lo que se lee ahi, sin adornos: Serve cuesta practicamente lo mismo con 9 que con 192
// refs (una linea de cache, numero fijo de comparaciones), y el barrido crece con la
// lista. La fila del umbral (1.5x) es la que justifica kMinRecordPos = 8: justo en el
// borde el memo apenas empata, y por DEBAJO del borde preguntar seria pagar por no
// ahorrar — ahi el bench no da numero, dice "NO APLICABLE". Nada de esto es un numero
// del NCO-LX3 ni de FPS: es el coste de la ESTRUCTURA en un x86 de escritorio.
#include "MvuFastPath.h"

#include <chrono>
#include <cstdio>
#include <vector>

using namespace mVUFastPath;

// El barrido de hoy, replicado con la fidelidad de la que se dispone en host: la rama
// rapida de microBlockManager::search (microVU-arm64.h:731-762) recorre un vector de
// refs (puntero + quick64) con la comparacion enmascarada. No es el codigo del motor —
// ese lo compila el CI — es la MISMA forma de acceso: una lectura por ref, sin indice.
struct Ref
{
	uint64_t key;
	void* block;
};

static int sweepWalk(const std::vector<Ref>& v, uint64_t key, uint32_t flagHack)
{
	const uint64_t m = flagHack ? ~kFlagMask : ~0ull;
	for (size_t i = 0; i < v.size(); i++)
	{
		if ((v[i].key & m) == (key & m))
			return (int)i;
	}
	return -1;
}

// La Query que el despacho armaba HOY con este PC y esta clave. Se arma dentro del
// bucle cronometrado, a proposito: el despacho real paga el flaghack y las dos epocas en
// CADA pregunta, y si el montaje de la Query quedara fuera del cronometro el numero del
// memo mentira por bajo.
static Query makeQuery(uint32_t pc, uint64_t key, uint64_t epoch, uint64_t gen,
	uint64_t progId, uint64_t mgrId, uint32_t flagHack, uint64_t cstart, uint64_t cend)
{
	Query q;
	q.pc = pc;
	q.vuId = kVU1;
	q.flagHack = flagHack;
	q.exact = 0u;
	q.cacheEpoch = epoch;
	q.candidateEpoch = gen;
	q.progId = progId;
	q.mgrId = mgrId;
	q.quick64 = key;
	q.cacheStart = cstart;
	q.cacheEnd = cend;
	return q;
}

static const uint64_t kCacheStart = 0x10000000ull;
static const uint64_t kCacheEnd = kCacheStart + 0x2000000ull;
static const uint32_t kIters = 200000u;
static uint64_t g_sink = 0; // acumulador de los cronometros: evita el pliegue y se revisa al final

int main(void)
{
	// Tres longitudes de lista, y son las que importan: por debajo del umbral (donde el
	// memo esta PROHIBIDO por la ley de costo y la unica columna honesta es la del
	// barrido), justo en el umbral, y larga (donde el memo existe para servir).
	// Cuatro filas, y la segunda y tercera son el borde: con 8 refs el acierto mas
	// lejano cae en posicion 7 (0-based), o sea UNA posicion por debajo del umbral —
	// ahi el memo esta prohibido y el bench lo dice en vez de dar un numero bonito. Con
	// 9 refs la posicion 8 ES el umbral, y ahi es donde el memo empieza a existir.
	static const unsigned kLens[5] = { 4u, 8u, 9u, 48u, 192u };
	static const char* kNames[5] = { "4 refs (pos 3, bajo umbral)",
		"8 refs (pos 7, bajo umbral)", "9 refs (pos 8, EL umbral)", "48 refs", "192 refs" };

	int equivFail = 0;
	int memoRefused = 0;
	int verdictFail = 0;

	for (int s = 0; s < 5; s++)
	{
		const unsigned L = kLens[s];

		// Mundo: L refs con claves DISTINTAS, cada una con su PC (en casillas
		// distintas). El acierto caliente es el ULTIMO ref, que es el peor caso del
		// barrido y el unico en el que el memo debe existir. Ponerlo al principio seria
		// trampa: ahi el memo esta prohibido por la ley de costo.
		std::vector<Ref> v;
		v.reserve(L);
		std::vector<uint32_t> lpc(L);
		for (unsigned i = 0; i < L; i++)
		{
			Ref r;
			r.key = 0x40000000ull | ((uint64_t)(i + 1u) << 16);
			r.block = (void*)(kCacheStart + 0x40ull * (i + 1u));
			v.push_back(r);
			lpc[i] = (uint32_t)(i * 8u); // PCs del dominio, todos en casillas distintas
		}
		const uint64_t hotKey = v.back().key;
		const uint32_t hotPc = lpc.back();

		// Llenado POR EL CAMINO LEGITIMO: Record con la posicion que dio el barrido.
		Table tab;
		tab.Reset();
		unsigned walkPos = 0;
		for (unsigned i = 0; i < L; i++)
		{
			const int idx = sweepWalk(v, v[i].key, 0u);
			walkPos = (unsigned)(idx < 0 ? 0 : idx);
			const Query q = makeQuery(lpc[i], v[i].key, 1ull, 7ull, 0x1000ull, 0x2000ull,
				0u, kCacheStart, kCacheEnd);
			tab.Record(q, (uint64_t)v[i].block, walkPos);
		}

		// EQUIVALENCIA ANTES DE CRONOMETRAR: la clave caliente tiene que dar el mismo
		// bloque por los dos caminos. Si Record se nego (umbral), Serve tiene que NEGAR
		// la respuesta — y esa fila se reporta como "memo no aplicable", no como numero.
		{
			uint64_t got = 0;
			const Query aq = makeQuery(hotPc, hotKey, 1ull, 7ull, 0x1000ull, 0x2000ull,
				0u, kCacheStart, kCacheEnd);
			const bool served = tab.Serve(aq, &got);
			const uint64_t want = (uint64_t)v.back().block;
			const bool shouldBeServed = (walkPos >= kMinRecordPos);
			if (shouldBeServed && (!served || got != want))
			{
				std::printf("%-22s EQUIVALENCIA ROTA: el memo no reproduce el barrido "
					"(pos %u) — no se cronometra\n", kNames[s], walkPos);
				equivFail++;
				continue;
			}
			if (!shouldBeServed && served)
			{
				std::printf("%-22s EQUIVALENCIA ROTA: el memo sirvio un barrido por "
					"debajo del umbral — no se cronometra\n", kNames[s]);
				equivFail++;
				continue;
			}
			if (!shouldBeServed)
				memoRefused++;
		}

		// -- cronometro del barrido (7 pasadas, se guarda la mejor y el rango)
		uint64_t sink = 0;
		double wmin = 1e30, wmax = 0, mmin = 1e30, mmax = 0;
		for (int t = 0; t < 7; t++)
		{
			auto t0 = std::chrono::steady_clock::now();
			for (uint32_t i = 0; i < kIters; i++)
			{
				const int j = sweepWalk(v, hotKey, 0u);
				sink += (uint64_t)j + (uint64_t)v[j].block;
			}
			auto t1 = std::chrono::steady_clock::now();
			const double w = std::chrono::duration<double, std::nano>(t1 - t0).count() / kIters;
			if (w < wmin) wmin = w;
			if (w > wmax) wmax = w;

			auto t2 = std::chrono::steady_clock::now();
			for (uint32_t i = 0; i < kIters; i++)
			{
				const Query q = makeQuery(hotPc, hotKey, 1ull, 7ull, 0x1000ull, 0x2000ull,
					0u, kCacheStart, kCacheEnd);
				uint64_t got = 0;
				if (tab.Serve(q, &got))
					sink += got;
			}
			auto t3 = std::chrono::steady_clock::now();
			const double m = std::chrono::duration<double, std::nano>(t3 - t2).count() / kIters;
			if (m < mmin) mmin = m;
			if (m > mmax) mmax = m;
		}

		std::printf("%-22s barrido %6.1f-%6.1f ns   ", kNames[s], wmin, wmax);
		if (walkPos >= kMinRecordPos)
			std::printf("memo %5.1f-%5.1f ns (%4.1fx menos)", mmin, mmax, wmin / mmax);
		else
			std::printf("memo NO APLICABLE (umbral %u: preguntar aqui seria pagar por no "
				"ahorrar)", kMinRecordPos);
		std::printf("\n");
		if (sink == 0)
			std::printf("  (sink==0: los bucles se optimizaron fuera, la fila no vale)\n");
	}

	std::printf("\n");
	// ==========================================================================
	// Cenit 0.6.36 — la fila que faltaba: CUANTO CUESTA LA PROPIA TELEMETRIA.
	//
	// AskCounted suma una cuenta en CADA despacho de VU1, incluidos los que la ley de
	// costo PROHIBE preguntar (el salto corto). En el perfil que midio el log de God of
	// War son ~43 900 despachos/s elegibles, o sea un contador que se suma ~43 900 veces
	// por segundo para poder decir "no pregunte". Eso es pagar por medir y la prohibicion
	// de pagar por no ahorro se aplica a mi propia telemetria, no la eximo por ser mia.
	//
	// Cuatro cronometrajes en dos PARES de la MISMA entrada, para que la unica
	// diferencia dentro de un par sean las escrituras:
	//   (a) ShouldAsk sobre la mezcla / (b) AskCounted sobre la misma mezcla
	//   (c) ShouldAsk sobre solo-corto / (d) AskCounted sobre solo-corto
	// La mezcla ejerce los tres motivos (corto/pregunta/exacto); solo-corto es el camino
	// caliente del perfil real y el unico que hay que justificar.
	//
	// Dos trampas que esta version ya piso, escritas para que no se repitan:
	//   1. Con una entrada constante, -O2 pliega la decision fuera del bucle y reduce
	//      "stats.lookups++" a una suma de registro: da 0.00 ns y miente por bajo. Por
	//      eso las tablas van al monton (la memoria escapa al SRA) y el bucle se
	//      auto-verifica abajo contra cuentas hechas a partir de la SECUECIA, no de la
	//      tabla (si no, la comprobacion seria tautologica).
	//   2. Un bucle cerrado que hace read-modify-write sobre la MISMA direccion arrastra
	//      una cadena store->load (~4-5 ciclos) que el despacho real NO tiene: entre dos
	//      lookups hay todo el camino del dispatcher. Por eso (e)/(f) repiten el camino
	//      caliente sobre ocho tablas distintas, que es la medida honesta del delta, y
	//      el veredicto usa el PEOR numero observado de las dos formas.
	// ==========================================================================
	{
		static const uint32_t kSeqLen = 512u;
		static const uint32_t kIters2 = 2048u * 1000u; // multiplo EXACTO de kSeqLen
		static int seqCounts[kSeqLen];
		static uint32_t seqExact[kSeqLen];
		static int seqShort[kSeqLen];
		for (uint32_t i = 0; i < kSeqLen; i++)
		{
			// mezcla: 5/8 corto, 2/8 pregunta, 1/8 exacto. No es un perfil medido en el
			// telefono: es una mezcla que ejercita los tres caminos en la proporcion que
			// el log hace plausible, dicho asi sin venderlo como dato del juego.
			const unsigned band = i % 8u;
			seqCounts[i] = (band < 5u) ? (int)(kMinRecordPos - 1u)
									   : (band < 7u) ? (int)(16u + band)
													 : (int)(kMinRecordPos - 2u);
			seqExact[i] = (band == 7u) ? 1u : 0u;
			seqShort[i] = (int)band; // 0..7: todos por debajo del umbral, nunca constantes
		}
		const uint32_t rounds = kIters2 / kSeqLen;
		uint32_t wantShort = 0, wantAsk = 0, wantExact = 0;
		for (uint32_t i = 0; i < kSeqLen; i++)
		{
			if (seqExact[i] != 0u) wantExact++;
			else if (seqCounts[i] < (int)kMinRecordPos) wantShort++;
			else wantAsk++;
		}

		Table* mixCounted = new Table();
		Table* shortCounted = new Table();
		Table* spread[8];
		mixCounted->Reset(); shortCounted->Reset();
		for (int j = 0; j < 8; j++) { spread[j] = new Table(); spread[j]->Reset(); }

		auto run = [&](Table* tab, bool counted, bool shortOnly) -> double {
			// UN cronometro, seis usos: la unica diferencia entre llamadas es que tabla
			// se usa, si se cuenta y que secuencia se come. Todo lo demas (longitud,
			// orden, costo del bucle) es identico a proposito.
			uint64_t acc = 0;
			const auto t0 = std::chrono::steady_clock::now();
			for (uint32_t r = 0; r < rounds; r++)
			{
				if (shortOnly)
				{
					if (counted)
						for (uint32_t i = 0; i < kSeqLen; i++)
							acc += (uint64_t)tab->AskCounted(0u, seqShort[i]);
					else
						for (uint32_t i = 0; i < kSeqLen; i++)
							acc += (uint64_t)Table::ShouldAsk(0u, seqShort[i]);
				}
				else if (counted)
				{
					for (uint32_t i = 0; i < kSeqLen; i++)
						acc += (uint64_t)tab->AskCounted(seqExact[i], seqCounts[i]);
				}
				else
				{
					for (uint32_t i = 0; i < kSeqLen; i++)
						acc += (uint64_t)Table::ShouldAsk(seqExact[i], seqCounts[i]);
				}
			}
			const auto t1 = std::chrono::steady_clock::now();
			g_sink += acc;
			return std::chrono::duration<double, std::nano>(t1 - t0).count() / kIters2;
		};

		// (i & 7) reparte el camino caliente sobre ocho tablas. En una sola tabla el
		// bucle puede cerrar una cadena store->load sobre la MISMA direccion que el
		// despacho real no tiene (entre dos lookups hay todo el camino del dispatcher);
		// con ocho se corta la cadena, pero a cambio se tocan ocho lineas de cache
		// distintas, que tampoco es el patron del telefono. Ninguna de las dos es "la
		// medida honesta": son dos acotaciones del mismo coste, y el veredicto usa la
		// PEOR observada.
		auto runSpread = [&]() -> double {
			uint64_t acc = 0;
			const auto t0 = std::chrono::steady_clock::now();
			for (uint32_t r = 0; r < rounds; r++)
				for (uint32_t i = 0; i < kSeqLen; i++)
					acc += (uint64_t)spread[i & 7u]->AskCounted(0u, seqShort[i]);
			const auto t1 = std::chrono::steady_clock::now();
			g_sink += acc;
			return std::chrono::duration<double, std::nano>(t1 - t0).count() / kIters2;
		};

		double a0 = 1e30, a1 = 0, b0 = 1e30, b1 = 0, c0 = 1e30, c1 = 0, d0 = 1e30, d1 = 0;
		double e0 = 1e30, e1 = 0, f0 = 1e30, f1 = 0;
		for (int t = 0; t < 7; t++)
		{
			const double a = run(mixCounted, false, false);
			if (a < a0)
				a0 = a;
			if (a > a1)
				a1 = a;
			const double b = run(mixCounted, true, false);
			if (b < b0)
				b0 = b;
			if (b > b1)
				b1 = b;
			const double c = run(shortCounted, false, true);
			if (c < c0)
				c0 = c;
			if (c > c1)
				c1 = c;
			const double d = run(shortCounted, true, true);
			if (d < d0)
				d0 = d;
			if (d > d1)
				d1 = d;
			const double e = runSpread();
			if (e < e0)
				e0 = e;
			if (e > e1)
				e1 = e;
			const double f = run(shortCounted, false, true);
			if (f < f0)
				f0 = f;
			if (f > f1)
				f1 = f;
		}

		std::printf("\n");
		std::printf("telemetria propia (x86 -O2 de sandbox, NO del telefono):\n");
		std::printf("  mezcla    : sin contar %5.2f-%5.2f ns | contado %5.2f-%5.2f ns | delta %5.2f ns\n",
			a0, a1, b0, b1, (b0 + b1) / 2.0 - (a0 + a1) / 2.0);
		const double dChainLo = d0 - c1, dChainHi = d1 - c0;
		std::printf("  solo-corto: sin contar %5.2f-%5.2f ns | contado %5.2f-%5.2f ns | delta %5.2f-%5.2f ns "
			"(una sola tabla: cadena de dependencia store->load)\n",
			c0, c1, d0, d1, dChainLo, dChainHi);
		const double dSpreadLo = e0 - f1, dSpreadHi = e1 - f0;
		std::printf("  solo-corto: sin contar %5.2f-%5.2f ns | contado %5.2f-%5.2f ns | delta %5.2f-%5.2f ns "
			"(8 tablas: sin cadena sobre una direccion)\n",
			f0, f1, e0, e1, dSpreadLo, dSpreadHi);

		// -- auto-verificacion del propio cronometro: las cuentas tienen que valer lo que
		//    la secuencia dice, por las 7 pasadas. consults NO se mueve aqui porque esta
		//    fila cronometra la DECISION sola (Serve no se llama); el invariante de la
		//    decision sola es lookups == skip-exact + skip-short.
		const uint64_t total = (uint64_t)rounds * kSeqLen * 7ull;
		int mal = 0;
		if (mixCounted->stats.lookups != total) mal |= 1;
		if (mixCounted->stats.skipShort != (uint64_t)wantShort * rounds * 7ull) mal |= 2;
		if (mixCounted->stats.skipExact != (uint64_t)wantExact * rounds * 7ull) mal |= 4;
		// consults NO se mueve en esta fila: lo mueve Serve y aqui se cronometra la
		// decision sola. O sea el invariante que tiene que cerrar es
		//     lookups == skip-exact + skip-short + (lo que pregunt6, sin servir)
		// y pedir consults == wantAsk seria pedirle al cronometro algo que no hizo.
		if (mixCounted->stats.consults != 0ull) mal |= 8;
		if (mixCounted->stats.lookups !=
				mixCounted->stats.skipExact + mixCounted->stats.skipShort +
				(uint64_t)wantAsk * rounds * 7ull) mal |= 16;
		if (shortCounted->stats.lookups != total || shortCounted->stats.skipShort != total) mal |= 32;
		uint64_t spreadLookups = 0, spreadShort = 0;
		for (int j = 0; j < 8; j++) { spreadLookups += spread[j]->stats.lookups; spreadShort += spread[j]->stats.skipShort; }
		if (spreadLookups != total || spreadShort != total) mal |= 64;
		if (mal != 0)
		{
			std::printf("  EL BENCH NO CUADRA (bitmask %d): mezcla lookups=%llu corto=%llu "
				"exacto=%llu consults=%llu, se esperaba %llu/%u/%u/0; cortos=%llu. O el "
				"bucle se plego (las filas de arriba no valen) o AskCounted no cuenta lo "
				"que dice.\n", mal,
				(unsigned long long)mixCounted->stats.lookups,
				(unsigned long long)mixCounted->stats.skipShort,
				(unsigned long long)mixCounted->stats.skipExact,
				(unsigned long long)mixCounted->stats.consults,
				(unsigned long long)total, wantShort, wantAsk,
				(unsigned long long)spreadShort);
			equivFail++;
		}
		else
		{
			std::printf("  cuentas del cronometro cuadradas sobre %llu decisiones: la "
				"mezcla dio %u/8 cortos, %u/8 preguntas, %u/8 exactos y lookups sumo los "
				"tres motivos — el bucle NO se plego, las filas de arriba valen\n",
				(unsigned long long)total, wantShort, wantAsk, wantExact);
		}

		// El limite se FIJA AQUI, antes de mirar el numero, y en la unidad correcta.
		// Comparar "1.5 ns de cuenta" contra "1.6 ns de ahorro del memo" es trampa de
		// unidades: la cuenta se paga en TODOS los despachos y el ahorro solo en los
		// SERVIDOS, y cuantos son servidos es justo lo que este cambio va a medir en el
		// telefono. Lo que si se puede afirmar con lo que hay es el coste total contra el
		// presupuesto del aparato, con la tasa que el log midio y no con una supuesta.
		// Limite autoimpuesto: la telemetria no puede comer mas del 1 % de un nucleo.
		const double kRate = 43900.0;     // despachos/s de VU1 elegibles, del log de GoW
		const double kLimitUsPerSec = 10000.0; // 1 % de un nucleo
		const double worst = (dChainHi > dSpreadHi) ? dChainHi : dSpreadHi;
		const double usPerSec = worst * kRate / 1000.0;
		const double usPerFrame = usPerSec / 47.32; // 47.32 fps medidos en esa sesion
		std::printf("  con la tasa del log (%.0f despachos/s) y el PEOR delta observado "
			"(%.2f ns): %.1f us de CPU por segundo = %.4f %% de un nucleo; %.2f us por "
			"frame de %.0f us (%.4f %% del frame). Limite autoimpuesto: 1 %% = %.0f us/s.\n",
			kRate, worst, usPerSec, usPerSec / 1e4, usPerFrame, 1e6 / 47.32,
			usPerFrame / (1e6 / 47.32) * 100.0, kLimitUsPerSec);
		if (usPerSec <= kLimitUsPerSec)
		{
			std::printf("  Veredicto: medir cabe en el limite, asi que la cuenta se queda "
				"SIN apagar y SIN muestrear (un contador muestreado romperia el invariante "
				"lookups == consults + skip-exact + skip-short, y un invariante roto a "
				"proposito no es telemetria, es decoracion).\n");
		}
		else
		{
			std::printf("  Veredicto: medir cuesta %.1f us/s, por encima del 1 %% de un "
				"nucleo. La cuenta del camino que no usa el memo se apaga o se muestrea "
				"antes de cerrar la version; NO se deja 'porque es telemetria'.\n", usPerSec);
			verdictFail++;
		}
		std::printf("  Lo que ESTO NO AFIRMA: que el delta sea %.2f ns en el Snapdragon "
			"778G — es x86 de sandbox, y una escritura puede costar varios multiples ahi. "
			"Tampoco dice FPS. Dice que el coste de medir es del orden de las decenas de "
			"microsegundos por segundo, es decir tres o cuatro ordenes de magnitud por "
			"debajo de lo que este proyecto puede pagar, y que por tanto medir no es la "
			"razon por la que el fast path sigue necesitando una prueba en el aparato: la "
			"prueba que falta es la linea de cierre de una version >= 0.6.36.\n", worst);

		if (g_sink == 0)
			std::printf("  (sink==0: los bucles se optimizaron fuera, las filas no valen)\n");
		delete mixCounted; delete shortCounted;
		for (int j = 0; j < 8; j++) delete spread[j];
	}

	std::printf("Leidos asi, estos numeros dicen: el barrido lineal cuesta ~una lectura\n");
	std::printf("por ref sobre la lista (lineas distintas de cache) y crece con la lista;\n");
	std::printf("Serve cuesta un numero FIJO de comparaciones sobre UNA linea. Lo que NO\n");
	std::printf("dicen: ni FPS, ni cuanto tarda un despacho real (que paga ademas\n");
	std::printf("start_pc, dos punteros y el bookkeeping), ni cuantos despachos por frame\n");
	std::printf("hay en un juego. Eso solo lo dice el telefono, con la linea\n");
	std::printf("«Cenit fastpath VU1» del cierre del VM.\n");
	std::printf("Filas sin numero de memo: %d — son el caso prohibido por la ley\n", memoRefused);
	std::printf("de costo, no un fallo.\n");

	if (equivFail)
	{
		std::printf("\nRESULTADO: RUIDO — equivalencia rota en %d fila(s)\n", equivFail);
		return 1;
	}
	std::printf("\nRESULTADO: OK — los dos metodos resolvieron lo mismo antes de cronometrar.\n");
	return 0;
}
