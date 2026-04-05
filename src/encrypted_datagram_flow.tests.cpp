#include "../third_party/doctest/doctest.h"

#include "EncryptedDatagramSocketFlowSeams.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("Encrypted datagram flow keeps plain protocol markers on the pass-through path")
{
	EncryptedDatagramFlowState state = CreateEncryptedDatagramFlowState();

	const EncryptedDatagramFlowAction action = AdvanceEncryptedDatagramFlow(state, EncryptedDatagramFlowEvent::ObservePlainProtocolMarker);
	CHECK(action.bShouldPassThrough);
	CHECK(state.bPlainProtocolDetected);
	CHECK(state.bPassedThrough);
	CHECK_FALSE(state.bPayloadReady);
}

TEST_CASE("Encrypted datagram flow exposes ED2K payload only after successful decrypt")
{
	EncryptedDatagramFlowState state = CreateEncryptedDatagramFlowState();

	CHECK(AdvanceEncryptedDatagramFlow(state, EncryptedDatagramFlowEvent::ObserveEncryptedCandidate).bShouldAttemptDecrypt);
	(void)AdvanceEncryptedDatagramFlow(state, EncryptedDatagramFlowEvent::MagicMatchedEd2k);
	const EncryptedDatagramFlowAction action = AdvanceEncryptedDatagramFlow(state, EncryptedDatagramFlowEvent::PayloadDecrypted);

	CHECK(action.bShouldExposePayload);
	CHECK(action.bShouldRecordCryptOverhead);
	CHECK(state.bPayloadReady);
	CHECK(state.bCryptOverheadRecorded);
	CHECK_FALSE(state.bPassedThrough);
}

TEST_CASE("Encrypted datagram flow requires Kad verify keys before exposing payload")
{
	EncryptedDatagramFlowState state = CreateEncryptedDatagramFlowState();

	(void)AdvanceEncryptedDatagramFlow(state, EncryptedDatagramFlowEvent::ObserveEncryptedCandidate);
	(void)AdvanceEncryptedDatagramFlow(state, EncryptedDatagramFlowEvent::MagicMatchedKadReceiverKey);
	CHECK(state.bKadPacket);
	CHECK(state.bKadReceiverKeyMode);
	CHECK(state.bVerifyKeysExpected);

	CHECK(AdvanceEncryptedDatagramFlow(state, EncryptedDatagramFlowEvent::VerifyKeysPresent).bShouldConsumeVerifyKeys);
	const EncryptedDatagramFlowAction action = AdvanceEncryptedDatagramFlow(state, EncryptedDatagramFlowEvent::PayloadDecrypted);
	CHECK(action.bShouldExposePayload);
	CHECK(state.bVerifyKeysConsumed);
}

TEST_CASE("Encrypted datagram flow falls back to pass-through on Kad verify-key and padding failures")
{
	EncryptedDatagramFlowState state = CreateEncryptedDatagramFlowState();

	(void)AdvanceEncryptedDatagramFlow(state, EncryptedDatagramFlowEvent::ObserveEncryptedCandidate);
	(void)AdvanceEncryptedDatagramFlow(state, EncryptedDatagramFlowEvent::MagicMatchedKadNodeId);
	CHECK(AdvanceEncryptedDatagramFlow(state, EncryptedDatagramFlowEvent::VerifyKeysMissing).bShouldPassThrough);
	CHECK(state.bPassedThrough);

	state = CreateEncryptedDatagramFlowState();
	(void)AdvanceEncryptedDatagramFlow(state, EncryptedDatagramFlowEvent::ObserveEncryptedCandidate);
	CHECK(AdvanceEncryptedDatagramFlow(state, EncryptedDatagramFlowEvent::PaddingTooLarge).bShouldPassThrough);
	CHECK(state.bPassedThrough);
}

TEST_CASE("Encrypted datagram flow can be reset after pass-through and decode a later ED2K payload")
{
	EncryptedDatagramFlowState state = CreateEncryptedDatagramFlowState();

	(void)AdvanceEncryptedDatagramFlow(state, EncryptedDatagramFlowEvent::ObserveEncryptedCandidate);
	CHECK(AdvanceEncryptedDatagramFlow(state, EncryptedDatagramFlowEvent::MagicMismatch).bShouldPassThrough);
	CHECK(state.bPassedThrough);

	(void)AdvanceEncryptedDatagramFlow(state, EncryptedDatagramFlowEvent::Reset);
	CHECK_FALSE(state.bPassedThrough);
	CHECK_FALSE(state.bDecryptAttempted);

	CHECK(AdvanceEncryptedDatagramFlow(state, EncryptedDatagramFlowEvent::ObserveEncryptedCandidate).bShouldAttemptDecrypt);
	(void)AdvanceEncryptedDatagramFlow(state, EncryptedDatagramFlowEvent::MagicMatchedEd2k);
	CHECK(AdvanceEncryptedDatagramFlow(state, EncryptedDatagramFlowEvent::PayloadDecrypted).bShouldExposePayload);
	CHECK(state.bPayloadReady);
}

TEST_CASE("Encrypted datagram flow requires verify keys before Kad payload decryption becomes visible")
{
	EncryptedDatagramFlowState state = CreateEncryptedDatagramFlowState();

	(void)AdvanceEncryptedDatagramFlow(state, EncryptedDatagramFlowEvent::ObserveEncryptedCandidate);
	(void)AdvanceEncryptedDatagramFlow(state, EncryptedDatagramFlowEvent::MagicMatchedKadNodeId);
	const EncryptedDatagramFlowAction action = AdvanceEncryptedDatagramFlow(state, EncryptedDatagramFlowEvent::PayloadDecrypted);

	CHECK_FALSE(action.bShouldExposePayload);
	CHECK_FALSE(action.bShouldRecordCryptOverhead);
	CHECK_FALSE(state.bPayloadReady);
	CHECK_FALSE(state.bVerifyKeysConsumed);
}

TEST_SUITE_END;
