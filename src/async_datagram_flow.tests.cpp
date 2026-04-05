#include "../third_party/doctest/doctest.h"

#include "AsyncDatagramSocketFlowSeams.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("Async datagram flow coalesces receive and send wakeups behind one posted dispatch")
{
	AsyncDatagramFlowState state = CreateAsyncDatagramFlowState();

	AsyncDatagramFlowAction action = AdvanceAsyncDatagramFlow(state, AsyncDatagramFlowEvent::ReceiveReady);
	CHECK(action.bShouldPostDispatch);
	CHECK(state.bReceivePending);
	CHECK(state.bDispatchPosted);

	action = AdvanceAsyncDatagramFlow(state, AsyncDatagramFlowEvent::SendReady);
	CHECK_FALSE(action.bShouldPostDispatch);
	CHECK(state.bReceivePending);
	CHECK(state.bSendPending);
	CHECK(state.bDispatchPosted);

	action = AdvanceAsyncDatagramFlow(state, AsyncDatagramFlowEvent::Dispatch);
	CHECK(action.bShouldDispatchReceive);
	CHECK(action.bShouldDispatchSend);
	CHECK_FALSE(state.bReceivePending);
	CHECK_FALSE(state.bSendPending);
	CHECK_FALSE(state.bDispatchPosted);
}

TEST_CASE("Async datagram flow ignores redundant write-interest toggles and only refreshes live sockets")
{
	AsyncDatagramFlowState state = CreateAsyncDatagramFlowState();

	AsyncDatagramFlowAction action = AdvanceAsyncDatagramFlow(state, AsyncDatagramFlowEvent::EnableWriteInterest);
	CHECK(action.bShouldRefreshAsyncSelect);
	CHECK(state.bWriteInterestEnabled);

	action = AdvanceAsyncDatagramFlow(state, AsyncDatagramFlowEvent::EnableWriteInterest);
	CHECK_FALSE(action.bShouldRefreshAsyncSelect);

	action = AdvanceAsyncDatagramFlow(state, AsyncDatagramFlowEvent::DisableWriteInterest);
	CHECK(action.bShouldRefreshAsyncSelect);
	CHECK_FALSE(state.bWriteInterestEnabled);

	(void)AdvanceAsyncDatagramFlow(state, AsyncDatagramFlowEvent::CloseSocket);
	action = AdvanceAsyncDatagramFlow(state, AsyncDatagramFlowEvent::EnableWriteInterest);
	CHECK_FALSE(action.bShouldRefreshAsyncSelect);
	CHECK(state.bWriteInterestEnabled);
}

TEST_CASE("Async datagram flow clears pending work on close and leaves later dispatches inert")
{
	AsyncDatagramFlowState state = CreateAsyncDatagramFlowState();

	(void)AdvanceAsyncDatagramFlow(state, AsyncDatagramFlowEvent::ReceiveReady);
	(void)AdvanceAsyncDatagramFlow(state, AsyncDatagramFlowEvent::SendReady);
	(void)AdvanceAsyncDatagramFlow(state, AsyncDatagramFlowEvent::CloseSocket);

	CHECK_FALSE(state.bSocketOpen);
	CHECK_FALSE(state.bReceivePending);
	CHECK_FALSE(state.bSendPending);
	CHECK_FALSE(state.bDispatchPosted);

	const AsyncDatagramFlowAction action = AdvanceAsyncDatagramFlow(state, AsyncDatagramFlowEvent::Dispatch);
	CHECK_FALSE(action.bShouldDispatchReceive);
	CHECK_FALSE(action.bShouldDispatchSend);
}

TEST_SUITE_END;
