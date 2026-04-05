#include "../third_party/doctest/doctest.h"

#include "UploadBandwidthThrottlerFlowSeams.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("Upload throttler flow wakes once, merges pending work, and drains priority before normal sockets")
{
	UploadBandwidthFlowState state = CreateUploadBandwidthFlowState();

	CHECK(AdvanceUploadBandwidthFlow(state, UploadBandwidthFlowEvent::EnqueuePriority).bShouldWakeScheduler);
	CHECK_FALSE(AdvanceUploadBandwidthFlow(state, UploadBandwidthFlowEvent::EnqueueNormal).bShouldWakeScheduler);
	CHECK_EQ(state.nPendingPriorityQueueCount, 1u);
	CHECK_EQ(state.nPendingNormalQueueCount, 1u);

	(void)AdvanceUploadBandwidthFlow(state, UploadBandwidthFlowEvent::MergePending);
	CHECK_EQ(state.nPriorityQueueCount, 1u);
	CHECK_EQ(state.nNormalQueueCount, 1u);

	CHECK(AdvanceUploadBandwidthFlow(state, UploadBandwidthFlowEvent::PopNext).bShouldSendPrioritySocket);
	CHECK(AdvanceUploadBandwidthFlow(state, UploadBandwidthFlowEvent::PopNext).bShouldSendNormalSocket);
	CHECK_FALSE(state.bSchedulerActive);
}

TEST_CASE("Upload throttler flow clears every queue domain on removal or shutdown")
{
	UploadBandwidthFlowState state = CreateUploadBandwidthFlowState();

	(void)AdvanceUploadBandwidthFlow(state, UploadBandwidthFlowEvent::EnqueuePriority);
	(void)AdvanceUploadBandwidthFlow(state, UploadBandwidthFlowEvent::EnqueueNormal);
	(void)AdvanceUploadBandwidthFlow(state, UploadBandwidthFlowEvent::MergePending);
	CHECK_EQ(state.nPriorityQueueCount, 1u);
	CHECK_EQ(state.nNormalQueueCount, 1u);

	CHECK(AdvanceUploadBandwidthFlow(state, UploadBandwidthFlowEvent::RemoveAllQueuedCopies).bShouldClearQueues);
	CHECK_EQ(state.nPriorityQueueCount, 0u);
	CHECK_EQ(state.nNormalQueueCount, 0u);

	(void)AdvanceUploadBandwidthFlow(state, UploadBandwidthFlowEvent::EnqueuePriority);
	CHECK(AdvanceUploadBandwidthFlow(state, UploadBandwidthFlowEvent::Shutdown).bShouldClearQueues);
	CHECK_FALSE(state.bSchedulerActive);
}

TEST_SUITE_END;
