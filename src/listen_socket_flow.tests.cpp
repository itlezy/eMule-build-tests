#include "../third_party/doctest/doctest.h"

#include <tchar.h>

#include "ListenSocketFlowSeams.h"
#include "ListenSocketSeams.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("Listen socket flow keeps the listener alive across parse errors and unknown exceptions")
{
	ListenSocketFlowState state = CreateListenSocketFlowState();

	CHECK(AdvanceListenSocketFlow(state, ListenSocketFlowEvent::AcceptReady).bShouldAcceptConnection);
	CHECK(AdvanceListenSocketFlow(state, ListenSocketFlowEvent::InvalidPacket).bShouldReportParseError);
	CHECK(state.bParseErrorSeen);
	CHECK(state.bSocketOpen);

	const ListenSocketFlowAction action = AdvanceListenSocketFlow(state, ListenSocketFlowEvent::UnknownException);
	CHECK(action.bShouldFallbackToUnknownExceptionMessage);
	CHECK(action.bShouldKeepSocketOpen);
	CHECK(state.bUnknownExceptionSeen);
	CHECK(_tcscmp(GetListenSocketUnknownPacketExceptionMessage(), _T("Unknown exception")) == 0);
}

TEST_CASE("Listen socket flow resets cleanly after a closed connection")
{
	ListenSocketFlowState state = CreateListenSocketFlowState();

	(void)AdvanceListenSocketFlow(state, ListenSocketFlowEvent::AcceptReady);
	(void)AdvanceListenSocketFlow(state, ListenSocketFlowEvent::CloseConnection);
	CHECK(state.bConnectionClosed);
	CHECK_FALSE(state.bAcceptPending);

	(void)AdvanceListenSocketFlow(state, ListenSocketFlowEvent::Reset);
	CHECK(state.bSocketOpen);
	CHECK_FALSE(state.bConnectionClosed);
	CHECK_FALSE(state.bParseErrorSeen);
}

TEST_SUITE_END;
