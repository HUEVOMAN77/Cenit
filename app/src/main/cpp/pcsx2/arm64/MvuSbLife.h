// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

// Cenit 0.6.32 (Fase 2 del prompt directo) — Máquina de estados POR CANDIDATO
// del motor de superbloques VU1.
//
// Por qué vive en un header puro, sin dependencias de PCSX2: porque es la única
// pieza de esta fase que se puede verificar SIN teléfono. El ejecutor host
// (Harness9, tools/arneses/h9-src/sb_life_test.cpp) compila ESTE archivo tal
// cual con g++ y fuerza cada transición, cada guarda y cada presupuesto. Lo que
// no vive aquí —la emisión, el digest, la ejecución sombra— sigue necesitando
// al CI como único compilador y sesiones reales para acreditarse.
//
// Qué cambia respecto al motor de 0.6.21, y por qué el prompt lo exige:
//
//  1) ANTES el ciclo de vida de un candidato eran cuatro estados internos
//     (Free/Building/Verify/Trusted) y el «no» era un bit de PC quemado válido
//     por RESTO DE PROCESO, con un auto-apagado GLOBAL tras 3 divergencias. Eso
//     castiga a todo el motor por un candidato malo y no distingue «nunca lo
//     vi» de «lo probé y no converge» de «divergió». El prompt pide máquina por
//     candidato, DEAD por candidato y proceso sano.
//
//  2) El prompt pide SHADOW con el camino NORMAL como autoritativo. Leer el
//     código dice por qué NO se puede ejecutar la variante sobre un clon del
//     estado: el dispatcher fija x19 = &mVU.regs() (microVU-arm64.cpp:671) y
//     los bloques ya emitidos hornean direcciones ABSOLUTAS de
//     VU0.VI[REG_VPU_STAT] en el camino del E-bit (microVU_Branch-arm64.inl:
//     120-123 y 247-250). Ejecutar la variante sobre un VU1 clonado sería
//     reescribir toda la emisión de microVU, y eso es tocar el camino normal —
//     prohibido. El SHADOW que sí es seguro: correr PRIMERO la cadena normal
//     (autoritativa), después la variante sobre el estado real con un
//     snapshot/restore COMPLETO, y comparar. Lo que ese snapshot debe cubrir no
//     es lo que el digest observa (que es un subconjunto) sino todo lo que el
//     episodio puede mutar: VURegs, los 16 KB de VU1.Mem, microVU1.prog.lpState
//     (96 B, microVU-arm64.cpp:937-946), microVU1.cycles (ibídem:774) y
//     VU0.VI[REG_VPU_STAT]. Esta máquina codifica esa condición como dato
//     (ShadowSafe), no como comentario.
//
//  3) Con MTVU (THREAD_VU1) hay un lateral que NINGÚN rollback puede deshacer:
//     el E-bit hace fetch_or sobre vu1Thread.mtvuInterrupts (microVU-arm64.cpp:
//     1946, solo bajo THREAD_VU1) y el hilo EE puede leerlo mientras el
//     episodio sombra corre. Retirar una interrupción que el otro hilo ya vio
//     no es un detalle: es perder un evento PS2, la regla de oro. Por eso, con
//     MTVU encendido el candidato se queda en VALIDATE_ONLY y eso se REPORTA —
//     que es exactamente la salida que el prompt autoriza para lo inseguro.
//
//  4) VALIDATED y TRUSTED son cosas distintas y ninguna es un favor: VALIDATED
//     pide pares limpios contados EN MAS DE UNA VENTANA; TRUSTED pide además
//     ganancia MEDIDA, P95 no peor, y sigue vigilado con comprobaciones
//     puntuales (un Trusted sin spot-check no puede divergir nunca, o sea su
//     confianza no es verificable: es la venta de 0.6.25 con otro nombre).
//
// Los números de esta versión, con su origen dicho:
//   kValidPairs      = 1000  → el prompt lo pide literal (>=1000 comparaciones
//                              sin divergencia).
//   kValidWindows    = 2     → «en más de una ventana».
//   kBudgetEpisodes  = 60000 → techo de TRABAJO de validación por candidato.
//                              Cada episodio sombra cuesta un snapshot+restore
//                              de ~21 KB y dos passes XXH3 sobre VU1.Mem. Con
//                              1000 pares hacen falta >=2000 episodios; 60000
//                              deja margen para entradas con clave distinta o
//                              presupuesto corto y limita el peaje de la
//                              instrumentación.
//   kTrustedGainPermille = 30 → CRITERIO PROPIO (+3 %), del prompt solo vienen
//                              los del tuner (+5/+8/+10 %). Se calibra con la
//                              primera ganancia medida real de un superbloque,
//                              no con mi opinión: por eso está nombrado y
//                              escrito, no empotrado en un if.

