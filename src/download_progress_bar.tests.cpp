#include "../third_party/doctest/doctest.h"

#include "DownloadProgressBarSeams.h"

TEST_SUITE_BEGIN("download_progress_bar");

TEST_CASE("Download progress-bar drawing requires a positive target extent")
{
	CHECK(DownloadProgressBarSeams::HasDrawableExtent(1, 1));
	CHECK(DownloadProgressBarSeams::HasDrawableExtent(120, 8));

	CHECK_FALSE(DownloadProgressBarSeams::HasDrawableExtent(0, 8));
	CHECK_FALSE(DownloadProgressBarSeams::HasDrawableExtent(120, 0));
	CHECK_FALSE(DownloadProgressBarSeams::HasDrawableExtent(-1, 8));
	CHECK_FALSE(DownloadProgressBarSeams::HasDrawableExtent(120, -1));
}

TEST_CASE("Download progress-bar drawing isolates DC state only for flat bars")
{
	CHECK(DownloadProgressBarSeams::ShouldIsolateFlatBarDcState(true));
	CHECK_FALSE(DownloadProgressBarSeams::ShouldIsolateFlatBarDcState(false));
}

TEST_SUITE_END();
