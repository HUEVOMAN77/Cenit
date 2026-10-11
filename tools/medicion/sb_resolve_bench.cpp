// Medicion de la resolucion del superbloque: barrido O(256) vs indice O(1).
//
// Que es y que NO es: es un MICROBENCHMARK de host (x86 de esta sandbox) sobre las
// dos maneras de resolver «que hueco vive en este PC». No es un numero de FPS, no
// es del NCO-LX3, y no dice cuanto acelera un juego: dice cuantas lecturas se
// ahorraban POR RESOLUCION y cuanto vale eso en nanosegundos de CPU de escritorio.
// El numero del telefono llega cuando alguien prenda el motor en un juego real, y
// no se inventa.
//
// Vive versionado en el repo porque el README y el registro de versiones citan sus
// numeros, y un numero que nadie puede volver a sacar no es una medicion, es una
// opinion. Como se corre:
//   g++ -O2 -std=c++17 -Wall -Wextra -I app/src/main/cpp/pcsx2/arm64 
//     -o /tmp/sbperf tools/medicion/sb_resolve_bench.cpp && /tmp/sbperf
// La medicion lee MvuSbIndex.h DEL REPO (misma regla anti-copia-vieja que h9..h11) y
// antes de cronometrar comprueba que los dos metodos devuelven el mismo hueco en
// los 2048 PCs: medir un metodo que no da el mismo resultado es ruido.
//
// Salida de referencia (siete corridas, g++ 13 de host, -O2, misma maquina):
//   tabla vacia              barrido 101-105 ns/resolucion   indice 1.7-1.8 ns   ~58-62x
//   2 candidatos             barrido 115-120 ns              indice 1.7-1.8 ns   ~65-69x
//   256 candidatos (llena)   barrido 117-122 ns              indice 1.7-1.9 ns   ~65-67x
//   con auditoria 1 de cada 512 (2 candidatos): 1.8 + 115/512 = 1.9-2.1 ns de media,
//     o sea el muestreo anade ~0.22 ns por resolucion, un ~13 % sobre el indice solo.
// No se cita UNA corrida: se cita el rango de siete, porque un numero suelto de un
// microbenchmark de host se mueve solo y decir «66x» de una medida que va de 58x a
// 69x seria adornar, que es lo que este proyecto hizo mal en 0.6.25.
// Lo que se lee ahi, dicho sin adornos: el barrido cuesta casi lo MISMO con la
// tabla vacia que con ella llena — ese era exactamente el defecto que el comentario
// viejo llamaba «caro-cero». Y el indice no depende de cuantos candidatos haya.
#include "MvuSbIndex.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace std::chrono;

struct Slot
{
	bool inUse = false;
	unsigned startPC = 0;
};

static constexpr int kVariants = 256; // == kSbMaxVariants

// El barrido tal y como estaba en SbResolve/FindForPc antes de 0.6.33.
__attribute__((noinline)) static int Sweep(const std::vector<Slot>& tab, unsigned pc)
{
	for (int i = 0; i < kVariants; i++)
	{
		if (tab[i].inUse && tab[i].startPC == pc)
			return i;
	}
	return -1;
}

// La resolucion con indice + confirmacion contra la tabla, como la hace ahora
// FindSlotForPc (sin el muestreo de auditoria, que es 1 de cada 512 y se mide aparte).
__attribute__((noinline)) static int Indexed(const mVUSbIndex::Index& idx,
	const std::vector<Slot>& tab, unsigned pc)
{
	const int i = idx.Lookup(mVUSbIndex::Index::IdxOf(pc));
	if (i >= 0 && tab[i].inUse && tab[i].startPC == pc)
		return i;
	return -1;
}

static double benchNanos(const std::vector<Slot>& tab,
	const mVUSbIndex::Index& idx, const std::vector<unsigned>& pcs, int mode, int reps)
{
	volatile int sink = 0;
	const auto t0 = steady_clock::now();
	for (int r = 0; r < reps; r++)
	{
		for (size_t k = 0; k < pcs.size(); k++)
			sink += (mode == 0) ? Sweep(tab, pcs[k]) : Indexed(idx, tab, pcs[k]);
	}
	const auto t1 = steady_clock::now();
	(void)sink;
	return duration_cast<nanoseconds>(t1 - t0).count() / double(pcs.size() * double(reps));
}

