// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// Harness14 — arnes de LOGICA de la decision de elegibilidad del superbloque VU1.
//
// Que prueba, en el unico sentido que host puede: que la LEY es la ley.
// MvuSbEligibility.h es C++ puro y sin dependencias a proposito, igual que
// MvuSbLife.h (h9) y MvuSbIndex.h (h11) y MvuFastPath.h (h12): es la pieza de
// este cambio verificable SIN telefono. Aqui se fuerza, contra la cadena de
// comprobaciones que el codigo del motor reemplaza:
//   (1) cada causa es alcanzable, y se alcanza con el input MINIMO que la nombra
//       (una causa que no se puede provocar es una etiqueta decorativa);
//   (2) el orden de cortocircuito es el del codigo vieja, comprobado por
//       precedencia: con dos razones simultaneas gana la que el motor miraba
//       primero (cambiar el orden cambia lo que se cuenta, y eso es un cambio de
//       telemetria que nadie debe poder hacer en silencio);
//   (3) los bordes de los siete numeros de la politica (piso A, piso B, ratio por
//       debajo/encima, par justo en el techo y un paso pasado);
//   (4) el INVARIANTE DE CUENTA: toda salida terminal de una cadena tiene casilla,
//       toda casilla tiene cubo, y Σ casillas terminales == salidas — que es lo
//       que el cierre del informe imprime en un telefono;
//   (5) la puerta (ArmGate): cobertura por PC, periodo, comportamiento en el
//       vuelco del contador u32, y el cierre considerados == intentos + cerrada.
//
// Que NO prueba, y por eso no lo afirma (la leccion de h12/f4: un ancla que pasa
// por accidente es peor que no tenerla):
//   - que el motor LLAME a estas funciones en el momento correcto, ni que los
//     datos que pasa (aristas, formas, vuBlkExecCnt) sean los vivos: eso vive en
//     microVU-arm64.cpp / MvuSuperblock-arm64.cpp, que host NO puede analizar
//     (tira de <arm_neon.h> y vixl). Lo fijan las anclas de mutacion_h14.sh
//     fase 2 y lo compila el CI.
//   - que un telefono construya superbloques. Eso SOLO lo dice el log, y es
//     exactamente el punto donde este proyecto se nego dos veces a afirmar sin
//     evidencia (0.6.25, y el build=0 de 0.6.36).

#include "MvuSbEligibility.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace mVUSbElig;

// El arnes vive en su propio TU, no en el del motor, asi que no trae los alias
// s32/u32/u64 de PCSX2. Se declaran aqui con los tipos estandar porque el codigo
// de abajo esta escrito en la forma del motor: son SOLO alias, no una segunda
// copia de nada — el encabezado puro sigue usando uint*_t, y esa es justo la
// razon por la que host lo puede compilar.
using u32 = uint32_t;
using u64 = uint64_t;

static int g_checks = 0;
static int g_fails = 0;

#define CK(cond, msg)                                                      \
	do                                                                     \
	{                                                                      \
		g_checks++;                                                        \
		if (!(cond))                                                       \
		{                                                                  \
			g_fails++;                                                     \
			std::printf("FALLO: %s\n", (msg));                             \
		}                                                                  \
	} while (0)

// La politica del motor, copiada aqui a mano con los NUMEROS del log. Es
// deliberado que sea una copia: si el header y el motor divergen, la copia de
// este arnes ruidA contra la del puente (h10 fija que kSbPolicy sea las siete
// constantes de siempre). Lo que se prueba es la LEY sobre ESTOS numeros.
static const Policy kMotor{200000u, 1000u, 40u, 400u, 5u, 192u, 384u};

// ------------------------------------------------------------------
// La cadena de armar.
// ------------------------------------------------------------------

static ArmInput ArmOk()
{
	// Un caso claramente elegible: A caliente sobre B, flujo 100 %, par chico.
	ArmInput in{};
	in.already_live = false;
	in.burned = false;
	in.edge_kind = 1u;
	in.shape_a_reason = 1u;
	in.shape_b_reason = 2u;
	in.slots_present = true;
	in.exec_a = 200000ull;
	in.exec_b = 200000ull;
	in.ops_a = 4u;
	in.ops_b = 5u;
	in.cycles_a = 8u;
	in.cycles_b = 10u;
	return in;
}