#include <cstdint>

namespace mVUSbLife
{
	// Orden fijo. Estos números viajan al HUD y al informe; reordenar es cambiar
	// el significado de datos guardados en teléfonos ajenos.
	enum class State : uint32_t
	{
		Unseen = 0,		// nunca evaluado por la política en esta sesión
		Building = 1,	// compilación variante en vuelo (hilo dispatcher)
		Validating = 2,	// VALIDATE_ONLY: se cuentan pares normal/SB y el
					// superbloque corre en el despacho. Cero rollback.
		Shadowing = 3,	// normal autoritativo + variante con snapshot/restore
		Validated = 4,	// sin divergencias y con los pares/ventanas exigidos
		Trusted = 5,	// Validated + ganancia medida + P95 no peor, y vigilado
		Dead = 6,		// este candidato no (por ESTA identidad), no el proceso
		Invalidated = 7	// cambió el contexto: se descarta la evidencia, no se
					// quema nada
	};

	static constexpr uint32_t kStateCount = 8;

	static constexpr const char* StateName(State s)
	{
		switch (s)
		{
		case State::Unseen: return "UNSEEN";
		case State::Building: return "BUILDING";
		case State::Validating: return "VALIDATE";
		case State::Shadowing: return "SHADOW";
		case State::Validated: return "VALIDATED";
		case State::Trusted: return "TRUSTED";
		case State::Dead: return "DEAD";
		case State::Invalidated: return "INVALIDATED";
		}
		return "STATE_??"; // nunca un estado inventado: se ve como bicho
	}

	// ------------------------------------------------------------------
	// Umbrales (política, no medición)
	// ------------------------------------------------------------------
	static constexpr uint32_t kValidPairs = 1000;
	static constexpr uint32_t kValidWindows = 2;
	static constexpr uint32_t kBudgetEpisodes = 60000;
	static constexpr int32_t kTrustedGainPermille = 30;

	// Un Trusted despacha la variante de verdad. Si no se volviera a comprobar
	// jamás, una divergencia causada por algo que la identidad no captura sería
	// invisible. El spot-check es el precio de llamar «confiable» a algo: un
	// episodio de cada 4096 se compara con la ruta normal. Con ~60 fps de VU1
	// esto es un chequeo cada minuto por candidato, y el coste (un snapshot) se
	// paga solo en ese episodio.
	static constexpr uint32_t kTrustedSpotPeriod = 4096;

	// Minimo de PARES con las dos duraciones medidas para que una ganancia
	// cuente. Un promedio de dos muestras no es un promedio: el primer par de
	// un bloque caliente puede estar dominado por icache frio, y decir "+12 %"
	// de eso es exactamente vender opinion como numero. Con 64 pares la
	// alternancia de ruta (la que va primero cambia cada par) ya cancelo
	// el sesgo de orden y la media tiene varianza acotada.
	static constexpr uint32_t kMinGainPairs = 64;

