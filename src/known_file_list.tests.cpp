#include "../third_party/doctest/doctest.h"
#include "../../eMule-build/eMule/srchybrid/KnownFileListSeams.h"

TEST_CASE("Known-file AICH purge seam keeps active hashsets for current files")
{
	CHECK_FALSE(ShouldPurgeKnownAICHHashset(true, false));
}

TEST_CASE("Known-file AICH purge seam drops hashsets for partially purged files")
{
	CHECK(ShouldPurgeKnownAICHHashset(true, true));
}

TEST_CASE("Known-file AICH purge seam drops orphaned hashsets without asserting")
{
	CHECK(ShouldPurgeKnownAICHHashset(false, false));
}