static void runCases()
{
	// Tres situaciones reales del motor, las tres con el MISMO set de PCs:
	//  (a) tabla vacia  -> el caso comun al empezar y el que el comentario viejo
	//      llamaba «caro-cero» pagando 256 lecturas
	//  (b) 2 candidatos -> lo que se ve casi siempre en juego
	//  (c) 256 candidatos -> el peor caso, la tabla llena
	static const int kCases = 3;
	const char* names[kCases] = { "tabla vacia", "2 candidatos", "256 candidatos (llena)" };

	for (int c = 0; c < kCases; c++)
	{
		// UNA sola tabla, leida por las dos implementaciones: ni Sweep ni Indexed
		// escriben, asi que la unica variable de la comparacion es el metodo.
		std::vector<Slot> tab(kVariants);
		mVUSbIndex::Index idx;
		int live = 0;
		if (c == 1)
			live = 2;
		else if (c == 2)
			live = kVariants;
		for (int i = 0; i < live; i++)
		{
			tab[i].inUse = true;
			tab[i].startPC = unsigned(i * 8);
			idx.Bind(mVUSbIndex::Index::IdxOf(tab[i].startPC), unsigned(i));
		}

		// Los MISMOS PCs de despacho para los dos metodos: los 2048 PCs de entrada
		// posibles de VU1, que es lo que el dispatcher le pasa a SbResolve. En el
		// caso (a) ninguno esta enlazado — el caso que el comentario viejo llamaba
		// «caro-cero».
		std::vector<unsigned> pcs;
		for (unsigned p = 0; p < 2048u; p++)
			pcs.push_back((p * 8u) & 0x3ff8u);

		// Equivalencia PRIMERO (una medicion de un metodo que no da el mismo
		// resultado que el otro no es una medicion, es ruido):
		for (size_t k = 0; k < pcs.size(); k++)
		{
			if (Sweep(tab, pcs[k]) != Indexed(idx, tab, pcs[k]))
			{
				std::printf("CASO %s: DIVERGENCIA en PC %04x\n", names[c], pcs[k]);
				return;
			}
		}

		const int reps = 20000;
		const double sw = benchNanos(tab, idx, pcs, 0, reps);
		const double ix = benchNanos(tab, idx, pcs, 1, reps);
		std::printf("%-26s barrido %8.1f ns/resolucion   indice %6.1f ns   %.0fx menos  (equivalentes en 2048 PCs)\n",
			names[c], sw, ix, sw / ix);
	}

	// El peaje de la autoverificacion, para que la decision se tome con el coste
	// puesto: 1 de cada 512 resoluciones paga el barrido.
	{
		std::vector<Slot> tab(kVariants);
		mVUSbIndex::Index idx;
		for (int i = 0; i < 2; i++)
		{
			tab[i].inUse = true;
			tab[i].startPC = unsigned(i * 8);
			idx.Bind(mVUSbIndex::Index::IdxOf(tab[i].startPC), unsigned(i));
		}
		std::vector<unsigned> pcs;
		for (unsigned p = 0; p < 2048u; p++)
			pcs.push_back((p * 8u) & 0x3ff8u);
		const double sw = benchNanos(tab, idx, pcs, 0, 20000);
		const double ix = benchNanos(tab, idx, pcs, 1, 20000);
		const double amort = ix + sw / 512.0;
		std::printf("\ncon auditoria 1/512 (2 candidatos): indice %.1f ns + barrido/512 = "
			"%.1f ns de media  (%.2f%% del indice solo)\n", ix, amort, 100.0 * (sw / 512.0) / ix);
		std::printf("comparado con el barrido puro de 0.6.32: %.1f ns -> %.1f ns de media\n",
			sw, amort);
	}
}

int main(void)
{
	runCases();
	return 0;
}