static void test_arm_causes()
{
	// (1) Cada causa alcanzable con el input minimo que la nombra.
	{
		ArmInput in = ArmOk();
		CK(DecideArm(in, kMotor) == ArmCause::kPolicyPass, "caso base elegible");
	}
	{
		ArmInput in = ArmOk();
		in.already_live = true;
		CK(DecideArm(in, kMotor) == ArmCause::kLive, "vivo: candidato en el PC");
	}
	{
		ArmInput in = ArmOk();
		in.burned = true;
		CK(DecideArm(in, kMotor) == ArmCause::kBurned, "quemado: PC ya juzgado");
	}
	{
		ArmInput in = ArmOk();
		in.edge_kind = 0u;
		CK(DecideArm(in, kMotor) == ArmCause::kNoEdge, "sin-arista: kind=0");
		in.edge_kind = 2u;
		CK(DecideArm(in, kMotor) == ArmCause::kNoEdge, "sin-arista: kind=2 cond");
	}
	{
		ArmInput in = ArmOk();
		in.shape_a_reason = 0u;
		CK(DecideArm(in, kMotor) == ArmCause::kNoShape, "sin-forma: origen");
		ArmInput j = ArmOk();
		j.shape_b_reason = 0u;
		CK(DecideArm(j, kMotor) == ArmCause::kNoShape, "sin-forma: destino");
	}
	{
		ArmInput in = ArmOk();
		in.slots_present = false;
		CK(DecideArm(in, kMotor) == ArmCause::kNoSlots, "sin-contador: slots null");
	}
	{
		ArmInput in = ArmOk();
		in.exec_a = 199999ull;
		CK(DecideArm(in, kMotor) == ArmCause::kFloor, "bajo-piso: origen");
		ArmInput j = ArmOk();
		j.exec_b = 999ull;
		CK(DecideArm(j, kMotor) == ArmCause::kFloor, "bajo-piso: destino");
	}
	{
		ArmInput in = ArmOk();
		in.exec_a = 200000ull;
		in.exec_b = 100000ull; // ratio 50 % -> ok; 79000 -> bajo
		CK(DecideArm(in, kMotor) == ArmCause::kPolicyPass, "ratio 50 dentro");
		in.exec_b = 79000ull;
		CK(DecideArm(in, kMotor) == ArmCause::kRatio, "ratio-fuera: por abajo");
		// El borde INFERIOR exacto, fijado por separado: min_ratio_pct=40 con
		// execB*100/execA == 40 tiene que ENTRAR. Sin esta linea, cambiar `<` por
		// `<=` en el suelo del ratio seguiria verde en el resto del arnes.
		in.exec_b = 80000ull;
		CK(DecideArm(in, kMotor) == ArmCause::kPolicyPass, "ratio 40 exacto entra");
		in.exec_b = 79000ull;
		in.exec_b = 802000ull; // 401: el primer entero que SI queda fuera
		// (800001 trunca a 400 y ENTRA; eso se fija aparte mas abajo, con el
		// truncado — aqui hace falta un valor que realmente rebase el techo.)
		CK(DecideArm(in, kMotor) == ArmCause::kRatio, "ratio-fuera: por encima");
	}
	{
		ArmInput in = ArmOk();
		in.ops_a = 100u;
		in.ops_b = 92u; // 192 exacto: cabe
		CK(DecideArm(in, kMotor) == ArmCause::kPolicyPass, "par justo en el techo");
		in.ops_b = 93u;
		CK(DecideArm(in, kMotor) == ArmCause::kPair, "par-grande: un ops pasado");
		ArmInput j = ArmOk();
		j.cycles_a = 200u;
		j.cycles_b = 184u;
		CK(DecideArm(j, kMotor) == ArmCause::kPolicyPass, "ciclos justo en el techo");
		j.cycles_b = 185u;
		CK(DecideArm(j, kMotor) == ArmCause::kPair, "par-grande: ciclos pasado");
	}

	// (2) Precedencia = orden del codigo que reemplaza. Con dos motivos a la vez,
	// gana el que el motor miraba primero. Estas cinco no son obvias: son las que
	// un reorden inocente de la cadena puede cambiar sin tocar ninguna condicion.
	{
		ArmInput in = ArmOk();
		in.already_live = true;
		in.burned = true;
		CK(DecideArm(in, kMotor) == ArmCause::kLive, "precedencia: vivo > quemado");
		in.edge_kind = 0u;
		CK(DecideArm(in, kMotor) == ArmCause::kLive, "precedencia: vivo > sin-arista");
		in.already_live = false;
		CK(DecideArm(in, kMotor) == ArmCause::kBurned, "precedencia: quemado > sin-arista");
		in.burned = false;
		in.shape_a_reason = 0u;
		CK(DecideArm(in, kMotor) == ArmCause::kNoEdge, "precedencia: sin-arista > sin-forma");
		in.edge_kind = 1u;
		in.slots_present = false;
		CK(DecideArm(in, kMotor) == ArmCause::kNoShape, "precedencia: sin-forma > sin-contador");
		in.shape_a_reason = 1u;
		in.shape_b_reason = 1u;
		CK(DecideArm(in, kMotor) == ArmCause::kNoSlots, "precedencia: sin-contador > piso");
		in.slots_present = true;
		in.exec_a = 1ull;
		CK(DecideArm(in, kMotor) == ArmCause::kFloor, "precedencia: piso > ratio");
		in.exec_a = 200000ull;
		in.exec_b = 2000000ull; // ratio 1000 % fuera de banda
		CK(DecideArm(in, kMotor) == ArmCause::kRatio, "precedencia: ratio > par");
		in.ops_a = 60000u;
		CK(DecideArm(in, kMotor) == ArmCause::kRatio, "ratio se cuenta aunque el par sea enorme");
	}

	// (3) Bordes aritmeticos del ratio. execA grande con execB chico no desborda y
	// el piso ya impide la division por cero — pero el header se protege igual, y
	// esa proteccion se prueba con un input que NINGUN telefono produce.
	{
		ArmInput in = ArmOk();
		in.exec_a = 0ull; // ilegal: el piso lo corta antes
		in.exec_b = 5000ull;
		CK(DecideArm(in, kMotor) == ArmCause::kFloor, "execA=0 lo para el piso, no la division");

		ArmInput j = ArmOk();
		j.exec_a = 200000ull;
		j.exec_b = 800000ull; // ratio 400 = techo exacto
		CK(DecideArm(j, kMotor) == ArmCause::kPolicyPass, "ratio 400 exacto entra");
		j.exec_b = 800001ull; // 400.0005 -> 400 entero: sigue dentro (truncado)
		CK(DecideArm(j, kMotor) == ArmCause::kPolicyPass, "truncado hacia abajo en el techo");
		j.exec_b = 801000ull; // 400.5 -> 400: dentro
		CK(DecideArm(j, kMotor) == ArmCause::kPolicyPass, "401k da 400: dentro");
		j.exec_b = 802000ull; // 401: fuera
		CK(DecideArm(j, kMotor) == ArmCause::kRatio, "402k da 401: fuera del techo");

		// El borde INFERIOR del ratio, escrito a proposito 1 ejecución por debajo
		// del piso de B (target_min=1000) y muy por encima de el: 79 999 pasa el
		// piso y el ratio 79999*100/200000 = 39 (truncado hacia abajo) es el que
		// corta. Se probo primero con una expectativa «piso o ratio» y estaba mal:
		// un OR en una prueba es un verde que no elige, asi que aqui se fija la
		// causa exacta. Cambiar el truncado (rounding) cambiaria ESTA linea.
		ArmInput m = ArmOk();
		m.exec_a = 200000ull;
		m.exec_b = 79999ull; // >= 1000 (piso ok), ratio 39 -> fuera por abajo
		CK(DecideArm(m, kMotor) == ArmCause::kRatio, "79999: el piso de B pasa y corta el ratio truncado");
		m.exec_b = 1000ull; // piso exacto de B: pasa el piso, ratio 0 -> kRatio
		CK(DecideArm(m, kMotor) == ArmCause::kRatio, "piso de B exacto no es rechazo de piso");
		m.exec_b = 999ull; // un paso debajo del piso: ahora SI es el piso
		CK(DecideArm(m, kMotor) == ArmCause::kFloor, "999: el piso de B corta antes que el ratio");
	}

	// Overflow del ratio: execB maximo u64 por 100 se desbordaria. El motor solo
	// puede dar valores que quepan (es un contador de ejecuciones de un cuadro de
	// VU1), pero la MUL es la unica multiplicacion del header y se prueba.
	{
		ArmInput in = ArmOk();
		in.exec_a = 200000ull;
		in.exec_b = ~0ull / 100ull; // execB*100 justito sin desbordar
		CK(DecideArm(in, kMotor) == ArmCause::kRatio, "ratio enorme: fuera por arriba");
		in.exec_b = 1ull << 40; // 1.09e12: *100 sigue en u64
		CK(DecideArm(in, kMotor) == ArmCause::kRatio, "1<<40 por 100 no desborda: fuera por arriba");
	}
}