	// ------------------------------------------------------------------
	// MASCARA de la comparacion. El prompt pide mascaras completas de
	// registros/memoria, y una mascara que vive en un comentario no mascara a
	// nadie: vive aqui como bits y el motor se niega a contar un par si el
	// observador registrado no declara cubiertos TODOS estos regiones. El
	// digest real esta en microVU-arm64.cpp (mVU1ExitDigest), que es el unico
	// TU que ve VURegs; si alguien le quita una region sin bajar el bit
	// declarado, la evidencia deja de formarse y el informe lo dice, en lugar
	// de validar una equivalencia sobre un estado a medias.
	// ------------------------------------------------------------------
	namespace Regions
	{
		enum : uint32_t
		{
			Vf = 1u << 0,	 // los 32 VF (con W y su máscara)
			Vi = 1u << 1,	 // los 32 VI
			Acc = 1u << 2,
			Qp = 1u << 3,	 // q/p
			Cycle = 1u << 4, // regs().cycle (continuo, mismo reloj en las dos patas)
			Flags = 1u << 5, // flags + ebit
			Pendings = 1u << 6,	// pending_q/p
			MacroClipStatus = 1u << 7, // micro_macflags/clipflags/statusflags + mac/status/clipflag
			NextBlockCycles = 1u << 8,
			Xgkick = 1u << 9,	// las nueve palabras xgkick*
			ViBackup = 1u << 10, // VIBackupCycles/VIOldValue/VIRegNumber
			LatencyRings = 1u << 11, // fmac/fdiv/efu/ialu + cabezas
			DataMem = 1u << 12,	// los 16 KB de VU1.Mem (lo que el EE lee por ventana)
			EpisodeCycles = 1u << 13, // consumo real del episodio, plegado aparte
		};
	}

	// Lo que mVU1ExitDigest declara cubrir HOY (enumerado campo por campo en
	// su comentario y en el encabezado de este archivo).
	static constexpr uint32_t kRequiredRegions =
		Regions::Vf | Regions::Vi | Regions::Acc | Regions::Qp | Regions::Cycle |
		Regions::Flags | Regions::Pendings | Regions::MacroClipStatus |
		Regions::NextBlockCycles | Regions::Xgkick | Regions::ViBackup |
		Regions::LatencyRings | Regions::DataMem | Regions::EpisodeCycles;

	// ------------------------------------------------------------------
	// Identidad del candidato. Dos candidatos con identidad distinta NO son el
	// mismo superbloque y no comparten evidencia: es la misma regla de la
	// Fase 1 (identidad completa o no se adopta), aplicada a un bloque VU.
	// ------------------------------------------------------------------
	struct Identity
	{
		uint32_t startPc = 0;		// bytes de microMem
		uint32_t codeDigest = 0;	// digest del área analizada
		uint64_t profileHash = 0;	// identidad de juego+dispositivo (Java la publica)
		uint32_t mtvu = 0;			// 0/1: cambia quién ejecuta y qué laterales hay
		int32_t cycleRate = 0;		// cambia el ritmo del episodio
		int32_t cycleSkip = 0;
		uint32_t engineAbi = 0;		// versión del compilador/emisión

		bool Same(const Identity& o) const
		{
			return startPc == o.startPc && codeDigest == o.codeDigest &&
				profileHash == o.profileHash && mtvu == o.mtvu &&
				cycleRate == o.cycleRate && cycleSkip == o.cycleSkip &&
				engineAbi == o.engineAbi;
		}

		// Identidad incompleta = no se sabe dónde se está. Sin perfil de
		// juego+dispositivo la evidencia no puede viajar ni invalidarse
		// correctamente, así que no se promueve nada (regla de la Fase 1). El
		// codeDigest también vale: 0 significa "nadie midió el área", y dos
		// candidatos con el mismo PC pero microcódigo distinto no son el mismo
		// superbloque — compartir evidencia ahí sería un error de identidad, no
		// de medición.
		bool Complete() const
		{
			return profileHash != 0 && engineAbi != 0 && codeDigest != 0;
		}
	};

