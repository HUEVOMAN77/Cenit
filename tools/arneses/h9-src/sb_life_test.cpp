// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// Cenit 0.6.32 (Fase 2) — Harness9: verificacion en HOST de la maquina de
// estados POR CANDIDATO del motor de superbloques VU1.
//
// Que prueba y que NO prueba:
//  - Prueba: las transiciones, las guardas de promocion, la contabilidad de
//    ventanas, el presupuesto, la invalidez por identidad y que el HUD no pueda
//    decir "acelerado" donde no hay ganancia medida.
//  - NO prueba: que un superbloque real no diverja, que el snapshot/restore
//    cubra todo, ni que haya ganancia. Eso solo se ve en el telefono con el
//    motor encendido. Un verde aqui es "la maquina no miente", no "el motor
//    funciona" — y este archivo lo dice en voz alta porque es la distincion que
//    costo el ciclo 0.6.25->0.6.26.
//
// Se compila con g++ del host contra el header REAL del repo (baseline.sh lo
// re-copia en cada corrida, igual que con los arneses de Java). No incluye
// nada de PCSX2: MvuSbLife.h es deliberadamente puro para poder existir aqui.
//
// Uso:  ARNES=h9 bash tools/arneses/baseline.sh

#include "MvuSbLife.h"

#include <cstdio>
#include <cstring>

using namespace mVUSbLife;

static int g_checks = 0;
static int g_fails = 0;

#define CK(cond, msg) do { g_checks++; if (!(cond)) { g_fails++; \
	std::printf("FALLO: %s\n", msg); } } while (0)

#define CKEQ(a, b, msg) do { g_checks++; if ((a) != (b)) { g_fails++; \
	std::printf("FALLO: %s (se esperaba %d, hay %d)\n", msg, (int)(b), (int)(a)); } } while (0)

static Identity ident(uint32_t pc, uint32_t mtvu, uint64_t profile, uint32_t abi)
{
	Identity id;
	id.startPc = pc;
	id.codeDigest = 0x1234;
	id.profileHash = profile;
	id.mtvu = mtvu;
	id.cycleRate = 0;
	id.cycleSkip = 0;
	id.engineAbi = abi;
	return id;
}

// La mascara que declara el observador del arnes: la completa. Un arnes que
// declarara una mascara parcial no podria probar nada de la promocion, y esa es
// exactamente la trampa que el bloque de Regions existe para cerrar.
static void armObserver(Machine& m)
{
	m.SetObserver(kRequiredRegions);
}

// Un candidato ya construido y en modo sombra, listo para acumular.
static Machine shadowCandidate()
{
	Machine m;
	m.identity = ident(0x0240, 0, 0xABCDEF01ULL, 8);
	m.StartBuild();
	m.FinishBuild(true, true, /*rollbackSurface=*/true);
	armObserver(m);
	return m;
}

// Un candidato en VALIDATE_ONLY por MTVU: la sombra no es segura porque el
// fetch_or de mtvuInterrupts lo lee el hilo EE y ningun rollback lo deshace.
static Machine validateCandidate()
{
	Machine m;
	m.identity = ident(0x0240, 1, 0xABCDEF01ULL, 8);
	m.StartBuild();
	m.FinishBuild(true, true, /*rollbackSurface=*/true);
	armObserver(m);
	return m;
}

// Sin MTVU PERO sin superficie de rollback implementada: el motor de HOY. Cae
// en VALIDATE y el HUD no puede decir SHADOW. Es el caso real del repo, y por
// eso vive afirmado aqui.
static Machine noSurfaceCandidate()
{
	Machine m;
	m.identity = ident(0x0240, 0, 0xABCDEF01ULL, 8);
	m.StartBuild();
	m.FinishBuild(true, true, /*rollbackSurface=*/false);
	armObserver(m);
	return m;
}

// ------------------------------------------------------------------
static void testNamesAndOrder()
{
	// El orden ES contrato: son los numeros que viajan al HUD y al informe.
	CKEQ((uint32_t)State::Unseen, 0u, "Unseen vale 0");
	CKEQ((uint32_t)State::Building, 1u, "Building vale 1");
	CKEQ((uint32_t)State::Validating, 2u, "Validating vale 2");
	CKEQ((uint32_t)State::Shadowing, 3u, "Shadowing vale 3");
	CKEQ((uint32_t)State::Validated, 4u, "Validated vale 4");
	CKEQ((uint32_t)State::Trusted, 5u, "Trusted vale 5");
	CKEQ((uint32_t)State::Dead, 6u, "Dead vale 6");
	CKEQ((uint32_t)State::Invalidated, 7u, "Invalidated vale 7");
	CKEQ(kStateCount, 8u, "la tabla tiene 8 estados");

	CK(std::strcmp(StateName(State::Unseen), "UNSEEN") == 0, "UNSEEN se llama UNSEEN");
	CK(std::strcmp(StateName(State::Validating), "VALIDATE") == 0, "VALIDATE se llama VALIDATE");
	CK(std::strcmp(StateName(State::Shadowing), "SHADOW") == 0, "SHADOW se llama SHADOW");
	CK(std::strcmp(StateName(State::Invalidated), "INVALIDATED") == 0, "INVALIDATED se llama igual");
	// Nombre fuera de rango: no se inventa un estado, se ve el bicho.
	CK(std::strcmp(StateName(static_cast<State>(99)), "STATE_??") == 0, "estado desconocido no se disfraza");
}

