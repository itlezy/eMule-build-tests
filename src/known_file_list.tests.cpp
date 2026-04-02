#include "../third_party/doctest/doctest.h"
#include "../../eMule-build/eMule/srchybrid/KnownFileListSeams.h"
#include "../../eMule-build/eMule/srchybrid/KnownFileProgressSeams.h"

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

TEST_CASE("Known-file progress seam posts only for matching known-file owners")
{
	CHECK(IsCompatibleKnownFileProgressOwner(true, 1024u, 1024u));
	CHECK_FALSE(IsCompatibleKnownFileProgressOwner(false, 1024u, 1024u));
	CHECK_FALSE(IsCompatibleKnownFileProgressOwner(true, 1024u, 2048u));
}
