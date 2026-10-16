// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// Harness15 — arnes de LOGICA de la clasificacion MEDIDA del equipo
// (CenitDeviceClass.h, 0.6.37).
//
// Que prueba, en el unico sentido en que host puede probar algo:
//   (1) LA TABLA MIDR->clase, casilla por casilla, con los NUMEROS que usa el cpuinfo
//       vendorizado del arbol (app/src/main/cpp/3rdparty/cpuinfo/src/arm/uarch.c). Cada
//       part que el header afirma esta aqui con su numero, y cada part que el header NO
//       conoce tiene que dar kUnknown: un caso de mas, inventado, es exactamente el error
//       que este proyecto ya cometi6 una vez con los MIDR (la primera version de esta
//       tabla traia 0xd20/0xd21/0xd83 y un "0xd06 = A55" que no existen; los encontro la
//       revision contra uarch.c, no el telefono).
//   (2) LA CASCADA: sus trece razones terminales, los bordes numericos exactos (1799999 vs
//       1800000, 3071 vs 3072, 4095 vs 4096, 1999999 vs 2000000, big_cores 1 vs 2,
//       total_cores 3 vs 4) y la PRECEDENCIA: con dos impedimentos simultaneos gana el que
//       la cascada mira primero, y eso es telemetria que nadie debe poder mover en
//       silencio.
//   (3) EL AGREGADO DE CLUSTERS, donde viven las dos trampas de esta version: el cluster
//       mas rapido del telefono de referencia tiene UN nucleo (1x A78 a 2.40 GHz) y los
//       otros tres A78 estan en el cluster de 2.20 GHz — un "maximo del cluster mas
//       rapido" daria big_cores=1 y bajar al 778G de gama alta por como el OEM particiono.
//       Y la retirada: si un cluster NO nombrable corre al mismo reloj o mas que el mejor
//       nombrado, la clase se RETIRA, porque afirmar "chica" solo porque el cluster chico
//       era lo unico que la tabla supo nombrar es inventarse el nucleo grande.
//   (4) UN BARRIDO EXHAUSTIVO de las combinaciones bien formadas de Measurement (4 clases x
//       7 relojes x 8 RAM x 5 nucleos x 5 nucleos grandes = 5600 casos menos los
//       malformados), con las invariantes de seguridad: NUNCA se promete alta con un campo sin
//       medir, el tier NO depende de los campos informativos, subir un numero ya medido jamas
//       baja el tier, y Coherent dice si para todo dato bien formado y NO para los rotos.
//   (5) LA PARIDAD CON LA LEY VIEJA, que es lo que el usuario va a sentir: aqui vive una
//       copia fiel de AndroidDeviceDetection.cpp::GetDeviceTier tal como estaba en 0.6.36
//       (`IsHighEndSnapdragon() ? 2 : 1` para TODO Qualcomm + kCapable[] + kWeak[] +
//       return 0) y se compara contra la medida sobre LOS MISMOS aparatos, casilla por
//       casilla (protecciones y cola optima). No es una equivalencia — son insumos
//       distintos, nombres contra registros — y por eso se cuenta en las dos direcciones
//       y se exigen testigos no-cero.
//   (6) LA IMPRESION DE LOS MIDR CRUDOS (FormatClusters): cadena exacta, tope de buffer y
//       recorte DECLARADO. Sin esa linea, un clase=desconocida no se puede atribuir a un
//       part, y extender la tabla vuelve a ser copiar fichas de memoria.
//
// Que NO prueba, y por eso no lo afirma:
//   - que el recolector lea BIEN /sys: que la frecuencia salga de cpuinfo_max_freq y no de
//     cpuinfo_cluster::frequency (que en este arbol queda SIN LLENAR en Linux/ARM64), que
//     el MIDR sea el del cluster, que la medida se calcule una sola vez. host no puede
//     abrir los archivos del telefono. Son anclas de TEXTO en mutacion_h15.sh fase 2, con
//     su limitacion escrita.
//   - que native-lib.cpp aplique las cuatro casillas en los cuatro sitios, ni que el HUD
//     imprima la fila ni que el JNI devuelva 0/1/2. Idem: anclas de texto; el CI compila,
//     el telefono demuestra.
//   - que un telefono alcance tal perfil. Eso SOLO lo dice su log, y es el punto donde
//     este proyecto se nego dos veces a afirmar sin evidencia (0.6.25, y el build=0 de
//     0.6.36). Los fixtures de abajo son TOPOLOGIAS escritas a partir de fichas publicas;
//     prueban la ley sobre esas topologias, no que un aparato las tenga.

#include "CenitDeviceClass.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace CenitDeviceClass;

static int g_checks = 0;
static int g_fails = 0;

// CK toma una o dos cosas: la comprobacion y, si se pasa, el nombre del caso. Cuando no se
// pasa, el mensaje es el TEXTO de la comprobacion (#cond): un FALLO que dice solo "FALLO:"
// no dice donde, y este arnes existe para que un rojo se pueda leer sin abrir el archivo.
#define CK_MSG1(cond) CK_MSG2(cond, #cond)
#define CK_PICK(_1, _2, NAME, ...) NAME
#define CK(...) CK_PICK(__VA_ARGS__, CK_MSG2, CK_MSG1)(__VA_ARGS__)

#define CK_MSG2(cond, msg)                                                   \
	do                                                                       \
	{                                                                        \
		g_checks++;                                                          \
		if (!(cond))                                                         \
		{                                                                    \
			g_fails++;                                                       \
			std::printf("FALLO: %s\n", (msg));                               \
		}                                                                    \
	} while (0)

// CK_EQ igual: con mensaje o sin el, porque la fila ya imprime obtenido/esperado y un
// rojo sin nombre de caso es igual de inutil en las dos formas.
#define CK_EQ_GET(_0, _1, _2, NAME, ...) NAME
#define CK_EQ2(a, b) CK_EQ3(a, b, #a " == " #b)
#define CK_EQ(...) CK_EQ_GET(__VA_ARGS__, CK_EQ3, CK_EQ2)(__VA_ARGS__)

#define CK_EQ3(a, b, msg)                                                  \
	do                                                                       \
	{                                                                        \
		g_checks++;                                                          \
		if (!((a) == (b)))                                                   \
		{                                                                    \
			g_fails++;                                                       \
			std::printf("FALLO: %s (obtenido=%lld esperado=%lld)\n", (msg),  \
				(long long)(a), (long long)(b));                             \
		}                                                                    \
	} while (0)

// ------------------------------------------------------------------
// Helpers de fixture
//
// MIDR se arma con las mascaras del arbol (3rdparty/cpuinfo/src/arm/midr.h:
// IMPLEMENTER 0xFF000000, VARIANT 0x00F00000, PART 0x0000FFF0 desplazado 4,
// REVISION 0x0000000F). Si el header cambia sus desplazamientos sin cambiar esta
// construccion, (1) se cae.
// ------------------------------------------------------------------
static uint32_t Midr(uint32_t implementer, uint32_t part, uint32_t variant = 0u, uint32_t revision = 0u)
{
	return (implementer << 24) | (variant << 20) | (part << 4) | revision;
}

static ClusterView Cl(uint32_t implementer, uint32_t part, uint32_t cores, uint32_t khz)
{
	ClusterView c;
	c.midr = Midr(implementer, part);
	c.core_count = cores;
	c.frequency_khz = khz;
	return c;
}

// Igual que el recolector: agrega clusters y despues pega la RAM. Se reimprime aqui porque
// lo que se quiere forzar es la COMPOSICION (ClusterAggregate -> Measurement), que es el
// unico punto donde un flag se puede perder por un copia-pega mal hecho.
static Measurement Measure(const ClusterView* v, uint32_t n, uint32_t ram_mb, uint32_t avail_mb = 0u)
{
	const ClusterAggregate a = Aggregate(v, n);
	Measurement m;
	m.big_class = a.big_class;
	m.big_class_measured = a.class_measured;
	m.big_freq_khz = a.big_freq_khz;
	m.big_freq_measured = a.freq_measured;
	m.big_cores = a.big_cores;
	m.total_cores = a.total_cores;
	m.clusters = a.clusters;
	m.cores_measured = a.cores_measured;
	if (ram_mb > 0u)
	{
		m.total_ram_mb = ram_mb;
		m.ram_measured = true;
	}
	if (avail_mb > 0u)
	{
		m.avail_ram_mb = avail_mb;
		m.avail_ram_measured = true;
	}
	return m;
}

// Un Measurement bien formado "de casa": todo medido y cumpliendo para alta. Cada test
// mueve UNA casilla, porque moviendo dos la razon puede salir correcta por accidente.
static Measurement Base()
{
	Measurement m;
	m.cores_measured = true;
	m.total_cores = 8u;
	m.ram_measured = true;
	m.total_ram_mb = 8192u;
	m.big_class_measured = true;
	m.big_class = BigClass::kPerformance;
	m.big_freq_measured = true;
	m.big_freq_khz = 2400000u;
	m.big_cores = 4u;
	m.clusters = 3u;
	m.avail_ram_measured = true;
	m.avail_ram_mb = 2858u;
	return m;
}

// ------------------------------------------------------------------
// (1) La tabla MIDR -> clase, contra los numeros de uarch.c.
// ------------------------------------------------------------------
struct TabCase
{
	uint32_t impl;
	uint32_t part;
	BigClass esperado;
	const char* nombre;
};