static void testBuildPaths()
{
	// Sin MTVU: el candidato nace en SHADOW (normal autoritativo + variante).
	Machine s = shadowCandidate();
	CK(s.state == State::Shadowing, "sin MTVU nace en SHADOW");
	CK(s.shadowMode, "shadowMode encendido sin MTVU");
	CK(!s.DispatchesVariant(), "en SHADOW el despacho NO toma la variante: el autoritativo es la cadena normal");

	// Con MTVU: nace en VALIDATE y se queda ahi aunque converja rapido.
	Machine v = validateCandidate();
	CK(v.state == State::Validating, "con MTVU nace en VALIDATE (sombra insegura)");
	CK(!v.shadowMode, "shadowMode apagado con MTVU");
	CK(Rules::ShadowSafe(s.identity) && !Rules::ShadowSafe(v.identity),
		"ShadowSafe depende SOLO de MTVU (el lateral que ningun rollback deshace)");

	// Area mala => DEAD por candidato, no apagado global.
	Machine b;
	b.identity = ident(0x0240, 0, 0x1ULL, 8);
	b.StartBuild();
	CK(b.state == State::Building, "StartBuild lleva a BUILDING");
	b.FinishBuild(false, true, true);
	CK(b.state == State::Dead, "area con forma prohibida => DEAD del candidato");
	CK(!b.Occupies(), "DEAD no ocupa hueco de tabla");

	// Sin presupuesto => INVALIDATED (no quemado: es "no medí", no "no sirve").
	Machine c;
	c.identity = ident(0x0240, 0, 0x1ULL, 8);
	c.StartBuild();
	c.FinishBuild(true, false, true);
	CK(c.state == State::Invalidated, "sin presupuesto => INVALIDATED, no DEAD");
	CK(c.Occupies() == false, "INVALIDATED tampoco ocupa hueco");
	// Y de INVALIDATED se puede volver a intentar; de DEAD no.
	c.StartBuild();
	CK(c.state == State::Building, "desde INVALIDATED se re-intenta");
	Machine d = b;
	d.StartBuild();
	CK(d.state == State::Dead, "desde DEAD NO se re-intenta con la misma identidad");

	// StartBuild desde un estado que no toca no mueve nada.
	Machine e = shadowCandidate();
	e.PairMatched();
	e.StartBuild();
	CK(e.state == State::Shadowing, "StartBuild desde SHADOW es inocuo (no borra evidencia en vuelo)");
}

static void testWindowAccounting()
{
	// El punto delicado: ">1 ventana" no puede inflarse con dos pares seguidos
	// en la MISMA ventana, y la primera ventana tiene que contar.
	Machine m = shadowCandidate();
	CKEQ(m.evidence.windows, 1u, "la ventana inicial es 1 (0 no es ventana real)");
	CKEQ(m.evidence.windowsWithPairs, 0u, "sin pares, sin ventanas con pares");

	m.PairMatched();
	CKEQ(m.evidence.pairs, 1u, "un par suma uno");
	CKEQ(m.evidence.windowsWithPairs, 1u, "el primer par cuenta su ventana");

	m.PairMatched();
	m.PairMatched();
	CKEQ(m.evidence.pairs, 3u, "tres pares en la misma ventana");
	CKEQ(m.evidence.windowsWithPairs, 1u, "y sigue habiendo UNA sola ventana (aqui se inflaba antes)");

	m.StartWindow(2);
	m.PairMatched();
	CKEQ(m.evidence.windowsWithPairs, 2u, "un par en la ventana 2 si cuenta la segunda");

	// Id no monotonico: no se cree, se anota y no cuenta.
	m.StartWindow(1);
	CKEQ(m.evidence.windows, 2u, "una ventana mas vieja no retrocede la actual");
	CKEQ(m.evidence.windowIdRejected, 1u, "y queda registrada como rechazada");
	m.StartWindow(0);
	CKEQ(m.evidence.windows, 2u, "ventana 0 no es una ventana");

	// Saltarse ventanas esta bien: lo que importa es que haya DOS DISTINTAS con pares.
	m.StartWindow(7);
	CKEQ(m.evidence.windows, 7u, "un salto de id se acepta");
	CKEQ(m.evidence.windowsWithPairs, 2u, "y no suma: no hubo par aun en la 7");
	m.PairMatched();
	CKEQ(m.evidence.windowsWithPairs, 3u, "el par de la 7 si suma su ventana");

	// StartBuild limpia la ventana del ultimo par: si no, el segundo intento de
	// un candidato jamas contaria su primera ventana.
	Machine r = shadowCandidate();
	r.StartWindow(1);
	r.PairMatched();
	r.InvalidateContext();
	r.StartBuild();
	r.FinishBuild(true, true, true); // reconstruida de verdad: un BUILDING no acumula pares
	CKEQ(r.evidence.windows, 1u, "reconstruido vuelve a la ventana 1");
	CKEQ(r.evidence.pairs, 0u, "y no trae pares de la construccion anterior");
	r.PairMatched();
	CKEQ(r.evidence.windowsWithPairs, 1u, "y su primer par SI cuenta la primera ventana");
}

