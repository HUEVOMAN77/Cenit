// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// Cenit 0.6.36 — contadores del camino de present sub-nativo (EASU).
//
// Por que existe este encabezador y no un par de `if` en el .cpp
// ------------------------------------------------------------
// El log real de un HUAWEI NCO-LX3 (God of War SCES-53133, sesion de 298.8 s)
// trae dos pruebas de que el telefono trabajo POR DEBAJO de 1x:
//   - el regidor imprime `sostuvo_escala=0.5` en el bloque FABRICA, y
//   - el nucleo avisa `Upscale multiplier is below native, this will break
//     rendering.` (VMManager::WarnAboutUnsafeSettings).
// Y NO trae ninguna prueba de que la reconstruccion EASU haya corrido. No es
// olvido del usuario: es que nada en el emulog la cuenta. La linea del HUD
// (ImGuiOverlays) muestra EASU ON/OFF del frame actual, y el reporte no lleva
// pantallazos. O sea: desde el log se puede afirmar "hubo cuadros sub-nativos"
// y NO se puede afirmar "se reconstruyeron".
//
// Eso es exactamente el defecto de la version 0.6.35 con `consults`: un
// contador que solo mira el camino afortunado no deja saber el uso real, y sin
// el uso real la funcion no puede dejar de llamarse experimental con honestidad
// (ni puede afirmarse inactiva con honestidad). La leccion quedo escrita ahi, y
// se aplica aqui antes de tocar ninguna etiqueta.
//
// Las cuatro casillas y su invariante
// -----------------------------------
// Todo cuadro presentado cae en UNA sola casilla, y la suma TIENE que dar los
// cuadros presentados:
//
//   frames == nativo + sub-nativo-apagado + sub-nativo-hecho + sub-nativo-fallado
//
// Un cuadro sub-nativo "fallado" es el peor caso posible y el unico que el log
// de 0.6.34 no podia distinguir: el llamante pidio la cadena, la opcion estaba
// encendida y el driver la soporta, y AUN ASI EASUChain devolvio false. El
// resultado en pantalla es el estirado bilinear de siempre, sin un solo aviso.
// Contarlo es lo que convierte "creo que se ve mejor" en "en tu telefono paso
// esto N veces de M".
//
// La suma se imprime en la linea de cierre y NO se calcula aqui a proposito:
// quien lee el registro del telefono tiene que poder ver el desajuste en el
// propio log si alguna vez el codigo deja de ser exhaustivo.
//
// Porque vive en un encabezador sin dependencias
// ----------------------------------------------
// MvuFastPath.h gano su arnes (h12) por el mismo motivo: las guardas que solo
// existen en un .cpp que el host no puede compilar son guardas que nadie puede
// romper, y una guarda que nadie rompe es una guarda que nadie prueba. Aqui la
// "guarda" es la clasificacion exhaustiva, y `bash tools/arneses/mutacion_h13.sh`
// la rompe casilla por casilla.
#pragma once

#include <cstdint>

namespace cenit_present
{
	// Orden fijado por el arnes: mover un valor sin mover la tabla de nombres
	// imprime una casilla con el nombre equivocado, que es peor que no imprimir.
	enum Reason : unsigned
	{
		kNativo = 0,       // la imagen interna no es mas chica que el destino
		kSubApagado,       // sub-nativo y la opcion (o el driver) no permiten la cadena
		kSubHecho,         // sub-nativo y la cadena corrio
		kSubFallado,       // sub-nativo, se pidio la cadena y devolvio false
		kReasonCount
	};

	// Nombre en el registro. Una sola fuente: el arnes comprueba que las cuatro
	// existen y que no hay una quinta sin nombrar.
	inline const char* ReasonName(Reason r)
	{
		switch (r)
		{
		case kNativo:
			return "nativo";
		case kSubApagado:
			return "sub-apagado";
		case kSubHecho:
			return "sub-hecho";
		case kSubFallado:
			return "sub-fallado";
		case kReasonCount:
			break;
		}
		return "sin-casilla";
	}

	// Clasificacion pura: sin GSConfig, sin device, sin Vulkan. El .cpp pasa los
	// TRES hechos que ya le toca conocer (si el cuadro es sub-nativo, si el porton
	// esta abierto, si la cadena devolvio cierto) y aqui se decide la casilla.
	//
	// Sin `default:` a proposito (misma politica que MvuFastPath.h): un Reason
	// nuevo sin casilla propia tiene que caer en la comprobacion del arnes, no
	// ser absorbido silenciosamente por un ramo general.
	inline Reason Classify(bool sub_native, bool gate_open, bool chain_ran)
	{
		if (!sub_native)
			return kNativo;
		if (!gate_open)
			return kSubApagado;
		return chain_ran ? kSubHecho : kSubFallado;
	}

	// El acumulado de una instancia de renderer. No es global: al reabrir el GS
	// (cambio de backend) la linea de cierre describe el tramo que se apago, el
	// mismo contrato que ya usan las lineas de vuSB y del compilador asincrono.
	struct Stats
	{
		uint64_t frames = 0;
		uint64_t by_reason[kReasonCount] = {};

		// Un cuadro presentado = una clasificacion. La suma por casilla NO puede
		// exceder frames ni quedarse corta, y por eso se comprueba aqui y en el
		// cierre, no solo en el arnes.
		void Tally(bool sub_native, bool gate_open, bool chain_ran)
		{
			frames++;
			by_reason[Classify(sub_native, gate_open, chain_ran)]++;
		}

		// Cuadros sub-nativos = las tres casillas que no son "nativo". Definido
		// como suma, no como contador aparte: dos contadores para la misma
		// cantidad es la fuente de la que salio el bug de 0.6.4.
		uint64_t SubNative() const
		{
			return by_reason[kSubApagado] + by_reason[kSubHecho] + by_reason[kSubFallado];
		}

		// Invariante de cierre. Se expone como pregunta para que el .cpp la
		// imprima y el arnes la exija; si alguna vez deja de cumplirse, el log
		// tiene que decirlo en la misma linea.
		bool Closes() const
		{
			uint64_t sum = 0;
			for (unsigned i = 0; i < kReasonCount; i++)
				sum += by_reason[i];
			return sum == frames;
		}
	};
} // namespace cenit_present
