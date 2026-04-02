#include "../third_party/doctest/doctest.h"
#include "../include/TestSupport.h"

#include "PartStatusOwnershipSeams.h"

#include <vector>

TEST_SUITE_BEGIN("parity");

TEST_CASE("Part-status ownership seam preserves the legacy NULL-for-empty raw view contract")
{
	std::vector<uint8> status;
	const uint8 *pEmptyView = PartStatusOwnershipSeams::GetRawStatusView(status);
	CHECK(pEmptyView == NULL);

	PartStatusOwnershipSeams::AssignPartStatus(status, 3u, 1u);
	const uint8 *pFilledView = PartStatusOwnershipSeams::GetRawStatusView(status);
	REQUIRE(pFilledView != NULL);
	CHECK(status[0] == static_cast<uint8>(1u));
	CHECK(status[2] == static_cast<uint8>(1u));
}

TEST_CASE("Part-status ownership seam clears the owned buffer and the synchronized part count together")
{
	std::vector<uint8> status;
	uint16 nPartCount = 4u;

	PartStatusOwnershipSeams::AssignPartStatus(status, nPartCount, 0u);
	PartStatusOwnershipSeams::ClearPartStatus(status, nPartCount);

	CHECK(status.empty());
	CHECK_EQ(nPartCount, static_cast<uint16>(0u));
	const uint8 *pClearedView = PartStatusOwnershipSeams::GetRawStatusView(status);
	CHECK(pClearedView == NULL);
}

TEST_CASE("Part-status ownership seam builds bounded pending overlays and display buffers")
{
	std::vector<char> overlay;
	REQUIRE(PartStatusOwnershipSeams::TryBuildPendingPartOverlay(5u, overlay));
	CHECK_EQ(overlay.size(), static_cast<size_t>(5u));
	CHECK(overlay[0] == 0);

	std::vector<uint8> status = {1u, 0u, 1u};
	std::vector<TCHAR> display;
	REQUIRE(PartStatusOwnershipSeams::TryBuildPartStatusDisplay(status, display));
	CHECK_EQ(display.size(), static_cast<size_t>(4u));
	CHECK(display[0] == _T('#'));
	CHECK(display[1] == _T('.'));
	CHECK(display[2] == _T('#'));
	CHECK(display[3] == _T('\0'));
}

TEST_SUITE_END;