// Meteos de N pares limpios en ventanas distintas, sin cruzar a VALIDATED antes
// de la condicion que se esta midiendo.
static void feed(Machine& m, uint32_t pairs, uint32_t windows)
{
	for (uint32_t w = 1; w <= windows; w++)
	{
		if (w > 1) m.StartWindow(w);
		const uint32_t per = (w == windows) ? (pairs - (windows - 1) * (pairs / windows))
			: (pairs / windows);
		for (uint32_t i = 0; i < per; i++) m.PairMatched();
	}
}

static void testValidatedGate()
{
	// 999 pares limpios en muchas ventanas NO validan: el umbral es el del prompt.
	Machine a = shadowCandidate();
	feed(a, kValidPairs - 1, 4);
	CK(a.state == State::Shadowing, "999 pares no validan");
	CK(!Rules::ValidatedReady(a.evidence), "y ValidatedReady dice no");
	CKEQ(a.evidence.pairs, kValidPairs - 1, "los pares llegan al borde exacto");

	// 1000 pares en UNA ventana tampoco: ">1 ventana" es una condicion aparte.
	Machine b = shadowCandidate();
	feed(b, kValidPairs, 1);
	CKEQ(b.evidence.pairs, kValidPairs, "1000 pares");
	CK(b.state != State::Validated, "1000 pares en una sola ventana NO validan");

	// 1000 en dos ventanas, si.
	Machine c = shadowCandidate();
	feed(c, kValidPairs, 2);
	CK(c.state == State::Validated, "1000 pares en dos ventanas validan");
	CK(Rules::ValidatedReady(c.evidence), "ValidatedReady dice si");

	// Una divergencia destruye la validacion: ">=1000 sin divergencia" es un todo.
	Machine d = c;
	d.Diverged();
	CK(d.state == State::Dead, "una divergencia mata AL CANDIDATO");
	CK(!Rules::DivergenceFree(d.evidence), "y la evidencia deja de estar limpia");
	CKEQ(d.evidence.divergences, 1u, "la divergencia queda contada");
	// El proceso sigue vivo: esta maquina no tiene manera de apagar nada global.
	CKEQ(d.evidence.pairs, kValidPairs, "los pares medidos no se borran: se anotan como no validos");

	// Pares en estado muerto no se acumulan.
	d.PairMatched();
	CKEQ(d.evidence.pairs, kValidPairs, "DEAD no suma evidencia");
	d.EpisodeDone();
	CKEQ(d.evidence.episodes, c.evidence.episodes, "DEAD no consume presupuesto");
}

