#include "../third_party/doctest/doctest.h"
#include "KnownFileListSeams.h"
#include "KnownFileProgressSeams.h"

TEST_SUITE_BEGIN("parity");

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

TEST_CASE("Known-file AICH purge seam drops orphaned partially purged hashsets too")
{
	CHECK(ShouldPurgeKnownAICHHashset(false, true));
}

TEST_CASE("Known-file progress seam posts only for matching known-file owners")
{
	CHECK(IsCompatibleKnownFileProgressOwner(true, 1024u, 1024u));
	CHECK_FALSE(IsCompatibleKnownFileProgressOwner(false, 1024u, 1024u));
	CHECK_FALSE(IsCompatibleKnownFileProgressOwner(true, 1024u, 2048u));
}

TEST_CASE("Known-file progress seam accepts zero-length owners and rejects stale owners")
{
	CHECK(IsCompatibleKnownFileProgressOwner(true, 0u, 0u));
	CHECK_FALSE(IsCompatibleKnownFileProgressOwner(false, 0u, 0u));
	CHECK_FALSE(IsCompatibleKnownFileProgressOwner(true, 0u, 1u));
}

TEST_SUITE_END;
