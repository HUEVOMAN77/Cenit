// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

// Cenit 0.6.33 — Indice PC->hueco del motor de superbloques VU1.
//
// Por que existe (el defecto que cierra, medido, no opinado): el motor llama a
// SbResolve en CADA despacho de VU1 mientras esta encendido — tanto por el stub
// rapido emitido (microVU-arm64.cpp mVUlookupProg_VU1) como por la ruta lenta —,
// y SbResolve, SbFinish y FindForPc barrian LOS 256 huecos de la tabla en cada
// uno. El encabezado del motor decia «escaneo O(vivos)» y no era cierto: era
// O(kSbMaxVariants) por despacho, es decir el camino mas caliente del emulador
// pagando 256 lecturas de `inUse` cuando lo normal es que haya 0 o 2 candidatos
// vivos. Eso es exactamente lo que impide que un superbloque sea «real y
// conectado a todo el emulador» en vez de un peaje por cuadro.
//
// Este indice convierte esa resolucion en O(1): un arreglo de kSlots entradas
// (una por PC de entrada posible de VU1 — 0x4000/8 = 2048, mismo dominio que
// mVUTraceProbe::kProbeSlots) que apunta al hueco que vive en ese PC, o vacio.
//
// INVARIANTE (la unica cosa que hace correcto un Lookup O(1), y lo que Harness11
// fuerza contra un modelo lineal):
//   - Como mucho UN hueco `inUse` por startPC. El motor ya la mantiene por
//     construccion: SbArmCompile rechaza armar si FindForPc encuentra algo, la
//     adopcion conserva el PC, y el quemado de SbCloseCompile libera el hueco. El
//     indice NO puede representar dos, y si alguien la rompiera el Lookup se
//     equivoca — por eso la invariante se comprueba, no se asume.
//   - Toda operacion que cambia (startPC, inUse) de un hueco pasa por AQUI en el
//     mismo momento. Bind/Unbind/ClearAll son los unicos escritores; Lookup solo
//     lee. Un hueco reasignado (AllocSlot segunda pasada) se Unbind de su PC viejo
//     antes de Bind del nuevo.
//
// No es atomico ni compartido entre hilos: lo usa SOLO el hilo dispatcher de VU1
// (igual que la tabla de candidatos). Un lector externo (HUD/informe) recorre la
// tabla, no este indice.
//
// Vive en un header puro, sin dependencias de PCSX2, por la misma razon que
// MvuSbLife.h: es la pieza de este cambio verificable SIN telefono. Harness11
// (tools/arneses/h11-src) compila ESTE archivo tal cual con g++ de host y fuerza
// cada secuencia de Bind/Unbind/reasignacion contra un modelo lineal de
// referencia. Lo que aqui se prueba es la EQUIVALENCIA indice<->barrido; que un
// superbloque real no diverja ni acelere sigue necesitando el telefono.

#include <cstdint>

namespace mVUSbIndex
{
	// Dominio de PCs de entrada de VU1: microMem 0x4000, una microinstruccion
	// cada 8 bytes, el dispatcher enmascara a 0x3ff8 -> 2048 entradas posibles.
	// Debe ser el MISMO dominio que mVUTraceProbe::kProbeSlots y que PcIdx() del
	// motor. Este header NO puede probarlo: es deliberadamente libre de
	// dependencias, asi que no ve la sonda. Lo prueba el MOTOR, con dos
	// static_assert al lado de su tabla en MvuSuperblock-arm64.cpp — ahi es donde
	// se ven las dos constantes a la vez. Cambiar una sin la otra se cae al
	// compilar, no en el telefono.
	static constexpr uint32_t kSlots = 2048u;

	// Techo del id de hueco que cabe en la casilla: se guarda slotId+1 en
	// uint16_t, con 0 reservado para «vacio» (asi el hueco 0 NO se confunde con
	// «nadie»). Igual que arriba, `kSbMaxVariants` no es visible aqui; el motor
	// afirma con un static_assert que quepa. El de aqui abajo solo deja constancia
	// del dominio del tipo — es el minimo que se puede afirmar sin dependencias.
	static constexpr uint16_t kEmpty = 0u;
	static constexpr uint32_t kMaxSlotId = 65534u; // id guardado = slotId + 1

	// El indice. Trivial de copiar/limpiar; sin punteros, sin duenos.
	struct Index
	{
		uint16_t at[kSlots];

		Index() { ClearAll(); }

		void ClearAll()
		{
			for (uint32_t i = 0; i < kSlots; i++)
				at[i] = kEmpty;
		}

