#include "../third_party/doctest/doctest.h"
#include "../include/TestSupport.h"

#include "CollectionSeams.h"

#include <limits>

TEST_SUITE_BEGIN("parity");

TEST_CASE("Collection seam builds exact message and signature spans from serialized data")
{
	CollectionSignatureLayout layout = {};
	CHECK(TryBuildCollectionSignatureLayout(24u, 16u, layout));
	CHECK(layout.nMessageLength == 16u);
	CHECK(layout.nSignatureLength == 8u);
}

TEST_CASE("Collection seam rejects impossible signature layouts and oversized serialized lengths")
{
	CollectionSignatureLayout layout = {};
	CHECK_FALSE(TryBuildCollectionSignatureLayout(16u, 16u, layout));
	CHECK_FALSE(TryBuildCollectionSignatureLayout(15u, 16u, layout));
	CHECK_FALSE(TryBuildCollectionSignatureLayout(static_cast<ULONGLONG>((std::numeric_limits<UINT>::max)()) + 1ull, 16u, layout));
}

TEST_CASE("Collection seam validates signed payload length conversion for on-disk writes")
{
	uint32 dwSerializedLength = 0;
	CHECK(TryConvertCollectionSerializedLength(4096u, dwSerializedLength));
	CHECK(dwSerializedLength == 4096u);
	CHECK_FALSE(TryConvertCollectionSerializedLength(static_cast<ULONGLONG>((std::numeric_limits<uint32>::max)()) + 1ull, dwSerializedLength));
}

TEST_CASE("Collection seam keeps malformed single-entry imports skippable")
{
	CHECK(ShouldContinueAfterCollectionEntryFailure());
}

TEST_SUITE_END;