static void testTrustedGate()
{
	// VALIDATED no es TRUSTED: falta la ganancia MEDIDA.
	Machine m = shadowCandidate();
	feed(m, kValidPairs, 2);
	CK(m.state == State::Validated, "validado");
	CK(!Rules::TrustedReady(m.identity, m.evidence, 0), "y sin ganancia medida no hay Trusted");
	CK(m.evidence.gainPermille == -1, "sin medir es -1, jamas 0");

	// Ganancia por debajo del criterio: no promociona.
	Machine a = m;
	a.ReportGain(kTrustedGainPermille - 1, 0, kMinGainPairs);
	CK(a.state == State::Validated, "una ganancia por debajo del umbral no promociona");

	// Ganancia justa, P95 peor: tampoco.
	Machine b = m;
	b.ReportGain(kTrustedGainPermille, 1, kMinGainPairs);
	CK(b.state == State::Validated, "ganancia con P95 peor no promociona");

	// Ganancia justa, P95 igual: si.
	Machine c = m;
	c.ReportGain(kTrustedGainPermille, 0, kMinGainPairs);
	CK(c.state == State::Trusted, "la ganancia minima con P95 no peor promociona");
	CK(c.DispatchesVariant(), "TRUSTED si despacha la variante");

	// P95 mejor vale mas que cero, y se acepta.
	Machine e = m;
	e.ReportGain(kTrustedGainPermille + 500, -12, kMinGainPairs);
	CK(e.state == State::Trusted, "P95 mejor no es un defecto");

	// Unidades: el delta de P95 es en MICROSEGUNDOS. Un episodio VU1 es de
	// microsegundos, y si la unidad fueran ms una regresion de 0,4 ms se leeria
	// "0" y el veto no vetaria nada. Se afirma con un numero chico pero real.
	Machine us = m;
	us.ReportGain(kTrustedGainPermille + 200, 1, kMinGainPairs);
	CK(us.state == State::Validated, "un P95 UNA fraccion de microsegundo peor ya veta (la unidad importa)");
	CKEQ(us.evidence.p95DeltaUs, 1, "y el numero queda registrado con su unidad en el nombre");

	// Ganancia NEGATIVA medida: no promociona nunca, aunque valide.
	Machine f = m;
	f.ReportGain(-40, -99, kMinGainPairs);
	CK(f.state == State::Validated, "una ganancia medida negativa no promociona (el superbloque NO acelera)");

	// Identidad incompleta: la mejor ganancia del mundo no promociona.
	Machine g = shadowCandidate();
	g.identity.profileHash = 0;
	feed(g, kValidPairs, 3);
	CK(g.state == State::Validated, "sin perfil igual se valida (las comparaciones son comparaciones)");
	g.ReportGain(900, -5, kMinGainPairs);
	CK(g.state == State::Validated, "pero NO se promociona: no se sabe en que aparato esta el numero");
	Machine h = shadowCandidate();
	h.identity.engineAbi = 0;
	feed(h, kValidPairs, 3);
	h.ReportGain(900, -5, kMinGainPairs);
	CK(h.state == State::Validated, "sin ABI del motor tampoco: la evidencia no sobreviviria a un cambio de compilador");

	// Digest de area sin medir (0) tampoco: dos candidatos con el mismo PC pero
	// microcodigo distinto no son el mismo superbloque.
	Machine jd = shadowCandidate();
	jd.identity.codeDigest = 0;
	feed(jd, kValidPairs, 3);
	jd.ReportGain(900, -5, kMinGainPairs);
	CK(jd.state == State::Validated, "sin digest de area NO se promociona: la identidad esta incompleta");

	// ReportGain en un estado que no es Validated es inocuo.
	Machine i = shadowCandidate();
	i.ReportGain(900, -5, kMinGainPairs);
	CK(i.state == State::Shadowing, "no se puede promocionar antes de validar");
	CK(i.evidence.gainPermille == -1, "y el numero no se guarda en un estado que no lo acepta");
}

static void testSpotCheck()
{
	// Un TRUSTED sigue vigilado: sin spot-check su confianza no es verificable.
	Machine m = shadowCandidate();
	feed(m, kValidPairs, 2);
	m.ReportGain(kTrustedGainPermille, 0, kMinGainPairs);
	CK(m.state == State::Trusted, "trusted");
	const uint32_t before = m.evidence.episodes;
	m.EpisodeDone();
	CKEQ(m.evidence.episodes, before, "un Trusted no consume presupuesto de validacion (no esta validando)");
	CKEQ(m.dispatches, 1u, "pero si cuenta despachos");

	for (uint32_t i = 2; i < kTrustedSpotPeriod; i++) m.EpisodeDone();
	CKEQ(m.dispatches, kTrustedSpotPeriod - 1, "casi un periodo");
	CK(!Rules::ShouldSpotCheck(m.dispatches), "y aun no toca comprobar");
	m.EpisodeDone();
	CKEQ(m.dispatches, kTrustedSpotPeriod, "un periodo exacto");
	CK(Rules::ShouldSpotCheck(m.dispatches), "ahora SI: cada kTrustedSpotPeriod despachos se compara contra la cadena normal");
	CK(!Rules::ShouldSpotCheck(0), "cero despachos no es un chequeo");

	// Y si el spot-check diverge, se retira: DEAD, no apagado del motor.
	m.Diverged();
	CK(m.state == State::Dead, "un TRUSTED que diverge en su spot-check se retira");
	CK(!m.DispatchesVariant(), "y deja de despachar la variante");
	CK(m.evidence.gainPermille >= kTrustedGainPermille, "la ganancia medida sigue ahi: lo que fallo es la equivalencia, no la medicion");
}

static void testBudget()
{
	Machine m = shadowCandidate();
	CK(!Rules::OutOfBudget(m.evidence), "recien nacido esta dentro del presupuesto");
	// Llenar el presupuesto sin pares suficientes: se archiva, no se quema.
	for (uint32_t i = 0; i < kBudgetEpisodes; i++) m.EpisodeDone();
	CK(Rules::OutOfBudget(m.evidence), "el techo se alcanza con trabajo real");
	CKEQ(m.evidence.pairs, 0u, "sin evidencia");
	m.BudgetReached();
	CK(m.state == State::Invalidated, "presupuesto agotado sin evidencia => INVALIDATED (otro momento del juego puede dar los pares)");
	CK(m.evidence.divergences == 0, "y no queda ni una divergencia: no se le acusa de nada");

	// Con evidencia suficiente, el mismo techo promociona a VALIDATED en vez de archivar.
	Machine n = shadowCandidate();
	feed(n, kValidPairs, 2);
	for (uint32_t i = 0; i < 10; i++) n.EpisodeDone();
	n.BudgetReached();
	CK(n.state == State::Validated, "con los pares hechos, agotar el presupuesto no destruye la validacion");
}

