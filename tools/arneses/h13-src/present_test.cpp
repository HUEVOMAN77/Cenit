// Cenit 0.6.36 — arnés H13 (host) de CenitPresentStats.h
//
// Qué verifica, en una frase: que TODOS los cuadros presentados caen en UNA sola
// casilla y que la suma de las casillas cierra contra los cuadros contados.
//
// Por qué hace falta un arnés para cuatro contadores
// --------------------------------------------------
// El encabezador existe porque el log real de un teléfono (God of War, 0.6.34)
// prueba que hubo cuadros por debajo de 1x —el regidor sostiene 0.5x y el núcleo
// avisa «below native»— y NO puede probar si la reconstrucción EASU los cubrió,
// los dejó estirados o falló al intentarlos. Un contador que nace para responder
// eso en el próximo log tiene que estar correcto antes de salir, porque su única
// auditoría posterior es un renglón que alguien lee una vez.
//
// Lo que se rompe aquí no es el formato del log: es la SEMÁNTICA de la
// clasificación. Cada mutación de tools/arneses/mutacion_h13.sh cambia una
// decisión de casilla (nativo ↔ apagado ↔ hecho ↔ fallado) o se lleva un
// incremento, y el verde solo vale si el arnés lo nombra.
//
// Lo que NO verifica (dicho para que nadie lea de más)
// ---------------------------------------------------
//  - Que el motor llame a Tally() en el momento correcto ni con los hechos
//    correctos: eso vive en GSRenderer.cpp, un TU que el host no puede compilar
//    (tira de Vulkan/ImGui/GSState). Eso lo fija la fase 2 de mutacion_h13.sh
//    contra el TEXTO del archivo, igual que la fase 2 de h12 con microVU.
//  - Que EASUChain haga algo bueno: eso es imagen en pantalla y solo lo dice el
//    teléfono.
#include "CenitPresentStats.h"

#include <cstdio>
#include <cstring>
#include <vector>

static int g_fallos = 0;
static int g_checks = 0;

// Mismo formato que h12: "FALLO: " al principio de la linea. El ejecutor de
// mutacion busca ^FALLO:, y un rojo que no se puede grepuear no cuenta.
#define CHECK(COND, MSG)                                    \
	do                                                      \
	{                                                       \
		g_checks++;                                         \
		if (!(COND))                                        \
		{                                                   \
			g_fallos++;                                     \
			printf("FALLO: ");                              \
			printf("%s\n", MSG);                            \
		}                                                   \
	} while (0)

using namespace cenit_present;

