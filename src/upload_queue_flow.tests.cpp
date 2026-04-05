#include "../third_party/doctest/doctest.h"

#include "UploadQueueFlowSeams.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("Upload queue flow keeps retired entries unreclaimable until pending IO drains")
{
	UploadQueueFlowState state = CreateUploadQueueFlowState();

	CHECK_EQ(AdvanceUploadQueueFlow(state, UploadQueueFlowEvent::AddLiveEntry).eAccessState, uploadQueueEntryLive);
	CHECK_EQ(AdvanceUploadQueueFlow(state, UploadQueueFlowEvent::MarkRetired).eAccessState, uploadQueueEntryRetired);

	(void)AdvanceUploadQueueFlow(state, UploadQueueFlowEvent::QueuePendingIO);
	UploadQueueFlowAction action = AdvanceUploadQueueFlow(state, UploadQueueFlowEvent::ReclaimIfSafe);
	CHECK_FALSE(action.bShouldReclaim);
	CHECK_FALSE(state.bReclaimed);

	(void)AdvanceUploadQueueFlow(state, UploadQueueFlowEvent::CompletePendingIO);
	action = AdvanceUploadQueueFlow(state, UploadQueueFlowEvent::ReclaimIfSafe);
	CHECK(action.bShouldReclaim);
	CHECK(state.bReclaimed);
}

TEST_CASE("Upload queue flow demotes dropped clients and keeps removed entries missing")
{
	UploadQueueFlowState state = CreateUploadQueueFlowState();

	CHECK_EQ(AdvanceUploadQueueFlow(state, UploadQueueFlowEvent::AddLiveEntry).eAccessState, uploadQueueEntryLive);
	CHECK_EQ(AdvanceUploadQueueFlow(state, UploadQueueFlowEvent::DropClient).eAccessState, uploadQueueEntryRetired);
	CHECK_FALSE(AdvanceUploadQueueFlow(state, UploadQueueFlowEvent::ReclaimIfSafe).bShouldReclaim);

	CHECK_EQ(AdvanceUploadQueueFlow(state, UploadQueueFlowEvent::RemoveEntry).eAccessState, uploadQueueEntryMissing);
	CHECK_FALSE(AdvanceUploadQueueFlow(state, UploadQueueFlowEvent::ReclaimIfSafe).bShouldReclaim);
}

TEST_SUITE_END;