	// ------------------------------------------------------------------
	// Evidencia acumulada de UN candidato.
	// ------------------------------------------------------------------
	struct Evidence
	{
		uint32_t pairs = 0;			// pares MATCH (comparaciones sin divergencia)
		uint32_t windows = 1;		// ventana actual en la que se cuenta (1 = la primera)
		uint32_t windowsWithPairs = 0;	// ventanas DISTINTAS que aportaron >=1 par
		uint32_t episodes = 0;		// trabajo consumido (presupuesto)
		uint32_t divergences = 0;		// debe ser 0 para promocionar
		uint32_t windowIdRejected = 0;	// ids no monótonos que no se contaron
		uint32_t trialEpisodesIgnored = 0; // pares que llegaron durante una ventana de
					// prueba y NO se contaron (se anotan: "ignoré" es
					// información, silenciar no)
		uint32_t pairsMaskRejected = 0; // pares NO contados porque el observador no
					// declaraba la mascara completa (se anotan: «no conté» es
					// informacion, silenciar no)
		uint32_t timedPairs = 0; // pares con LAS DOS duraciones medidas (base de la ganancia)
		int32_t gainPermille = -1;		// -1 = sin medir (0 = medido sin ganancia)
		// Microsegundos, no milisegundos: un episodio VU1 es de microsegundos, y
		// en ms una regresión REAL del P95 se leería 0 — o sea el veto «P95 no
		// peor» no vetaría nada. Es la unidad la que hace el criterio, no el nombre.
		int32_t p95DeltaUs = 0;		// >0 = P95 del episodio peor con el superbloque

		bool HasGain() const { return gainPermille >= 0; }
	};

	// ------------------------------------------------------------------
	// Reglas puras: sin reloj, sin config global, sin PCSX2. Todo lo que el
	// motor tiene que saber entra como parámetro ya decidido.
	// ------------------------------------------------------------------
	struct Rules
	{
		// ¿Se puede correr la variante con rollback SIN mover un evento PS2?
		// Con MTVU la respuesta es no (ver encabezado, punto 3) y el candidato
		// se queda en VALIDATE_ONLY.
		static bool ShadowSafe(const Identity& id) { return id.mtvu == 0; }

		static bool DivergenceFree(const Evidence& e) { return e.divergences == 0 && e.pairs > 0; }

		static bool ValidatedReady(const Evidence& e)
		{
			return DivergenceFree(e) && e.pairs >= kValidPairs &&
				e.windowsWithPairs >= kValidWindows;
		}

		// La ganancia se mide en MICROSEGUNDOS de episodio; p95DeltaUs es P95(variante)
		// - P95(normal) en microsegundos: <=0 significa «no peor».
		static bool TrustedReady(const Identity& id, const Evidence& e, int32_t p95DeltaUs)
		{
			if (!id.Complete()) return false;
			if (!ValidatedReady(e)) return false;
			if (!e.HasGain()) return false; // sin número medido no hay Trusted
			if (e.timedPairs < kMinGainPairs) return false; // media con dos muestras no es media
			if (e.gainPermille < kTrustedGainPermille) return false;
			return p95DeltaUs <= 0; // «P95 no peor»: 0 = igual, negativo = mejor
		}

		// Un observador que no declara la mascara completa NO puede formar
		// evidencia: se quedaria en VALIDATE_ONLY sin promocionar nada, y el informe
		// diría por qué. Es la única manera de que «las máscaras son completas» sea
		// comprobable y no una promesa.
		static bool MaskComplete(uint32_t declared)
		{
			return (declared & kRequiredRegions) == kRequiredRegions;
		}

		static bool OutOfBudget(const Evidence& e) { return e.episodes >= kBudgetEpisodes; }

		// Comprobación puntual de un Trusted. n = despachos de ESTE candidato.
		static bool ShouldSpotCheck(uint32_t n)
		{
			return n > 0 && (n % kTrustedSpotPeriod) == 0;
		}
	};

	// ------------------------------------------------------------------
	// La máquina de un candidato. Cada transición es total (no deja el estado
	// a medias) y no depende de nada externo; el que llama decide CUÁNDO.
	// ------------------------------------------------------------------
	struct Machine
	{
		State state = State::Unseen;
		Identity identity{};
		Evidence evidence{};
		bool shadowMode = false;	// true => los episodios se corren con rollback
		bool trialLeg = false;		// true => UNA ventana B declarada: la variante
					// sirve el juego para poder medirle ganancia
		bool spotLeg = false;		// true => ventana de VIGILANCIA de un
					// Trusted: el replay se reabre, porque sin
					// superficie de snapshot/restore es la unica
					// manera honesta de volver a comprobar
		uint32_t dispatches = 0;	// despachos de este candidato (para el spot-check)
		// Mascara que DECLARA el observador de salida (Regions::). Cero = nadie
		// declaro nada, y sin declaracion no hay evidencia: PairMatched lo hace
		// explícito. Se pone con SetObserver desde el motor, con el valor que la
		// funcion de digest realmente cubre.
		uint32_t observerMask = 0;

