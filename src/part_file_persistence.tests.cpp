#include "../third_party/doctest/doctest.h"

#include "PartFilePersistenceSeams.h"

TEST_SUITE_BEGIN("parity");

#if defined(EMULE_TEST_HAVE_PART_FILE_PERSISTENCE_SEAMS)
TEST_CASE("Part-file persistence seam blocks metadata writes when the free-space floor is not met")
{
	CHECK_FALSE(PartFilePersistenceSeams::CanWritePartMetWithFreeSpace(0u));
	CHECK_FALSE(PartFilePersistenceSeams::CanWritePartMetWithFreeSpace(PartFilePersistenceSeams::kMinPartMetWriteFreeBytes - 1u));
	CHECK(PartFilePersistenceSeams::CanWritePartMetWithFreeSpace(PartFilePersistenceSeams::kMinPartMetWriteFreeBytes));
	CHECK(PartFilePersistenceSeams::CanWritePartMetWithFreeSpace(PartFilePersistenceSeams::kMinPartMetWriteFreeBytes + 1u));
}

TEST_CASE("Part-file persistence seam reuses cached disk-space decisions only until a refresh is forced")
{
	CHECK_FALSE(PartFilePersistenceSeams::ShouldReusePartMetWriteCache(false, false));
	CHECK(PartFilePersistenceSeams::ShouldReusePartMetWriteCache(true, false));
	CHECK_FALSE(PartFilePersistenceSeams::ShouldReusePartMetWriteCache(true, true));
}

TEST_CASE("Part-file persistence seam skips destructor flushes only during shutdown after the write thread is gone")
{
	CHECK(PartFilePersistenceSeams::ShouldFlushPartFileOnDestroy(false, false, false));
	CHECK(PartFilePersistenceSeams::ShouldFlushPartFileOnDestroy(true, true, true));
	CHECK_FALSE(PartFilePersistenceSeams::ShouldFlushPartFileOnDestroy(true, false, false));
	CHECK_FALSE(PartFilePersistenceSeams::ShouldFlushPartFileOnDestroy(true, true, false));
}
#endif

TEST_SUITE_END;