// ------------------------------------------------------------------
// Union y cierre.
// ------------------------------------------------------------------

static JunctionInput JunOk()
{
	JunctionInput in{};
	in.slot_in_range = true;
	in.is_building = true;
	in.struct_clean = true;
	in.junctions_so_far = 0u;
	in.region_ops = 8u;
	in.region_cycles = 16u;
	in.target_aligned = true;
	in.target_has_shape = true;
	in.target_is_self = false;
	return in;
}

static void test_junction()
{
	{
		JunctionInput in = JunOk();
		CK(DecideJunction(in, kMotor) == JunctionCause::kJoined, "union base concedida");
	}
	{
		JunctionInput in = JunOk();
		in.slot_in_range = false;
		CK(DecideJunction(in, kMotor) == JunctionCause::kBadSlot, "hueco-fuera");
		in.is_building = false;
		CK(DecideJunction(in, kMotor) == JunctionCause::kBadSlot, "precedencia: hueco-fuera > no-construyendo");
		in.slot_in_range = true;
		CK(DecideJunction(in, kMotor) == JunctionCause::kNotBuilding, "no-construyendo");
		in.is_building = true;
		in.struct_clean = false;
		CK(DecideJunction(in, kMotor) == JunctionCause::kDirty, "rama-sucia");
		in.struct_clean = true;
		in.junctions_so_far = 5u;
		CK(DecideJunction(in, kMotor) == JunctionCause::kCap, "tope-uniones en 5");
		in.junctions_so_far = 4u;
		CK(DecideJunction(in, kMotor) == JunctionCause::kJoined, "4 uniones: cabe una mas");
		in.junctions_so_far = 0u;
		in.region_ops = 192u;
		CK(DecideJunction(in, kMotor) == JunctionCause::kRegion, "sin-hueco-de-area: ops en el techo");
		in.region_ops = 191u;
		CK(DecideJunction(in, kMotor) == JunctionCause::kJoined, "191: un hueco de margen, y eso es el >=");
		in.region_cycles = 384u;
		CK(DecideJunction(in, kMotor) == JunctionCause::kRegion, "sin-hueco-de-area: ciclos");
		in.region_cycles = 16u;
		in.target_aligned = false;
		CK(DecideJunction(in, kMotor) == JunctionCause::kMisalign, "destino-desalineado");
		in.target_aligned = true;
		in.target_has_shape = false;
		CK(DecideJunction(in, kMotor) == JunctionCause::kNoShape, "destino-sin-forma");
		in.target_has_shape = true;
		in.target_is_self = true;
		CK(DecideJunction(in, kMotor) == JunctionCause::kSelf, "destino-es-entrada");
		in.target_is_self = false;
		CK(DecideJunction(in, kMotor) == JunctionCause::kJoined, "cerrado: vuelve a conceder");
	}
	// Precedencia de la union: el orden del codigo viejo (sucia > tope > region >
	// alineacion > forma > sí-mismo). Un reorden cambiaria cuales causas se cuentan.
	{
		JunctionInput in = JunOk();
		in.struct_clean = false;
		in.junctions_so_far = 5u;
		CK(DecideJunction(in, kMotor) == JunctionCause::kDirty, "rama-sucia > tope");
		in.struct_clean = true;
		in.region_ops = 200u;
		CK(DecideJunction(in, kMotor) == JunctionCause::kCap, "tope > region");
		in.junctions_so_far = 0u;
		in.target_aligned = false;
		CK(DecideJunction(in, kMotor) == JunctionCause::kRegion, "region > alineacion");
		in.region_ops = 8u;
		CK(DecideJunction(in, kMotor) == JunctionCause::kMisalign, "alineacion > forma");
		in.target_aligned = true;
		in.target_has_shape = false;
		in.target_is_self = true;
		CK(DecideJunction(in, kMotor) == JunctionCause::kNoShape, "forma > si-mismo");
	}
}

static CloseInput CloseOk()
{
	CloseInput in{};
	in.slot_in_range = true;
	in.is_building = true;
	in.area_bad = false;
	in.junctions = 1u;
	in.has_entry = true;
	in.ops = 12u;
	in.cycles = 20u;
	return in;
}

