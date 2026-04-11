#include "../third_party/doctest/doctest.h"

#include "UploadQueueSeams.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("Upload queue seam classifies only active non-retired entries with a client as live")
{
	CHECK_EQ(ClassifyUploadQueueEntryAccess(true, false, true), uploadQueueEntryLive);
	CHECK_EQ(ClassifyUploadQueueEntryAccess(true, true, true), uploadQueueEntryRetired);
	CHECK_EQ(ClassifyUploadQueueEntryAccess(true, false, false), uploadQueueEntryRetired);
	CHECK_EQ(ClassifyUploadQueueEntryAccess(false, false, true), uploadQueueEntryMissing);
}

TEST_CASE("Upload queue seam reclaims retired entries only after pending IO drains")
{
	CHECK(CanReclaimUploadQueueEntry(true, 0));
	CHECK_FALSE(CanReclaimUploadQueueEntry(true, 1));
	CHECK_FALSE(CanReclaimUploadQueueEntry(false, 0));
}

TEST_CASE("Upload queue seam prefers strictly higher effective scores for next-client selection and cache updates")
{
	CHECK(PreferHigherUploadQueueScore(11u, 10u));
	CHECK_FALSE(PreferHigherUploadQueueScore(10u, 10u));
	CHECK_FALSE(PreferHigherUploadQueueScore(9u, 10u));

	uint32_t uMaxScore = 3u;
	UpdateUploadQueueMaxScore(uMaxScore, 7u);
	CHECK_EQ(uMaxScore, 7u);
	UpdateUploadQueueMaxScore(uMaxScore, 5u);
	CHECK_EQ(uMaxScore, 7u);
}

TEST_CASE("Upload queue seam increments waiting rank only for strictly higher scores")
{
	CHECK_EQ(AddHigherUploadQueueScoreToRank(1u, 11u, 10u), 2u);
	CHECK_EQ(AddHigherUploadQueueScoreToRank(2u, 10u, 10u), 2u);
	CHECK_EQ(AddHigherUploadQueueScoreToRank(2u, 9u, 10u), 2u);
}

TEST_CASE("Upload queue seam keeps soft-limit admission on combined credit and file-priority, not full effective score")
{
	CHECK(RejectSoftQueueCandidateByCombinedScore(false, true, false, 40.0f, 50.0f));
	CHECK_FALSE(RejectSoftQueueCandidateByCombinedScore(false, true, true, 40.0f, 50.0f));
	CHECK_FALSE(RejectSoftQueueCandidateByCombinedScore(false, false, false, 40.0f, 50.0f));
	CHECK(RejectSoftQueueCandidateByCombinedScore(true, false, true, 90.0f, 10.0f));
}

TEST_SUITE_END;
