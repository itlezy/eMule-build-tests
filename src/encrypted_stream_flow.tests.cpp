#include "../third_party/doctest/doctest.h"

#include "EncryptedStreamSocketFlowSeams.h"
#include "EncryptedStreamSocketSeams.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("Encrypted stream flow completes delayed send only after the negotiation buffer drains")
{
	EncryptedStreamFlowState state = {true, true, false, false};

	EncryptedStreamFlowAction action = AdvanceEncryptedStreamFlow(state, EncryptedStreamFlowEvent::FlushNegotiationBuffer);
	CHECK_FALSE(action.bShouldCompleteDelayedSend);

	state.bHasPendingNegotiationBuffer = false;
	action = AdvanceEncryptedStreamFlow(state, EncryptedStreamFlowEvent::FlushNegotiationBuffer);
	CHECK(action.bShouldCompleteDelayedSend);
	CHECK(EncryptedStreamSocketSeams::ShouldCompleteDelayedServerSendAfterFlush(state.bDelayedServerSendState, state.bHasPendingNegotiationBuffer));
}

TEST_CASE("Encrypted stream flow marks handshake failure on wrong magic or unsupported methods")
{
	EncryptedStreamFlowState state = {true, true, false, false};

	EncryptedStreamFlowAction action = AdvanceEncryptedStreamFlow(state, EncryptedStreamFlowEvent::ReceiveWrongMagic);
	CHECK(action.bShouldFailHandshake);
	CHECK(state.bHandshakeFailed);
	CHECK_FALSE(state.bDelayedServerSendState);

	state = {true, true, false, false};
	action = AdvanceEncryptedStreamFlow(state, EncryptedStreamFlowEvent::ReceiveUnsupportedMethod);
	CHECK(action.bShouldFailHandshake);
	CHECK(state.bHandshakeFailed);
	CHECK_FALSE(state.bDelayedServerSendState);
}

TEST_CASE("Encrypted stream flow seals successful negotiation and clears delayed-send buffering")
{
	EncryptedStreamFlowState state = {true, true, false, false};

	const EncryptedStreamFlowAction action = AdvanceEncryptedStreamFlow(state, EncryptedStreamFlowEvent::ReceiveHandshakeSuccess);
	CHECK(action.bShouldMarkHandshakeFinished);
	CHECK(state.bHandshakeFinished);
	CHECK_FALSE(state.bDelayedServerSendState);
	CHECK_FALSE(state.bHasPendingNegotiationBuffer);
}

TEST_SUITE_END;