static void test_close()
{
	{
		CloseInput in = CloseOk();
		CK(DecideClose(in, kMotor) == CloseCause::kAdopted, "cierre base adoptado");
	}
	{
		CloseInput in = CloseOk();
		in.slot_in_range = false;
		CK(DecideClose(in, kMotor) == CloseCause::kBadSlot, "cierre: hueco-fuera");
		in.is_building = false;
		CK(DecideClose(in, kMotor) == CloseCause::kBadSlot, "precedencia: hueco-fuera > estado");
		in.slot_in_range = true;
		CK(DecideClose(in, kMotor) == CloseCause::kNotBuilding, "cierre: no-construyendo");
		in.is_building = true;
		in.area_bad = true;
		CK(DecideClose(in, kMotor) == CloseCause::kAreaBad, "cierre: area-con-kick");
		in.area_bad = false;
		in.junctions = 0u;
		CK(DecideClose(in, kMotor) == CloseCause::kNoJunction, "cierre: cero-uniones");
		in.junctions = 1u;
		in.has_entry = false;
		CK(DecideClose(in, kMotor) == CloseCause::kNoEntry, "cierre: sin-entrada");
		in.has_entry = true;
		in.ops = 0u;
		CK(DecideClose(in, kMotor) == CloseCause::kZeroOps, "cierre: cero-ops");
		in.ops = 193u;
		CK(DecideClose(in, kMotor) == CloseCause::kOverBudget, "cierre: fuera-de-techos ops");
		in.ops = 192u;
		CK(DecideClose(in, kMotor) == CloseCause::kAdopted, "192 exacto: adopta (techo inclusivo)");
		in.cycles = 384u;
		CK(DecideClose(in, kMotor) == CloseCause::kAdopted, "384 exacto: adopta");
		in.cycles = 385u;
		CK(DecideClose(in, kMotor) == CloseCause::kOverBudget, "385: fuera");
		in.cycles = 20u;
		CK(DecideClose(in, kMotor) == CloseCause::kAdopted, "cerrado: adopta");
	}
	// Nota que el arnes deja escrita: el CIERRE usa `>` y la UNION usa `>=`. No es
	// una inconsistencia a corregir aqui — es el codigo de 0.6.36 tal cual (el
	// cierre juzga un area ya medida, la union pide hueco PARA el tramo que falta).
	// Cambiar uno sin cambiar el otro es un cambio de telemetria, y estas dos
	// lineas lo impiden.
	{
		CloseInput c = CloseOk();
		c.ops = 192u;
		CK(DecideClose(c, kMotor) == CloseCause::kAdopted, "cierre inclusivo fijado");
		JunctionInput j = JunOk();
		j.region_ops = 192u;
		CK(DecideJunction(j, kMotor) == JunctionCause::kRegion, "union exclusiva fijada");
	}
}

// ------------------------------------------------------------------
// (4) El cierre de cuentas: causa -> cubo -> casilla.
// ------------------------------------------------------------------

static void test_buckets()
{
	// Toda causa terminal de armar tiene cubo y nombre; kPolicyPass y kArmed NO
	// son rechaces (kArmed es el exito, kPolicyPass no es resultado).
	for (u32 k = 0; k < kArmCauseCount; k++)
	{
		const ArmCause c = static_cast<ArmCause>(k);
		CK(std::strlen(ArmCauseName(c)) > 0, "toda causa de armado tiene nombre");
		if (!IsArmRejection(c))
			continue;
		const Bucket b = ArmBucketOf(c);
		CK(static_cast<u32>(b) < static_cast<u32>(Bucket::kDispatch) + 1u,
			"rechace de armado tiene cubo valido");
		CK(std::strlen(BucketName(b)) > 0, "el cubo tiene nombre");
	}
	CK(!IsArmRejection(ArmCause::kPolicyPass), "politica-ok no es rechace");
	CK(!IsArmRejection(ArmCause::kArmed), "armado no es rechace");
	CK(IsArmOutcome(ArmCause::kArmed), "armado SI es resultado terminal");
	CK(!IsArmOutcome(ArmCause::kPolicyPass), "politica-ok NO es resultado");

	for (u32 k = 0; k < kJunctionCauseCount; k++)
	{
		const JunctionCause c = static_cast<JunctionCause>(k);
		CK(std::strlen(JunctionCauseName(c)) > 0, "toda causa de union tiene nombre");
		if (!IsJunctionRejection(c))
			continue;
		CK(static_cast<u32>(JunctionBucketOf(c)) < static_cast<u32>(Bucket::kDispatch) + 1u,
			"rechace de union tiene cubo valido");
	}
	CK(!IsJunctionRejection(JunctionCause::kJoined), "unida no es rechace");

	for (u32 k = 0; k < kCloseCauseCount; k++)
	{
		const CloseCause c = static_cast<CloseCause>(k);
		CK(std::strlen(CloseCauseName(c)) > 0, "toda causa de cierre tiene nombre");
		if (!IsCloseRejection(c))
			continue;
		CK(static_cast<u32>(CloseBucketOf(c)) < static_cast<u32>(Bucket::kDispatch) + 1u,
			"rechace de cierre tiene cubo valido");
	}
	CK(!IsCloseRejection(CloseCause::kAdopted), "adoptado no es rechace");

	// Nombres unicos por cadena: dos causas con el mismo nombre en una misma linea
	// del informe serian una cuenta que no se puede leer. Se comprueba por pares.
	for (u32 a = 0; a < kArmCauseCount; a++)
		for (u32 b = a + 1; b < kArmCauseCount; b++)
			CK(std::strcmp(ArmCauseName(static_cast<ArmCause>(a)),
					ArmCauseName(static_cast<ArmCause>(b))) != 0,
				"armado: nombres de causa unicos");
	for (u32 a = 0; a < kJunctionCauseCount; a++)
		for (u32 b = a + 1; b < kJunctionCauseCount; b++)
			CK(std::strcmp(JunctionCauseName(static_cast<JunctionCause>(a)),
					JunctionCauseName(static_cast<JunctionCause>(b))) != 0,
				"union: nombres de causa unicos");
	for (u32 a = 0; a < kCloseCauseCount; a++)
		for (u32 b = a + 1; b < kCloseCauseCount; b++)
			CK(std::strcmp(CloseCauseName(static_cast<CloseCause>(a)),
					CloseCauseName(static_cast<CloseCause>(b))) != 0,
				"cierre: nombres de causa unicos");
}