		// Enlace PC (bytes de microMem, ya enmascarado por el llamador) -> hueco.
		// idx debe ser PcIdx(pc) = (pc >> 3) & (kSlots - 1). Devuelve el id de
		// hueco o -1 si ningun hueco esta enlazado a ese PC.
		static uint32_t SlotOf(uint32_t idx) { return idx & (kSlots - 1u); }

		int Lookup(uint32_t idx) const
		{
			const uint32_t s = SlotOf(idx);
			const uint16_t v = at[s];
			if (v == kEmpty)
				return -1;
			return static_cast<int>(v - 1u);
		}

		// Enlaza un PC a un hueco. Si el PC ya apuntaba a OTRO hueco, eso es la
		// invariante rota (dos huecos para un startPC) y se reporta con -2 en vez
		// de pisar en silencio: quien llama tiene que poder enterarse. Sobrescribir
		// el MISMO hueco es idempotente y devuelve 0.
		int Bind(uint32_t idx, uint32_t slotId)
		{
			// Un id que la casilla NO puede representar tampoco se rechaza, y por un
			// motivo silencioso: con uint16_t, slotId = kMaxSlotId + 1 da want = 0,
			// que es «vacio», y sobre una casilla vacia `cur == want` se cumple — o
			// sea el Bind devolveria 0 («idempotente») habiendo enlazado NADA. Un
			// candidato perdido sin ruido, que es el peor fallo posible de un indice.
			// El motor ya impide llegar ahi con un static_assert sobre
			// kSbMaxVariants, asi que esto no es un escenario vivo: es la puerta del
			// componente, que no deja pasar lo que su propio tipo no puede expresar.
			if (slotId > kMaxSlotId)
				return -2;
			const uint32_t s = SlotOf(idx);
			const uint16_t want = static_cast<uint16_t>(slotId + 1u);
			const uint16_t cur = at[s];
			if (cur == want)
				return 0; // re-bind del mismo hueco: no-op honesto
			if (cur != kEmpty)
				return -2; // invariante rota: no se pisa, se avisa
			at[s] = want;
			return 1;
		}

		// Desenlaza SI el PC apunta exactamente a ese hueco. Devuelve true si habia
		// ese enlace y lo quito; false si el PC estaba vacio o apuntaba a otro
		// hueco (no toca nada en ese caso — un Unbind mal dirigido no puede robarle
		// el enlace a un hueco legitimo).
		bool Unbind(uint32_t idx, uint32_t slotId)
		{
			// Misma puerta que Bind: con slotId fuera del dominio, want trunca a 0,
			// que es «vacio», y la comparacion de abajo se cumpliria sobre una
			// casilla vacia devolviendo true — un Unbind afirmando que quito un
			// enlace que nunca existio.
			if (slotId > kMaxSlotId)
				return false;
			const uint32_t s = SlotOf(idx);
			const uint16_t want = static_cast<uint16_t>(slotId + 1u);
			if (at[s] != want)
				return false;
			at[s] = kEmpty;
			return true;
		}

		// cuantos huecos enlazados (para el informe y para el modelo de referencia).
		uint32_t Count() const
		{
			uint32_t n = 0;
			for (uint32_t i = 0; i < kSlots; i++)
				if (at[i] != kEmpty)
					n++;
			return n;
		}

		// Vista cruda del arreglo: la usa Harness11 para comparar cada casilla
		// contra su modelo lineal de referencia. No es una API del motor.
		const uint16_t* Raw() const { return at; }

		// Indice de dominio a partir de un PC en bytes de microMem: el MISMO
		// calculo que PcIdx() del motor y que la sonda (8 bytes por
		// microinstruccion, 2048 entradas, enmascarado). Vive aqui para que el
		// arnes lo ejercite sin traer ningun header de PCSX2, y porque un indice
		// mal calculado es un Lookup que sirve el hueco equivocado.
		static uint32_t IdxOf(uint32_t pc_bytes)
		{
			return (pc_bytes >> 3) & (kSlots - 1u);
		}
	};

	// Que kSbMaxVariants quepa en la casilla LO AFIRMA EL MOTOR con un
	// static_assert (MvuSuperblock-arm64.cpp, junto a la tabla), que es el unico
	// sitio donde las dos constantes son visibles a la vez. Aqui no se reproduce:
	// un `static_assert(kMaxSlotId >= 255)` con las dos partes escritas a mano es
	// siempre cierto, o sea un chequeo decorativo del tipo que este proyecto ya
	// cazó dos veces. Lo que si queda dicho es el dominio del tipo: id+1 con id
	// <= 65534 cabe en uint16_t y 0 sigue significando «vacio».
	static_assert(kEmpty == 0u, "indice: kEmpty tiene que ser 0 para que vacio sea distinguible");
}
