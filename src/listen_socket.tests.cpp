#include "../third_party/doctest/doctest.h"
#include "../include/TestSupport.h"

#include "ListenSocketSeams.h"

#include <tchar.h>

TEST_SUITE_BEGIN("parity");

TEST_CASE("Listen socket seam keeps the parse-boundary fallback message deterministic")
{
	CHECK(_tcscmp(GetListenSocketUnknownPacketExceptionMessage(), _T("Unknown exception")) == 0);
}

TEST_SUITE_END;