// ------------------------------------------------------------------
// (5) La puerta.
// ------------------------------------------------------------------

static void test_gate()
{
	// Cobertura: el primer juicio de cada PC SIEMPRE pasa (con esto el motor puede
	// construir en cuanto un PC se vuelve elegible, que es lo que se vino a arreglar).
	ArmGate g;
	CK(g.Open(0x0378u), "primer juicio de un PC pasa");
	CK(g.Open(0x0cd8u), "primer juicio de otro PC pasa");

	// Periodo: exactamente kGatePeriodConsideraciones consideraciones entre juicios
	// del mismo PC, contado —no estimado.
	{
		ArmGate h;
		u32 abiertas = 0;
		for (u32 i = 0; i < 10u * kGatePeriodConsiderations; i++)
			if (h.Open(0x0100u))
				abiertas++;
		CK(abiertas == 10u, "un PC se re-julga 10 veces en 10 periodos");
		CK(h.considered == 10u * kGatePeriodConsiderations, "consideradas cuenta TODAS las llegadas");
	}

	// Cierre de la cuenta de la puerta, con PCs mezclados: la suma de las aperturas
	// mas los rechaces tiene que dar las llegadas.
	{
		ArmGate h;
		u32 abiertas = 0;
		const u32 total = 5000u;
		for (u32 i = 0; i < total; i++)
		{
			const u32 pc = ((i * 8u) & 0x3ff8u);
			if (h.Open(pc))
				abiertas++;
		}
		CK(abiertas + (total - abiertas) == h.considered, "cierre: aperturas + cerradas = consideradas");
		CK(abiertas > 0u, "la puerta no cierra todo (con 640 PCs en 5000 llegadas)");
		CK(abiertas <= total, "la puerta no abre mas de lo que llega");
	}

	// Idempotencia del indice: PCs que IdxOf pliega en la misma casilla comparten
	// ventana. En el dominio real de VU1 no pasan (el indice es biyectivo), y eso
	// tambien se comprueba: las 2048 direcciones validas distintas dan 2048 indices
	// distintos.
	{
		std::vector<uint8_t> seen(kGateSlots, 0u);
		int dup = 0;
		for (u32 pc = 0; pc <= 0x3ff8u; pc += 8u)
		{
			const uint32_t i = ArmGate::IdxOf(pc);
			if (seen[i])
				dup++;
			seen[i] = 1u;
		}
		CK(dup == 0, "IdxOf es biyectivo en el dominio de PCs de VU1");
		int llenas = 0;
		for (uint32_t i = 0; i < kGateSlots; i++)
			if (seen[i])
				llenas++;
		CK(llenas == static_cast<int>(kGateSlots), "el dominio cubre la tabla justa (2048)");
	}

	// Vuelco del contador u32: con `considered` cerca de 2^32, una comparacion sin
	// signo (considered >= next[i]) mentira al dar la vuelta y la puerta se cerraria
	// para TODOS los PCs en silencio. La forma conresta lo hace bien, y eso es lo
	// que esta prueba exige. 26 horas de partida a 44 000/s lo produce de verdad.
	{
		ArmGate h;
		h.considered = 0xFFFFFFF0u;
		h.next[ArmGate::IdxOf(0x0200u)] = h.considered + kGatePeriodConsiderations; // envuelve
		const uint32_t before = h.considered;
		int abiertas = 0;
		for (u32 i = 0; i < kGatePeriodConsiderations + 4u; i++)
		{
			if (h.Open(0x0200u))
				abiertas++;
		}
		CK(abiertas == 1, "a traves del vuelco la puerta abre UNA vez, no cero ni dos");
		// El contador NO se detiene en el vuelco: sigue sumando y da la vuelta. Se
		// exige el valor exacto (envuelto), no un "> antes" que era falso — la
		// primera version de esta prueba estaba mal escrita y aprobaba por azar.
		CK(h.considered == before + (kGatePeriodConsiderations + 4u),
			"el contador sigue contando a traves del vuelco (valor exacto envuelto)");
		// y para un PC nunca visto, la ventana vieja (next=0) con consideradas
		// pos-vuelco tiene que dar ABIERTA: es el caso en que una resta con signo
		// invertiria el signo.
		ArmGate j;
		j.considered = 0x00000010u; // recien dado la vuelta
		CK(j.Open(0x0300u), "PC nuevo tras el vuelco se juzga (next=0 no lo bloquea)");
	}

	// Clear: con la tabla limpia la puerta vuelve a estar abierta para todo PC
	// (el motor lo llama en ResetTableForEnable; si no, el re-encendido heredaria
	// ventanas cerradas y no armaria nada).
	{
		ArmGate h;
		for (u32 i = 0; i < 200u; i++)
			h.Open(0x0400u);
		h.Clear();
		CK(h.considered == 0u, "Clear deja el contador a cero");
		// ReadyCount se mira ANTES de consumir la primera ventana: Open() sube el
		// contador y cierra la casilla de ese PC hasta dentro de 1024, asi que
		// medirlo despues daria 2047 y el "fallo" seria del arnes, no de la puerta.
		CK(h.ReadyCount() == kGateSlots, "tras Clear las 2048 ventanas estan abiertas");
		CK(h.Open(0x0400u), "tras Clear el PC se vuelve a juzgar");
		CK(h.ReadyCount() == kGateSlots - 1u, "consumida una ventana quedan 2047");
	}
}