static const TabCase kTabla[] = {
	// ---- ARM (0x41): clase chica — in-order o nucleo de eficiencia ----
	{0x41, 0xd01, BigClass::kWeak, "Cortex-A32"},
	{0x41, 0xd03, BigClass::kWeak, "Cortex-A53"},
	{0x41, 0xd04, BigClass::kWeak, "Cortex-A35"},
	{0x41, 0xd05, BigClass::kWeak, "Cortex-A55/A55r0"},
	{0x41, 0xd07, BigClass::kWeak, "Cortex-A57"},
	{0x41, 0xd46, BigClass::kWeak, "Cortex-A510"},
	{0x41, 0xd80, BigClass::kWeak, "Cortex-A520"},
	{0x41, 0xd8f, BigClass::kWeak, "Cortex-A320"},
	{0x41, 0xc05, BigClass::kWeak, "Cortex-A5 (rama 32 bits)"},
	{0x41, 0xc07, BigClass::kWeak, "Cortex-A7 (rama 32 bits)"},
	{0x41, 0xc08, BigClass::kWeak, "Cortex-A8 (rama 32 bits)"},
	{0x41, 0xc09, BigClass::kWeak, "Cortex-A9 (rama 32 bits)"},
	{0x41, 0xc0c, BigClass::kWeak, "Cortex-A12 (rama 32 bits)"},
	{0x41, 0xc0d, BigClass::kWeak, "Cortex-A12 Rockchip (rama 32 bits)"},
	{0x41, 0xc0e, BigClass::kWeak, "Cortex-A17 (rama 32 bits)"},
	{0x41, 0xc0f, BigClass::kWeak, "Cortex-A15 (rama 32 bits)"},
	{0x41, 0xd8a, BigClass::kWeak, "C1-Nano (eficiencia, sucesor del A520)"},
	// ---- ARM: clase media ----
	{0x41, 0xd06, BigClass::kMainstream, "Cortex-A65"},
	{0x41, 0xd08, BigClass::kMainstream, "Cortex-A72"},
	{0x41, 0xd09, BigClass::kMainstream, "Cortex-A73"},
	{0x41, 0xd0a, BigClass::kMainstream, "Cortex-A75 (el del Helio G88/G85)"},
	// ---- ARM: rendimiento ----
	{0x41, 0xd0b, BigClass::kPerformance, "Cortex-A76"},
	{0x41, 0xd0c, BigClass::kPerformance, "Neoverse N1"},
	{0x41, 0xd0d, BigClass::kPerformance, "Cortex-A77"},
	{0x41, 0xd0e, BigClass::kPerformance, "Cortex-A76AE"},
	{0x41, 0xd40, BigClass::kPerformance, "Neoverse V1"},
	{0x41, 0xd41, BigClass::kPerformance, "Cortex-A78 (el del telefono de referencia)"},
	{0x41, 0xd4b, BigClass::kPerformance, "Cortex-A78C"},
	{0x41, 0xd44, BigClass::kPerformance, "Cortex-X1"},
	{0x41, 0xd47, BigClass::kPerformance, "Cortex-A710"},
	{0x41, 0xd48, BigClass::kPerformance, "Cortex-X2"},
	{0x41, 0xd49, BigClass::kPerformance, "Neoverse N2"},
	{0x41, 0xd4d, BigClass::kPerformance, "Cortex-A715"},
	{0x41, 0xd4e, BigClass::kPerformance, "Cortex-X3"},
	{0x41, 0xd4f, BigClass::kPerformance, "Neoverse V2"},
	{0x41, 0xd81, BigClass::kPerformance, "Cortex-A720"},
	{0x41, 0xd82, BigClass::kPerformance, "Cortex-X4"},
	{0x41, 0xd85, BigClass::kPerformance, "Cortex-X925"},
	{0x41, 0xd87, BigClass::kPerformance, "Cortex-A725"},
	{0x41, 0xd8b, BigClass::kPerformance, "C1-Pro"},
	{0x41, 0xd8c, BigClass::kPerformance, "C1-Ultra"},
	{0x41, 0xd90, BigClass::kPerformance, "C1-Premium"},
	// ---- ARM: parts que uarch.c NO tiene (y los que este header deja fuera) ----
	{0x41, 0xd42, BigClass::kUnknown, "0xd42 no esta en uarch.c"},
	{0x41, 0xd83, BigClass::kUnknown, "0xd83 no esta en uarch.c"},
	{0x41, 0xdff, BigClass::kUnknown, "0xdff no esta en uarch.c"},
	// ---- Qualcomm (0x51): los Kryo que uarch.c re-mapea a un Cortex ----
	{0x51, 0x800, BigClass::kMainstream, "Kryo 260/280 Gold -> A73 (SD665/SD660)"},
	{0x51, 0x802, BigClass::kMainstream, "Kryo 385 Gold -> A75"},
	{0x51, 0x804, BigClass::kPerformance, "Kryo 485 Gold -> A76 (SD765/768)"},
	{0x51, 0x801, BigClass::kWeak, "Kryo 260/280 Silver -> A53"},
	{0x51, 0x803, BigClass::kWeak, "Kryo 385 Silver -> A55r0"},
	{0x51, 0x805, BigClass::kWeak, "Kryo 485 Silver -> A55"},
	{0x51, 0x001, BigClass::kPerformance, "Oryon"},
	{0x51, 0x002, BigClass::kPerformance, "Oryon V3"},
	// ---- Qualcomm: los que el header DECLARA desconocidos a proposito ----
	{0x51, 0x201, BigClass::kUnknown, "Kryo 821 silver (uarch.c no dice el nucleo de origen)"},
	{0x51, 0x205, BigClass::kUnknown, "Kryo 820/821 gold (idem)"},
	{0x51, 0x211, BigClass::kUnknown, "Kryo 820 silver (idem)"},
	{0x51, 0x04d, BigClass::kUnknown, "Krait dual"},
	{0x51, 0x06f, BigClass::kUnknown, "Krait quad"},
	{0x51, 0x02d, BigClass::kUnknown, "Scorpion dual"},
	{0x51, 0x00f, BigClass::kUnknown, "Scorpion"},
	{0x51, 0xc00, BigClass::kUnknown, "Falkor (servidor, vive bajo 'Q')"},
	{0x51, 0xc01, BigClass::kUnknown, "Saphira (servidor)"},
	{0x51, 0x806, BigClass::kUnknown, "part Qualcomm sin entrada (el caso Kryo 670)"},
	// ---- Samsung (0x53): los nucleos M, que la lista vieja tenia por MODELO ----
	{0x53, 0x001, BigClass::kPerformance, "Exynos M1/M2"},
	{0x53, 0x002, BigClass::kPerformance, "Exynos M3"},
	{0x53, 0x003, BigClass::kPerformance, "Exynos M4"},
	{0x53, 0x004, BigClass::kPerformance, "Exynos M5"},
	{0x53, 0x005, BigClass::kUnknown, "part Samsung sin entrada"},
	// ---- Huawei (0x48) ----
	{0x48, 0xd40, BigClass::kPerformance, "Kirin 980 big/medium (uarch.c lo mapea a A76)"},
	{0x48, 0xd01, BigClass::kUnknown, "Taishan v110 (Kunpeng, servidor)"},
	// ---- implementers que el header no toca ----
	{0x42, 0x00f, BigClass::kUnknown, "Broadcom Brahma"},
	{0x43, 0x0a1, BigClass::kUnknown, "Cavium ThunderX"},
	{0x4e, 0x000, BigClass::kUnknown, "Nvidia Denver"},
	{0x50, 0x004, BigClass::kUnknown, "Ampere Xgene"},
	{0x61, 0x022, BigClass::kUnknown, "Apple Icestorm (excluido a proposito)"},
	{0x61, 0x045, BigClass::kUnknown, "Apple Coll-Everest (excluido a proposito)"},
	{0x56, 0x581, BigClass::kUnknown, "Marvell PJ4"},
	{0x00, 0x000, BigClass::kUnknown, "implementer 0"},
	{0xff, 0xfff, BigClass::kUnknown, "todo desconocido"},
};

static void test_tabla_midr()
{
	for (const TabCase& t : kTabla)
		CK(ClassOfMidr(t.impl, t.part) == t.esperado, t.nombre);

	// El par (implementer, part) manda: 0xd40 es Neoverse V1 bajo ARM y Kirin 980 bajo
	// Huawei — los dos de rendimiento — pero Falkor vive bajo 0x51 y no bajo 0x43, asi que
	// 0x51/0xd40 tiene que seguir siendo desconocido.
	CK(ClassOfMidr(0x41, 0xd40) == BigClass::kPerformance);
	CK(ClassOfMidr(0x48, 0xd40) == BigClass::kPerformance);
	CK(ClassOfMidr(0x51, 0xd40) == BigClass::kUnknown);

	// MIDR reales con variant y revision llenas (0x410fd083 = A72 r0p3; 0x510f8022 =
	// Kryo 385 Gold). Las mascaras tienen que recortar variant/revision sin tocar el part.
	CK_EQ(MidrImplementer(0x410fd083u), 0x41u, "implementer de un A72 r0p3");
	CK_EQ(MidrPart(0x410fd083u), 0xd08u, "part de un A72 r0p3");
	CK(ClassOfMidr(MidrImplementer(0x410fd083u), MidrPart(0x410fd083u)) == BigClass::kMainstream);
	CK_EQ(MidrImplementer(0x510f8022u), 0x51u, "implementer Kryo");
	CK_EQ(MidrPart(0x510f8022u), 0x802u, "part Kryo con variant/revision no-cero");
	CK(ClassOfMidr(MidrImplementer(0x510f8022u), MidrPart(0x510f8022u)) == BigClass::kMainstream);

	// variant no cambia NADA de esta tabla (a diferencia del A55r0 de uarch.c, que se
	// distingue por variant pero cae en la misma clase): el header solo mira impl+part.
	CK(ClassOfCluster(Cl(0x41, 0xd05, 4u, 1800000u)) == BigClass::kWeak);
	{
		ClusterView v;
		v.midr = Midr(0x41u, 0xd05u, 0u, 0u); // variant 0 => a55r0 en uarch.c
		v.core_count = 4u;
		v.frequency_khz = 1800000u;
		CK(ClassOfCluster(v) == BigClass::kWeak);
		v.midr = Midr(0x41u, 0xd05u, 1u, 1u); // variant != 0 => a55
		CK(ClassOfCluster(v) == BigClass::kWeak);
	}

	// midr == 0 es "el kernel no lo reporto", no "un nucleo con implementer 0 part 0".
	{
		ClusterView c;
		c.midr = 0u;
		c.core_count = 4u;
		c.frequency_khz = 2000000u;
		CK(ClassOfCluster(c) == BigClass::kUnknown);
		// Un (0x41, 0x003) — que ARM no tiene — tampoco se disfraza de lectura valida.
		CK(ClassOfCluster(Cl(0x41, 0x003, 4u, 2000000u)) == BigClass::kUnknown);
	}

	// Unicidad de kTabla: un par repetido con expectativas distintas taparia una
	// comprobacion (el compilador si que rechaza el `case` duplicado en el header).
	for (size_t i = 0; i < sizeof(kTabla) / sizeof(kTabla[0]); i++)
		for (size_t j = i + 1; j < sizeof(kTabla) / sizeof(kTabla[0]); j++)
			CK(!(kTabla[i].impl == kTabla[j].impl && kTabla[i].part == kTabla[j].part),
				"kTabla tiene un (impl,part) duplicado");

	// Cobertura: los cuatro valores de clase tienen que estar representados en la tabla, y
	// cada nombre de clase tiene que ser distinto (el registro imprime esos nombres).
	uint32_t vistas = 0;
	for (const TabCase& t : kTabla)
		vistas |= (1u << static_cast<uint32_t>(t.esperado));
	CK((vistas & (1u << static_cast<uint32_t>(BigClass::kUnknown))) != 0u);
	CK((vistas & (1u << static_cast<uint32_t>(BigClass::kWeak))) != 0u);
	CK((vistas & (1u << static_cast<uint32_t>(BigClass::kMainstream))) != 0u);
	CK((vistas & (1u << static_cast<uint32_t>(BigClass::kPerformance))) != 0u);
}

