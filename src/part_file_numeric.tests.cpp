#include "../third_party/doctest/doctest.h"
#include "../include/TestSupport.h"

#include <limits>

#include "PartFileNumericSeams.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("Part file numeric seam derives ordinary AICH hashset sizes without changing the wire format")
{
	uint32 nHashSetSize = 0;

	CHECK(PartFileNumericSeams::TryDeriveAICHHashSetSize(20u, 4u, &nHashSetSize));
	CHECK_EQ(nHashSetSize, static_cast<uint32>(102u));
}

TEST_CASE("Part file numeric seam rejects AICH hashset sizes that overflow the serialized span")
{
	uint32 nHashSetSize = 0;

	CHECK_FALSE(PartFileNumericSeams::TryDeriveAICHHashSetSize((std::numeric_limits<size_t>::max)() / 2u, 2u, &nHashSetSize));
	CHECK_FALSE(PartFileNumericSeams::TryDeriveAICHHashSetSize(static_cast<size_t>((std::numeric_limits<uint32>::max)()), 1u, &nHashSetSize));
}

TEST_CASE("Part file numeric seam keeps rare-part source bucketing stable for ordinary counts and clamps impossible ones")
{
	CHECK_EQ(PartFileNumericSeams::CalculateRareChunkSourceLimit(0u), static_cast<uint16>(3u));
	CHECK_EQ(PartFileNumericSeams::CalculateRareChunkSourceLimit(21u), static_cast<uint16>(3u));
	CHECK_EQ(PartFileNumericSeams::CalculateRareChunkSourceLimit(31u), static_cast<uint16>(4u));
	CHECK_EQ(PartFileNumericSeams::CalculateRareChunkSourceLimit((std::numeric_limits<size_t>::max)()), (std::numeric_limits<uint16>::max)());
}

TEST_CASE("Part file numeric seam preserves the rounded-up completion percentages and bounds them safely")
{
	CHECK_EQ(PartFileNumericSeams::CalculateChunkCompletionPercent(0u, 9728000u), static_cast<uint16>(0u));
	CHECK_EQ(PartFileNumericSeams::CalculateChunkCompletionPercent(1u, 3u), static_cast<uint16>(34u));
	CHECK_EQ(PartFileNumericSeams::CalculateChunkCompletionPercent(2u, 3u), static_cast<uint16>(67u));
	CHECK_EQ(PartFileNumericSeams::CalculateChunkCompletionPercent(9728000u, 9728000u), static_cast<uint16>(100u));
	CHECK_EQ(PartFileNumericSeams::CalculateChunkCompletionPercent(9728001u, 9728000u), static_cast<uint16>(100u));
	CHECK_EQ(PartFileNumericSeams::CalculateChunkCompletionPercent(1u, 0u), static_cast<uint16>(0u));
}

TEST_CASE("Part file numeric seam clamps list counts and 32-bit scores before narrowing to uint16")
{
	CHECK_EQ(PartFileNumericSeams::ClampCountToUInt16(-1), static_cast<uint16>(0u));
	CHECK_EQ(PartFileNumericSeams::ClampCountToUInt16(42), static_cast<uint16>(42u));
	CHECK_EQ(PartFileNumericSeams::ClampCountToUInt16(static_cast<INT_PTR>((std::numeric_limits<uint16>::max)()) + 1), (std::numeric_limits<uint16>::max)());

	CHECK_EQ(PartFileNumericSeams::ClampUInt32ToUInt16(17u), static_cast<uint16>(17u));
	CHECK_EQ(PartFileNumericSeams::ClampUInt32ToUInt16(static_cast<uint32>((std::numeric_limits<uint16>::max)()) + 1u), (std::numeric_limits<uint16>::max)());
}

TEST_SUITE_END;