// ------------------------------------------------------------------
// (6) PARIDAD CON LA CADENA DE 0.6.36 — el diferencial que justifica el cambio.
//
// Las funciones de aqui abajo son el codigo VIEJO, copiado literal de
// git show HEAD:.../MvuSuperblock-arm64.cpp (SbArmCompile / SbAskJunction /
// SbCloseCompile, 0.6.36), reducido a la decision y con el CUBO que movia. No
// son un modelo idealizado: las comparaciones, los `>=` frente a `>` y el orden
// estan copiados como estaban. Lo que se exige, casilla por casilla del espacio
// de entradas, es:
//
//   (a) donde el codigo viejo movia un cubo, el nuevo deriva el MISMO cubo —
//       o sea que las cuatro cifras del log viejo siguen siendo comparables con
//       las del nuevo, que es lo que hace falta para que una partida de hoy se
//       pueda confrontar con la de God of War de 0.6.36;
//   (b) donde el codigo viejo NO movia nada, el nuevo SI mueve una casilla.
//       Eso no es una divergencia que haya que tapar: es el defecto que este
//       arnés viene a cerrar, y por eso se cuenta por separado y se imprime
//       como testigo (los "huecos de 0.6.36"). En SbAskJunction y SbCloseCompile
//       el id de hueco fuera de tabla y el "no esta en BUILDING" devolvian sin
//       tocar ningun contador: dos rechaces que un log no podia ver, y que con
//       los cubos agregados eran literalmente imposibles de deducir.
//
// El espacio se recorre por producto cartesiano pequeno pero completo en los
// valores que cambian el veredicto (pisos, techos, bordes de ratio), no por
// valores al azar: un aleatorio daria un verde que depende de la semilla.
// ------------------------------------------------------------------

// El viejo devuelve el cubo que movio, o Bucket::kCount si NO movia ninguno
// (devolvia sin contar). `*contaba = false` es el hueco de telemetria.
static Bucket OldArmModel(const ArmInput& in, const Policy& p, bool alloc_ok,
	bool link_ok, bool* contaba)
{
	*contaba = true;
	if (in.already_live)
		return Bucket::kIdentity; // st.rejected_identity
	if (in.burned)
		return Bucket::kIdentity; // st.rejected_identity
	if (in.edge_kind != 1u)
		return Bucket::kStruct; // st.rejected_struct
	if (in.shape_a_reason == 0u || in.shape_b_reason == 0u)
		return Bucket::kStruct; // st.rejected_struct
	if (!in.slots_present)
		return Bucket::kCold; // st.rejected_cold
	const uint64_t execA = in.exec_a;
	const uint64_t execB = in.exec_b;
	if (execA < static_cast<uint64_t>(p.hot_entries) || execB < static_cast<uint64_t>(p.target_min))
		return Bucket::kCold; // st.rejected_cold
	const uint64_t ratioPct = execB * 100ull / (execA ? execA : 1ull);
	if (ratioPct < static_cast<uint64_t>(p.min_ratio_pct) || ratioPct > static_cast<uint64_t>(p.max_ratio_pct))
		return Bucket::kCold; // st.rejected_cold
	if (static_cast<uint64_t>(in.ops_a) + in.ops_b > p.max_ops ||
		static_cast<uint64_t>(in.cycles_a) + in.cycles_b > p.max_cycles)
		return Bucket::kBudget; // st.rejected_budget
	// Llego hasta la tabla: AllocSlot nulo o LinkSlot -2 movian rejected_identity.
	if (!alloc_ok || !link_ok)
		return Bucket::kIdentity;
	// Exito: el viejo no movia rechace ninguno.
	*contaba = false;
	return Bucket::kCount;
}

// La cadena NUEVA como la ejecuta el motor: DecideArm (el header puro, que es lo
// que este arnés prueba a fondo) y DESPUES la tabla. Esto NO es una segunda
// implementacion de la ley: DecideArm sigue siendo la unica que juzga la forma
// del microcodigo; aqui solo se modela el paso que el header no puede ver porque
// necesita la tabla real (AllocSlot/LinkSlot). Que el motor los llame justo
// despues de kPolicyPass es ancla de mutacion_h14.sh fase 2, no de este arnés.
static ArmCause NewArmChain(const ArmInput& in, const Policy& p, bool alloc_ok, bool link_ok)
{
	const ArmCause c = DecideArm(in, p);
	if (c != ArmCause::kPolicyPass)
		return c;
	if (!alloc_ok)
		return ArmCause::kFull;
	if (!link_ok)
		return ArmCause::kDesync;
	return ArmCause::kArmed;
}

static Bucket OldJunctionModel(const JunctionInput& in, const Policy& p, bool* contaba)
{
	*contaba = true;
	if (!in.slot_in_range)
		return Bucket::kCount; // 0.6.36: `return -1` SIN tocar st
	if (!in.is_building)
		return Bucket::kCount; // 0.6.36: `return -1` SIN tocar st
	if (!in.struct_clean)
		return Bucket::kStruct;
	if (in.junctions_so_far >= p.max_junctions)
		return Bucket::kBudget;
	if (in.region_ops >= p.max_ops || in.region_cycles >= p.max_cycles)
		return Bucket::kBudget;
	if (!in.target_aligned)
		return Bucket::kStruct;
	if (!in.target_has_shape)
		return Bucket::kStruct;
	if (in.target_is_self)
		return Bucket::kStruct;
	*contaba = false; // concedida
	return Bucket::kCount;
}

static Bucket OldCloseModel(const CloseInput& in, const Policy& p, bool* contaba)
{
	*contaba = true;
	if (!in.slot_in_range)
		return Bucket::kCount; // 0.6.36: `return` SIN tocar st
	if (!in.is_building)
		return Bucket::kCount; // 0.6.36: `return` SIN tocar st
	const bool shapeOk = !in.area_bad && in.junctions >= 1u && in.has_entry && in.ops != 0u &&
		in.ops <= p.max_ops && in.cycles <= p.max_cycles;
	if (!shapeOk)
		return in.area_bad ? Bucket::kStruct : Bucket::kBudget;
	*contaba = false; // adoptado
	return Bucket::kCount;
}

