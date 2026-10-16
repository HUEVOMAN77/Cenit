// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// CenitDeviceClass.h — clasificacion MEDIDA del equipo, sin lista de nombres.
//
// C++ puro y sin dependencias a proposito, por la misma razon que MvuSbLife.h (h9),
// MvuSbIndex.h (h11), MvuFastPath.h (h12), MvuSbEligibility.h (h14) y
// CenitPresentStats.h (h13): es la unica forma de que la LEY se pueda compilar y
// romper con g++ de host. El recolector (AndroidDeviceDetection.cpp) necesita
// <sys/system_properties.h> y cpuinfo del NDK, o sea el CI; lo que se decide aqui no
// necesita nada de eso — ni siquiera los numeros de MIDR, que son registros
// arquitecturales de ARM definidos por numero, no por nombre.
//
// ---------------------------------------------------------------------------
// Que esta mal hoy, medido en el codigo que reemplaza
// (AndroidDeviceDetection.cpp::GetDeviceTier, vigente desde 0.6.14):
//
//   if (vendor == GPUVendor::Qualcomm)
//           return IsHighEndSnapdragon() ? 2 : 1;
//
// Todo Qualcomm sale >= 1, tambien un Snapdragon de entrada: la marca decide antes de
// mirar cualquier numero del chip. Y tier >= 1 fuerza `VsyncQueueSize = 0` en
// native-lib.cpp, cuyo propio comentario dice por que eso NO va en gama baja:
//   «en gama baja quitar los dos cuadros de amortiguacion convierte cualquier pico del
//    GPU en el EE durmiendo, y la velocidad de emulacion cae».
// O sea: la proteccion que existe para los telefonos de entrada se les aplica a los
// Snapdragon de entrada por ser Snapdragon, sin medir nada. Ese es el defecto que
// esta version ataca, y es de native-lib, no del juego.
//
// Para el resto del mundo hay DOS listas de nombres escritos a mano:
//   kCapable[] = mt6873, mt6875, ... exynos2400      (19 MediaTek + 10 Exynos + tablets)
//   kWeak[]    = mt6739 ... kirin710, "helio"
// Un SoC que no este en la lista cae en el `return 0` final: desconocido == gama baja.
// Eso deja un Helio G99 (2x Cortex-A76 a 2.2 GHz + 6x A55 a 2.0 GHz, segun la ficha
// oficial de MediaTek) en el perfil mas conservador NO porque nadie midiera el chip sino
// porque su nombre no esta en la lista. Y la cadena suelta "helio" en kWeak arrastra a
// TODA la serie G por el apellido, incluyendo las que llevan A76/A78. Ademas tres
// codenames de MediaTek con nucleos de clase de rendimiento estan listados como debiles,
// con sus fichas publicas enfrente: mt6833 (Dimensity 700, 2x Cortex-A76 a 2.2 GHz),
// mt6853 (Dimensity 720, 2x Cortex-A76 a 2.0 GHz) y mt6855 (2x Cortex-A78 a 2.2 GHz, la
// lista de SoCs de MediaTek). Ningun telefono con un nombre nuevo — Snapdragon 6 Gen 4,
// Dimensity 6300, cualquier kirin o tensor — puede clasificarse sin editar estas listas,
// que es exactamente "no es universal".
//
// La consecuencia se puede confrontar con el log del telefono de referencia (SM7325,
// 8 nucleos, 3 clusters, 7349 MB fisicos): hoy es 2 POR APELLIDO, y su partida de
// God of War volco «PerfLog session: 14138 frames in 298.8s wall = 47.32 fps average»
// con la GPU entre 54 % y 98 % y el regidor dictando GPU_BOUND («GPU 95.0% >= 85%:
// resolution can help»). Con `VsyncQueueSize=0` fijado por tier>=1, eso es justo el
// cuadro que el comentario de native-lib describe: el EE esperando al GS en cada
// pico del GPU. Lo que NO puedo afirmar desde ese log: cuanto se perdio por la cola,
// porque la partida no corrio con las dos configuraciones. Lo que si puedo afirmar es
// que la decision no se tomo sobre ningun numero del chip.
// ---------------------------------------------------------------------------
//
// De donde salen los numeros con los que se decide. No son heuristica inventada: son
// las frecuencias que el kernel expone por sysfs (leidas aqui por cpuinfo) y la clase
// del nucleo que el MIDR reporta. Las fichas publicas de los equipos que el usuario
// nombra, para poder poner los cortes ENTRE generaciones que existen:
//
//   Helio G85 / G88 : 2x Cortex-A75 a 2.00 GHz + 6x Cortex-A55 a 1.80 GHz, octa-core.
//   Helio G99       : 2x Cortex-A76 a 2.20 GHz + 6x Cortex-A55 a 2.00 GHz.
//   Dimensity 700   : 2x Cortex-A76 a 2.20 GHz + 6x Cortex-A55 a 2.00 GHz.
//   Snapdragon 778G : 1x A78 a 2.40 GHz + 3x A78 a 2.20 GHz + 4x A55 a 1.90 GHz.
//   Snapdragon 665  : 4x Kryo 260 Gold (Cortex-A73) a 2.00 GHz + 4x Silver (A53).
//
//  * 1.80 GHz — piso de frecuencia aprovechable. Es el reloj del cluster chico de un
//    G88/G85: por debajo de ahi el "nucleo grande" que reporta el kernel no es un
//    nucleo de rendimiento.
//  * 2.00 GHz — techo de la clase media y piso de la alta. Los A75 de G85/G88 y los
//    Kryo 260/385 del 6xx llegan justo a 2.0 y los A76 de G99/Dimensity 700 corren a
//    2.2. Poner el corte en 2.0 NO basta para separarlos (ambos cumplen), y por eso la
//    CLASE del nucleo se mira antes que la frecuencia: el numero solo no distingue un
//    A75 de un A76, que es el error que una lista de nombres comete al reves.
//  * 3072 MB / 4096 MB — dos cortes, no uno: por debajo de 3 GB el equipo no da para
//    readback sincronizado ni colas largas (gama baja), y para prometer 2x de escala
//    se exige ademas llegar a 4 GB, porque la GPU del telefono comparte la RAM del
//    sistema. Este proyecto solo tiene evidencia directa funcionando por encima de
//    esos numeros (el telefono de referencia reporta 7349 MB), asi que lo de debajo es
//    conservador declarado, no medido.
//  * 2 nucleos grandes / 4 nucleos totales — un solo nucleo de rendimiento no sostiene
//    EE + VU0 + VU1 + GS, y con menos de 4 nucleos no hay donde poner los hilos.
//
// Ningun umbral depende del nombre del fabricante, del modelo ni del apellido
// comercial. Lo unico que se parece a una lista es la tabla MIDR->clase de abajo, y es
// una tabla de NUMEROS DE REGISTRO definidos por ARM (implementer + part), no de
// cadenas: un SoC nuevo con un A78 trae el part 0xd41 y se clasifica solo.
//
// ---------------------------------------------------------------------------
// Como esta escrita la ley, y por que asi:
//
//  * UNA sola funcion devuelve la RAZON (DecideTier). El tier es DERIVADO de la razon
//    (TierOf), igual que ShouldAsk/AskCounted sale de DecideAsk en MvuFastPath.h y
//    DecideArm/DecideJunction/DecideClose de MvuSbEligibility.h: dos copias de una ley
//    siempre acaban diciendo cosas distintas, y aqui una de las dos se imprimiria en el
//    log mientras la otra aplica los ajustes del telefono.
//  * cada razon mapea a EXACTAMENTE un tier, y h15 lo comprueba casilla por casilla.
//  * cada razon tiene nombre propio, asi que el registro puede decir "me bajo a gama
//    baja porque el nucleo grande es clase chica", no solo "0".
//  * lo que NO se midio tiene casilla propia. Es la leccion de las tres ultimas
//    versiones: un numero sin denominador no es evidencia, y "el kernel no me lo dijo"
//    nunca puede disfrazarse de "este telefono es de gama baja".
// ---------------------------------------------------------------------------

