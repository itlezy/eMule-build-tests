#include "../third_party/doctest/doctest.h"

#include "TestSupport.h"
#include "EncryptedDatagramFramingSeams.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("UDP obfuscation framing classifies plain protocol markers as pass-through traffic")
{
	const EncryptedDatagramFrameSnapshot snapshot = InspectEncryptedDatagramFrame(OP_EMULEPROT, 32u, false);

	CHECK(snapshot.eMarkerKind == EEncryptedDatagramMarkerKind::PlainProtocol);
	CHECK_FALSE(snapshot.bMarkerAllowed);
	CHECK_FALSE(snapshot.bRequiresVerifyKeys);
	CHECK(snapshot.bHeaderLongEnough);
}

TEST_CASE("UDP obfuscation framing distinguishes ed2k, Kad node-id, and Kad receiver-key candidates")
{
	const EncryptedDatagramFrameSnapshot ed2k = InspectEncryptedDatagramFrame(0x05u, 8u, false);
	const EncryptedDatagramFrameSnapshot kadNodeId = InspectEncryptedDatagramFrame(0x10u, 16u, false);
	const EncryptedDatagramFrameSnapshot kadReceiverKey = InspectEncryptedDatagramFrame(0x12u, 16u, false);

	CHECK(ed2k.eMarkerKind == EEncryptedDatagramMarkerKind::Ed2kCandidate);
	CHECK_FALSE(ed2k.bRequiresVerifyKeys);
	CHECK_EQ(ed2k.nExpectedOverhead, 8u);

	CHECK(kadNodeId.eMarkerKind == EEncryptedDatagramMarkerKind::KadNodeIdCandidate);
	CHECK(kadNodeId.bRequiresVerifyKeys);
	CHECK_EQ(kadNodeId.nExpectedOverhead, 16u);

	CHECK(kadReceiverKey.eMarkerKind == EEncryptedDatagramMarkerKind::KadReceiverKeyCandidate);
	CHECK(kadReceiverKey.bRequiresVerifyKeys);
	CHECK_EQ(kadReceiverKey.nExpectedOverhead, 16u);
}

TEST_CASE("UDP obfuscation framing rejects truncated candidate datagrams before payload parsing")
{
	const EncryptedDatagramFrameSnapshot kadShort = InspectEncryptedDatagramFrame(0x12u, 15u, false);
	const EncryptedDatagramFrameSnapshot serverShort = InspectEncryptedDatagramFrame(0x34u, 7u, true);

	CHECK_FALSE(kadShort.bHeaderLongEnough);
	CHECK_FALSE(serverShort.bHeaderLongEnough);
	CHECK(serverShort.eMarkerKind == EEncryptedDatagramMarkerKind::ServerCandidate);
}

TEST_SUITE_END;