		// --- transiciones ---

		// El motor declara aqui, con conocimiento de causa, que su digest cubre
		// estas regiones. No es una decoracion: mientras la mascara no sea la
		// exigida, los pares NO se cuentan (ver PairMatched).
		void SetObserver(uint32_t declaredMask)
		{
			observerMask = declaredMask;
		}


		// La política dijo que hay material. Desde UNSEEN o desde INVALIDATED
		// (el contexto volvió a su sitio); desde DEAD no: quemado es quemado.
		void StartBuild()
		{
			if (state != State::Unseen && state != State::Invalidated) return;
			evidence = Evidence{};
			shadowMode = false;
			trialLeg = false;
			dispatches = 0;
			// Se resetea AQUI tambien: lastPairWindow sobrevive a StartBuild porque
			// es privado al llamador, y si la construccion anterior se habia
			// quedado en la ventana 1, el primer par de la nueva no contaria
			// ventana nueva -> windowsWithPairs no arranca y el candidato jamas se
			// valida. Harness9 lo afirma.
			lastPairWindow = 0;
			state = State::Building;
		}

		// La identidad se construye ANTES de conocer los tramos reales del área
		// (el armado la pide con un tramo, el cierre con los que salieron).
		// Mientras la compilación está en vuelo no hay evidencia que romper, así
		// que el llamador puede REFINARLA aquí. Fuera de BUILDING devuelve false:
		// cambiar la identidad con evidencia acumulada es otro asunto y ese lo
		// decide RebindIfChanged (descarta la evidencia, no la reetiqueta).
		bool RelabelWhileBuilding(const Identity& id)
		{
			if (state != State::Building) return false;
			identity = id;
			return true;
		}

		// Compilación terminada. areaOk: el área pasó la política de forma (sin
		// kick, sin bits prohibidos, dentro de techos). budgetOk: queda trabajo
		// por presupuestar. rollbackSurface: el LLAMADOR afirma que existe la
		// superficie de snapshot/restore completa — VURegs, los 16 KB de VU1.Mem,
		// microVU1.prog.lpState, microVU1.cycles y VU0.VI[REG_VPU_STAT] (encabezado,
		// punto 2). Vive como parámetro y no como decisión de este header porque la
		// máquina no puede saber si el motor la implementó: mientras no exista, el
		// candidato cae en VALIDATE_ONLY, que es la salida que el prompt autoriza
		// para lo inseguro — pero la dice el dato, no un if suelto del llamador.
		// Con MTVU encendido también va a VALIDATE_ONLY aunque el área sea perfecta.
		void FinishBuild(bool areaOk, bool budgetOk, bool rollbackSurface)
		{
			if (state != State::Building) return;
			if (!areaOk) { Burn(); return; }
			if (!budgetOk) { state = State::Invalidated; return; }
			// Las dos condiciones son independientes y las dos necesarias:
			// ShadowSafe dice si el rollback PUEDE deshacer los laterales; el
			// parámetro dice si el motor LO IMPLEMENTÓ.
			shadowMode = rollbackSurface && Rules::ShadowSafe(identity);
			state = shadowMode ? State::Shadowing : State::Validating;
		}

		// Un episodio de validación consumido, cuente o no cuente como par: es
		// el trabajo REAL hecho y lo que mide el presupuesto.
		void EpisodeDone()
		{
			if (!Occupies()) return;
			if (state == State::Trusted) { dispatches++; return; }
			evidence.episodes++;
			if (state == State::Validating || state == State::Shadowing)
				dispatches++;
		}

