#include "../third_party/doctest/doctest.h"
#include "SharedFileListSeams.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("Shared file list accepts files from shared directories")
{
	CHECK(SharedFileListSeams::CanAddSharedFile(false, true, false));
}

TEST_CASE("Shared file list accepts explicitly single-shared files outside shared directories")
{
	CHECK(SharedFileListSeams::CanAddSharedFile(false, false, true));
}

TEST_CASE("Shared file list accepts part files outside shared directories")
{
	CHECK(SharedFileListSeams::CanAddSharedFile(true, false, false));
}

TEST_CASE("Shared file list rejects complete files that are neither directory-shared nor explicitly shared")
{
	CHECK_FALSE(SharedFileListSeams::CanAddSharedFile(false, false, false));
}
