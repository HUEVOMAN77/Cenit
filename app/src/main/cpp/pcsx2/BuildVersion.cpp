// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "svnrev.h"

namespace BuildVersion
{
	const char* GitTag = GIT_TAG;
	bool GitTaggedCommit = GIT_TAGGED_COMMIT;
	int GitTagHi = GIT_TAG_HI;
	int GitTagMid = GIT_TAG_MID;
	int GitTagLo = GIT_TAG_LO;
	const char* GitRev = GIT_REV;
#ifndef GIT_SHORT
	// preBuild.cmd (Windows) no escribe GIT_SHORT en su svnrev.h. Sin esto,
	// ese compilador no podría ni incluir el archivo.
#define GIT_SHORT GIT_REV
#endif
	const char* GitShort = GIT_SHORT;
	const char* GitHash = GIT_HASH;
	const char* GitDate = GIT_DATE;

#ifndef CENIT_APP_VERSION
#define CENIT_APP_VERSION "unknown"
#endif
	const char* AppVersion = CENIT_APP_VERSION;
} // namespace BuildVersion