		// Cambio de ventana de medición. El llamador pasa un id MONÓTONO de la
		// sesión del governor; un id que no crece no se cuenta (no se puede
		// distinguir de un intento de inflar la ventana, y la evidencia no se
		// construye sobre lo que no se puede verificar).
		void StartWindow(uint32_t windowSeq)
		{
			if (!Occupies()) return;
			trialLeg = false; // una ventana nueva no heredó la declaración de prueba
			if (windowSeq == 0 || windowSeq == evidence.windows) return;
			if (windowSeq < evidence.windows)
			{
				evidence.windowIdRejected++;
				return;
			}
			evidence.windows = windowSeq;
		}

		// Ventana declarada de PRUEBA: durante ESTA ventana la variante sirve el
		// juego, para poder medirle ganancia contra la ventana anterior. Es la
		// misma disciplina de las ventanas A/B del sintonizador de la Fase 1, y por
		// la misma razón: sin servir el candidato no hay número de ganancia, y sin
		// número no hay TRUSTED honesto.
		//
		// Solo se declara desde SHADOW o desde VALIDATED — los dos estados donde el
		// camino autoritativo es la cadena normal. Desde VALIDATE no tiene sentido:
		// allí la variante YA sirve el juego en mitad de los despachos, y llamar
		// "prueba" a una ventana con las dos rutas alternadas sería medir una mezcla
		// (y decirle mentira al HUD). Por eso tampoco se cuentan PARES mientras la
		// prueba dura: un intervalo con dos dueños no produce evidencia de
		// equivalencia ni de ganancia; produce las dos cosas a medias.
		bool StartTrial(uint32_t windowSeq)
		{
			if (state != State::Shadowing && state != State::Validated) return false;
			if (windowSeq == 0 || windowSeq < evidence.windows) return false;
			evidence.windows = windowSeq;
			trialLeg = true;
			return true;
		}

		void EndTrial() { trialLeg = false; }

		// Par MATCH cerrado. Suma en la ventana ACTUAL; la primera ventana que
		// aporta un par cuenta como una, y una segunda como dos.
		//
		// Durante una ventana de prueba NO se cuentan pares: la variante está
		// sirviendo el juego entero (no alternada), así que no hay pareja normal
		// con la que compararla, y admitir el par sería inventar evidencia de
		// equivalencia sobre un intervalo que ya tiene otro dueño (la medición de
		// ganancia). La regla de la Fase 1, aplicada al revés.
		void PairMatched()
		{
			if (state != State::Validating && state != State::Shadowing) return;
			// Sin mascara completa no hay par: estaríamos afirmando equivalencia
			// sobre un estado que no se miró entero. Es el defecto que el prompt
			// llama "comparaciones con mascaras parciales", y se niega aqui, no en
			// el informe.
			if (!Rules::MaskComplete(observerMask)) { evidence.pairsMaskRejected++; return; }
			if (trialLeg) { evidence.trialEpisodesIgnored++; return; }
			evidence.pairs++;
			// lastPairWindow arranca en 0 y ninguna ventana real es 0, así que el
			// primer par siempre cuenta como "ventana nueva". Sin este orden, dos
			// pares seguidos en la MISMA ventana inflarían windowsWithPairs a 2 y
			// el candidato se promocionaría con evidencia de un solo rincón del
			// juego — exactamente lo que el ">1 ventana" quiere impedir.
			if (lastPairWindow != evidence.windows)
			{
				evidence.windowsWithPairs++;
				lastPairWindow = evidence.windows;
			}
			if (Rules::ValidatedReady(evidence)) state = State::Validated;
		}

		// Divergencia: muere el CANDIDATO, no el proceso. Con SHADOW el estado ya
		// fue restaurado antes de llegar aquí, así que el juego sigue con la
		// cadena normal intacta; en VALIDATE_ONLY el episodio divergente YA
		// corrió como autoritativo — por eso ese modo es el que no puede
		// promocionar sin la ventana de pares que pide el prompt.
		void Diverged()
		{
			evidence.divergences++;
			Burn();
		}