static void testIdentityAndContext()
{
	const Identity base = ident(0x0240, 0, 0xABCDEF01ULL, 8);

	// Cada campo que cambia el codigo emitido o el aparato rompe la identidad.
	Machine m = shadowCandidate();
	feed(m, 500, 3);
	CK(m.evidence.pairs == 500, "con evidencia acumulada");

	struct Case { Identity id; const char* what; };
	const Case cases[] = {
		{ ident(0x0248, 0, 0xABCDEF01ULL, 8), "otro PC de entrada" },
		{ ident(0x0240, 1, 0xABCDEF01ULL, 8), "MTVU encendido (cambia quien ejecuta)" },
		{ ident(0x0240, 0, 0xABCDEF02ULL, 8), "otro perfil de juego+dispositivo" },
		{ ident(0x0240, 0, 0ULL, 8), "perfil perdido" },
		{ ident(0x0240, 0, 0xABCDEF01ULL, 9), "otro ABI del motor" },
	};
	for (const Case& c : cases)
	{
		Machine x = m;
		x.RebindIfChanged(c.id);
		CK(x.state == State::Unseen, c.what);
		CKEQ(x.evidence.pairs, 0u, c.what);
		CKEQ(x.evidence.windowsWithPairs, 0u, c.what);
		CK(!x.shadowMode, c.what);
	}

	// La misma identidad no hace nada (no se puede perder evidencia por un no-cambio).
	Machine same = m;
	same.RebindIfChanged(base);
	CK(same.state == State::Shadowing, "identidad igual no rebindea");
	CKEQ(same.evidence.pairs, 500u, "y no pierde evidencia");

	// Contexto: microcodigo escrito, config cambiada, juego parado.
	Machine ctx = m;
	ctx.InvalidateContext();
	CK(ctx.state == State::Invalidated, "contexto cambiado => INVALIDATED");
	CKEQ(ctx.evidence.pairs, 0u, "la evidencia se descarta");
	ctx.PairMatched();
	CKEQ(ctx.evidence.pairs, 0u, "INVALIDATED no acumula");

	// Un UNSEEN no se invalida: no hay nada que invalidar.
	Machine fresh;
	fresh.InvalidateContext();
	CK(fresh.state == State::Unseen, "UNSEEN no pasa a INVALIDATED");
}

static void testHudHonesty()
{
	// El HUD no puede decir "acelerado" donde no hay ganancia medida. Esta es la
	// regla del prompt y vive aqui, no en el llamador.
	Machine v = validateCandidate();
	CK(std::strstr(v.HudLabel(), "VALIDATE") != nullptr, "VALIDATE_ONLY se etiqueta VALIDATE");
	Machine s = shadowCandidate();
	CK(std::strstr(s.HudLabel(), "SHADOW") != nullptr, "la sombra se etiqueta SHADOW");
	CK(std::strstr(s.HudLabel(), "TRUSTED") == nullptr, "SHADOW no puede llamarse TRUSTED");
	CK(std::strstr(v.HudLabel(), "TRUSTED") == nullptr, "VALIDATE no puede llamarse TRUSTED");
	feed(s, kValidPairs, 2);
	CK(std::strcmp(s.HudLabel(), "VU-SB: VALIDATED") == 0, "validado sin ganancia se dice VALIDATED");
	s.ReportGain(kTrustedGainPermille, 0, kMinGainPairs);
	CK(std::strcmp(s.HudLabel(), "VU-SB: TRUSTED") == 0, "y TRUSTED solo con numero medido");

	// Cada estado tiene su etiqueta y ninguna cae al default.
	const State all[] = { State::Unseen, State::Building, State::Validating, State::Shadowing,
		State::Validated, State::Trusted, State::Dead, State::Invalidated };
	for (const State st : all)
	{
		Machine mm;
		mm.state = st;
		g_checks++;
		if (std::strstr(mm.HudLabel(), "VU-SB: ") != mm.HudLabel())
		{ g_fails++; std::printf("FALLO: etiqueta sin prefijo VU-SB para %s\n", StateName(st)); }
	}
}