#pragma once

#include <cstddef> // size_t, la firma de FormatClusters
#include <cstdint>
#include <cstdio>  // snprintf de FormatClusters: nada de <fmt>, host tiene que poder leerlo

namespace CenitDeviceClass
{
	// ------------------------------------------------------------------
	// Umbrales. Numeros, no adjetivos: se cambian con una medida al lado.
	// ------------------------------------------------------------------

	// Frecuencia maxima de los nucleos grandes, en kHz. La unidad es la del archivo que la
	// dice: /sys/devices/system/cpu/cpuN/cpufreq/cpuinfo_max_freq ya esta en kHz, y el
	// recolector la lee de ahi en vez de confiar en cpuinfo_cluster::frequency, que en este
	// arbol vendorizado queda SIN LLENAR en la ruta de Linux/Android (lo comprueba el
	// recolector con el literal de su propio archivo, y la diferencia entre 2.4 GHz y "0"
	// es justo la que DecideTier no puede inventar).
	static const uint32_t kFreqWeakKhz = 1800000u; // por debajo: no es un nucleo de rendimiento
	static const uint32_t kFreqHighKhz = 2000000u; // piso para prometer 2x (los A76 corren a 2.2)

	// RAM fisica total, en MB. Dos cortes, no uno:
	static const uint32_t kRamTightMb = 3072u;   // por debajo: perfil de entrada, sin mas
	static const uint32_t kRamFloorHighMb = 4096u; // y para 2x hace falta ademas llegar a 4 GB

	// Nucleos.
	//
	// Que cuenta `big_cores`, dicho exactamente porque de eso depende un telefono real:
	// son los nucleos de TODOS los clusters que quedaron en la CLASE maxima vista, no los
	// del cluster mas rapido. El Snapdragon 778G del telefono de referencia tiene TRES
	// clusters y el mas rapido (1x Cortex-A78 a 2.40 GHz) tiene UN nucleo: los otros tres
	// A78 estan en el cluster de 2.20 GHz. Si big_cores fuera "los del cluster mas
	// rapido", el 778G daria 1 y se caeria de gama alta por una particion del cluster
	// que al silicio le da exactamente igual. Con la definicion de clase maxima da 4
	// (1+3), que es lo que el chip realmente tiene de A78.
	//
	// `big_freq_khz` es el reloj MAXIMO del telefono (ver Aggregate: se agrega sobre todos
	// los clusters, no solo sobre los de la clase ganadora, porque la casilla
	// "solo-frecuencia-alta" tiene que seguir siendo alcanzable cuando la clase no se
	// conoce).
	static const uint8_t kBigCoresFloorHigh = 2u; // un solo nucleo grande no sostiene EE+VU+GS
	static const uint8_t kTotalCoresFloor = 4u;   // menos de 4 no hay donde poner los hilos

	// ------------------------------------------------------------------
	// Clases del nucleo grande. `kUnknown` es un estado propio: NO se confunde con
	// "debil" en el codigo, tiene su razon y su casilla.
	// ------------------------------------------------------------------
	enum class BigClass : uint8_t
	{
		kUnknown = 0, // el kernel no reporto MIDR, o el part no esta en la tabla de abajo
		kWeak,        // A32/A35/A53/A55/A57/A510/A520, Krait/Kryo "Silver": in-order o chico
		kMainstream,  // A65/A72/A73/A75 y sus clones Kryo "Gold": OOO de primera generacion
		kPerformance, // A76/A77/A78/A7x/A7xx, X1..X4, Neoverse, Oryon, Exynos M: sostiene 2x
		kCount
	};

	static const uint32_t kBigClassCount = static_cast<uint32_t>(BigClass::kCount);