		// Presupuesto de trabajo agotado sin evidencia suficiente: se archiva,
		// no se quema. Otro momento del juego puede dar los pares que faltan.
		void BudgetReached()
		{
			if (state != State::Validating && state != State::Shadowing) return;
			// Una VIGILANCIA de un Trusted que se queda sin presupuesto no puede
			// degradar al candidato: la evidencia ya existe y ya dio su numero. Lo
			// que pasa es que esta comprobacion no se pudo terminar — se cierra
			// inconclusa y el Trusted sigue siendo Trusted (la siguiente ventana de
			// vigilancia llegara cuando haya trabajo). Devolverlo a VALIDATED por no
			// haber terminado un control seria castigar al medido por el presupuesto
			// y disfrazar de «equivale pero no esta confiable» algo que si lo esta.
			if (spotLeg)
			{
				spotLeg = false;
				if (Rules::TrustedReady(identity, evidence, evidence.p95DeltaUs))
					state = State::Trusted;
				else
					state = State::Validated;
				return;
			}
			if (Rules::ValidatedReady(evidence)) { state = State::Validated; return; }
			state = State::Invalidated;
		}

		// La medición en ventana entrega la ganancia del candidato. Con
		// identidad incompleta NO promociona, aunque el número sea un regalo:
		// un +10 % de un sitio que no sé cuál es no es evidencia, es suerte.
		// timedPairs: cuantos pares de ESTA evidencia tienen las dos duraciones
		// medidas. Se guarda junto al numero: un «+12 %» sin su n es una opinion.
		// p95DeltaUs: P95 de la duracion del episodio, variante menos normal, en
		// microsegundos (ver Evidence). Se pasa como parametro y no se lee de
		// evidence porque el llamador lo calcula sobre su propio muestreo.
		void ReportGain(int32_t gainPermille, int32_t p95DeltaUs, uint32_t timedPairs)
		{
			if (state != State::Validated) return;
			evidence.timedPairs = timedPairs;
			evidence.gainPermille = gainPermille;
			evidence.p95DeltaUs = p95DeltaUs;
			if (Rules::TrustedReady(identity, evidence, p95DeltaUs)) state = State::Trusted;
		}

		// La identidad con la que se construyó la evidencia ya no es la actual:
		// la evidencia no vale. No quema — el candidato nuevo se evalúa solo.
		void RebindIfChanged(const Identity& now)
		{
			if (identity.Same(now)) return;
			identity = now;
			evidence = Evidence{};
			shadowMode = false;
			dispatches = 0;
			lastPairWindow = 0;
			state = State::Unseen;
		}

		// Microcódigo reescrito / config cambiada / juego parado.
		void InvalidateContext()
		{
			if (state == State::Unseen) return;
			evidence = Evidence{};
			shadowMode = false;
			lastPairWindow = 0;
			state = State::Invalidated;
		}

		// Vigilancia de un TRUSTED: cada kTrustedSpotPeriod despachos se REABRE
		// el replay diferencial de una ventana. Por qué así y no «una comprobación
		// suelta»: sin superficie de snapshot/restore no se puede correr la
		// variante y la normal sobre el MISMO estado dentro de un episodio;
		// reabrir la ventana alternada es la única manera honesta de volver a
		// comprobar, y es exactamente la que el motor ya sabe hacer. La evidencia
		// NO se borra: si esta ventana diverge el candidato muere (la confianza
		// estaba mal); si cierra limpia, EndSpotCheck devuelve el Trusted.
		bool BeginSpotCheck()
		{
			if (state != State::Trusted || spotLeg || shadowMode) return false;
			spotLeg = true;
			trialLeg = false;
			dispatches = 0; // el periodo de vigilancia vuelve a contar desde aqui
			// shadowMode esta bloqueado arriba: la vigilancia de un Trusted se hace
			// reabriendo el replay diferencial. La sombra se comprueba con otro
			// mecanismo (snapshot/restore dentro del episodio), y mientras ese
			// mecanismo no exista, un Trusted en modo sombra no tiene camino de
			// vigilancia — no se finge uno.
			state = State::Validating;
			return true;
		}