// ------------------------------------------------------------------
// (3) El agregado de clusters.
// ------------------------------------------------------------------
static void test_agregado()
{
	// 778G leyendose con parts ARM: 1x A78 2.40 | 3x A78 2.20 | 4x A55 1.90. Es la
	// topologia que su propio log reporta (Cluster Count = 3 clusters).
	{
		ClusterView v[3] = {Cl(0x41, 0xd41, 1u, 2400000u), Cl(0x41, 0xd41, 3u, 2200000u),
			Cl(0x41, 0xd05, 4u, 1900000u)};
		const ClusterAggregate a = Aggregate(v, 3u);
		CK(a.big_class == BigClass::kPerformance);
		CK(a.class_measured, "778G con parts ARM: la clase queda AFIRMADA, no solo calculada");
		CK_EQ(a.big_cores, 4u, "778G: big_cores son los de la clase maxima (1+3), NO los del cluster mas rapido");
		CK_EQ(a.total_cores, 8u, "778G: ocho nucleos");
		CK_EQ(a.big_freq_khz, 2400000u, "778G: el reloj maximo del telefono");
		CK(a.freq_measured);
		CK_EQ(a.clusters, 3u, "778G: tres clusters, como dice su log");
		CK(a.cores_measured);
		CK(DecideTier(Measure(v, 3u, 7349u, 2858u)) == Reason::kPerfHigh,
			"778G con su RAM real (7349 MB) tiene que llegar a alta por MERITO, no por apellido");
	}

	// El mismo 778G leyendose con Kryo: parts de Qualcomm que este cpuinfo NO conoce en el
	// cluster grande, y el cluster chico 0x805 -> A55, que SI esta en la tabla. La "clase
	// maxima ingenua" saldria CHICA (la del unico cluster nombrable) y el telefono se
	// leeria nucleo-chico, gama baja. La clase tiene que salir RETIRADA.
	{
		ClusterView v[3] = {Cl(0x51, 0x806, 1u, 2400000u), Cl(0x51, 0x806, 3u, 2200000u),
			Cl(0x51, 0x805, 4u, 1900000u)};
		const ClusterAggregate a = Aggregate(v, 3u);
		CK(!a.class_measured, "Kryo sin tabla: NO se puede afirmar clase chica");
		CK(a.big_class == BigClass::kUnknown);
		CK_EQ(a.big_cores, 0u, "sin clase no se sabe cuales son los grandes: 0, no 8");
		CK_EQ(a.big_freq_khz, 2400000u, "el reloj se agrega sobre TODOS los clusters");
		CK(a.freq_measured);
		CK_EQ(a.total_cores, 8u, "los nucleos totales si se cuentan");
		const Measurement m = Measure(v, 3u, 6144u);
		CK(DecideTier(m) == Reason::kFreqOnlyCapable, "desconocido + 2.4 GHz + 6 GB: media honesta, no nucleo-chico");
		CK(TierOf(DecideTier(m)) == Tier::kMid, "desconocido con reloj alto: media, nunca alta");
		CK(Coherent(m, DecideTier(m)));
	}

	// Retirada con clase chica ganadora y un desconocido MAS rapido.
	{
		ClusterView v[2] = {Cl(0x41, 0xd03, 2u, 1600000u), Cl(0xff, 0xfff, 4u, 2600000u)};
		const ClusterAggregate a = Aggregate(v, 2u);
		CK(!a.class_measured, "cluster no nombrable mas rapido: la clase se retira");
		CK_EQ(a.big_freq_khz, 2600000u);
		CK_EQ(a.big_cores, 0u);
	}

	// Empate a reloj tambien retira: con 2.4 GHz en ambos, afirmar la clase del nombrado
	// seria un 50/50, y el registro no puede decir "lo medimos" sobre un empate.
	{
		ClusterView v[2] = {Cl(0x41, 0xd41, 2u, 2400000u), Cl(0xff, 0xfff, 2u, 2400000u)};
		const ClusterAggregate a = Aggregate(v, 2u);
		CK(!a.class_measured, "empate a reloj con un cluster no nombrable: se retira");
	}

	// NO retirada cuando el desconocido corre MAS LENTO: ahi la clase del grande si esta
	// afirmada, y retirar seria tirar informacion que el registro dio.
	{
		ClusterView v[2] = {Cl(0x41, 0xd41, 4u, 2400000u), Cl(0xff, 0xfff, 2u, 1800000u)};
		const ClusterAggregate a = Aggregate(v, 2u);
		CK(a.class_measured, "desconocido mas lento: la clase del grande se mantiene");
		CK(a.big_class == BigClass::kPerformance);
		CK_EQ(a.big_cores, 4u);
		CK_EQ(a.big_freq_khz, 2400000u);
	}

	// NO retirada cuando el reloj del desconocido NO se pudo leer (khz == 0): sin numero no
	// hay evidencia, y retirar por una lectura fallada seria otro centinela-vuelto-dato.
	{
		ClusterView v[2] = {Cl(0x41, 0xd0a, 2u, 2000000u), Cl(0x51, 0x806, 4u, 0u)};
		const ClusterAggregate a = Aggregate(v, 2u);
		CK(a.class_measured, "desconocido sin reloj leido: no se retira la clase");
		CK(a.big_class == BigClass::kMainstream);
		CK_EQ(a.big_freq_khz, 2000000u);
		CK_EQ(a.big_cores, 2u);
	}

	// Los dos clusters SIN reloj: el nombrable conserva su clase y el desconocido no puede
	// retirar una afirmacion con un 0 que no es una medida. Esta fila es la unica que fija
	// el `freq_unknown > 0u` de la guarda: sin esa comprobacion, `0 >= 0` retiraria la clase
	// en cualquier telefono cuyo sysfs este cerrado, o sea el perfil conservador dependeria
	// de un permiso en lugar de depender del hardware.
	{
		ClusterView v[2] = {Cl(0x41, 0xd0a, 2u, 0u), Cl(0xff, 0xfff, 4u, 0u)};
		const ClusterAggregate a = Aggregate(v, 2u);
		CK(a.class_measured, "sin reloj en NINGUN cluster: la clase leida no se retira");
		CK(a.big_class == BigClass::kMainstream);
		CK_EQ(a.big_cores, 2u);
		CK(!a.freq_measured, "y el reloj sigue sin medir, que es lo que el log tiene que decir");
		CK(DecideTier(Measure(v, 2u, 6144u)) == Reason::kBigFreqUnmeasured);
	}

	// Los tres aparatos que el usuario nombro, con las topologias de sus fichas.
	{
		// Helio G88/G85: 2x A75 2.00 + 6x A55 1.80.
		ClusterView g88[2] = {Cl(0x41, 0xd0a, 2u, 2000000u), Cl(0x41, 0xd05, 6u, 1800000u)};
		const ClusterAggregate a = Aggregate(g88, 2u);
		CK(a.big_class == BigClass::kMainstream);
		CK_EQ(a.big_cores, 2u);
		CK_EQ(a.total_cores, 8u);
		CK(DecideTier(Measure(g88, 2u, 4096u)) == Reason::kMainstreamCapable, "G88 con 4 GB: media por clase, no por nombre");

		// Helio G99: 2x A76 2.20 + 6x A55 2.00. Con 6 GB tiene que llegar a alta; es justo
		// el caso que la lista vieja clavaba en gama baja por la cadena "helio".
		ClusterView g99[2] = {Cl(0x41, 0xd0b, 2u, 2200000u), Cl(0x41, 0xd05, 6u, 2000000u)};
		const ClusterAggregate b = Aggregate(g99, 2u);
		CK(b.big_class == BigClass::kPerformance);
		CK_EQ(b.big_cores, 2u);
		CK_EQ(b.big_freq_khz, 2200000u);
		CK(DecideTier(Measure(g99, 2u, 6144u)) == Reason::kPerfHigh, "G99 con 6 GB: alta");
		CK(DecideTier(Measure(g99, 2u, 3072u)) == Reason::kPerfNotEnough, "G99 con 3 GB: NO alta (la RAM manda)");

		// Snapdragon 665: 4x Kryo 260 Gold (A73) 2.00 + 4x Silver (A53) 1.80.
		ClusterView sd665[2] = {Cl(0x51, 0x800, 4u, 2000000u), Cl(0x51, 0x801, 4u, 1800000u)};
		const ClusterAggregate c = Aggregate(sd665, 2u);
		CK(c.big_class == BigClass::kMainstream);
		CK_EQ(c.big_cores, 4u, "SD665: los cuatro Kryo 260 Gold son de la clase maxima");
		CK_EQ(c.total_cores, 8u);
		CK(DecideTier(Measure(sd665, 2u, 4096u)) == Reason::kMainstreamCapable);
	}

	// Dimensity 700/720 (mt6833/mt6853, ambos listados como DEBILES en la kWeak vieja):
	// 2x A76 con 6 GB. La medida los tiene que subir.
	{
		ClusterView d700[2] = {Cl(0x41, 0xd0b, 2u, 2200000u), Cl(0x41, 0xd05, 6u, 2000000u)};
		CK(DecideTier(Measure(d700, 2u, 4096u)) == Reason::kPerfHigh, "A76 con 4 GB: alta");
		ClusterView d720[2] = {Cl(0x41, 0xd0b, 2u, 2000000u), Cl(0x41, 0xd05, 6u, 2000000u)};
		CK(DecideTier(Measure(d720, 2u, 6144u)) == Reason::kPerfHigh, "A76 justo a 2.00 GHz sigue siendo A76");
	}

	// Un cluster con core_count == 0 es una lectura que fallo, no un cluster de cero
	// nucleos: no entra ni al total ni al recuento ni a la clase.
	{
		ClusterView v[3] = {Cl(0x41, 0xd41, 4u, 2400000u), Cl(0x41, 0xd03, 0u, 2000000u),
			Cl(0x41, 0xd05, 4u, 1900000u)};
		const ClusterAggregate a = Aggregate(v, 3u);
		CK_EQ(a.total_cores, 8u);
		CK_EQ(a.clusters, 2u, "cluster con 0 nucleos es una lectura fallida, no un cluster");
		CK(a.class_measured);
		CK_EQ(a.big_freq_khz, 2400000u);
		// Y un cluster muerto que sea el UNICO: nucleos sin medir, no "cero nucleos".
		ClusterView w[1] = {Cl(0x41, 0xd03, 0u, 0u)};
		const ClusterAggregate z = Aggregate(w, 1u);
		CK(!z.cores_measured, "cluster muerto: nucleos sin medir, no cero nucleos");
		CK(!z.class_measured, "la clase de un cluster que no existe tampoco se afirma");
	}

	// Todo-muerto: el estado "el kernel no me dijo nada".
	{
		ClusterView v[1];
		const ClusterAggregate a = Aggregate(v, 1u);
		CK(!a.cores_measured);
		CK(!a.class_measured);
		CK(!a.freq_measured);
		CK_EQ(a.total_cores, 0u);
		CK_EQ(a.clusters, 0u);
		CK_EQ(a.big_cores, 0u);
		CK(DecideTier(Measure(v, 1u, 0u)) == Reason::kNoMeasurement);
	}

	// count == 0 y count por encima del techo. El recolector nunca pasa kMaxClusters, pero
	// la ley no puede depender de que el llamador sea correcto.
	{
		const ClusterAggregate a = Aggregate(nullptr, 0u);
		CK(!a.cores_measured);
		CK(!a.freq_measured);
		CK_EQ(a.clusters, 0u);

		std::vector<ClusterView> v(kMaxClusters + 8u, Cl(0x41, 0xd41, 4u, 2400000u));
		const ClusterAggregate b = Aggregate(v.data(), static_cast<uint32_t>(v.size()));
		CK_EQ(b.clusters, kMaxClusters, "el agregado se corta en kMaxClusters");
		CK_EQ(b.total_cores, kMaxClusters * 4u, "y no cuenta los que quedaron afuera");
	}

	// Saturacion: nucleos que no caben en uint8_t se declaran en el techo, no dan la vuelta
	// (un 256 impreso como 0 seria un dato falso, y ningun telefono real llega ahi).
	{
		std::vector<ClusterView> v(16u);
		for (uint32_t i = 0; i < 16u; i++)
			v[i] = Cl(0x41, 0xd41, 30u, 2400000u);
		const ClusterAggregate a = Aggregate(v.data(), 16u);
		CK_EQ(a.total_cores, 255u, "total_cores se satura en 255 y no da la vuelta");
		CK_EQ(a.big_cores, 255u, "big_cores se satura igual");
	}

	// StrongerOf: el orden es el de la cascada y kUnknown no sube el techo.
	CK(StrongerOf(BigClass::kUnknown, BigClass::kWeak) == BigClass::kWeak, "orden de clases: desconocida no sube el techo");
	CK(StrongerOf(BigClass::kPerformance, BigClass::kMainstream) == BigClass::kPerformance, "orden de clases: la mas fuerte gana, en el orden que lleguen");
	CK(StrongerOf(BigClass::kMainstream, BigClass::kPerformance) == BigClass::kPerformance);
	CK(StrongerOf(BigClass::kWeak, BigClass::kUnknown) == BigClass::kWeak);
	CK(StrongerOf(BigClass::kUnknown, BigClass::kUnknown) == BigClass::kUnknown);
}

// ------------------------------------------------------------------
// (2) La cascada: bordes, precedencia y cobertura.
// ------------------------------------------------------------------
static uint32_t g_reason_vista = 0; // bitset de razones alcanzadas por cualquier test

static void Visto(Reason r)
{
	if (static_cast<uint32_t>(r) < kReasonCount)
		g_reason_vista |= (1u << static_cast<uint32_t>(r));
}

