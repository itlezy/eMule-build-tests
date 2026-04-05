#include "../third_party/doctest/doctest.h"

#include "WorkerUiMessageFlowSeams.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("Worker UI flow coalesces repeated queued payloads behind one wakeup until the UI drains them")
{
	WorkerUiFlowState state = CreateWorkerUiFlowState();

	CHECK(AdvanceWorkerUiFlow(state, WorkerUiFlowEvent::QueuePayload).bShouldPostMessage);
	CHECK_FALSE(AdvanceWorkerUiFlow(state, WorkerUiFlowEvent::QueuePayload).bShouldPostMessage);
	CHECK_EQ(state.nQueuedPayloads, 2u);
	CHECK(state.bWakeupPosted);

	CHECK(AdvanceWorkerUiFlow(state, WorkerUiFlowEvent::UiConsumesWakeup).bShouldTransferPayloadToUi);
	CHECK(state.bPayloadAvailableToUi);
	(void)AdvanceWorkerUiFlow(state, WorkerUiFlowEvent::UiTakesPayload);
	CHECK_EQ(state.nQueuedPayloads, 1u);
	CHECK(state.bPayloadQueued);
}

TEST_CASE("Worker UI flow rejects posts once the owner enters close or the window is destroyed")
{
	WorkerUiFlowState state = CreateWorkerUiFlowState();

	(void)AdvanceWorkerUiFlow(state, WorkerUiFlowEvent::BeginClose);
	CHECK(AdvanceWorkerUiFlow(state, WorkerUiFlowEvent::QueuePayload).bShouldRejectPost);
	CHECK(AdvanceWorkerUiFlow(state, WorkerUiFlowEvent::PostPlainMessage).bShouldRejectPost);

	state = CreateWorkerUiFlowState();
	(void)AdvanceWorkerUiFlow(state, WorkerUiFlowEvent::DestroyWindow);
	CHECK(AdvanceWorkerUiFlow(state, WorkerUiFlowEvent::QueuePayload).bShouldRejectPost);
	CHECK_FALSE(state.bWindowAlive);
}

TEST_CASE("Worker UI flow destroys queued payloads during teardown and ignores stale wakeups")
{
	WorkerUiFlowState state = CreateWorkerUiFlowState();

	(void)AdvanceWorkerUiFlow(state, WorkerUiFlowEvent::QueuePayload);
	(void)AdvanceWorkerUiFlow(state, WorkerUiFlowEvent::QueuePayload);
	CHECK_EQ(state.nQueuedPayloads, 2u);

	CHECK(AdvanceWorkerUiFlow(state, WorkerUiFlowEvent::DiscardOwnerPayloads).bShouldDestroyPayload);
	CHECK_EQ(state.nQueuedPayloads, 0u);
	CHECK(state.bPayloadDestroyed);

	const WorkerUiFlowAction action = AdvanceWorkerUiFlow(state, WorkerUiFlowEvent::UiConsumesWakeup);
	CHECK_FALSE(action.bShouldTransferPayloadToUi);
	CHECK_FALSE(state.bPayloadAvailableToUi);
}

TEST_SUITE_END;