	// ------------------------------------------------------------------
	// MIDR -> clase. MIDR = Machine ID Register: un registro arquitectural con el
	// `implementer` en los bits [31:24] y el `part` en [11:4]. No es una propiedad
	// comercial ni una cadena: es un numero que el kernel devuelve por
	// /sys/devices/system/cpu/cpuN/midr, y cpuinfo lo expone como
	// cpuinfo_cluster::midr.
	//
	// Unica fuente de los numeros de abajo: la tabla de partes del cpuinfo vendorizado
	// del proyecto (app/src/main/cpp/3rdparty/cpuinfo/src/arm/uarch.c). Cada case esta
	// copiado de ahi con el nombre de microarquitectura que cpuinfo le asigna; lo que
	// se agrega aqui es la CLASE (chica / media / rendimiento), no un numero nuevo ni un
	// recuerdo de fichas comerciales. Las lineas marcadas "32 bits" son partes que
	// uarch.c solo reconoce bajo CPUINFO_ARCH_ARM; en un telefono arm64 de hoy no
	// aparecen, y se listan para que la cobertura de la tabla sea visible, no para
	// adivinar.
	//
	// Que NO hace esta tabla: adivinar. Los nucleos de servidor que uarch.c si conoce
	// (Cavium ThunderX/T2, Ampere Xgene, Broadcom Brahma, Marvell Falkor/Saphira y PJ4,
	// Nvidia Denver, Huawei Taishan/Kunpeng) quedan en kUnknown a proposito — no son un
	// telefono — y lo mismo cualquier part que uarch.c no tenga, incluido Neoverse E1 y
	// cualquier Cortex o Lumex posterior al arbol vendorizado aqui: uarch.c hace exactamente
	// lo mismo con su
	// `cpuinfo_log_warning("unknown ... CPU part ignored")`. Apple esta deliberadamente
	// fuera: sus parts (0x022..0x04x) son nucleos muy potentes, pero no hay manera
	// soportada de correr este port en iOS, y una clase inventada para un chip que este
	// port nunca corre es exactamente el tipo de numero que este proyecto prometio no
	// volver a escribir.
	//
	// Consecuencia de la que hay que estar de acuerdo: un nucleo nuevo y bueno que todavia
	// no esta en la tabla de cpuinfo se clasifica kUnknown y NO puede llegar a alta por si
	// solo; le queda la casilla "solo-frecuencia-alta" (media) si su reloj y su RAM
	// alcanzan. Es conservador declarado, no un bug: preferimos no prometer 2x a prometerlo
	// por un numero que nadie verifico.
	// ------------------------------------------------------------------
	inline BigClass ClassOfMidr(uint32_t implementer, uint32_t part)
	{
		switch (implementer)
		{
			case 0x41: // ARM Ltd — los Cortex puros: es lo que reportan MediaTek Helio/
					   // Dimensity, Kirin con nucleos ARM, Exynos con nucleos ARM y los
					   // "big" que Qualcomm licencia.
				switch (part)
				{
					// ---- clase chica: in-order o nucleo de eficiencia ----
					case 0xd01: // Cortex-A32            (uarch.c: cortex_a32, arm64)
					case 0xd03: // Cortex-A53            (cortex_a53)
					case 0xd04: // Cortex-A35            (cortex_a35)
					case 0xd05: // Cortex-A55 / A55r0    (cortex_a55 o a55r0 por variant)
					case 0xd07: // Cortex-A57            (cortex_a57)
					case 0xd46: // Cortex-A510           (cortex_a510)
					case 0xd80: // Cortex-A520           (cortex_a520)
					case 0xd8f: // Cortex-A320           (cortex_a320)
						return BigClass::kWeak;

					// ---- clase media: OOO de primera/segunda generacion ----
					case 0xd08: // Cortex-A72            (cortex_a72)
					case 0xd09: // Cortex-A73            (cortex_a73)
					case 0xd0a: // Cortex-A75            (cortex_a75)
					case 0xd06: // Cortex-A65            (cortex_a65)
						return BigClass::kMainstream;

					// ---- clase de rendimiento: sostiene 2x de entrada ----
					case 0xd0b: // Cortex-A76            (cortex_a76)
					case 0xd0c: // Neoverse N1           (neoverse_n1)
					case 0xd0d: // Cortex-A77            (cortex_a77)
					case 0xd0e: // Cortex-A76AE          (cortex_a76)
					case 0xd40: // Neoverse V1           (neoverse_v1)
					case 0xd41: // Cortex-A78            (cortex_a78)
					case 0xd4b: // Cortex-A78C           (uarch.c lo mapea a cortex_a78)
					case 0xd44: // Cortex-X1             (cortex_x1)
					case 0xd47: // Cortex-A710           (cortex_a710)
					case 0xd48: // Cortex-X2             (cortex_x2)
					case 0xd49: // Neoverse N2           (neoverse_n2)
					case 0xd4d: // Cortex-A715           (cortex_a715)
					case 0xd4e: // Cortex-X3             (cortex_x3)
					case 0xd4f: // Neoverse V2           (neoverse_v2)
					case 0xd81: // Cortex-A720           (cortex_a720)
					case 0xd82: // Cortex-X4             (cortex_x4)
					case 0xd85: // Cortex-X925           (cortex_x925)
					case 0xd87: // Cortex-A725           (cortex_a725)
						return BigClass::kPerformance;

					// ---- familia C1 (Lumex), 2025: los nombres los pone uarch.c, la
					// ---- categoria la pone ARM en sus fichas publicas. C1-Nano se
					// vende como el nucleo de eficiencia (sucesor del A520) y C1-Pro/
					// Premium/Ultra como los big/flagship (C1-Ultra sucediendo al X925,
					// C1-Pro al A725). Se listan los cuatro para que la cobertura sea
					// visible: un D9400/9500 que traiga estos parts se clasifica solo,
					// sin editar ninguna lista de nombres.
					case 0xd8a: // Lumex C1-Nano         (lumex_c1_nano, eficiencia)
						return BigClass::kWeak;
					case 0xd8b: // Lumex C1-Pro          (lumex_c1_pro, big)
					case 0xd8c: // Lumex C1-Ultra        (lumex_c1_ultra, flagship)
					case 0xd90: // Lumex C1-Premium      (lumex_c1_premium, big)
						return BigClass::kPerformance;

					// ---- 32 bits (CPUINFO_ARCH_ARM en uarch.c): no aplica en arm64 ----
					case 0xc05: // Cortex-A5             (cortex_a5)
					case 0xc07: // Cortex-A7             (cortex_a7)
					case 0xc08: // Cortex-A8             (cortex_a8)
					case 0xc09: // Cortex-A9             (cortex_a9)
					case 0xc0c: // Cortex-A12            (cortex_a12)
					case 0xc0d: // Cortex-A12 (Rockchip RK3288)
					case 0xc0e: // Cortex-A17            (cortex_a17)
					case 0xc0f: // Cortex-A15            (cortex_a15)
						return BigClass::kWeak;

					default:
						return BigClass::kUnknown;
				}

			case 0x51: // Qualcomm — los Kryo "Gold/Silver" son clones licenciados:
					   // uarch.c los re-mapea al Cortex de origen (por eso cambian
					   // *vendor a cpuinfo_vendor_arm dentro del switch). Aqui se usa EL
					   // MISMO origen para la clase, sin nombres comerciales y sin
					   // apellido: eso es lo que permite clasificar un Snapdragon 6 o 7
					   // por lo que es, no por como se llama.
				switch (part)
				{
					case 0x800: // Kryo 260/280 "Gold" -> Cortex-A73
						return BigClass::kMainstream;
					case 0x802: // Kryo 385 "Gold"     -> Cortex-A75
						return BigClass::kMainstream;
					case 0x804: // Kryo 485 "Gold"/"Gold Prime" -> Cortex-A76
						return BigClass::kPerformance;
					case 0x801: // Kryo 260/280 "Silver" -> Cortex-A53
					case 0x803: // Kryo 385 "Silver"     -> Cortex-A55r0
					case 0x805: // Kryo 485 "Silver"     -> Cortex-A55
						return BigClass::kWeak;
					case 0x001: // Oryon                (8 Gen 3 / 8 Elite)
					case 0x002: // Oryon V3             (8 Elite Gen 5)
						return BigClass::kPerformance;
					// Kryo de la epoca de 820/821 (0x201/0x205/0x211) y Krait/Scorpion
					// (0x04d/0x06f/0x02d/0x00f) quedan kUnknown a proposito: uarch.c los
					// agrupa como kryo/krait/scorpion sin decir el nucleo de origen, y
					// 0x205 es un nucleo de rendimiento de 2016 cuyo nombre no alcanza
					// para prometer 2x en 2026.
					default:
						return BigClass::kUnknown;
				}

			case 0x53: // Samsung — los nucleos M de Exynos, que hoy estan en la lista de
					   // nombres por MODELO (exynos9810/9820/990/2400...). uarch.c los
					   // distingue por (variant, part) juntos — M1 = 0x00100010, M2 =
					   // 0x00400010, M3 = 0x00100020, M4 = 0x00100030, M5 = 0x00100040 —
					   // y aqui basta el `part`, porque los cinco caen en la MISMA clase:
					   // M1 y M2 comparten part 0x001 y se separan solo por variant, y M3,
					   // M4 y M5 son 0x002, 0x003 y 0x004. El unico caso que esta tabla
					   // afirma de mas es un part 0x001..0x004 con una variant que cpuinfo
					   // no reconoce: uarch.c lo dejaria con un warning y aqui sale
					   // rendimiento. Se acepta porque no existe en el silicio que se
					   // vendo (los Exynos con nucleos ARM traen implementer 0x41, no 0x53),
					   // y porque si algun dia aparece, la linea `Cenit equipo: midr` del
					   // registro deja el numero crudo para auditarlo.
				switch (part)
				{
					case 0x001: // M1 (8890) o M2 (8895), segun variant
					case 0x002: // M3 (9810)
					case 0x003: // M4 (9820)
					case 0x004: // M5 (9820, cluster intermedio)
						return BigClass::kPerformance;
					default:
						return BigClass::kUnknown;
				}

			case 0x48: // Huawei — uarch.c mapea explicitamente el part 0xD40 de Huawei
					   // ("Kirin 980 Big/Medium cores -> Cortex-A76") y lo devuelve como
					   // microarquitectura de ARM. Es el unico nucleo de telefono de este
					   // implementer que cpuinfo resuelve; el 0xD01 es Taishan v110
					   // (Kunpeng, servidor) y aqui queda kUnknown.
				switch (part)
				{
					case 0xd40: // Kirin 980 big/medium -> Cortex-A76 (literal de uarch.c)
						return BigClass::kPerformance;
					default:
						return BigClass::kUnknown;
				}

			default:
				return BigClass::kUnknown;
		}
	}