static void test_cascada()
{
	// Estado limpio: alta con todo medido.
	{
		const Measurement m = Base();
		const Reason r = DecideTier(m);
		CK(r == Reason::kPerfHigh);
		CK(TierOf(r) == Tier::kHigh);
		CK(Coherent(m, r));
		CK_EQ(MissingFields(m), 0u);
		Visto(r);
	}

	// Nada medido.
	{
		Measurement m;
		const Reason r = DecideTier(m);
		CK(r == Reason::kNoMeasurement, "los cuatro flags apagados: nada-medido");
		CK_EQ(MissingFields(m), kMeasuredFields, "los cuatro campos que deciden, contados");
		CK(Coherent(m, r));
		CK(TierOf(r) == Tier::kLow);
		Visto(r);
	}

	// Nucleos sin leer con RAM si leida: "nucleos-no-leidos", NO "pocos-nucleos". Sin esta
	// casilla el 0 de un Measurement sin llenar culpaba al hardware de una lectura fallida.
	{
		Measurement m = Base();
		m.cores_measured = false;
		m.total_cores = 0u;
		const Reason r = DecideTier(m);
		CK(r == Reason::kCoresUnmeasured, "nucleos sin bandera: nucleos-no-leidos, no pocos-nucleos");
		CK(std::strcmp(ReasonName(r), "nucleos-no-leidos") == 0);
		CK(Coherent(m, r));
		CK_EQ(MissingFields(m), 1u);
		CK(TierOf(r) == Tier::kLow);
		Visto(r);
	}

	// Bordes de nucleos: 3 -> pocos, 4 -> sigue.
	{
		Measurement m = Base();
		m.total_cores = 3u;
		CK(DecideTier(m) == Reason::kFewCores, "tres nucleos: pocos-nucleos");
		CK(Coherent(m, DecideTier(m)));
		Visto(Reason::kFewCores);
		m.total_cores = 4u;
		CK(DecideTier(m) == Reason::kPerfHigh, "cuatro nucleos: sigue, el piso es inclusivo");
	}

	// RAM sin leer con todo lo demas listo.
	{
		Measurement m = Base();
		m.ram_measured = false;
		m.total_ram_mb = 0u;
		const Reason r = DecideTier(m);
		CK(r == Reason::kRamUnmeasured);
		CK(std::strcmp(ReasonName(r), "ram-no-leida") == 0);
		CK(Coherent(m, r));
		CK(TierOf(r) == Tier::kLow);
		Visto(r);
	}

	// Bordes de RAM: 3071 poca, 3072 no; y el piso de 4 GB para alta.
	{
		Measurement m = Base();
		m.total_ram_mb = 3071u;
		CK(DecideTier(m) == Reason::kRamTight, "3071 MB: poca-ram");
		CK(Coherent(m, DecideTier(m)));
		Visto(Reason::kRamTight);

		// Con los NUMEROS escritos, no con la constante: si alguien mueve el umbral de
		// 4 GB, estas tres filas son las que dicen cual era el piso que se prometio.
		for (uint32_t ram : {3072u, 3584u, 4095u})
		{
			Measurement x = Base();
			x.total_ram_mb = ram;
			CK(DecideTier(x) == Reason::kPerfNotEnough, "4095 MB o menos: alta NO, la RAM de 4 GB manda");
			CK(Coherent(x, DecideTier(x)));
		}
		Measurement x = Base();
		x.total_ram_mb = 4096u; // el piso de 4 GB, en NUMERO escrito
		CK(DecideTier(x) == Reason::kPerfHigh, "4096 MB exactos con el resto cumplido: alta");
		Visto(Reason::kPerfNotEnough);
	}

	// Clase desconocida, con sus cuatro bordes por separado.
	{
		Measurement m = Base();
		m.big_class_measured = false;
		m.big_class = BigClass::kUnknown;
		CK(DecideTier(m) == Reason::kFreqOnlyCapable, "clase sin medir + 2.4 GHz + 8 GB: solo-frecuencia-alta");
		CK(TierOf(DecideTier(m)) == Tier::kMid);
		CK(Coherent(m, DecideTier(m)));
		Visto(Reason::kFreqOnlyCapable);

		m.big_freq_khz = 1999999u; // un kHz debajo del piso de 2 GHz
		CK(DecideTier(m) == Reason::kBigClassUnknown, "1999999 kHz: ya no alcanza solo-frecuencia-alta");
		CK(Coherent(m, DecideTier(m)));
		Visto(Reason::kBigClassUnknown);
		m.big_freq_khz = 2000000u; // el piso, en NUMERO escrito
		CK(DecideTier(m) == Reason::kFreqOnlyCapable,
			"2000000 kHz exactos con 4 GB: solo-frecuencia-alta sigue alcanzable (borde inclusivo)");

		m.total_ram_mb = 4095u; // un MB debajo del piso de 4 GB
		CK(DecideTier(m) == Reason::kBigClassUnknown, "4095 MB con la clase sin medir: nucleo-desconocido");
		m.total_ram_mb = 4096u;
		CK(DecideTier(m) == Reason::kFreqOnlyCapable, "4096 MB exactos: la puerta de solo-frecuencia se abre");

		m.big_freq_measured = false;
		m.big_freq_khz = 0u;
		CK(DecideTier(m) == Reason::kBigClassUnknown);
		CK(Coherent(m, DecideTier(m)));
		CK_EQ(MissingFields(m), 2u, "clase y frecuencia sin medir");
	}

	// Clase chica manda sobre el reloj: un A55 a 2.0 GHz es un nucleo de eficiencia. Es el
	// error que la lista de nombres comete al reves.
	{
		Measurement m = Base();
		m.big_class = BigClass::kWeak;
		m.big_freq_khz = 2000000u;
		const Reason r = DecideTier(m);
		CK(r == Reason::kBigClassWeak, "A55 a 2.0 GHz: nucleo-chico, la clase manda sobre el reloj");
		CK(TierOf(r) == Tier::kLow);
		CK(std::strcmp(ReasonName(r), "nucleo-chico") == 0);
		CK(Coherent(m, r));
		Visto(r);
	}

	// Bordes de frecuencia con clase medida.
	{
		Measurement m = Base();
		m.big_class = BigClass::kMainstream;
		m.big_freq_khz = 1799999u;
		CK(DecideTier(m) == Reason::kBigFreqWeak, "1799999 kHz: frecuencia-baja");
		CK(Coherent(m, DecideTier(m)));
		Visto(Reason::kBigFreqWeak);
		m.big_freq_khz = 1800000u; // el piso, en NUMERO escrito
		CK(DecideTier(m) == Reason::kMainstreamCapable, "1800000 kHz exactos: pasa a nucleo-medio (borde inclusivo)");
		CK(TierOf(DecideTier(m)) == Tier::kMid, "nucleo-medio es media, no alta");
		CK(std::strcmp(ReasonName(DecideTier(m)), "nucleo-medio") == 0);
		Visto(Reason::kMainstreamCapable);
	}

	// Clase conocida con frecuencia ILEGIBLE: se nombra como lectura que fallo.
	{
		Measurement m = Base();
		m.big_freq_measured = false;
		m.big_freq_khz = 0u;
		const Reason r = DecideTier(m);
		CK(r == Reason::kBigFreqUnmeasured, "clase si, reloj no: frecuencia-no-leida");
		CK(std::strcmp(ReasonName(r), "frecuencia-no-leida") == 0);
		CK(TierOf(r) == Tier::kMid);
		CK(Coherent(m, r));
		CK_EQ(MissingFields(m), 1u);
		Visto(r);
	}

	// big_cores: 1 -> grande-pero-corto, 2 -> alta.
	{
		Measurement m = Base();
		m.big_cores = 1u;
		CK(DecideTier(m) == Reason::kPerfNotEnough, "un solo nucleo de rendimiento: grande-pero-corto");
		CK(Coherent(m, DecideTier(m)));
		m.big_cores = 2u; // el piso, en NUMERO escrito
		CK(DecideTier(m) == Reason::kPerfHigh, "dos nucleos grandes exactos: alcanza (borde inclusivo)");
	}

	// Frecuencia de rendimiento por debajo del techo de 2 GHz.
	{
		Measurement m = Base();
		m.big_freq_khz = 1999999u;
		CK(DecideTier(m) == Reason::kPerfNotEnough, "1999999 kHz con nucleo de rendimiento: NO es alta");
		m.big_freq_khz = 2000000u;
		CK(DecideTier(m) == Reason::kPerfHigh, "2000000 kHz exactos: alta (el piso se cumple con el numero de la casa)");
	}

	// Precedencia, una por una: con DOS impedimentos simultaneos gana el que la cascada
	// mira primero. Cada una de estas filas es un cambio de telemetria si alguien reordena.
	{
		// Paso 1 (nada medido) manda sobre todo: los cuatro flags apagados.
		const Measurement m;
		CK(DecideTier(m) == Reason::kNoMeasurement, "los cuatro flags apagados: paso 1 gana");

		// Y el mismo arreglo con un solo flag encendido ya NO es "nada medido": es paso 2a,
		// porque la caida del perfil tiene que nombrar la lectura que falto, no el total.
		Measurement uno;
		uno.ram_measured = true;
		CK(DecideTier(uno) == Reason::kCoresUnmeasured, "un flag encendido: paso 2a, no paso 1");
		CK(!Coherent(uno, DecideTier(uno)), "ram_medido con ram=0 es el centinela: INCOHERENCIA");

		// Paso 2a (nucleos sin leer) manda sobre RAM leida floja y sobre RAM sin leer.
		{
			Measurement p = Measurement();
			p.ram_measured = true;
			p.total_ram_mb = 1024u;
			CK(DecideTier(p) == Reason::kCoresUnmeasured, "nucleos sin leer manda sobre poca-ram");
			// Para mirar el par "nucleos sin leer + RAM sin leer" sin caer en el paso 1 hace
			// falta una tercera medida (aqui el reloj): con dos banderas apagadas y dos
			// encendidas el paso 1 ya no aplica, y el registro tiene que decir la que la
			// cascada mira primero.
			Measurement q = Measurement();
			q.big_freq_measured = true;
			q.big_freq_khz = 2400000u;
			CK(DecideTier(q) == Reason::kCoresUnmeasured, "nucleos sin leer manda sobre ram-no-leida");
		}
	}
	{
		// Paso 2b (pocos nucleos) manda sobre RAM floja, clase chica y frecuencia chica.
		Measurement m = Base();
		m.total_cores = 2u;
		m.total_ram_mb = 1024u;
		m.big_class = BigClass::kWeak;
		m.big_freq_khz = 1200000u;
		CK(DecideTier(m) == Reason::kFewCores, "pocos-nucleos manda sobre todo lo demas");
	}
	{
		// Paso 3 (RAM) manda sobre clase chica y frecuencia sin leer.
		Measurement m = Base();
		m.total_ram_mb = 2048u;
		m.big_class = BigClass::kWeak;
		m.big_freq_khz = 1200000u;
		CK(DecideTier(m) == Reason::kRamTight, "poca-ram manda sobre nucleo-chico");
		m = Base();
		m.total_ram_mb = 2048u;
		m.big_freq_measured = false;
		m.big_freq_khz = 0u;
		CK(DecideTier(m) == Reason::kRamTight, "poca-ram manda sobre frecuencia-no-leida");
	}
	{
		// Paso 4 (clase) manda sobre frecuencia sin leer y sobre frecuencia chica.
		Measurement m = Base();
		m.big_class = BigClass::kWeak;
		m.big_freq_measured = false;
		m.big_freq_khz = 0u;
		CK(DecideTier(m) == Reason::kBigClassWeak, "nucleo-chico manda sobre frecuencia-no-leida");
		m.big_freq_khz = 1500000u;
		m.big_freq_measured = true;
		CK(DecideTier(m) == Reason::kBigClassWeak, "nucleo-chico manda sobre frecuencia-baja");
	}
	{
		// Paso 5 (frecuencia) manda sobre el paquete de la clase (par / 2 GHz / 4 GB).
		Measurement m = Base();
		m.big_class = BigClass::kPerformance;
		m.big_cores = 1u;
		m.big_freq_khz = 1500000u;
		CK(DecideTier(m) == Reason::kBigFreqWeak, "frecuencia-baja manda sobre grande-pero-corto");
	}
	{
		// Paso 6: clase media se resuelve ANTES que el paquete de alta, asi que un A75 con
		// 3.5 GB es nucleo-medio, no grande-pero-corto.
		Measurement m = Base();
		m.big_class = BigClass::kMainstream;
		m.total_ram_mb = 3584u;
		CK(DecideTier(m) == Reason::kMainstreamCapable, "la clase media no se castiga por la RAM de 4 GB");
	}

	// Un valor fuera del enum (un llamador roto) no se gana el 2x por ser el ultimo case.
	{
		const Reason roto = static_cast<Reason>(99u);
		CK(TierOf(roto) == Tier::kLow, "razon fuera del enum: conservador");
		CK(!Coherent(Base(), roto), "razon fuera del enum: no es coherente con nada");
		CK(std::strcmp(ReasonName(roto), "?") == 0);
		CK(std::strcmp(BigClassName(static_cast<BigClass>(77u)), "?") == 0);
		CK(std::strcmp(TierName(static_cast<Tier>(9u)), "?") == 0);

		Measurement sucio = Base();
		sucio.big_class = static_cast<BigClass>(77u);
		CK(DecideTier(sucio) == Reason::kPerfNotEnough, "clase fuera del enum: no promete alta");
		CK(TierOf(DecideTier(sucio)) != Tier::kHigh,
			"clase fuera del enum: se queda en media como mucho, nunca en 2x");
		// ... y el flag de clase tiene que decir lo mismo que el valor: 77 != kUnknown, o
		// sea el recolector no puede apagarlo. Si lo apagara, Coherent tiene que ruidar.
		sucio.big_class_measured = false;
		CK(!Coherent(sucio, DecideTier(sucio)), "clase real con flag apagado: INCOHERENCIA");
	}

	// Los nombres del registro: todos distintos, ninguno vacio, y el de cada razon es el
	// que el log va a imprimir (si dos razones compartieran nombre, un log dejaria de
	// explicar por que bajo el perfil).
	{
		const char* nombres[kReasonCount];
		for (uint32_t i = 0; i < kReasonCount; i++)
		{
			nombres[i] = ReasonName(static_cast<Reason>(i));
			CK(std::strlen(nombres[i]) > 0u, "una razon no tiene nombre");
			CK(std::strcmp(nombres[i], "?") != 0, "una razon valida cayo en el nombre de guarda");
			for (uint32_t j = 0; j < i; j++)
				CK(std::strcmp(nombres[i], nombres[j]) != 0, "dos razones comparten nombre en el registro");
		}
		// kCount es la guarda: tiene que dar "?".
		CK(std::strcmp(ReasonName(Reason::kCount), "?") == 0);
		CK(std::strcmp(BigClassName(BigClass::kCount), "?") == 0);
		CK(std::strcmp(TierName(Tier::kCount), "?") == 0);
		// Y los tres nombres de tier existen, porque el HUD los imprime.
		CK(std::strcmp(TierName(Tier::kLow), "baja") == 0);
		CK(std::strcmp(TierName(Tier::kMid), "media") == 0);
		CK(std::strcmp(TierName(Tier::kHigh), "alta") == 0);
	}

	// El tier es UNA tabla derivada: cada razon valida mapea a un tier conocido, y hay
	// exactamente una razon de alta (kPerfHigh) — si alguien agrega una segunda, esta
	// comprobacion lo ve.
	{
		uint32_t altas = 0, medias = 0, bajas = 0;
		for (uint32_t i = 0; i < kReasonCount; i++)
		{
			const Reason r = static_cast<Reason>(i);
			const Tier t = TierOf(r);
			CK(t == Tier::kLow || t == Tier::kMid || t == Tier::kHigh, "tier fuera del enum");
			if (t == Tier::kHigh)
				altas++;
			else if (t == Tier::kMid)
				medias++;
			else
				bajas++;
		}
		CK_EQ(altas, 1u, "exactamente una razon llega a alta");
		CK_EQ(medias, 4u, "cuatro razones llegan a media");
		CK_EQ(bajas, 8u, "ocho razones llegan a baja");
		CK_EQ(altas + medias + bajas, kReasonCount, "ninguna razon se quedo sin tier");
	}

	// Cobertura de la cascada: las trece razones tienen que estar vistas por ESTE test
	// (los bits se van llenando arriba; si una razon fuera inalcanzable, la linea de
	// testigos lo dice y el verde se declara invalido).
	for (uint32_t i = 0; i < kReasonCount; i++)
	{
		if (i == static_cast<uint32_t>(Reason::kCount))
			continue;
		CK((g_reason_vista & (1u << i)) != 0u, "hay una razon de DecideTier que NINGUN fixture alcanza");
	}
}