// ---------------------------------------------------------------------------
// 1. La clasificación es exhaustiva: las tres entradas binarias cubren las 8
//    combinaciones y cada una tiene CASILLA. Ninguna combinación puede quedar
//    sin clasificar, porque una casilla faltante es un cuadro que no aparece en
//    ningún número del cierre.
// ---------------------------------------------------------------------------
static void testExhaustivo()
{
	printf("Cenit 0.6.36 h13: clasificacion exhaustiva de cuadros presentados\n");

	// (sub, gate, ran) -> casilla esperada, escrita A MANO desde la ley, no
	// derivada del código: si se deriva del código, el arnés aprueba lo que el
	// código hace en vez de lo que debe hacer.
	struct Fila
	{
		bool sub, gate, ran;
		Reason quiere;
		const char* ley;
	};
	const Fila filas[] = {
		// No es sub-nativo: da igual el portón y la cadena, el cuadro no necesitaba
		// reconstrucción. Las cuatro combinaciones son "nativo" a propósito: esta
		// casilla NO puede tragarse un sub-nativo.
		{false, false, false, kNativo, "no sub-nativo, porton cerrado"},
		{false, false, true, kNativo, "no sub-nativo, porton cerrado, cadena cierta"},
		{false, true, false, kNativo, "no sub-nativo, porton abierto, cadena falsa"},
		{false, true, true, kNativo, "no sub-nativo, porton abierto, cadena cierta"},
		// Sub-nativo con el portón cerrado: la opcion esta apagada o el driver no
		// soporta. Es estirado, y es DISTINGUIBLE de un fallo.
		{true, false, false, kSubApagado, "sub-nativo con porton cerrado"},
		// La cuarta combinación con gate=false (sub, !gate, ran=true) es
		// físicamente imposible —nadie corre la cadena con el portón cerrado—, pero
		// sigue teniendo casilla: la clasificación no depende de que el llamante
		// sea ordenado.
		{true, false, true, kSubApagado, "sub-nativo con porton cerrado (entrada imposible)"},
		// Sub-nativo, portón abierto, la cadena devolvió false: EL CASO QUE EL LOG
		// DE 0.6.34 NO PODIA VER. Estirado con la opcion encendida, sin aviso.
		{true, true, false, kSubFallado, "sub-nativo pedido y fallado"},
		{true, true, true, kSubHecho, "sub-nativo reconstruido"},
	};
	const int n = (int)(sizeof(filas) / sizeof(filas[0]));
	for (int i = 0; i < n; i++)
	{
		const Reason r = Classify(filas[i].sub, filas[i].gate, filas[i].ran);
		CHECK(r == filas[i].quiere, filas[i].ley);
		if (r != filas[i].quiere)
			printf("    dio %s, esperaba %s\n", ReasonName(r), ReasonName(filas[i].quiere));
	}

	// Las ocho filas tienen que producir las cuatro casillas, ninguna mas, ninguna
	// menos: si una casilla queda infalsificable, su contador del teléfono no
	// significa nada.
	int vistas[kReasonCount] = {};
	for (int i = 0; i < n; i++)
		vistas[(int)Classify(filas[i].sub, filas[i].gate, filas[i].ran)]++;
	for (unsigned c = 0; c < kReasonCount; c++)
		CHECK(vistas[c] > 0, "cada casilla es alcanzable por alguna entrada");

	// Nombre para toda casilla: una casilla sin nombre no se puede imprimir.
	CHECK(std::strcmp(ReasonName(kNativo), "nativo") == 0, "nombre de nativo");
	CHECK(std::strcmp(ReasonName(kSubApagado), "sub-apagado") == 0, "nombre de sub-apagado");
	CHECK(std::strcmp(ReasonName(kSubHecho), "sub-hecho") == 0, "nombre de sub-hecho");
	CHECK(std::strcmp(ReasonName(kSubFallado), "sub-fallado") == 0, "nombre de sub-fallado");
	CHECK(std::strcmp(ReasonName(kReasonCount), "sin-casilla") == 0,
		"un valor fuera de las casillas se nombra como lo que es");
}