	// ------------------------------------------------------------------
	// Lo que el sistema midio. Un struct plano: el recolector lo llena, la ley lo
	// lee, y h15 lo construye desde fixtures. Los ceros significan "no medido" y NUNCA
	// "cero de verdad" (la leccion del «system has 0 byte lines» de 0.6.36: un
	// centinela impreso como si fuera una caracteristica del telefono).
	// ------------------------------------------------------------------
	struct Measurement
	{
		// Cada campo trae su "no medido" escrito: un recolector que olvida llenar uno no
		// puede dejar ahi basura de stack, y la diferencia entre 0 y "no medido" es
		// justamente la que este proyecto ya imprimio mal una vez («system has 0 byte
		// lines»). Los flags son los que mandan; los numeros se leen solo si su flag dice
		// que existen.
		uint32_t big_freq_khz = 0;   // maximo entre los clusters de la clase maxima vista
		uint32_t total_ram_mb = 0;   // RAM fisica total
		uint32_t avail_ram_mb = 0;   // RAM disponible al arranque (informativa, no decide)
		uint8_t big_cores = 0;       // nucleos de los clusters de la clase maxima vista
		uint8_t total_cores = 0;     // nucleos totales
		uint8_t clusters = 0;        // clusters distintos vistos (informativo, no decide)
		BigClass big_class = BigClass::kUnknown; // clase del nucleo grande, de ClassOfMidr
		bool big_class_measured = false; // false si el kernel no reporto MIDR o el part no esta cubierto
		bool big_freq_measured = false;  // false si cpuinfo_max_freq no era legible o valia 0
		bool ram_measured = false;       // false si no se pudo leer la RAM
		bool cores_measured = false;     // false si cpuinfo no conto nucleos
		// La RAM disponible al arranque es INFORMATIVA: no entra en kMeasuredFields ni en
		// ninguna rama, pero tiene su flag porque si no su 0 se imprimira como
		// "disponible = 0 MB", que es el centinela-vuelto-dato que ya paso una vez.
		bool avail_ram_measured = false; // false si /proc/meminfo no dijo MemAvailable
	};

	// ------------------------------------------------------------------
	// Razones terminales. Cada una es un hecho medible, no una opinion, y la linea del
	// registro imprime la que salio: "me bajo a gama baja porque el nucleo grande es de
	// clase chica", no un `0` pelado. La enumeracion no esta en el orden exacto de la
	// cascada (kBigClassUnknown y kFreqOnlyCapable se deciden juntos en el paso 4), asi
	// que lo que h15 comprueba no es el orden sino la cobertura: cada camino de
	// DecideTier devuelve UNA de estas trece y cada una tiene al menos un fixture que la
	// produce.
	// ------------------------------------------------------------------
	enum class Reason : uint8_t
	{
		kNoMeasurement = 0, // nada medible: perfil conservador, dicho como "no medido"
		kCoresUnmeasured,   // cpuinfo no retorno nucleos: el 0 de total_cores no es un dato
		kFewCores,          // menos de 4 nucleos: no hay donde poner EE/VU/GS
		kRamUnmeasured,     // no se leyo la RAM: no se puede prometer 2x
		kRamTight,          // RAM fisica por debajo de 3 GB
		kBigClassUnknown,   // sin microarquitectura y sin frecuencia alta: no se adivina
		kFreqOnlyCapable,   // frecuencia >= 2 GHz y RAM >= 4 GB, pero sin clase medida
		kBigClassWeak,      // nucleo grande de clase chica (A32/A35/A53/A55/A520)
		kBigFreqWeak,       // frecuencia medida por debajo de 1.8 GHz
		kBigFreqUnmeasured, // clase conocida, frecuencia ilegible: no se sube a alta
		kMainstreamCapable, // A72/A73/A75 con frecuencia aprovechable: gama media
		kPerfNotEnough,     // nucleo de rendimiento al que le falta reloj, par o 4 GB
		kPerfHigh,          // cumple todo: clase, 2 nucleos, 2 GHz, 4 GB
		kCount
	};

	static const uint32_t kReasonCount = static_cast<uint32_t>(Reason::kCount);

	enum class Tier : uint8_t
	{
		kLow = 0,  // 1x de entrada, governor y CAS cargan con el resto
		kMid = 1,  // 1.5x de entrada
		kHigh = 2, // 2x de entrada
		kCount
	};