// ------------------------------------------------------------------
// (4) Coherencia: si para los datos bien formados, NO para los rotos.
// ------------------------------------------------------------------
static void test_coherencia()
{
	// Controles NEGATIVOS: cada uno es una medida que un recolector podria producir por un
	// copia-pega mal, y que el registro NO tiene derecho a imprimir como si fuera un hecho
	// del telefono. Si Coherent dejara pasar alguno, la linea `coherente=si` del log es
	// decoracion.
	{
		Measurement m = Base();
		m.total_cores = 0u; // flag encendido, valor centinela
		CK(!Coherent(m, DecideTier(m)), "nucleos=0 con nucleos_medido=true: tiene que ruidar");
	}
	{
		Measurement m = Base();
		m.total_ram_mb = 0u;
		CK(!Coherent(m, DecideTier(m)), "ram=0 con ram_medido=true: tiene que ruidar");
	}
	{
		Measurement m = Base();
		m.big_freq_khz = 0u;
		CK(!Coherent(m, DecideTier(m)), "frecuencia=0 con frecuencia_medida=true: tiene que ruidar");
	}
	{
		Measurement m = Base();
		m.big_class = BigClass::kUnknown;
		CK(!Coherent(m, DecideTier(m)), "clase desconocida con clase_medida=true: tiene que ruidar");
	}
	{
		Measurement m = Base();
		m.big_cores = 0u;
		m.total_cores = 0u; // el flag de nucleos se apaga solo: aqui lo que se prueba es
		                   // que una clase real NO puede venir con su flag apagado
		m.big_class_measured = false;
		CK(!Coherent(m, DecideTier(m)), "clase rendimiento con flag apagado: tiene que ruidar");
	}
	{
		// Y la trampa de "prometo alta sin haber medido": kPerfHigh exige los cuatro campos.
		Measurement m = Base();
		m.big_freq_measured = false;
		m.big_freq_khz = 0u;
		CK(!Coherent(m, Reason::kPerfHigh), "alta con frecuencia sin medir: INCOHERENTE");
		m = Base();
		m.ram_measured = false;
		m.total_ram_mb = 0u;
		CK(!Coherent(m, Reason::kPerfHigh), "alta con RAM sin medir: INCOHERENTE");
		m = Base();
		m.big_class = BigClass::kUnknown;
		m.big_class_measured = false;
		CK(!Coherent(m, Reason::kPerfHigh), "alta con clase sin medir: INCOHERENTE");
		m = Base();
		m.cores_measured = false;
		m.total_cores = 0u;
		CK(!Coherent(m, Reason::kPerfHigh), "alta con nucleos sin medir: INCOHERENTE");
	}
	{
		// Cada razon afirma sus propios numeros: kFewCores con 8 nucleos, o kRamTight con
		// 8 GB, son mentiras aunque el flag este bien.
		Measurement m = Base();
		m.cores_measured = false;
		m.total_cores = 0u;
		CK(!Coherent(m, Reason::kFewCores), "pocos-nucleos con nucleos sin leer: INCOHERENTE");
		m = Base();
		CK(!Coherent(m, Reason::kRamTight), "poca-ram con 8 GB: INCOHERENTE");
		m = Base();
		CK(!Coherent(m, Reason::kBigClassWeak), "nucleo-chico con nucleo de rendimiento: INCOHERENTE");
		m = Base();
		m.big_class = BigClass::kWeak;
		CK(!Coherent(m, Reason::kBigFreqUnmeasured), "frecuencia-no-leida con frecuencia leida: INCOHERENTE");
		m = Base();
		m.total_ram_mb = 3584u;
		CK(!Coherent(m, Reason::kFreqOnlyCapable), "solo-frecuencia-alta con menos de 4 GB: INCOHERENTE");
	}
	// MissingFields cuenta exactamente los cuatro campos que deciden, y nada de los dos
	// informativos.
	{
		Measurement m = Base();
		CK_EQ(MissingFields(m), 0u);
		m.avail_ram_measured = false;
		m.avail_ram_mb = 0u;
		CK_EQ(MissingFields(m), 0u, "la RAM disponible es informativa: no cuenta");
		m.clusters = 0u;
		CK_EQ(MissingFields(m), 0u, "el recuento de clusters es informativo: no cuenta");
		m.ram_measured = false;
		m.total_ram_mb = 0u;
		m.big_class_measured = false;
		m.big_class = BigClass::kUnknown;
		CK_EQ(MissingFields(m), 2u, "cuenta RAM y clase, exactamente dos");
	}
	{
		Measurement m;
		m.avail_ram_measured = true;
		m.avail_ram_mb = 100u;
		m.clusters = 3u;
		CK_EQ(MissingFields(m), kMeasuredFields, "nada medido = 4/4");
		CK(DecideTier(m) == Reason::kNoMeasurement, "los informativos no sacan del nada-medido");
	}
}