// ---------------------------------------------------------------------------
// 2. El invariante del cierre: la suma de casillas == cuadros contados. Se
//    verifica con secuencias largas y mezcladas, no con un caso simbólico.
// ---------------------------------------------------------------------------
static void testCierre()
{
	printf("Cenit 0.6.36 h13: el cierre suma las casillas contra los cuadros\n");

	Stats s;
	CHECK(s.frames == 0, "empieza en cero");
	CHECK(s.Closes(), "el vacio tambien cierra (0 == 0)");
	CHECK(s.SubNative() == 0, "sin cuadros no hay sub-nativos");

	// Secuencia determinista que cubre las cuatro casillas muchas veces, con
	// proporciones distintas: 3 nativos por cada sub-nativo de cada tipo.
	std::vector<uint64_t> esper(kReasonCount, 0);
	uint64_t frames = 0;
	for (int ronda = 0; ronda < 250; ronda++)
	{
		for (int k = 0; k < 3; k++)
		{
			s.Tally(false, true, true);
			esper[kNativo]++;
			frames++;
		}
		s.Tally(true, false, false);
		esper[kSubApagado]++;
		frames++;
		s.Tally(true, true, true);
		esper[kSubHecho]++;
		frames++;
		s.Tally(true, true, false);
		esper[kSubFallado]++;
		frames++;
	}
	CHECK(s.frames == frames, "frames == cuadros tallados");
	for (unsigned c = 0; c < kReasonCount; c++)
	{
		char msg[96];
		snprintf(msg, sizeof(msg), "casilla %s == %llu", ReasonName((Reason)c),
			(unsigned long long)esper[c]);
		CHECK(s.by_reason[c] == esper[c], msg);
	}
	CHECK(s.Closes(), "la suma de las cuatro casillas cierra contra frames");
	CHECK(s.SubNative() == esper[kSubApagado] + esper[kSubHecho] + esper[kSubFallado],
		"sub-nativo es la suma de las tres casillas que no son nativo");

	// El sub-nativo NO incluye los nativos: un contador de sub-nativos que sumara
	// kNativo inflaria el uso de EASU, que es exactamente el error de denominador
	// que esta version existe para no repetir.
	CHECK(s.SubNative() < s.frames, "hay mas cuadros que sub-nativos (la secuencia mezcla)");

	// CONTROL NEGATIVO: el invariante tiene que DECIR NO cuando falta un cuadro.
	// Sin este caso, debilitar Closes() (comparar <= en vez de ==, o dejar de
	// sumar una casilla) pasa el arnes entero: un invariante que solo se comprueba
	// sobre codigo bueno no es un invariante, es un adorno. Aqui se rompe la cuenta
	// a mano, por fuera del clasificador, y se exige el rojo.
	Stats mal;
	mal.Tally(true, true, true);
	mal.frames++; // un cuadro presentado sin casilla: es exactamente el defecto
	CHECK(!mal.Closes(), "un cuadro sin casilla NO cierra (control negativo)");
	Stats mal2;
	mal2.Tally(false, false, false);
	mal2.by_reason[kSubHecho]++; // una casilla inflada sin cuadro
	CHECK(!mal2.Closes(), "una casilla inflada sin cuadro NO cierra (control negativo)");
	CHECK(mal.frames == 2 && mal.by_reason[kSubHecho] == 1,
		"el control negativo rompio la cuenta, no el clasificador");

	// Un cuadro de cada casilla, en orden arbitrario, y la suma tiene que seguir
	// cerrando: 4 tallados = 4 sumados.
	Stats t;
	t.Tally(false, false, false);
	t.Tally(true, false, false);
	t.Tally(true, true, false);
	t.Tally(true, true, true);
	CHECK(t.frames == 4 && t.Closes(), "una pasada por cada casilla cierra");
	CHECK(t.by_reason[kNativo] == 1 && t.by_reason[kSubApagado] == 1 &&
			t.by_reason[kSubHecho] == 1 && t.by_reason[kSubFallado] == 1,
		"cada tallado movio UNA sola casilla");
}

// ---------------------------------------------------------------------------
// 3. Auto-aval: si la secuencia del arnés no pasó por donde debía pasar, el
//    verde no vale. Mismo criterio que h12: un diferencial que nunca ejercitó
//    un camino no prueba ese camino.
// ---------------------------------------------------------------------------
static void testTestigo()
{
	Stats s;
	for (int i = 0; i < 64; i++)
	{
		s.Tally(false, true, false);
		s.Tally(true, false, false);
		s.Tally(true, true, true);
		s.Tally(true, true, false);
	}
	const bool testigo = s.by_reason[kNativo] == 64 && s.by_reason[kSubApagado] == 64 &&
	                     s.by_reason[kSubHecho] == 64 && s.by_reason[kSubFallado] == 64 &&
	                     s.frames == 256 && s.Closes();
	CHECK(testigo, "el arnes paso por las cuatro casillas 64 veces cada una");
	printf("  testigos: frames=%llu nativo=%llu sub-apagado=%llu sub-hecho=%llu sub-fallado=%llu\n",
		(unsigned long long)s.frames, (unsigned long long)s.by_reason[kNativo],
		(unsigned long long)s.by_reason[kSubApagado], (unsigned long long)s.by_reason[kSubHecho],
		(unsigned long long)s.by_reason[kSubFallado]);
}

int main()
{
	testExhaustivo();
	testCierre();
	testTestigo();

	printf("H13 (CenitPresentStats.h): %d Comprobaciones, %d Fallos\n", g_checks, g_fallos);
	return g_fallos == 0 ? 0 : 1;
}