static void testDispatchPolicy()
{
	// VALIDATE alternate: la variante corre en el despacho, porque sin eso no hay
	// pares que contar. SHADOW: la variante NUNCA es el camino del juego.
	Machine v = validateCandidate();
	CK(v.DispatchesVariant(), "VALIDATE despacha la variante (es como se forman los pares)");
	Machine s = shadowCandidate();
	CK(!s.DispatchesVariant(), "SHADOW no: el autoritativo es la cadena normal");

	// VALIDATED no despacha por defecto. Es la regla que evita el error de 0.6.25:
	// "equivale" no es "vale la pena", y servirlo sin ganancia medida es vender no
	// medido. La unica ventana en la que un Validated sirve la variante es aquella
	// donde se le esta MIDIENDO la ganancia.
	feed(s, kValidPairs, 2);
	CK(s.state == State::Validated, "VALIDATE/SHADOW con evidencia llega a VALIDATED");
	CK(!s.DispatchesVariant(), "y un VALIDATED sin medir NO sirve la variante");
	CK(!s.trialLeg, "sin ventana de prueba declarada");

	// La ganancia se mide en una ventana declarada; si no gana, no se promociona y
	// ademas la ventana de prueba se cierra (no puede quedarse sirviendo gratis).
	CK(s.StartTrial(5), "declarar ventana de prueba desde VALIDATED");
	CK(s.trialLeg, "y queda declarada");
	CK(s.DispatchesVariant(), "durante ESA ventana la variante si sirve el juego");
	CK(std::strcmp(s.HudLabel(), "VU-SB: VALIDATED/TRIAL") == 0,
		"y el HUD dice las dos cosas: estado y que es una prueba");
	s.EndTrial();
	CK(!s.trialLeg && !s.DispatchesVariant(), "cerrada la ventana, se deja de servir");
	s.ReportGain(-20, 5, kMinGainPairs);
	CK(s.state == State::Validated, "una ganancia perdida no promociona ni un Validated");
	CK(std::strcmp(s.HudLabel(), "VU-SB: VALIDATED") == 0, "y el HUD vuelve a su etiqueta sola");

	// StartTrial desde un estado que no puede probarse, o con ventana no creciente.
	Machine dead = s;
	dead.Burn();
	CK(!dead.StartTrial(9), "un DEAD no declara ventana de prueba");
	Machine t = shadowCandidate();
	CK(!t.StartTrial(0), "ventana 0 no es una ventana");
	t.StartWindow(4);
	CK(!t.StartTrial(3), "una ventana que no crece no declara prueba");
	CK(!t.trialLeg, "y no queda declarada");
	CK(t.StartTrial(4), "la misma ventana actual si se puede declarar (mide sin cambiar de fase)");
	t.EndTrial();

	// VALIDATE NO declara prueba: alli la variante ya sirve el juego a medias con
	// la cadena normal, y llamar "prueba" a esa ventana seria medir una mezcla.
	Machine vv = validateCandidate();
	CK(!vv.StartTrial(5), "VALIDATE no declara ventana de prueba");
	CK(!vv.trialLeg, "y no queda declarada");

	// Durante la prueba no se cuentan pares: no hay pareja normal con la que
	// comparar, y admitirlos seria inventar evidencia de equivalencia.
	Machine mix = shadowCandidate();
	const uint32_t pairsBefore = mix.evidence.pairs;
	mix.StartTrial(3);
	mix.PairMatched();
	CKEQ(mix.evidence.pairs, pairsBefore, "un par que llega durante la prueba NO se cuenta");
	CKEQ(mix.evidence.trialEpisodesIgnored, 1u, "pero si se anota que se ignoro");
	mix.EndTrial();
	mix.PairMatched();
	CKEQ(mix.evidence.pairs, pairsBefore + 1, "cerrada la prueba, se vuelven a contar");

	// SHADOW tambien puede declarar su ventana de prueba (es como se obtiene el
	// numero de ganancia cuando el camino normal es el autoritativo).
	Machine sh = shadowCandidate();
	CK(sh.StartTrial(2), "SHADOW declara prueba");
	CK(sh.DispatchesVariant(), "y durante esa ventana despacha la variante");
	CK(std::strcmp(sh.HudLabel(), "VU-SB: SHADOW/TRIAL") == 0, "con su etiqueta compuesta");
	sh.EndTrial();
	CK(!sh.DispatchesVariant(), "fuera de la prueba, la sombra no interfiere el juego");

	// TRUSTED: ya medido, despacha y se le vigila.
	Machine w = shadowCandidate();
	feed(w, kValidPairs, 2);
	w.ReportGain(kTrustedGainPermille, 0, kMinGainPairs);
	CK(w.state == State::Trusted, "con ganancia medida, TRUSTED");
	CK(w.DispatchesVariant(), "TRUSTED despacha la variante con su spot-check");
	w.Diverged();
	CK(!w.DispatchesVariant(), "tras divergir, nadie despacha la variante");
}

