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