	// El tier es DERIVADO: una sola tabla, sin `if` duplicados en el recolector.
	inline Tier TierOf(Reason r)
	{
		switch (r)
		{
			case Reason::kPerfHigh:
				return Tier::kHigh;
			case Reason::kMainstreamCapable:
			case Reason::kPerfNotEnough:
			case Reason::kBigFreqUnmeasured:
			case Reason::kFreqOnlyCapable:
				return Tier::kMid;
			case Reason::kNoMeasurement:
			case Reason::kCoresUnmeasured:
			case Reason::kFewCores:
			case Reason::kRamUnmeasured:
			case Reason::kRamTight:
			case Reason::kBigClassUnknown:
			case Reason::kBigClassWeak:
			case Reason::kBigFreqWeak:
				return Tier::kLow;
			case Reason::kCount:
				break;
		}
		// Un valor fuera del enum es un llamador roto: cae a lo conservador, y h15
		// comprueba que NINGUNA razon valida llegue aqui.
		return Tier::kLow;
	}

	// ------------------------------------------------------------------
	// La cascada. Cortocircuita en el primer hecho que impide prometer algo y
	// devuelve SIEMPRE una razon terminal: no existe la salida "sin casilla".
	// ------------------------------------------------------------------
	inline Reason DecideTier(const Measurement& m)
	{
		// 1. Sin ninguna medida no hay perfil que prometer. Antes "desconocido"
		// significaba "la lista de nombres no te conocia"; aqui significa "el kernel no
		// me dijo nada" y se imprime con esas palabras.
		if (!m.big_class_measured && !m.big_freq_measured && !m.ram_measured && !m.cores_measured)
			return Reason::kNoMeasurement;

		// 2a. Que cpuinfo no haya devuelto NINGUN nucleo es una falla del lector, no un
		// telefono de dos nucleos. Sin esta casilla, el `total_cores` de un Measurement
		// sin llenar (0) se leia como "pocos nucleos" y el registro culpaba al hardware de
		// lo que era una medida que no llego.
		if (!m.cores_measured)
			return Reason::kCoresUnmeasured;

		// 2b. Nucleos: es independiente de la clase — un quad-core A78 no pone EE + VU1 +
		// GS + MTGS mejor que un octa-core A73, y un dual-core no los pone ninguno.
		if (m.total_cores < kTotalCoresFloor)
			return Reason::kFewCores;

		// 3. RAM: escalar a 2x es memoria de GPU, y la GPU del telefono comparte la RAM
		// del sistema. Sin medirla no se promete; por debajo de 3 GB tampoco.
		if (!m.ram_measured)
			return Reason::kRamUnmeasured;
		if (m.total_ram_mb < kRamTightMb)
			return Reason::kRamTight;

		// 4. La clase del nucleo manda sobre la frecuencia: un A55 a 2.0 GHz es un nucleo
		// de eficiencia, no de rendimiento, y la frecuencia sola lo confundiria (es el
		// error que la lista de nombres comete al reves, metiendo mt6853 en kWeak).
		if (!m.big_class_measured)
		{
			// Frecuencia alta + RAM suficiente alcanzan para media, y no pasa de ahi
			// porque no sabemos que nucleo es: la clase no se inventa.
			if (m.big_freq_measured && m.big_freq_khz >= kFreqHighKhz &&
				m.total_ram_mb >= kRamFloorHighMb)
				return Reason::kFreqOnlyCapable;
			return Reason::kBigClassUnknown;
		}

		if (m.big_class == BigClass::kWeak)
			return Reason::kBigClassWeak;

		// 5. Frecuencia.
		if (!m.big_freq_measured)
			return Reason::kBigFreqUnmeasured;
		if (m.big_freq_khz < kFreqWeakKhz)
			return Reason::kBigFreqWeak;

		// 6. Con clase, frecuencia y RAM medidas, solo falta el paquete.
		if (m.big_class == BigClass::kMainstream)
			return Reason::kMainstreamCapable;

		// kPerformance: para 2x hace falta el par de nucleos grandes, el reloj de la casa
		// Y los 4 GB. La condicion de RAM existe para que esta linea diga lo que su
		// comentario promete: sin ella, un A78 con 3 GB de RAM fisica se llevaban el 2x
		// por la puerta de atras, porque el corte de arriba (kRamTightMb) solo baja a
		// gama baja por debajo de 3 GB. Los tres recortes son comprobables por separado:
		// 3 GB < RAM < 4 GB cae aqui, big_cores == 1 cae aqui, freq == 1.9 GHz cae aqui.
		//
		// El `== BigClass::kPerformance` explicito es intencional: un valor de enum que no
		// es ninguna de las cuatro clases (un llamador roto) no se gana el 2x por ser el
		// ultimo case de un switch.
		if (m.big_class != BigClass::kPerformance || m.big_cores < kBigCoresFloorHigh ||
			m.big_freq_khz < kFreqHighKhz || m.total_ram_mb < kRamFloorHighMb)
		{
			return Reason::kPerfNotEnough;
		}

		return Reason::kPerfHigh;
	}

	// ------------------------------------------------------------------
	// Proyecciones de ajuste. Son TRES casillas sobre el MISMO tier derivado, y cada una
	// existe para que native-lib.cpp pueda dejar de escribir `GetDeviceTier() == 0` y
	// `tier >= 1` sueltos: si la condicion vive aqui, h15 la puede romper con una
	// comprobacion, y el registro y el ajuste no pueden decir cosas distintas.
	//
	// La traducci6n de semantica esta dicha en terms del codigo que reemplaza, no de una
	// opinion:
	//   * En 0.6.36 el SET PROTECTOR (nivel de guardado barato + readback no sincronizado
	//     + pre-carga parcial) colgaba de `GetDeviceTier() == 0`, o sea "baja o
	//     desconocido". Con nombres, un Dimensity nuevo caia en desconocido y se
	//     protegia; con medida, lo que hay que proteger es lo que NO demostro ser alto,
	//     que es la misma frase con un denominador distinto. Por eso las tres valen
	//     `tier != alta`.
	//   * La cola optima (VsyncQueueSize = 0) colgaba de `tier >= 1`, es decir de TODO
	//     Qualcomm por apellido. Es la unica de las cuatro que NO se traduce a `!= alta`
	//     sino a `== alta`, porque su propio comentario en native-lib.cpp explica por que
	//     es al reves en gama baja: «quitar los dos cuadros de amortiguacion convierte
	//     cualquier pico del GPU en el EE durmiendo, y la velocidad de emulacion cae».
	//
	// Ninguna de las cuatro depende ya del nombre del fabricante. Ninguna agrega una
	// condicion de RAM que el codigo anterior no tuviera: la RAM entra por DecideTier, no
	// por aqui, y esa separacion es la que hace comprobable cada casilla.
	// ------------------------------------------------------------------