// ------------------------------------------------------------------
// (4b) El barrido: las invariantes de seguridad sobre TODAS las combinaciones bien
// formadas. Un test a mano cubre los bordes; esto cubre los rincones, y es donde vive la
// promesa "nunca se promete 2x sobre algo que no se midio".
// ------------------------------------------------------------------
static void test_barrido()
{
	static const uint32_t kKhz[] = {0u, 1200000u, 1799999u, 1800000u, 1900000u, 2000000u, 2400000u};
	static const uint32_t kRam[] = {0u, 2048u, 3071u, 3072u, 3584u, 4095u, 4096u, 8192u};
	static const uint32_t kCores[] = {0u, 1u, 3u, 4u, 8u};
	static const uint32_t kBig[] = {0u, 1u, 2u, 4u, 8u};

	uint64_t casos = 0;
	for (uint32_t cls = 0; cls < kBigClassCount; cls++)
	{
		for (uint32_t fi = 0; fi < sizeof(kKhz) / sizeof(kKhz[0]); fi++)
		{
			for (uint32_t ri = 0; ri < sizeof(kRam) / sizeof(kRam[0]); ri++)
			{
				for (uint32_t ci = 0; ci < sizeof(kCores) / sizeof(kCores[0]); ci++)
				{
					for (uint32_t bi = 0; bi < sizeof(kBig) / sizeof(kBig[0]); bi++)
					{
						const BigClass clase = static_cast<BigClass>(cls);
						// Bien formado: cada flag encendido SI y SOLO si su numero existe.
						// Los centinelas con flag encendido son el fallo que se persigue, y
						// ya tienen su test de arriba (no tienen que estar en el barrido).
						const bool clase_ok = (clase != BigClass::kUnknown);
						if (!clase_ok && kKhz[fi] == 0u && kRam[ri] == 0u && kCores[ci] == 0u)
							continue; // nada-medido puro ya esta cubierto a mano

						const uint32_t avail = (bi % 2u) * 1024u;    // informativo
						const uint32_t clusters = 2u + (ci % 3u);    // informativo

						Measurement m;
						m.big_class = clase;
						m.big_class_measured = clase_ok;
						m.big_freq_khz = kKhz[fi];
						m.big_freq_measured = (kKhz[fi] > 0u);
						m.total_ram_mb = kRam[ri];
						m.ram_measured = (kRam[ri] > 0u);
						m.total_cores = static_cast<uint8_t>(kCores[ci]);
						m.cores_measured = (kCores[ci] > 0u);
						m.big_cores = static_cast<uint8_t>(kBig[bi]);
						m.clusters = static_cast<uint8_t>(clusters);
						m.avail_ram_mb = avail;
						m.avail_ram_measured = (avail > 0u);

						const Reason r = DecideTier(m);
						const Tier t = TierOf(r);
						casos++;
						Visto(r);
						g_checks += 8;

						// (a) la salida siempre es una razon terminal, nunca la guarda.
						if (r == Reason::kCount)
						{
							g_fails++;
							std::printf("FALLO: DecideTier devolvio kCount en el barrido\n");
							continue;
						}
						// (b) el registro se puede explicar: ley y datos no se contradicen.
						if (!Coherent(m, r))
						{
							g_fails++;
							std::printf("FALLO: barrido incoherente cls=%u khz=%u ram=%u cores=%u big=%u razon=%s\n",
								cls, kKhz[fi], kRam[ri], kCores[ci], kBig[bi], ReasonName(r));
							continue;
						}
						// (c) NUNCA se promete alta con un campo sin medir. Es la invariante
						// por la que existe este cambio: el perfil tiene que ser una medida.
						if (t == Tier::kHigh && MissingFields(m) != 0u)
						{
							g_fails++;
							std::printf("FALLO: alta con %u campos sin medir (razon=%s)\n",
								MissingFields(m), ReasonName(r));
							continue;
						}
						// (d) el tier es derivado: las cuatro casillas no pueden opinar por
						// su cuenta.
						const bool prot = WantsFastSavestate(r) || WantsUnsyncedReadback(r) ||
							WantsPartialTexturePreload(r);
						if (prot == (t == Tier::kHigh))
						{
							g_fails++;
							std::printf("FALLO: protecciones y tier se contradicen (razon=%s)\n", ReasonName(r));
							continue;
						}
						if (WantsOptimalPacing(r) != (t == Tier::kHigh))
						{
							g_fails++;
							std::printf("FALLO: pacing y tier se contradicen (razon=%s)\n", ReasonName(r));
							continue;
						}
						// (e) los campos informativos no mueven nada: misma razon con otro
						// disponible y con otro recuento de clusters.
						{
							Measurement m2 = m;
							m2.avail_ram_mb = avail + 1u;
							m2.avail_ram_measured = true;
							m2.clusters = static_cast<uint8_t>(clusters + 1u);
							if (DecideTier(m2) != r)
							{
								g_fails++;
								std::printf("FALLO: un campo informativo movio la clasificacion\n");
							}
							g_checks++;
						}
						// (f) monoticidad: subir UN numero MEDIDO no puede bajar el tier. El
						// filtro por los flags es la mitad dura de esta invariante: pasar de
						// "sin leer" a "leido y resulto chico" SI baja el perfil, y eso no es
						// una caida de la ley sino su purpose. Lo que se comprueba aqui es la
						// monotonia pura: dentro del MISMO conjunto de campos leidos, mas
						// numero nunca es peor.
						{
							Measurement up = m;
							bool subio_algo = false;
							if (up.ram_measured && kRam[ri] + 1024u <= 16384u)
							{
								up.total_ram_mb = kRam[ri] + 1024u;
								subio_algo = true;
							}
							if (up.big_freq_measured && kKhz[fi] + 200000u <= 4000000u)
							{
								up.big_freq_khz = kKhz[fi] + 200000u;
								subio_algo = true;
							}
							if (up.cores_measured && kCores[ci] + 2u <= 255u)
							{
								up.total_cores = static_cast<uint8_t>(kCores[ci] + 2u);
								up.big_cores = static_cast<uint8_t>(kBig[bi] + 2u);
								subio_algo = true;
							}
							if (subio_algo)
							{
								const Tier tu = TierOf(DecideTier(up));
								if (static_cast<uint32_t>(tu) < static_cast<uint32_t>(t))
								{
									g_fails++;
									std::printf("FALLO: subir numeros BAJO el tier (%s -> %s)\n",
										TierName(t), TierName(tu));
								}
								g_checks++;
							}
						}
						// (g) la razon es estable: la misma entrada da la misma salida dos
						// veces seguidas (no hay estado oculto en la ley).
						if (DecideTier(m) != r)
						{
							g_fails++;
							std::printf("FALLO: DecideTier no es determinista\n");
						}
						g_checks++;
					}
				}
			}
		}
	}

	// El techo del enum de clases esta donde la ley lo necesita: el barrido tiene que
	// haber probado las cuatro, no tres.
	CK_EQ(kBigClassCount, 4u, "BigClass::kCount vale 4: hay una clase nueva sin cobertura");
	CK(casos > 5000u, "el barrido no recorrio el espacio esperado (4 clases x 7 relojes x "
			"8 RAM x 5 nucleos x 5 grandes)");
	(void)casos;
}

// ------------------------------------------------------------------
// (5) PARIDAD CON LA LEY VIEJA.
//
// Copia fiel de AndroidDeviceDetection.cpp::GetDeviceTier tal como estaba en 0.6.36
// (git show HEAD^:app/src/main/cpp/AndroidDeviceDetection.cpp), con sus dos listas de
// nombres y su IsHighEndSnapdragon por numero de modelo. Se simplifica UNA cosa y se
// dice: la deteccion de VENDEDOR (DetectGPUVendor/IsSnapdragon/IsMediatek) NO se
// reimprime porque no la toco este cambio — entra como un campo del fixture. Lo que se
// compara es la LEY de clasificacion, casilla por casilla, sobre los mismos aparatos.
// ------------------------------------------------------------------
struct OldDevice
{
	const char* nombre;
	bool qualcomm;         // DetectGPUVendor() == Qualcomm (sin cambiar en 0.6.37)
	unsigned soc_id;       // GetQualcommSocModel(): SM7325 -> 7325, 0 = no resuelto
	const char* platform;  // ro.board.platform en minusculas (la cadena que buscaban las listas)
	const std::vector<ClusterView>* clusters; // lo que midio el kernel de ese mismo aparato
	uint32_t ram_mb;
};

static int OldIsHighEnd(unsigned soc)
{
	if (soc == 0u)
		return 0;
	const unsigned series = soc / 1000u;
	const unsigned rest = soc % 1000u;
	if (series >= 8u)
		return 1;
	if (series == 7u && rest >= 300u)
		return 1;
	return 0;
}

// 0 baja, 1 media, 2 alta — igual que el viejo GetDeviceTier.
static int OldTier(const OldDevice& d)
{
	if (d.qualcomm)
		return OldIsHighEnd(d.soc_id) ? 2 : 1;

	const std::string hay = (d.platform != nullptr) ? std::string(d.platform) : std::string();

	// kCapable[] de 0.6.36, copiada literal.
	static const char* const kCapable[] = {
		"mt6873", "mt6875", "mt6877", "mt6883", "mt6885", "mt6889",
		"mt6890", "mt6891", "mt6892", "mt6893", "mt6895", "mt6897",
		"mt6980", "mt6983", "mt6985", "mt6989", "mt6990", "mt6991", "mt6993",
		"mt8183", "mt8192", "mt8195",
		"exynos9810", "exynos9820", "exynos9825", "exynos980", "exynos9815",
		"exynos1380", "exynos2100", "exynos2200", "exynos2400",
	};
	for (const char* p : kCapable)
		if (hay.find(p) != std::string::npos)
			return 1;

	// kWeak[] de 0.6.36, copiada literal — incluida la cadena suelta "helio".
	static const char* const kWeak[] = {
		"mt6739", "mt6761", "mt6762", "mt6763", "mt6765", "mt6768", "mt6771",
		"mt6779", "mt6785", "mt6832", "mt6833", "mt6853", "mt6855",
		"exynos7870", "exynos7880", "exynos7884", "exynos7885", "exynos850",
		"exynos9609", "exynos9610", "exynos9611",
		"kirin710", "kirin620", "kirin650", "helio",
	};
	for (const char* p : kWeak)
		if (hay.find(p) != std::string::npos)
			return 0;

	return 0; // desconocido == gama baja
}

static uint64_t g_igual = 0, g_subio = 0, g_bajo = 0, g_gano_proteccion = 0, g_gano_pacing = 0,
	g_perdio_proteccion = 0, g_cambio_razon_no_medida = 0;