		// clean: la ventana termino sin divergencia. El llamador no lo decide a
		// ojo: si hubiera divergencia ya llamo a Diverged y el estado es DEAD, y
		// aqui eso se traduce en «no vuelve a Trusted».
		bool EndSpotCheck(bool clean)
		{
			if (!spotLeg) return false;
			spotLeg = false;
			if (!clean || state == State::Dead) return false;
			// Vuelve a Trusted solo si la evidencia SIGUE dando Trusted: la
			// vigilancia no refuto la equivalencia y el numero medido no se
			// invento de nuevo.
			if (Rules::TrustedReady(identity, evidence, evidence.p95DeltaUs))
			{
				state = State::Trusted;
				return true;
			}
			return false;
		}


		// Este candidato, con ESTA identidad, queda fuera por el resto de
		// sesión. Un DEAD no liberaba antes el slot: ahora sí lo permite.
		void Burn()
		{
			shadowMode = false;
			state = State::Dead;
		}

		// ¿Ocupa un hueco de la tabla? DEAD e INVALIDATED no: no hay variante
		// viva que despachar ni evidencia que seguir acumulando.
		bool Occupies() const
		{
			return state != State::Unseen && state != State::Dead &&
				state != State::Invalidated;
		}

		// Un hueco con candidato que ya NO ocupa (DEAD / INVALIDATED / UNSEEN)
		// puede reasignarse a otro PC. Antes no: esos dos seguian mordiendo la
		// tabla. Es la otra mitad del «DEAD por candidato»: si el castigo es del
		// candidato, su hueco vuelve al monton.
		bool FreeForReassign() const { return !Occupies(); }

		// ¿Debe el despacho tomar la variante?
		//  - VALIDATE: sí, siempre (es la única manera de formar pares).
		//  - SHADOW: NO, salvo ventana de prueba declarada — el autoritativo es la
		//    cadena normal y la variante solo corre dentro del snapshot.
		//  - VALIDATED: NO por defecto. Validado significa "equivale", no "vale la
		//    pena"; servirlo sin haberle medido ganancia es exactamente vender como
		//    activo lo que no está medido. Solo despacha en su ventana de prueba,
		//    que es como se obtiene el número.
		//  - TRUSTED: sí, con su spot-check periódico.
		bool DispatchesVariant() const
		{
			if (state == State::Trusted) return true;
			if (state == State::Validating) return true;
			return trialLeg && (state == State::Shadowing || state == State::Validated);
		}

		// Etiqueta del HUD. En VALIDATE y SHADOW el HUD NUNCA puede decir
		// «acelerado»: el prompt lo prohíbe y aquí está codificado, no delegada
		// al llamador — un HUD que miente por cómo se llama es el error que
		// costó el ciclo 0.6.25→0.6.26. La ventana de prueba se dice JUNTO al
		// estado, no en lugar de él: el usuario necesita ver las dos cosas a la
		// vez (en qué fase está el candidato y que esta ventana la está sirviendo
		// una ruta todavía sin ganancia medida).
		const char* HudLabel() const
		{
			if (trialLeg && state == State::Shadowing) return "VU-SB: SHADOW/TRIAL";
			if (trialLeg && state == State::Validated) return "VU-SB: VALIDATED/TRIAL";
			// Una ventana de vigilancia sobre un candidato YA medido no es un
			// VALIDATE virgen: el HUD tiene que decir las dos cosas.
			if (spotLeg && (state == State::Validating || state == State::Shadowing))
				return "VU-SB: TRUSTED/SPOT";
			switch (state)
			{
			case State::Validating: return "VU-SB: VALIDATE";
			case State::Shadowing: return "VU-SB: SHADOW";
			case State::Validated: return "VU-SB: VALIDATED";
			case State::Trusted: return "VU-SB: TRUSTED";
			case State::Dead: return "VU-SB: DEAD";
			case State::Invalidated: return "VU-SB: INVALIDATED";
			case State::Building: return "VU-SB: BUILDING";
			default: return "VU-SB: UNSEEN";
			}
		}

	private:
		uint32_t lastPairWindow = 0; // en qué ventana cayó el último par
	};
}