	// Nivel de compresion de guardado mas barato (zstd nivel 0): escribir un estado no
	// debe costar cuadros. Cambia el tamano del archivo, no la fiabilidad.
	inline bool WantsFastSavestate(Reason r)
	{
		return TierOf(r) != Tier::kHigh;
	}

	// Readback no sincronizado (HWDownloadMode=3): la medida conservadora para el ancho
	// de banda. Antes la decidia `GetDeviceTier() == 0`, que en CUALQUIER Qualcomm nunca
	// ocurria — o sea un Snapdragon 6 de entrada se quedaba sin ella solo por ser
	// Snapdragon.
	inline bool WantsUnsyncedReadback(Reason r)
	{
		return TierOf(r) != Tier::kHigh;
	}

	// Pre-carga de texturas parcial: "Full" (el default del core) sube a la GPU toda
	// textura que el juego toca, incluidas las que nunca se ven; en un telefono eso son
	// cientos de MB de VRAM compartida y micro-cortes cuando el cacheador purga.
	inline bool WantsPartialTexturePreload(Reason r)
	{
		return TierOf(r) != Tier::kHigh;
	}

	// Cola de cuadro optima (VsyncQueueSize=0): el EE espera al GS CADA cuadro. Es lo
	// mejor cuando sobra cabeza y lo peor cuando el GPU es el cuello de botella — que es
	// literalmente lo que dice el comentario de native-lib.cpp, y tambien lo que el log
	// del telefono de referencia reporta (GPU 95 % y «resolution can help»). En 0.6.36 se
	// aplicaba con `tier >= 1`, o sea para todo Qualcomm por apellido. Aqui se la gana
	// quien cumple clase, par de nucleos, reloj y RAM.
	inline bool WantsOptimalPacing(Reason r)
	{
		return TierOf(r) == Tier::kHigh;
	}

	// ------------------------------------------------------------------
	// Nombres para el registro. Una sola tabla, la misma que clasifica: el numero que
	// lee un usuario y el que agrega el HUD no pueden divergir.
	// ------------------------------------------------------------------
	inline const char* ReasonName(Reason r)
	{
		switch (r)
		{
			case Reason::kNoMeasurement: return "nada-medido";
			case Reason::kCoresUnmeasured: return "nucleos-no-leidos";
			case Reason::kFewCores: return "pocos-nucleos";
			case Reason::kRamUnmeasured: return "ram-no-leida";
			case Reason::kRamTight: return "poca-ram";
			case Reason::kBigClassUnknown: return "nucleo-desconocido";
			case Reason::kFreqOnlyCapable: return "solo-frecuencia-alta";
			case Reason::kBigClassWeak: return "nucleo-chico";
			case Reason::kBigFreqWeak: return "frecuencia-baja";
			case Reason::kBigFreqUnmeasured: return "frecuencia-no-leida";
			case Reason::kMainstreamCapable: return "nucleo-medio";
			case Reason::kPerfNotEnough: return "grande-pero-corto";
			case Reason::kPerfHigh: return "cumple-todo";
			case Reason::kCount:
				break;
		}
		return "?";
	}

	inline const char* BigClassName(BigClass c)
	{
		switch (c)
		{
			case BigClass::kUnknown: return "desconocida";
			case BigClass::kWeak: return "chica";
			case BigClass::kMainstream: return "media";
			case BigClass::kPerformance: return "rendimiento";
			case BigClass::kCount:
				break;
		}
		return "?";
	}

	inline const char* TierName(Tier t)
	{
		switch (t)
		{
			case Tier::kLow: return "baja";
			case Tier::kMid: return "media";
			case Tier::kHigh: return "alta";
			case Tier::kCount:
				break;
		}
		return "?";
	}

	// ------------------------------------------------------------------
	// Agregar CLUSTERS, no nombres. El recolector (AndroidDeviceDetection.cpp) tiene
	// DELANTE una lista de clusters que cpuinfo ya agrupo por MIDR, y lo que decide aqui
	// es como se convierten esos clusters en los numeros del Measurement. Se escribe en
	// el header — y no dentro del recolector — por la misma razon que el resto de la ley:
	// es la parte con las trampas, y la trampa de esta version es precisamente el cluster
	// mas rapido del 778G, que tiene UN nucleo (1x A78 a 2.40) mientras los otros tres A78
	// viven en el cluster de 2.20. Un "maximo del cluster mas rapido" escrito a mano en el
	// recolector daria big_cores = 1 y el telefono de referencia perderia la gama alta por
	// como el OEM particiono los clusters, no por lo que es el chip. host si puede romper
	// ese agregado; romper la lectura de /sys, no.
	//
	// Entrada: lo que cpuinfo sabe de cada cluster, dicho por numero y con su flag de
	// "esto lo lei". `frequency_khz` vale 0 porque en este arbol vendorizado
	// cpuinfo_cluster::frequency queda SIN llenar en Linux/Android (el literal del
	// constructor en 3rdparty/cpuinfo/src/arm/linux/init.c no lista .frequency, y los unicos
	// `cluster->frequency =` del proyecto estan en la ruta de Windows): el recolector tiene
	// que leer /sys/.../cpu<N>/cpufreq/cpuinfo_max_freq del procesador mas rapido del
	// cluster, o cpuinfo nunca va a decir la frecuencia. Eso se fija con un ancla de
	// mutacion, porque es un detalle del arbol, no algo que se pueda deducir del header.
	// ------------------------------------------------------------------
	// Techo de clusters que el agregado mira. Un telefono real tiene 2, 3 o (los X
	// Series de Samsung) 4; 16 es holgado, y el recolector imprime "agregados/reales"
	// para que pasar este techo sea visible en el log en lugar de cambiar el perfil en
	// silencio.
	static const uint32_t kMaxClusters = 16u;

	// Los dos campos del MIDR que importan, extraidos con las mascaras de
	// 3rdparty/cpuinfo/src/arm/midr.h (IMPLEMENTER 0xFF000000, PART 0x0000FFF0, desplazado
	// 4). Viven aqui porque el header es el que decide que es "la clase maxima", y esa
	// decision depende de como se lee el registro: si el desplazamiento se escribe mal, un
	// A78 (part 0xd41) se ve como otro numero y el telefono baja de perfil sin que nadie
	// haya medido mal nada.
	inline uint32_t MidrImplementer(uint32_t midr) { return (midr >> 24) & 0xffu; }
	inline uint32_t MidrPart(uint32_t midr) { return (midr >> 4) & 0xfffu; }

	struct ClusterView
	{
		uint32_t midr = 0;         // cpuinfo_cluster::midr (0 = el kernel no lo reporto)
		uint32_t frequency_khz = 0; // max cpuinfo_max_freq entre sus procesadores (kHz)
		uint32_t core_count = 0;   // cpuinfo_cluster::core_count
	};