static void test_paridad_vieja()
{
	// Topologias. Cada una es la ficha publica del aparato, y los fixtures hipoteticos
	// (RAM rara, sysfs cerrado) estan NOMBRADOS como hipotesis: lo que se prueba es la
	// LEY sobre esa topologia, no que alguien tenga ese telefono.
	const std::vector<ClusterView> sd778g_arm = {Cl(0x41, 0xd41, 1u, 2400000u),
		Cl(0x41, 0xd41, 3u, 2200000u), Cl(0x41, 0xd05, 4u, 1900000u)};
	const std::vector<ClusterView> sd778g_kryo = {Cl(0x51, 0x806, 1u, 2400000u),
		Cl(0x51, 0x806, 3u, 2200000u), Cl(0x51, 0x805, 4u, 1900000u)};
	const std::vector<ClusterView> sd778g_sin_sysfs = {Cl(0x41, 0xd41, 1u, 0u),
		Cl(0x41, 0xd41, 3u, 0u), Cl(0x41, 0xd05, 4u, 0u)};
	const std::vector<ClusterView> sd665 = {Cl(0x51, 0x800, 4u, 2000000u),
		Cl(0x51, 0x801, 4u, 1800000u)};
	const std::vector<ClusterView> sd665_sin_clase = {Cl(0x51, 0x806, 4u, 2000000u),
		Cl(0x51, 0x807, 4u, 1800000u)};
	const std::vector<ClusterView> sd855 = {Cl(0x41, 0xd0b, 1u, 2840000u),
		Cl(0x41, 0xd0b, 3u, 2420000u), Cl(0x41, 0xd05, 4u, 1800000u)};
	const std::vector<ClusterView> sd8gen1 = {Cl(0x41, 0xd44, 1u, 3000000u),
		Cl(0x41, 0xd47, 3u, 2500000u), Cl(0x41, 0xd46, 4u, 1800000u)};
	const std::vector<ClusterView> g88 = {Cl(0x41, 0xd0a, 2u, 2000000u),
		Cl(0x41, 0xd05, 6u, 1800000u)};
	const std::vector<ClusterView> g99 = {Cl(0x41, 0xd0b, 2u, 2200000u),
		Cl(0x41, 0xd05, 6u, 2000000u)};
	const std::vector<ClusterView> d720 = {Cl(0x41, 0xd0b, 2u, 2000000u),
		Cl(0x41, 0xd05, 6u, 2000000u)};
	const std::vector<ClusterView> a53_only = {Cl(0x41, 0xd03, 4u, 1500000u),
		Cl(0x41, 0xd03, 4u, 1300000u)};
	const std::vector<ClusterView> sin_nada; // el kernel no dijo clusters
	const std::vector<ClusterView> kirin980 = {Cl(0x48, 0xd40, 2u, 2600000u),
		Cl(0x48, 0xd40, 2u, 1920000u), Cl(0x41, 0xd05, 4u, 1800000u)};
	const std::vector<ClusterView> exynos9810 = {Cl(0x53, 0x002, 4u, 2700000u),
		Cl(0x41, 0xd03, 4u, 1700000u)};
	const std::vector<ClusterView> nombre_nuevo = {Cl(0x41, 0xd47, 2u, 2400000u),
		Cl(0x41, 0xd46, 6u, 1800000u)};

	// (No `static`: las filas apuntan a los vectors de arriba, que son locales de esta
	// funcion. Un array estatico con punteros a locales viviria una segunda llamada con
	// memoria colgada, y el arnes no tiene derecho a ese tipo de verde.)
	const OldDevice kDevices[] = {
		// nombre, qualcomm, soc_id, platform, clusters, ram
		{"SM7325 (778G, MIDR ARM)", true, 7325u, "holi", &sd778g_arm, 7349u},
		{"SM7325 (778G, Kryo sin tabla)", true, 7325u, "holi", &sd778g_kryo, 7349u},
		{"SM7325 (778G, sysfs cerrado)", true, 7325u, "holi", &sd778g_sin_sysfs, 7349u},
		{"SD665 (entrada, Kryo 260)", true, 6115u, "bengal", &sd665, 4096u},
		{"SD665 (entrada, sin clase)", true, 6115u, "bengal", &sd665_sin_clase, 4096u},
		{"SD665 (entrada, 3 GB)", true, 6115u, "bengal", &sd665, 3072u},
		{"SM8150 (855) con 3 GB", true, 8150u, "msmnile", &sd855, 3072u},
		{"SM8450 (8 Gen 1)", true, 8450u, "kalama", &sd8gen1, 12288u},
		{"Helio G88 (mt6769)", false, 0u, "mt6769", &g88, 4096u},
		{"Helio G88 con 3 GB", false, 0u, "mt6769", &g88, 3072u},
		{"Helio G99 (mt6789)", false, 0u, "mt6789", &g99, 8192u},
		{"Dimensity 720 (mt6853)", false, 0u, "mt6853", &d720, 8192u},
		{"Dimensity 700 (mt6833)", false, 0u, "mt6833", &g99, 6144u},
		{"Helio de dos nucleos", false, 0u, "mt6739", &a53_only, 2048u},
		{"Kirin 980", false, 0u, "kirin980", &kirin980, 8192u},
		{"Kirin 710 (en kWeak)", false, 0u, "kirin710", &a53_only, 4096u},
		{"Exynos 9810 (en kCapable)", false, 0u, "exynos9810", &exynos9810, 8192u},
		{"A53 puro sin nombre", false, 0u, "rk33", &a53_only, 2048u},
		{"Nombre nuevo sin lista (A710)", false, 0u, "mt6991x", &nombre_nuevo, 12288u},
		{"Sin clusters leidos (hipotesis)", false, 0u, "mt6893", &sin_nada, 8192u},
	};

	for (const OldDevice& d : kDevices)
	{
		const int viejo = OldTier(d);
		const Measurement m = Measure(d.clusters->data(), static_cast<uint32_t>(d.clusters->size()), d.ram_mb);
		const Reason r = DecideTier(m);
		const int nuevo = static_cast<int>(TierOf(r));
		g_checks += 4;

		// La medida se puede explicar siempre, incluso en los aparatos hipoteticos.
		CK(Coherent(m, r));

		if (nuevo == viejo)
			g_igual++;
		else if (nuevo > viejo)
			g_subio++;
		else
			g_bajo++;

		// Y lo que el usuario siente, casilla por casilla, no el numero pelado:
		const bool prot_vieja = (viejo == 0); // GetDeviceTier() == 0
		const bool prot_nueva = WantsFastSavestate(r);
		const bool pace_vieja = (viejo >= 1); // tier >= 1, TODO Qualcomm por apellido
		const bool pace_nueva = WantsOptimalPacing(r);
		if (prot_nueva && !prot_vieja)
			g_gano_proteccion++;
		if (pace_nueva && !pace_vieja)
			g_gano_pacing++;
		if (!prot_nueva && prot_vieja)
			g_perdio_proteccion++;
		if (r == Reason::kNoMeasurement || r == Reason::kCoresUnmeasured ||
			r == Reason::kRamUnmeasured || r == Reason::kBigFreqUnmeasured ||
			r == Reason::kBigClassUnknown || r == Reason::kFreqOnlyCapable)
			g_cambio_razon_no_medida++;

		// La invariante dura del arreglo de entrada, dicha como asercion y no como
		// prosa: NINGUN Qualcomm de entrada (serie 6/7 baja) se queda sin las
		// protecciones por ser Qualcomm, y NINGUNO se lleva la cola optima. Es
		// exactamente el defecto que se ataco, y es la unica linea de este arnes que
		// dice algo del MUNDO REAL: en 0.6.36 un SD665 (tier 1 por apellido) recibia
		// VsyncQueueSize=0 y no recibia ninguna de las tres protecciones.
		if (d.qualcomm && !OldIsHighEnd(d.soc_id))
		{
			CK(pace_vieja, "la ley vieja le ponia la cola optima a CUALQUIER Qualcomm, incluso de entrada");
			CK(!pace_nueva, "la ley nueva se la quita: un Qualcomm de entrada ya no la recibe por apellido");
			if (nuevo < 2)
			{
				CK(WantsFastSavestate(r) && WantsUnsyncedReadback(r) && WantsPartialTexturePreload(r),
					"Qualcomm de entrada con las tres protecciones de gama baja (antes no tenia ninguna)");
			}
		}
	}

	// Las CUATRO bajadas del arreglo, afirmadas por nombre y por razon. No es un contador que
	// se pueda mover en silencio: si manana una de estas sube, o cambia de motivo, o aparece
	// una quinta bajada sin explicar, estas filas lo ven. Son el precio honesto de medir — un
	// aparato al que la ley vieja le daba 2x solo por apellido lo pierde cuando su registro
	// dice que le falta RAM, cuando el sysfs esta cerrado, cuando la tabla no nombro ninguno
	// de sus clusters o cuando el kernel no reporto clusters.
	{
		const Measurement kryo = Measure(sd778g_kryo.data(), 3u, 7349u);
		CK(DecideTier(kryo) == Reason::kFreqOnlyCapable,
			"bajada 1: 778G leyendo Kryo sin tabla -> media, y la razon es solo-frecuencia-alta "
			"(la clase quedo RETIRADA por el cluster no nombrable de 2.4 GHz, así que no es nucleo-desconocido)");
		const Measurement nosys = Measure(sd778g_sin_sysfs.data(), 3u, 7349u);
		CK(DecideTier(nosys) == Reason::kBigFreqUnmeasured,
			"bajada 2: 778G sin cpuinfo_max_freq -> media, y el log tiene que decir frecuencia-no-leida");
		const Measurement s855 = Measure(sd855.data(), 3u, 3072u);
		CK(DecideTier(s855) == Reason::kPerfNotEnough,
			"bajada 3: 855 con 3 GB no es alta por mucho que sea SM8150");
		const Measurement sincl = Measure(sin_nada.data(), 0u, 8192u);
		CK(DecideTier(sincl) == Reason::kCoresUnmeasured,
			"bajada 4: sin clusters leidos -> baja, dicha como nucleos-no-leidos");
		CK(Coherent(sincl, DecideTier(sincl)));

		// Y los tres contadores, clavados. Cambiar el arreglo de aparatos obliga a pasar por
		// aqui y recontar a mano, que es la unica manera de que una bajada nueva no entre
		// como ruido.
		CK_EQ(g_igual, 8u, "aparatos que la medida deja donde la lista");
		CK_EQ(g_subio, 8u, "aparatos que la medida sube");
		CK_EQ(g_bajo, 4u, "aparatos que la medida baja: tienen que ser exactamente los cuatro de arriba");
		CK_EQ(g_igual + g_subio + g_bajo, sizeof(kDevices) / sizeof(kDevices[0]),
			"ningun aparato del arreglo se quedo sin contar");
	}

	// Tres casos concretos, afirmados uno por uno porque son los nombres que el usuario
	// pronuncio: el G88 (la cadena suelta "helio" de kWeak lo bajaba a 0), el G99/Dimensity
	// (ni siquiera estaba en las listas) y el telefono de referencia (que sigue en alta,
	// pero por MERITO medido, no por apellido).
	{
		const std::vector<ClusterView> g88topo = {Cl(0x41, 0xd0a, 2u, 2000000u),
			Cl(0x41, 0xd05, 6u, 1800000u)};
		const OldDevice g88v{"Helio G88 (mt6769)", false, 0u, "mt6769", &g88topo, 4096u};
		const Measurement g88 = Measure(g88topo.data(), 2u, 4096u);
		CK_EQ(OldTier(g88v), 0, "ley vieja: el G88 caia a baja por la cadena suelta 'helio'");
		CK(static_cast<int>(TierOf(DecideTier(g88))) == 1, "G88 medido: media, no la baja del apellido");
		CK(DecideTier(g88) == Reason::kMainstreamCapable);
		CK(!WantsOptimalPacing(DecideTier(g88)), "G88: la cola optima NO le toca (ahi esta la ganancia)");

		const std::vector<ClusterView> g99topo = {Cl(0x41, 0xd0b, 2u, 2200000u),
			Cl(0x41, 0xd05, 6u, 2000000u)};
		const OldDevice g99v{"Helio G99 (mt6789)", false, 0u, "mt6789", &g99topo, 8192u};
		const Measurement mm = Measure(g99topo.data(), 2u, 8192u);
		CK_EQ(OldTier(g99v), 0, "ley vieja: el G99 tampoco estaba en kCapable");
		CK(TierOf(DecideTier(mm)) == Tier::kHigh, "G99 medido con 8 GB: alta por registros");
		CK(WantsOptimalPacing(DecideTier(mm)), "G99: la cola optima se la gana");
		CK(!WantsUnsyncedReadback(DecideTier(mm)), "G99 en alta: sale de la proteccion de readback");

		const std::vector<ClusterView> reftopo = {Cl(0x41, 0xd41, 1u, 2400000u),
			Cl(0x41, 0xd41, 3u, 2200000u), Cl(0x41, 0xd05, 4u, 1900000u)};
		const OldDevice refv{"SM7325 (778G, MIDR ARM)", true, 7325u, "holi", &reftopo, 7349u};
		const Measurement rm = Measure(reftopo.data(), 3u, 7349u, 2858u);
		CK_EQ(OldTier(refv), 2, "la ley vieja tambien lo ponian en 2, pero por SM73xx");
		CK(TierOf(DecideTier(rm)) == Tier::kHigh, "el telefono de referencia sigue en alta, MEDIDO");
		CK(DecideTier(rm) == Reason::kPerfHigh, "y la razon que va a salir en su log es cumple-todo");
	}
}