// ------------------------------------------------------------------
// La mascara de comparacion: el prompt pide "mascaras completas de
// registros/memoria". Aqui esa exigencia es un dato con bits y una guarda,
// asi que se puede probar que UN OBSERVADOR PARCIAL NO FORMA EVIDENCIA.
// ------------------------------------------------------------------
static void testMaskHonesty()
{
	Machine m = shadowCandidate();
	CK(m.observerMask == kRequiredRegions, "el arnes declara la mascara completa");
	CK(Rules::MaskComplete(kRequiredRegions), "y la mascara completa es completa");
	m.PairMatched();
	CKEQ(m.evidence.pairs, 1u, "con mascara completa el par se cuenta");

	// Un observador al que le falta UNA region (p.ej. la memoria de datos, que
	// es justo la que el EE observa por ventana): no cuenta ni un par.
	Machine partial = shadowCandidate();
	partial.SetObserver(kRequiredRegions & ~Regions::DataMem);
	CK(!Rules::MaskComplete(partial.observerMask), "sin DataMem la mascara NO es completa");
	partial.PairMatched();
	partial.PairMatched();
	CKEQ(partial.evidence.pairs, 0u, "y no se forma evidencia con un estado a medias");
	CKEQ(partial.evidence.pairsMaskRejected, 2u, "pero si se anota que se rechazaron");
	CK(partial.state == State::Shadowing, "rechazar pares no promociona ni noquea al candidato");

	// Region por region: ninguna puede faltar. Si alguien quita una del digest
	// real sin bajar el bit declarado, esto sigue afirmando la regla.
	uint32_t bit = 1u;
	for (int i = 0; i < 14; i++)
	{
		Machine x = shadowCandidate();
		x.SetObserver(kRequiredRegions & ~bit);
		x.PairMatched();
		CKEQ(x.evidence.pairs, 0u, "quitar UNA region del observador rompe la mascara");
		bit <<= 1;
	}

	// Con mascara incompleta el candidato jamas llega a VALIDATED, aunque el
	// motor le sirva mil despachos: la evidencia no se construye a ciegas.
	Machine blind = shadowCandidate();
	blind.SetObserver(0); // nadie declaro nada (el caso de un motor sin digest)
	for (uint32_t i = 0; i < 5000; i++) blind.PairMatched();
	CKEQ(blind.evidence.pairs, 0u, "sin observador declarado, cero pares");
	CK(blind.state != State::Validated, "y nunca se valida");
	CK(!Rules::ValidatedReady(blind.evidence), "ValidatedReady lo dice tambien");
	CKEQ(blind.evidence.pairsMaskRejected, 5000u, "los 5000 quedan anotados como rechazados");

	// La mascara describe al OBSERVADOR (la funcion de digest del proceso), no
	// al candidato: sobrevive a reconstruir y a invalidar el contexto. Si se
	// perdiera ahi, cada invalidacion dejaria la tabla sin evidencia posible
	// por un motivo que no es del candidato.
	Machine surv = shadowCandidate();
	surv.InvalidateContext();
	CK(surv.observerMask == kRequiredRegions, "invalidar el contexto no desarma al observador");
	surv.StartBuild();
	surv.FinishBuild(true, true, true);
	CKEQ(surv.observerMask, kRequiredRegions, "ni rearmar la compilacion");
	surv.PairMatched();
	CKEQ(surv.evidence.pairs, 1u, "y por eso el primer par de la nueva construccion cuenta");
}

// Sin MTVU PERO sin superficie de rollback implementada: el estado REAL del
// motor de este repo. Es la pieza que evita que el HUD prometa una sombra que
// nadie programo.
static void testNoSurfaceMeansValidate()
{
	Machine m = noSurfaceCandidate();
	CK(m.state == State::Validating, "sin superficie de rollback el candidato nace en VALIDATE");
	CK(!m.shadowMode, "y shadowMode apagado aunque MTVU este apagado");
	CK(Rules::ShadowSafe(m.identity), "la SOMBRA seria segura con este aparato...");
	CK(std::strcmp(m.HudLabel(), "VU-SB: VALIDATE") == 0, "pero el HUD dice VALIDATE");
	CK(std::strstr(m.HudLabel(), "SHADOW") == nullptr, "y jamas puede decir SHADOW sin superficie");
	// Con la superficie declarada, el mismo candidato si entra en sombra.
	Machine with = noSurfaceCandidate();
	with.StartBuild(); // desde VALIDATE no re-armea: se comprueba la via limpia
	CK(with.state == State::Validating, "StartBuild desde VALIDATE es inocuo");
	Machine w2;
	w2.identity = ident(0x0240, 0, 0xABCDEF01ULL, 8);
	w2.StartBuild();
	w2.FinishBuild(true, true, true);
	CK(w2.state == State::Shadowing, "misma identidad + superficie => SHADOW");
}

static void testGainSampleSize()
{
	// Ganancia con menos de kMinGainPairs pares cronometrados: no promociona.
	Machine m = shadowCandidate();
	feed(m, kValidPairs, 2);
	CK(m.state == State::Validated, "validado");
	m.ReportGain(500, -5, kMinGainPairs - 1);
	CK(m.state == State::Validated, "una ganancia con 63 pares medidos NO es un numero, es una opinion");
	CKEQ(m.evidence.gainPermille, 500, "el numero se guarda: se midio, no se invento");
	CKEQ(m.evidence.timedPairs, kMinGainPairs - 1, "y su n tambien, para que el informe lo muestre");
	m.ReportGain(500, -5, kMinGainPairs);
	CK(m.state == State::Trusted, "con la muestra minima si, y es el mismo numero");
	CKEQ(m.evidence.timedPairs, kMinGainPairs, "con su n registrado");
	// El umbral de muestra existe y esta nombrado, no empotrado.
	CK(kMinGainPairs >= 32, "la muestra minima es razonable (>=32 pares)");
	CK(kMinGainPairs < kValidPairs, "y cabe dentro del minimo de pares de validacion");
}