	struct ClusterAggregate
	{
		BigClass big_class = BigClass::kUnknown;
		uint32_t big_freq_khz = 0;
		uint32_t big_cores = 0;
		uint32_t total_cores = 0;
		uint32_t clusters = 0;
		bool class_measured = false;
		bool freq_measured = false;
		bool cores_measured = false;
	};

	// El orden de clases es el de la cascada (chica < media < rendimiento): "la clase
	// maxima vista" es la mas alta que cualquier cluster del telefono demostro ser, y los
	// nucleos y el reloj se agregan SOLO sobre los clusters de esa clase. Un cluster
	// desconocido nunca sube el techo; como mucho deja la clase sin medir, que es la que
	// despues baja al telefono a la casilla "nucleo-desconocido".
	inline BigClass StrongerOf(BigClass a, BigClass b)
	{
		return static_cast<uint8_t>(b) > static_cast<uint8_t>(a) ? b : a;
	}

	// Un cluster se clasifica por su MIDR. midr == 0 significa "el kernel no lo reporto",
	// y se devuelve kUnknown sin descomponer el registro: implementar 0 con part 0 no es
	// ningun nucleo conocido, y disfrazarlo de lectura valida seria exactamente el error
	// del centinela impreso como dato.
	inline BigClass ClassOfCluster(const ClusterView& c)
	{
		if (c.midr == 0u)
			return BigClass::kUnknown;
		return ClassOfMidr(MidrImplementer(c.midr), MidrPart(c.midr));
	}

	inline ClusterAggregate Aggregate(const ClusterView* views, uint32_t count)
	{
		ClusterAggregate out;

		// 1. Totales y clase maxima. `total_cores` suma los clusters con core_count > 0;
		// un cluster con 0 no es un cluster, es una lectura que fallo, y contarlo daria
		// nucleos inventados.
		//
		// A la vez se anotan los dos techos de reloj que hacen falta para saber si la clase
		// que encontro la tabla es RECLAMABLE: `freq_known` es el reloj mas alto entre los
		// clusters que la tabla si supo nombrar, y `freq_unknown` el mas alto entre los que
		// no. La razon es una trampa con nombre: el 778G puede reportar sus Kryo 670 como
		// implementer Qualcomm con un part que esta arbol vendorizado no conoce (el switch
		// 'Q' de uarch.c llega hasta 0x805) y su cluster chico como 0x805 -> Cortex-A55, que
		// si esta en la tabla. Ahi la "clase maxima vista" seria kWeak — la del cluster
		// CHICO, porque es el unico nombrable — y el telefono se leeria "nucleo-chico,
		// gama baja" cuando lo que no se pudo leer es justo el nucleo grande. Afirmar
		// `chica` en ese caso es inventar: el cluster mas rapido del telefono es uno que no
		// reconocimos, asi que la unica respuesta honesta es `desconocida` (y su casilla
		// propia, nucleo-desconocido o solo-frecuencia-alta segun el reloj y la RAM).
		uint32_t total = 0;
		uint32_t seen = 0;
		BigClass best = BigClass::kUnknown;
		uint32_t freq_known = 0;
		uint32_t freq_unknown = 0;

		for (uint32_t i = 0; i < count && i < kMaxClusters; i++)
		{
			const ClusterView& c = views[i];
			if (c.core_count == 0u)
				continue;
			total += c.core_count;
			seen++;

			const BigClass cls = ClassOfCluster(c);
			if (cls == BigClass::kUnknown)
			{
				if (c.frequency_khz > freq_unknown)
					freq_unknown = c.frequency_khz;
				continue;
			}
			if (c.frequency_khz > freq_known)
				freq_known = c.frequency_khz;
			best = StrongerOf(best, cls);
		}

		// Retirada de la afirmacion: si algun cluster NO nombrable corre al mismo reloj o
		// mas que el mejor cluster nombrado, no se puede afirmar cual es el nucleo grande.
		// Es conservador en la direccion correcta: el costo de retirar es quedarse en
		// "no medido" (y decirlo), el costo de no retirar es prometer o negar 2x sobre una
		// clase que la tabla no vio. `freq_unknown == 0` (kernel sin cpuinfo_max_freq
		// legible) NO retira: sin reloj no hay evidencia de que el desconocido sea mas
		// rapido, y retirar por falta de lectura seria otro centinela-vuelto-dato.
		if (freq_unknown > 0u && freq_unknown >= freq_known)
			best = BigClass::kUnknown;

		out.total_cores = (total > 255u) ? 255u : static_cast<uint8_t>(total);
		// `seen` no necesita tope: el bucle lo corta en kMaxClusters (16), que entra holgado
		// en un uint8_t. Poner un `> 255` aqui seria codigo muerto con forma de proteccion, y
		// el arnes comprueba la invariante real (clusters <= kMaxClusters) en su lugar.
		out.clusters = static_cast<uint8_t>(seen);
		out.cores_measured = (total > 0u);
		out.big_class = best;
		out.class_measured = (best != BigClass::kUnknown);

		// 2. El reloj del telefono es el MAXIMO entre TODOS los clusters, incluidos los no
		// nombrables, y cuantos nucleos hay de la clase reclamada.
		//
		// Las dos asimetrias de esta pasada son la parte de esta funcion que mas facil se
		// escribe mal:
		//  * el RELOJ se agrega sobre todos los clusters, porque "nucleo desconocido pero a
		//    2.6 GHz" tiene que poder llegar a kFreqOnlyCapable; si se filtrara por una
		//    clase que no existe, esa casilla seria inalcanzable y el telefono caeria en
		//    nucleo-desconocido con 2.6 GHz medidos, que es mas falso.
		//  * los NUCLEOS GRANDES NO: sin clase no se sabe cuales son los grandes, y sumar
		//    los ocho daria "grandes=8" como si fuera una medida. Queda 0 y el registro lo
		//    muestra como clase=NO, nuc=0/8.
		uint32_t freq = 0;
		uint32_t cores = 0;
		for (uint32_t i = 0; i < count && i < kMaxClusters; i++)
		{
			const ClusterView& c = views[i];
			if (c.core_count == 0u)
				continue;

			if (c.frequency_khz > freq)
				freq = c.frequency_khz;

			if (!out.class_measured)
				continue;

			if (ClassOfCluster(c) != best)
				continue;

			cores += c.core_count;
		}

		out.big_freq_khz = freq;
		out.big_cores = (cores > 255u) ? 255u : static_cast<uint8_t>(cores);
		out.freq_measured = (freq > 0u);
		return out;
	}