// ------------------------------------------------------------------
// (6) La impresion de los MIDR CRUDOS.
//
// Esta funcion existe porque "universal" sin numeros no se puede mantener: cuando un
// telefono sale clase=desconocida hay que poder decir CUAL de las tres causas es (el
// kernel no reporto el registro, el registro existe y su part no esta en la tabla, o el
// reloj no se pudo leer). Si la impresion esta mal — un desplazamiento, una mascara, un
// ancho de campo — la tabla se "extiende" contra un numero que ningun hardware produjo.
// ------------------------------------------------------------------
static void test_impresion()
{
	char buf[1024];

	// Cadena exacta, topologia del telefono de referencia. Los numeros son los que el log
	// de un 778G tiene que mostrar: 0x4100d410 es un A78 (part 0xd41 << 4) y 0x4100d050 un
	// A55. Si alguien cambia el desplazamiento de MidrPart, esta fila se cae antes que el
	// registro de un usuario real.
	{
		ClusterView v[3] = {Cl(0x41, 0xd41, 1u, 2400000u), Cl(0x41, 0xd41, 3u, 2200000u),
			Cl(0x41, 0xd05, 4u, 1900000u)};
		const uint32_t n = FormatClusters(v, 3u, buf, sizeof(buf));
		CK_EQ(n, 3u, "los tres clusters caben");
		CK(std::strcmp(buf,
			"#0 midr=0x4100d410 n=1 khz=2400000 clase=rendimiento"
			" | #1 midr=0x4100d410 n=3 khz=2200000 clase=rendimiento"
			" | #2 midr=0x4100d050 n=4 khz=1900000 clase=chica") == 0,
			"la cadena impresa es la ley, no un resumen");
		CK_EQ(std::strlen(buf), 156u, "longitud exacta: un campo de mas o de menos se ve aqui");
	}

	// Un Kryo sin entrada en la tabla: el midr se imprime igual y la clase dice
	// desconocida. Es el caso por el que existe esta funcion.
	{
		ClusterView v[2] = {Cl(0x51, 0x806, 4u, 2400000u), Cl(0x51, 0x805, 4u, 1900000u)};
		const uint32_t n = FormatClusters(v, 2u, buf, sizeof(buf));
		CK_EQ(n, 2u);
		CK(std::strcmp(buf,
			"#0 midr=0x51008060 n=4 khz=2400000 clase=desconocida"
			" | #1 midr=0x51008050 n=4 khz=1900000 clase=chica") == 0,
			"part sin tabla: se imprime el numero para poder extender la tabla con un dato real");
	}

	// midr == 0 y khz == 0: las otras dos causas, separables en el log.
	{
		ClusterView v[1];
		v[0].midr = 0u;
		v[0].core_count = 8u;
		v[0].frequency_khz = 0u;
		const uint32_t n = FormatClusters(v, 1u, buf, sizeof(buf));
		CK_EQ(n, 1u);
		CK(std::strcmp(buf, "#0 midr=0x00000000 n=8 khz=0 clase=desconocida") == 0,
			"registro sin leer y reloj sin leer se imprimen como lo que son, no como ceros");
	}

	// count == 0: cadena vacia, no basura del buffer.
	{
		buf[0] = 'X';
		buf[1] = '\0';
		CK_EQ(FormatClusters(nullptr, 0u, buf, sizeof(buf)), 0u);
		CK_EQ(buf[0], '\0', "con cero clusters la cadena tiene que quedar vacia");
	}

	// El cap: con buffer justo para dos clusters, se imprimen DOS y se declara el recorte.
	// Lo que no puede pasar es que quede un tercer cluster a medias (un numero truncado en
	// el registro es un numero inventado) ni que se pierdan los que si cabian.
	{
		ClusterView v[3] = {Cl(0x41, 0xd41, 1u, 2400000u), Cl(0x41, 0xd41, 3u, 2200000u),
			Cl(0x41, 0xd05, 4u, 1900000u)};
		char chico[100];
		const uint32_t n = FormatClusters(v, 3u, chico, sizeof(chico));
		CK_EQ(n, 1u, "en 100 bytes cabe el primer cluster y no el segundo (52 + ' | ' + 52)");
		CK(std::strcmp(chico, "#0 midr=0x4100d410 n=1 khz=2400000 clase=rendimiento") == 0,
			"lo que entro es el cluster completo");
		CK_EQ(std::strlen(chico), 52u, "el buffer termina en el ultimo cluster COMPLETO");
		// La regla del llamador: escrito < usados => recortado, y eso se imprime.
		CK(n < 3u, "aqui es donde el recolector tiene que poner recortado=si");
	}

	// Ajuste EXACTO: 52 del primer cluster + 3 del separador + 52 del segundo = 107, o sea
	// el segundo cluster pide exactamente los 55 bytes que quedan. snprintf puede escribir
	// 54 y cerrar, asi que NO cabe: ahi es donde `>=` y `>` difieren, y donde un `>` deja en
	// el registro un MIDR partido por la mitad. Este es el unico caso del arnes que distingue
	// las dos comparaciones, y por eso esta escrito a mano y no deducido del tamano.
	{
		ClusterView v[2] = {Cl(0x41, 0xd41, 1u, 2400000u), Cl(0x41, 0xd41, 1u, 2400000u)};
		char exacto[108];
		const uint32_t n = FormatClusters(v, 2u, exacto, 107u);
		CK_EQ(n, 1u, "hueco justo para el separador mas el cluster: no entra, se declara recorte");
		CK_EQ(std::strlen(exacto), 52u, "con hueco justo la cadena para en el primer cluster");
		// Y con UN byte mas si cabe: 108 = 52 + 55 + NUL.
		CK_EQ(FormatClusters(v, 2u, exacto, 108u), 2u, "108 bytes: los dos clusters caben justos");
	}

	// Cap de un byte: no se escribe nada aparte del NUL, y devuelve 0.
	{
		ClusterView v[1] = {Cl(0x41, 0xd41, 1u, 2400000u)};
		char uno[2];
		uno[0] = 'Z';
		uno[1] = 'Z';
		CK_EQ(FormatClusters(v, 1u, uno, 1u), 0u);
		CK_EQ(uno[0], '\0', "sin espacio: cadena vacia terminada, no el byte que estaba");
		CK_EQ(FormatClusters(v, 1u, nullptr, sizeof(uno)), 0u, "buffer nulo no revienta");
		CK_EQ(FormatClusters(v, 1u, uno, 0u), 0u, "cap 0 no escribe");
	}

	// Que el tamano del buffer del recolector (kMaxClusters * 64) alcance para la topologia
	// maxima que la ley mira. Si kMaxClusters sube sin subir el buffer, el recorte es
	// silencioso para el usuario pero no para esta fila.
	{
		std::vector<ClusterView> v(kMaxClusters);
		for (uint32_t i = 0; i < kMaxClusters; i++)
			v[i] = Cl(0x41, 0xd41, 99u, 4000000u); // lo mas largo que puede salir por cluster
		std::vector<char> big(kMaxClusters * 64u);
		const uint32_t n = FormatClusters(v.data(), kMaxClusters, big.data(), big.size());
		CK_EQ(n, kMaxClusters, "el buffer del recolector tiene que dar para kMaxClusters clusters");
		CK(std::strlen(big.data()) + 1u <= big.size());
	}
}

// ------------------------------------------------------------------
// (7) Las cuatro proyecciones: derivadas del MISMO tier, sin opinion propia.
//
// Son las cuatro casillas que native-lib.cpp escribe. Si alguna deja de derivar del tier
// (un `||`, un `&&`, una razon agregada a mano), el registro puede decir "alta" y aplicar
// una proteccion al mismo tiempo, que es el tipo de incoherencia que este proyecto ya
// persiguio en la sub-cadena `== 0`.
// ------------------------------------------------------------------
static void test_proyecciones()
{
	static const Reason todas[] = {
		Reason::kNoMeasurement,   Reason::kCoresUnmeasured, Reason::kFewCores,
		Reason::kRamUnmeasured,   Reason::kRamTight,         Reason::kBigClassUnknown,
		Reason::kFreqOnlyCapable, Reason::kBigClassWeak,     Reason::kBigFreqWeak,
		Reason::kBigFreqUnmeasured, Reason::kMainstreamCapable, Reason::kPerfNotEnough,
		Reason::kPerfHigh,
	};
	CK_EQ(sizeof(todas) / sizeof(todas[0]), kReasonCount,
		"el arreglo de proyecciones no cubre todas las razones: falta una casilla nueva");

	for (const Reason r : todas)
	{
		const Tier t = TierOf(r);
		const bool prot = WantsFastSavestate(r) && WantsUnsyncedReadback(r) &&
			WantsPartialTexturePreload(r);
		const bool pace = WantsOptimalPacing(r);

		// Las tres bajan exactamente cuando no hay alta; la cola sube exactamente cuando la
		// hay. Una de las dos invertidas y el telefono de entrada se queda sin proteccion.
		CK_EQ(prot, t != Tier::kHigh, "las tres protecciones y el tier");
		CK_EQ(pace, t == Tier::kHigh, "la cola optima y el tier");
		CK(!(prot && pace), "ningun perfil se protege y acelera a la vez");
		Visto(r);

		// Casilla por casilla: ninguna de las tres puede divergir de las otras dos, porque
		// en 0.6.36 las tres colgaban del mismo `== 0` y aqui siguen colgando del mismo tier.
		CK_EQ(WantsFastSavestate(r), WantsUnsyncedReadback(r), "dos protecciones divergen entre si");
		CK_EQ(WantsUnsyncedReadback(r), WantsPartialTexturePreload(r), "otras dos divergen entre si");
	}

	// Los tres casos con nombre propio, porque son los que el usuario puede leer en el log.
	CK(!WantsOptimalPacing(Reason::kMainstreamCapable), "G88 (nucleo-medio): sin cola optima");
	CK(WantsFastSavestate(Reason::kMainstreamCapable), "G88: con las tres protecciones");
	CK(WantsOptimalPacing(Reason::kPerfHigh), "cumple-todo: cola optima");
	CK(!WantsPartialTexturePreload(Reason::kPerfHigh), "cumple-todo: pre-carga completa");
	CK(WantsUnsyncedReadback(Reason::kFreqOnlyCapable),
		"nucleo-desconocido con 2 GHz: proteccion de readback ENCENDIDA (es la media honesta)");
	// Un valor fuera del enum (el llamador roto) tiene que salir protegido, no acelerado.
	const Reason roto = static_cast<Reason>(200u);
	CK(WantsFastSavestate(roto) && WantsUnsyncedReadback(roto) && WantsPartialTexturePreload(roto),
		"razon fuera del enum sin protecciones: se promete 2x a un llamador roto");
	CK(!WantsOptimalPacing(roto), "razon fuera del enum con cola optima");
}

int main()
{
	test_tabla_midr();
	test_agregado();
	test_cascada();
	test_coherencia();
	test_barrido();
	test_paridad_vieja();
	test_impresion();
	test_proyecciones();

	// Autovalerse. Dos cosas, y las dos tienen que ser rojas si no se cumplen:
	//   * el recuento de comprobaciones (un arnes que no recorrio la cascada ni el barrido
	//     no puede declararse verde), y
	//   * los testigos de la paridad: si la comparacion contra la ley vieja no encontro ni
	//     una subida ni una ganancia de proteccion, lo que se probo fue una igualdad
	//     declarada, no la mejora que esta version promete.
	const bool cubierto = (g_reason_vista == ((1u << kReasonCount) - 1u));
	if (g_fails == 0 && !cubierto)
	{
		std::printf("FALLO: hay razones de DecideTier que NINGUN fixture alcanzo (bits=0x%x)\n",
			g_reason_vista);
		g_fails++;
	}
	if (g_fails == 0 && g_checks < 50000)
	{
		std::printf("FALLO: el arnes hizo %d comprobaciones — debajo del recuento que sale de "
			"la tabla (~90) + la cascada y la coherencia (~200) + el barrido (9 invariantes x "
			"~5575 casos) + la paridad (4 x 20 aparatos). Verde declarativo, no verificado.\n",
			g_checks);
		g_fails++;
	}
	if (g_fails == 0 && (g_subio == 0u || g_gano_proteccion == 0u))
	{
		std::printf("FALLO: la paridad con la ley vieja no mostro ninguna subida "
			"(subio=%llu gano-proteccion=%llu). La mejora NO se verifico, solo se dijo.\n",
			(unsigned long long)g_subio, (unsigned long long)g_gano_proteccion);
		g_fails++;
	}

	// Los testigos, siempre impresos (el baseline exige la linea): son lo que la revision
	// puede releer sin abrir el archivo. `bajo` no tiene que ser cero — medir SI baja cosas
	// (un 855 con 3 GB ya no es 2x) — y por eso cada bajada esta nombrada arriba, una por una.
	std::printf("testigos: igual=%llu subio=%llu bajo=%llu gano-proteccion=%llu gano-pacing=%llu "
		"perdio-proteccion=%llu razon-sin-medir=%llu\n",
		(unsigned long long)g_igual, (unsigned long long)g_subio, (unsigned long long)g_bajo,
		(unsigned long long)g_gano_proteccion, (unsigned long long)g_gano_pacing,
		(unsigned long long)g_perdio_proteccion, (unsigned long long)g_cambio_razon_no_medida);
	std::printf("H15 (CenitDeviceClass.h): %d Comprobaciones, %d Fallos\n", g_checks, g_fails);
	return g_fails ? 1 : 0;
}