static void testSpotCheckReplay()
{
	// Un Trusted se vigila REABRIENDO el replay: es la unica manera honesta de
	// volver a comprobar cuando no hay superficie de snapshot/restore. En SHADOW
	// la variante no despacha, asi que la vigilancia se prueba sobre un candidato
	// VALIDATE — el caso real del repo de hoy, sin superficie.
	Machine v = validateCandidate();
	feed(v, kValidPairs, 2);
	v.ReportGain(kTrustedGainPermille, 0, kMinGainPairs);
	CK(v.state == State::Trusted, "trusted para vigilar");
	CK(v.BeginSpotCheck(), "la vigilancia se puede abrir");
	CK(!v.BeginSpotCheck(), "pero no dos veces (un intervalo con dos dueños)");
	CK(v.state == State::Validating, "la vigilancia reabre el replay (VALIDATE alternatea)");
	CK(v.spotLeg, "y queda declarada como vigilancia, no como VALIDATE virgen");
	CK(std::strcmp(v.HudLabel(), "VU-SB: TRUSTED/SPOT") == 0,
		"el HUD dice las dos cosas: que esta medido y que esta en vigilancia");
	CK(v.DispatchesVariant(), "durante la vigilancia la variante vuelve a despacharse (es el par)");

	// Cierre limpio: vuelve a Trusted con la MISMA evidencia medida.
	CK(v.EndSpotCheck(true), "cerrar limpia devuelve el Trusted");
	CK(v.state == State::Trusted, "y el estado dice Trusted otra vez");
	CK(!v.spotLeg, "con la vigilancia cerrada");
	CK(std::strcmp(v.HudLabel(), "VU-SB: TRUSTED") == 0, "y el HUD vuelve a su etiqueta sola");

	// Divergencia durante la vigilancia: muere el candidato, no vuelve a Trusted.
	Machine x = validateCandidate();
	feed(x, kValidPairs, 2);
	x.ReportGain(kTrustedGainPermille, 0, kMinGainPairs);
	CK(x.state == State::Trusted, "otro trusted");
	x.BeginSpotCheck();
	x.Diverged();
	CK(x.state == State::Dead, "una divergencia en la vigilancia mata AL CANDIDATO");
	CK(!x.EndSpotCheck(true), "y no hay vuelta a Trusted, ponga lo que ponga el llamador");
	CK(x.state == State::Dead, "el cierre no resucita muertos");

	// EndSpotCheck sin vigilancia declarada es inocuo.
	Machine y = shadowCandidate();
	CK(!y.EndSpotCheck(true), "cerrar una vigilancia que no existe no hace nada");
	CK(y.state == State::Shadowing, "y no mueve el estado");
	CK(!shadowCandidate().BeginSpotCheck(), "un SHADOW sin medir no se auto-vigila");

	// BeginSpotCheck no interfiere con la ventana de prueba.
	Machine z = validateCandidate();
	feed(z, kValidPairs, 2);
	z.ReportGain(kTrustedGainPermille, 0, kMinGainPairs);
	z.BeginSpotCheck();
	CK(!z.StartTrial(9), "durante la vigilancia no se declara ventana de prueba (dos dueños)");
	CK(!z.trialLeg, "y no queda declarada");
	z.EndSpotCheck(true);
	CK(z.trialLeg == false && z.state == State::Trusted, "cerrada, sigue Trusted sin prueba");

	// El periodo vuelve a contar desde la vigilancia (si no, el spot-check se
	// dispararia en cada despacho siguiente).
	CKEQ(z.dispatches, 0u, "BeginSpotCheck pone los despachos a cero");
	CK(!Rules::ShouldSpotCheck(z.dispatches), "y con cero despachos no vuelve a tocar")
;
}

int main()
{
	testNamesAndOrder();
	testBuildPaths();
	testWindowAccounting();
	testMaskHonesty();
	testNoSurfaceMeansValidate();
	testGainSampleSize();
	testSpotCheckReplay();
	testValidatedGate();
	testTrustedGate();
	testSpotCheck();
	testBudget();
	testIdentityAndContext();
	testHudHonesty();
	testDispatchPolicy();

	std::printf("Harness9: %d Comprobaciones, %d Fallos\n", g_checks, g_fails);
	std::printf("Harness9: NOTE - esto verifica LA MAQUINA de estados, no un superbloque real.\n");
	std::printf("Harness9: NOTE - divergencias/ganancia/sombra en hardware siguen pendientes de telefono.\n");
	return g_fails ? 1 : 0;
}