	// ------------------------------------------------------------------
	// Los MIDR CRUDOS, impresos. Esta funcion existe por la promesa de "universal":
	// cuando un telefono sale clase=desconocida hay que poder decir CUAL de las dos
	// causas es — el kernel no reporto el registro, o el registro existe y su part no
	// esta en la tabla — y la unica manera de saberlo es ver el numero. Con el numero
	// en el log, la tabla se extiende con un dato leido en un telefono real; sin el,
	// extenderla vuelve a ser lo que este proyecto ya hizo: copiar fichas comerciales
	// de memoria.
	//
	// Formato por cluster, separados por " | ":
	//   `#i midr=0x%08x n=%u khz=%u clase=%s`
	// con `khz=0` cuando el sysfs no dijo nada (otras dos causas distintas que el log
	// tiene que poder separar: part desconocido vs registro sin leer vs reloj sin leer).
	//
	// Devuelve CUANTOS clusters caben en `out`. El llamador imprime "caben X de Y"
	// cuando X < count: un registro recortado tiene que decir que esta recortado, que
	// es la misma regla que hizo imprimir `sin-medir=N/4`.
	//
	// Vive en el header, y no en el recolector, porque el desplazamiento y la mascara
	// del MIDR son parte de la ley: si alguien los escribe mal, un A78 (part 0xd41) se
	// imprime como otro numero y la tabla se "extiende" contra un dato inventado. host
	// puede romper esta impresion casilla por casilla; romper el fopen de /sys no.
	// ------------------------------------------------------------------
	inline uint32_t FormatClusters(const ClusterView* views, uint32_t count, char* out, size_t cap)
	{
		if (out == nullptr || cap == 0u)
			return 0u;

		out[0] = '\0';
		size_t used = 0;
		uint32_t written = 0;

		for (uint32_t i = 0; i < count; i++)
		{
			// Con un solo cluster que no quepa separa el recorte: no se imprime a medias.
			if (used + 1u >= cap)
				break;

			const ClusterView& c = views[i];
			const int n = snprintf(out + used, cap - used, "%s#%u midr=0x%08x n=%u khz=%u clase=%s",
				(written == 0u) ? "" : " | ", i, c.midr, c.core_count, c.frequency_khz,
				BigClassName(ClassOfCluster(c)));

			if (n <= 0)
				break;

			const size_t need = static_cast<size_t>(n);
			if (need >= cap - used)
			{
				// snprintf ya escribio el trozo que cabia y lo termino en NUL: se retrocede el
				// buffer al ultimo cluster COMPLETO. Un `midr=0x410fd4` a medias en el registro
				// seria un numero inventado, que es justo lo que esta linea existe para evitar.
				out[used] = '\0';
				break;
			}

			used += need;
			written++;
		}

		return written;
	}

	// ------------------------------------------------------------------
	// Coherencia del registro. Cada campo que la cascada usa esta en uno de dos estados,
	// y el numero de campos SIN medir tiene que ser visible: un telefono cuyo kernel no
	// expone cpuinfo_max_freq tiene que decirlo, porque si no su perfil conservador
	// parece una opinion sobre el hardware cuando es una limitacion del lector.
	//
	// Son CUATRO los campos que deciden (nucleos, RAM, clase, frecuencia). avail_ram y
	// clusters son informativos: no entran en el conteo porque ninguna rama de DecideTier
	// los mira, y contarlos daria "campos sin medir" que no explican nada.
	// ------------------------------------------------------------------
	static const uint32_t kMeasuredFields = 4u; // nucleos, RAM, clase, frecuencia

	inline uint32_t MissingFields(const Measurement& m)
	{
		return (m.cores_measured ? 0u : 1u) + (m.ram_measured ? 0u : 1u) +
			(m.big_class_measured ? 0u : 1u) + (m.big_freq_measured ? 0u : 1u);
	}

	// Lo que cada razon AFIRMA, comprobado contra los campos que esa razon nombra. Es mas
	// exigente que "al menos un campo sin medir": kFewCores tiene que traer nucleos
	// medidos y un numero por debajo del piso, y kPerfHigh tiene que traer los cuatro
	// campos. Si DecideTier y los datos se contradicen, el perfil del telefono no se
	// puede explicar, que es exactamente el fallo que esta version viene persiguiendo.
	inline bool Coherent(const Measurement& m, Reason r)
	{
		// El flag de clase y el valor tienen que decir lo mismo: big_class_measured es, por
		// definicion, "la tabla le dio una clase real al MIDR". Un recolector que ponga el
		// flag en true con big_class == kUnknown esta mintiendo en el registro, y al reves
		// tambien: una clase real no puede venir con el flag apagado.
		if (m.big_class_measured != (m.big_class != BigClass::kUnknown))
			return false;

		// Mismo criterio para los nucleos: total_cores == 0 con el flag encendido es el
		// centinela que ya engano una vez, y > 0 con el flag apagado es un numero que el
		// recolector leyo pero nego en su propia linea.
		if (m.cores_measured != (m.total_cores > 0u))
			return false;

		// Y para la RAM: la cascada solo compara total_ram_mb cuando ram_measured.
		if (m.ram_measured != (m.total_ram_mb > 0u))
			return false;

		// Y para la frecuencia: no existe "frecuencia medida igual a 0".
		if (m.big_freq_measured != (m.big_freq_khz > 0u))
			return false;

		switch (r)
		{
			case Reason::kNoMeasurement:
				return MissingFields(m) == kMeasuredFields;
			case Reason::kCoresUnmeasured:
				return !m.cores_measured;
			case Reason::kFewCores:
				return m.cores_measured && m.total_cores < kTotalCoresFloor;
			case Reason::kRamUnmeasured:
				return m.cores_measured && !m.ram_measured;
			case Reason::kRamTight:
				return m.ram_measured && m.total_ram_mb < kRamTightMb;
			case Reason::kBigClassUnknown:
				return m.ram_measured && !m.big_class_measured;
			case Reason::kFreqOnlyCapable:
				return m.ram_measured && m.total_ram_mb >= kRamFloorHighMb &&
					!m.big_class_measured && m.big_freq_measured &&
					m.big_freq_khz >= kFreqHighKhz;
			case Reason::kBigClassWeak:
				return m.ram_measured && m.big_class_measured && m.big_class == BigClass::kWeak;
			case Reason::kBigFreqWeak:
				return m.big_freq_measured && m.big_freq_khz < kFreqWeakKhz;
			case Reason::kBigFreqUnmeasured:
				return m.big_class_measured && !m.big_freq_measured;
			case Reason::kMainstreamCapable:
				return m.big_class_measured && m.big_freq_measured && m.ram_measured;
			case Reason::kPerfNotEnough:
			case Reason::kPerfHigh:
				return MissingFields(m) == 0u;
			case Reason::kCount:
				break;
		}
		return false;
	}
}