// Testigos del diferencial: se imprimen para que un verde no sea anonimo.
static uint64_t g_diff_cases = 0;
static uint64_t g_diff_holes = 0; // entradas que el viejo dejaba sin cubo
static uint64_t g_diff_rejects = 0; // entradas que el viejo si cubricaba
static uint64_t g_diff_passes = 0; // entradas que llegaban al final de la cadena

static void test_parity_arm()
{
	// Cada vector es un conjunto de CLASES DE EQUIVALENCIA, no valores al azar:
	// `reason` solo importa en cero/no-cero (la cadena antigua lo compara contra
	// 0), `edge_kind` en 1/distinto, y los contadores en los bordes de la
	// politica. Con clases en lugar de valores el producto cartesiano cubre el
	// espacio completo en lo que hace al veredicto y el arnés sigue siendo de
	// segundos — 6 valores por dimension lo mandaban a decenas de millones.
	const uint32_t kinds[] = {0u, 1u, 2u};
	const uint32_t reasons[] = {0u, 1u};
	const uint64_t execsA[] = {0ull, 1ull, 199999ull, 200000ull};
	const uint64_t execsB[] = {0ull, 999ull, 1000ull, 79999ull, 80000ull, 200000ull,
		800000ull, 800001ull, 802000ull, 1ull << 40};
	// Los valores del par se eligen JUSTO en los bordes de la politica (192 ops,
	// 384 ciclos) y un paso a cada lado, no al azar: es lo que puede mover el
	// veredicto. Con 6 valores por dimension el producto cartesiano se iba a
	// 100 millones de casos y el arnés dejaría de ser de segundos.
	const uint16_t opss[] = {0u, 95u, 96u, 97u};
	const uint16_t cycls[] = {0u, 191u, 192u, 193u};

	for (uint32_t live = 0; live < 2u; live++)
		for (uint32_t burned = 0; burned < 2u; burned++)
			for (uint32_t ki = 0; ki < 3u; ki++)
				for (uint32_t ra = 0; ra < 2u; ra++)
					for (uint32_t rb = 0; rb < 2u; rb++)
						for (uint32_t slots = 0; slots < 2u; slots++)
							for (uint32_t ea = 0; ea < 4u; ea++)
								for (uint32_t eb = 0; eb < 10u; eb++)
									for (uint32_t oa = 0; oa < 4u; oa++)
										for (uint32_t ob = 0; ob < 4u; ob++)
											for (uint32_t ca = 0; ca < 4u; ca++)
												for (uint32_t cb = 0; cb < 4u; cb++)
													for (uint32_t alloc = 0; alloc < 2u; alloc++)
														for (uint32_t link = 0; link < 2u; link++)
														{
															ArmInput in{};
															in.already_live = live != 0u;
															in.burned = burned != 0u;
															in.edge_kind = kinds[ki];
															in.shape_a_reason = reasons[ra];
															in.shape_b_reason = reasons[rb];
															in.slots_present = slots != 0u;
															in.exec_a = execsA[ea];
															in.exec_b = execsB[eb];
															in.ops_a = opss[oa];
															in.ops_b = opss[ob];
															in.cycles_a = cycls[ca];
															in.cycles_b = cycls[cb];

															bool contaba = false;
															const Bucket viejo = OldArmModel(in, kMotor, alloc != 0u, link != 0u, &contaba);
															const ArmCause c = NewArmChain(in, kMotor, alloc != 0u, link != 0u);
															g_diff_cases++;

															// (a) Paridad de CUBO, casilla por casilla:
															// donde el viejo movia rejected_X, el nuevo
															// tiene que mover una causa que DERIVA el
															// mismo cubo X. Con esto las cuatro cifras
															// del log de 0.6.36 siguen siendo comparables
															// con las de esta version.
															if (contaba)
															{
																g_diff_rejects++;
																CK(IsArmRejection(c), "paridad arm: el viejo rechazo y el nuevo aprueba");
																if (IsArmRejection(c))
																	CK(ArmBucketOf(c) == viejo, "paridad arm: mismo rechace, cubo distinto");
															}
															else
															{
																// El viejo arma el candidato: el nuevo
																// tiene que armarlo tambien. Si no, la
																// ganancia de telemetria compro un cambio
																// de conducta, y eso es lo que este
																// diferencial existe para pillar.
																g_diff_passes++;
																CK(c == ArmCause::kArmed, "paridad arm: el viejo armaba y el nuevo no");
															}

															// (b) Invariante de la cadena nueva para TODO
															// input: terminal => casilla con nombre y cubo.
															if (IsArmOutcome(c))
															{
																CK(std::strlen(ArmCauseName(c)) > 0u, "paridad arm: terminal sin nombre");
																if (IsArmRejection(c))
																	CK(static_cast<uint32_t>(ArmBucketOf(c)) < static_cast<uint32_t>(Bucket::kCount),
																		"paridad arm: rechace sin cubo valido");
															}
															else
															{
																CK(c == ArmCause::kPolicyPass,
																	"paridad arm: la unica salida no terminal es politica-ok");
															}
														}
}

