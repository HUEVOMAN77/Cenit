// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

// This file provides the same information as svnrev.h except you don't need to
// recompile each object file using it when said information is updated.
namespace BuildVersion
{
	extern const char* GitTag;
	extern bool GitTaggedCommit;
	extern int GitTagHi;
	extern int GitTagMid;
	extern int GitTagLo;
	extern const char* GitRev;
	// Cenit 0.6.14: hash corto siempre de 7 caracteres. GitRev puede ser un
	// "git describe" largo (base-0.6.13-5-g2f2c92a) y en el HUD de un teléfono
	// no cabe; para el build ID inequívoco usamos este.
	extern const char* GitShort;
	extern const char* GitHash;
	extern const char* GitDate;

	// Cenit 0.6.14: versionName de la APK, inyectado por CMake desde Gradle. El
	// núcleo no la conocía, así que un HUD con "PCSX2 <rev>" no permitía
	// distinguir dos Cenit consecutivos. Vacía/"unknown" en builds sin Gradle.
	extern const char* AppVersion;
} // namespace BuildVersion
