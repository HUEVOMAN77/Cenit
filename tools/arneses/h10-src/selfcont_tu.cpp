// Harness10, pata 1/3 — AUTOCONTENCION.
//
// Este archivo NO toca el contrato: solo incluye UNA cabecera del area arm64 y no
// declara nada. Si la cabecera depende de que otro include aparezca antes (el clasico
// "compila dentro de microVU-arm64.cpp porque alli ya estaba incluido X"), aqui se
// cae. Es la comprobacion que el CI no hace: CI compila los .cpp suyos, no prueba las
// cabeceras solas.
//
// El ejecutor (baseline.sh, runh10) compila este TU una vez POR CADA cabecera,
// pasando por linea de ordenes cual header (-DH10_HEADER=...), asi que la regla
// anti-copia-vieja se mantiene: las cabeceras vienen del repo en cada corrida.

#if !defined(H10_HEADER)
#error "h10 pata 1: falta -DH10_HEADER=<cabecera del repo>"
#endif

#define H10_STR2(x) #x
#define H10_STR(x) H10_STR2(x)

#include H10_STR(H10_HEADER)

// Nada mas. La ausencia de errores ES el resultado del arnes.