static void test_parity_junction_close()
{
	const uint32_t jsf[] = {0u, 1u, 4u, 5u, 6u};
	const uint32_t regs[] = {0u, 8u, 191u, 192u, 193u, 5000u};

	for (uint32_t rng = 0; rng < 2u; rng++)
		for (uint32_t bld = 0; bld < 2u; bld++)
			for (uint32_t clean = 0; clean < 2u; clean++)
				for (uint32_t j = 0; j < 5u; j++)
					for (uint32_t ro = 0; ro < 6u; ro++)
						for (uint32_t rc = 0; rc < 6u; rc++)
							for (uint32_t al = 0; al < 2u; al++)
								for (uint32_t sh = 0; sh < 2u; sh++)
									for (uint32_t self = 0; self < 2u; self++)
									{
										JunctionInput in{};
										in.slot_in_range = rng != 0u;
										in.is_building = bld != 0u;
										in.struct_clean = clean != 0u;
										in.junctions_so_far = jsf[j];
										in.region_ops = regs[ro];
										in.region_cycles = regs[rc];
										in.target_aligned = al != 0u;
										in.target_has_shape = sh != 0u;
										in.target_is_self = self != 0u;

										bool contaba = false;
										const Bucket viejo = OldJunctionModel(in, kMotor, &contaba);
										const JunctionCause c = DecideJunction(in, kMotor);
										g_diff_cases++;
										if (!contaba)
										{
											// El viejo concedio la union: el nuevo tiene que
											// concederla tambien, o la paridad seria una
											// ganancia de telemetria comprada con conducta.
											g_diff_passes++;
											CK(c == JunctionCause::kJoined,
												"paridad union: el viejo concedia y el nuevo rechaza");
										}
										else if (viejo == Bucket::kCount)
										{
											g_diff_holes++;
											// El HUECO de 0.6.36: devolvia sin contar. Aqui el nuevo
											// SI tiene que tener casilla, y esa es la ganancia medida.
											CK(IsJunctionRejection(c), "paridad union: el hueco de 0.6.36 sigue sin casilla");
											CK(JunctionBucketOf(c) == Bucket::kIdentity,
												"paridad union: el hueco de 0.6.36 cae en estado-tabla");
										}
										else
										{
											g_diff_rejects++;
											CK(IsJunctionRejection(c), "paridad union: el viejo rechazo y el nuevo aprueba");
											if (IsJunctionRejection(c))
												CK(JunctionBucketOf(c) == viejo, "paridad union: mismo rechace, cubo distinto");
										}
									}

	const uint32_t jc[] = {0u, 1u, 2u};
	const uint32_t ops[] = {0u, 1u, 192u, 193u};
	const uint32_t cyc[] = {0u, 20u, 384u, 385u};

	for (uint32_t rng = 0; rng < 2u; rng++)
		for (uint32_t bld = 0; bld < 2u; bld++)
			for (uint32_t bad = 0; bad < 2u; bad++)
				for (uint32_t j = 0; j < 3u; j++)
					for (uint32_t he = 0; he < 2u; he++)
						for (uint32_t o = 0; o < 4u; o++)
							for (uint32_t cy = 0; cy < 4u; cy++)
							{
								CloseInput in{};
								in.slot_in_range = rng != 0u;
								in.is_building = bld != 0u;
								in.area_bad = bad != 0u;
								in.junctions = jc[j];
								in.has_entry = he != 0u;
								in.ops = ops[o];
								in.cycles = cyc[cy];

								bool contaba = false;
								const Bucket viejo = OldCloseModel(in, kMotor, &contaba);
								const CloseCause c = DecideClose(in, kMotor);
								g_diff_cases++;
								if (!contaba)
								{
									g_diff_passes++;
									CK(c == CloseCause::kAdopted, "paridad cierre: el viejo adoptaba y el nuevo rechaza");
								}
								else if (viejo == Bucket::kCount)
								{
									g_diff_holes++;
									CK(IsCloseRejection(c), "paridad cierre: el hueco de 0.6.36 sigue sin casilla");
									CK(CloseBucketOf(c) == Bucket::kIdentity,
										"paridad cierre: el hueco de 0.6.36 cae en estado-tabla");
								}
								else
								{
									g_diff_rejects++;
									CK(IsCloseRejection(c), "paridad cierre: el viejo rechazo y el nuevo aprueba");
									if (IsCloseRejection(c))
										CK(CloseBucketOf(c) == viejo, "paridad cierre: mismo rechace, cubo distinto");
								}
							}
}

int main()
{
	test_arm_causes();
	test_junction();
	test_close();
	test_buckets();
	test_gate();
	test_parity_arm();
	test_parity_junction_close();

	// Autovalerse: un verde que no toco los caminos raros no es evidencia.
	const bool toco_bordes = (g_checks > 100000);
	// Un diferencial que no recorrio el espacio no probo la paridad: los tres
	// testigos tienen que ser no-cero, y tiene que haber huecos de 0.6.36 (si
	// alguno los tapara, el verde del diferencial seria una paridad declarada).
	const bool dif_real = (g_diff_cases > 50000u) && (g_diff_rejects > 0u) &&
		(g_diff_holes > 0u) && (g_diff_passes > 0u);
	if (g_fails == 0 && !toco_bordes)
	{
		std::printf("FALLO: el arnes hizo %d comprobaciones — debajo del recuento "
			"que la cubierta de causas+precedencia+puerta exige. Verde declarativo, "
			"no verificado.\n",
			g_checks);
		g_fails++;
	}

	if (g_fails == 0 && !dif_real)
	{
		std::printf("FALLO: el diferencial con la cadena de 0.6.36 no recorrio el espacio "
			"(casos=%llu cubos=%llu huecos=%llu finales=%llu). La paridad NO se verifico.\n",
			(unsigned long long)g_diff_cases, (unsigned long long)g_diff_rejects,
			(unsigned long long)g_diff_holes, (unsigned long long)g_diff_passes);
		g_fails++;
	}

	std::printf("testigos: casos=%llu con-cubo-viejo=%llu sin-cubo-viejo(hueco)=%llu el-viejo-llegaba-al-final=%llu\n",
		(unsigned long long)g_diff_cases, (unsigned long long)g_diff_rejects,
		(unsigned long long)g_diff_holes, (unsigned long long)g_diff_passes);
	std::printf("H14 (MvuSbEligibility.h): %d Comprobaciones, %d Fallos\n", g_checks, g_fails);
	return g_fails ? 1 : 0;
}
