#include "../third_party/doctest/doctest.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>
#include <windows.h>

#include "TestSupport.h"
#include "EncryptedDatagramFramingSeams.h"
#include "EncryptedDatagramSequenceSeams.h"

namespace
{
	struct EncryptedDatagramScenario
	{
		std::vector<BYTE> bytes;
		bool bServerPacket;
	};

	struct EncryptedDatagramLoopResult
	{
		std::vector<size_t> payloadLengths;
		size_t nPassThroughCount;
	};

	void AppendUInt16LE(std::vector<BYTE> &rBytes, uint16_t value)
	{
		rBytes.push_back(static_cast<BYTE>(value & 0xFFu));
		rBytes.push_back(static_cast<BYTE>((value >> 8) & 0xFFu));
	}

	void AppendUInt32LE(std::vector<BYTE> &rBytes, uint32_t value)
	{
		rBytes.push_back(static_cast<BYTE>(value & 0xFFu));
		rBytes.push_back(static_cast<BYTE>((value >> 8) & 0xFFu));
		rBytes.push_back(static_cast<BYTE>((value >> 16) & 0xFFu));
		rBytes.push_back(static_cast<BYTE>((value >> 24) & 0xFFu));
	}

	std::vector<BYTE> BuildDeterministicEncryptedDatagram(
		uint8_t byProtocol,
		bool bKadPacket,
		const std::vector<BYTE> &rPayload,
		uint16_t nRandomKeyPart,
		uint32_t nMagicValue,
		uint32_t nReceiverVerifyKey = 0u,
		uint32_t nSenderVerifyKey = 0u)
	{
		std::vector<BYTE> bytes;
		bytes.push_back(byProtocol);
		AppendUInt16LE(bytes, nRandomKeyPart);
		AppendUInt32LE(bytes, nMagicValue);
		bytes.push_back(0u); // padding length
		if (bKadPacket) {
			AppendUInt32LE(bytes, nReceiverVerifyKey);
			AppendUInt32LE(bytes, nSenderVerifyKey);
		}
		bytes.insert(bytes.end(), rPayload.begin(), rPayload.end());
		return bytes;
	}

	std::filesystem::path CreateEncryptedDatagramTempPath()
	{
		WCHAR szTempPath[MAX_PATH] = {};
		WCHAR szTempFile[MAX_PATH] = {};
		REQUIRE(::GetTempPathW(_countof(szTempPath), szTempPath) != 0);
		REQUIRE(::GetTempFileNameW(szTempPath, L"eds", 0, szTempFile) != 0);
		return std::filesystem::path(szTempFile);
	}

	void WriteEncryptedDatagramFixture(const std::filesystem::path &rPath, const std::vector<BYTE> &rBytes)
	{
		std::ofstream stream(rPath, std::ios::binary | std::ios::trunc);
		REQUIRE(stream.is_open());
		stream.write(reinterpret_cast<const char*>(rBytes.data()), static_cast<std::streamsize>(rBytes.size()));
		REQUIRE(stream.good());
	}

	std::vector<size_t> ReplayEncryptedDatagramSequence(
		const std::vector<BYTE> &rDatagram,
		const std::vector<size_t> &rSliceSizes,
		bool bServerPacket,
		bool *pbPassedThrough = nullptr)
	{
		std::vector<size_t> payloadLengths;
		EncryptedDatagramSequenceState state = CreateEncryptedDatagramSequenceState();
		const EncryptedDatagramFrameSnapshot snapshot = InspectEncryptedDatagramFrame(rDatagram.front(), rDatagram.size(), bServerPacket);

		size_t nOffset = 0;
		for (size_t nSliceSize : rSliceSizes) {
			if (nOffset >= rDatagram.size())
				break;
			const size_t nReadableSlice = (nOffset + nSliceSize <= rDatagram.size()) ? nSliceSize : (rDatagram.size() - nOffset);
			const EncryptedDatagramSequenceAction action = AdvanceEncryptedDatagramSequence(state, rDatagram.size(), nReadableSlice, snapshot, true);
			nOffset += action.nBytesAccepted;
			if (action.bShouldPassThrough) {
				if (pbPassedThrough != nullptr)
					*pbPassedThrough = true;
				return payloadLengths;
			}
			if (action.bShouldExposePayload)
				payloadLengths.push_back(state.nPayloadBytesExpected);
		}

		if (pbPassedThrough != nullptr)
			*pbPassedThrough = state.bPassedThrough;
		return payloadLengths;
	}

	std::vector<BYTE> ReplayEncryptedDatagramSendSequence(
		const std::vector<BYTE> &rDatagram,
		const std::vector<size_t> &rSliceBudgets,
		bool bServerPacket,
		bool *pbPassedThrough = nullptr)
	{
		std::vector<BYTE> emittedBytes;
		const EncryptedDatagramFrameSnapshot snapshot = InspectEncryptedDatagramFrame(rDatagram.front(), rDatagram.size(), bServerPacket);
		EncryptedDatagramSendSequenceState state = CreateEncryptedDatagramSendSequenceState(rDatagram.size() - snapshot.nExpectedOverhead, snapshot, true);

		for (size_t nSliceBudget : rSliceBudgets) {
			const EncryptedDatagramSendSequenceAction action = AdvanceEncryptedDatagramSendSequence(state, nSliceBudget);
			if (action.bShouldPassThrough) {
				if (pbPassedThrough != nullptr)
					*pbPassedThrough = true;
				return emittedBytes;
			}
			if (action.bShouldEmitHeaderSlice) {
				const size_t nHeaderOffset = state.nHeaderBytesEmitted - action.nHeaderBytesEmitted;
				emittedBytes.insert(
					emittedBytes.end(),
					rDatagram.begin() + static_cast<std::ptrdiff_t>(nHeaderOffset),
					rDatagram.begin() + static_cast<std::ptrdiff_t>(nHeaderOffset + action.nHeaderBytesEmitted));
			}
			if (action.bShouldEmitPayloadSlice) {
				const size_t nPayloadBase = snapshot.nExpectedOverhead + action.nPayloadOffset;
				emittedBytes.insert(
					emittedBytes.end(),
					rDatagram.begin() + static_cast<std::ptrdiff_t>(nPayloadBase),
					rDatagram.begin() + static_cast<std::ptrdiff_t>(nPayloadBase + action.nPayloadBytesEmitted));
			}
		}

		if (pbPassedThrough != nullptr)
			*pbPassedThrough = state.bPassedThrough;
		return emittedBytes;
	}

	std::vector<BYTE> ReplayEncryptedDatagramSendSequenceWithState(
		EncryptedDatagramSendSequenceState &state,
		const std::vector<BYTE> &rDatagram,
		const std::vector<size_t> &rSliceBudgets,
		bool *pbPassedThrough = nullptr)
	{
		std::vector<BYTE> emittedBytes;
		const EncryptedDatagramFrameSnapshot snapshot = InspectEncryptedDatagramFrame(rDatagram.front(), rDatagram.size(), false);

		for (size_t nSliceBudget : rSliceBudgets) {
			const EncryptedDatagramSendSequenceAction action = AdvanceEncryptedDatagramSendSequence(state, nSliceBudget);
			if (action.bShouldPassThrough) {
				if (pbPassedThrough != nullptr)
					*pbPassedThrough = true;
				return emittedBytes;
			}
			if (action.bShouldEmitHeaderSlice) {
				const size_t nHeaderOffset = state.nHeaderBytesEmitted - action.nHeaderBytesEmitted;
				emittedBytes.insert(
					emittedBytes.end(),
					rDatagram.begin() + static_cast<std::ptrdiff_t>(nHeaderOffset),
					rDatagram.begin() + static_cast<std::ptrdiff_t>(nHeaderOffset + action.nHeaderBytesEmitted));
			}
			if (action.bShouldEmitPayloadSlice) {
				const size_t nPayloadBase = snapshot.nExpectedOverhead + action.nPayloadOffset;
				emittedBytes.insert(
					emittedBytes.end(),
					rDatagram.begin() + static_cast<std::ptrdiff_t>(nPayloadBase),
					rDatagram.begin() + static_cast<std::ptrdiff_t>(nPayloadBase + action.nPayloadBytesEmitted));
			}
		}

		if (pbPassedThrough != nullptr)
			*pbPassedThrough = state.bPassedThrough;
		return emittedBytes;
	}

	EncryptedDatagramLoopResult ReplayEncryptedDatagramReceiveLoop(
		const std::vector<EncryptedDatagramScenario> &rDatagrams,
		const std::vector<size_t> &rReadBudgets)
	{
		EncryptedDatagramLoopResult result = {};
		if (rDatagrams.empty())
			return result;

		size_t nDatagramIndex = 0u;
		size_t nDatagramOffset = 0u;
		EncryptedDatagramSequenceState state = CreateEncryptedDatagramSequenceState();
		EncryptedDatagramFrameSnapshot snapshot = InspectEncryptedDatagramFrame(rDatagrams.front().bytes.front(), rDatagrams.front().bytes.size(), rDatagrams.front().bServerPacket);

		for (size_t nBudget : rReadBudgets) {
			size_t nRemainingBudget = nBudget;
			while (nRemainingBudget > 0u && nDatagramIndex < rDatagrams.size()) {
				const EncryptedDatagramScenario &scenario = rDatagrams[nDatagramIndex];
				const size_t nBytesRemaining = scenario.bytes.size() > nDatagramOffset ? (scenario.bytes.size() - nDatagramOffset) : 0u;
				if (nBytesRemaining == 0u) {
					++nDatagramIndex;
					nDatagramOffset = 0u;
					state = CreateEncryptedDatagramSequenceState();
					if (nDatagramIndex < rDatagrams.size())
						snapshot = InspectEncryptedDatagramFrame(rDatagrams[nDatagramIndex].bytes.front(), rDatagrams[nDatagramIndex].bytes.size(), rDatagrams[nDatagramIndex].bServerPacket);
					continue;
				}

				const size_t nSlice = nRemainingBudget < nBytesRemaining ? nRemainingBudget : nBytesRemaining;
				const EncryptedDatagramSequenceAction action = AdvanceEncryptedDatagramSequence(
					state,
					scenario.bytes.size(),
					nSlice,
					snapshot,
					true);
				nDatagramOffset += action.nBytesAccepted;
				nRemainingBudget -= action.nBytesAccepted;

				if (action.bShouldPassThrough || action.bShouldExposePayload) {
					if (action.bShouldPassThrough)
						++result.nPassThroughCount;
					if (action.bShouldExposePayload)
						result.payloadLengths.push_back(state.nPayloadBytesExpected);
					++nDatagramIndex;
					nDatagramOffset = 0u;
					state = CreateEncryptedDatagramSequenceState();
					if (nDatagramIndex < rDatagrams.size())
						snapshot = InspectEncryptedDatagramFrame(rDatagrams[nDatagramIndex].bytes.front(), rDatagrams[nDatagramIndex].bytes.size(), rDatagrams[nDatagramIndex].bServerPacket);
				}

				if (action.nBytesAccepted == 0u)
					break;
			}
		}

		return result;
	}
}

TEST_SUITE_BEGIN("parity");

TEST_CASE("Encrypted datagram sequence replays an ED2K client packet across deterministic slices")
{
	const std::vector<BYTE> payload = {0xDE, 0xAD, 0xBE, 0xEF};
	const std::vector<BYTE> datagram = BuildDeterministicEncryptedDatagram(0x05u, false, payload, 0x1234u, 0x395F2EC1u);
	bool bPassedThrough = false;

	const std::vector<size_t> payloadLengths = ReplayEncryptedDatagramSequence(datagram, {2u, 3u, 3u, 4u}, false, &bPassedThrough);
	CHECK_FALSE(bPassedThrough);
	CHECK(payloadLengths == std::vector<size_t>{payload.size()});
	CHECK(std::vector<BYTE>(datagram.begin() + 8, datagram.end()) == payload);
}

TEST_CASE("Encrypted datagram sequence replays a Kad receiver-key packet and exposes the payload after verify-key bytes")
{
	const std::vector<BYTE> payload = {0x10, 0x20, 0x30};
	const std::vector<BYTE> datagram = BuildDeterministicEncryptedDatagram(0x12u, true, payload, 0x4567u, 0x395F2EC1u, 0x01020304u, 0xAABBCCDDu);
	bool bPassedThrough = false;

	const std::vector<size_t> payloadLengths = ReplayEncryptedDatagramSequence(datagram, {5u, 5u, 3u, 6u}, false, &bPassedThrough);
	CHECK_FALSE(bPassedThrough);
	CHECK(payloadLengths == std::vector<size_t>{payload.size()});
	CHECK(std::vector<BYTE>(datagram.begin() + 16, datagram.end()) == payload);
}

TEST_CASE("Encrypted datagram sequence passes through truncated server candidates before exposing payload bytes")
{
	const std::vector<BYTE> payload = {0xAA, 0xBB};
	const std::vector<BYTE> fullDatagram = BuildDeterministicEncryptedDatagram(0x34u, false, payload, 0x1111u, 0x13EF24D5u);
	const std::vector<BYTE> truncated(fullDatagram.begin(), fullDatagram.begin() + 7);
	bool bPassedThrough = false;

	const std::vector<size_t> payloadLengths = ReplayEncryptedDatagramSequence(truncated, {3u, 4u}, true, &bPassedThrough);
	CHECK(payloadLengths.empty());
	CHECK(bPassedThrough);
}

TEST_CASE("Encrypted datagram send sequence emits deterministic header and payload slices for ED2K traffic")
{
	const std::vector<BYTE> payload = {0xFA, 0xCE, 0xB0, 0x0C};
	const std::vector<BYTE> datagram = BuildDeterministicEncryptedDatagram(0x05u, false, payload, 0x2468u, 0x395F2EC1u);
	bool bPassedThrough = false;

	const std::vector<BYTE> emitted = ReplayEncryptedDatagramSendSequence(datagram, {3u, 3u, 2u, 1u, 3u}, false, &bPassedThrough);
	CHECK_FALSE(bPassedThrough);
	CHECK(emitted == datagram);
	CHECK(std::vector<BYTE>(emitted.begin(), emitted.begin() + 8) == std::vector<BYTE>(datagram.begin(), datagram.begin() + 8));
	CHECK(std::vector<BYTE>(emitted.begin() + 8, emitted.end()) == payload);
}

TEST_CASE("Encrypted datagram send sequence emits Kad verify-key headers before payload bytes")
{
	const std::vector<BYTE> payload = {0x11, 0x22, 0x33, 0x44};
	const std::vector<BYTE> datagram = BuildDeterministicEncryptedDatagram(0x12u, true, payload, 0xBEEFu, 0x395F2EC1u, 0x01020304u, 0x05060708u);

	const std::vector<BYTE> emitted = ReplayEncryptedDatagramSendSequence(datagram, {4u, 4u, 4u, 4u, 4u}, false);
	CHECK(emitted == datagram);
	CHECK(std::vector<BYTE>(emitted.begin(), emitted.begin() + 16) == std::vector<BYTE>(datagram.begin(), datagram.begin() + 16));
	CHECK(std::vector<BYTE>(emitted.begin() + 16, emitted.end()) == payload);
}

TEST_CASE("Encrypted datagram send sequence passes through server candidates rejected by framing rules")
{
	const std::vector<BYTE> payload = {0x01, 0x02, 0x03};
	const std::vector<BYTE> datagram = BuildDeterministicEncryptedDatagram(OP_EMULEPROT, false, payload, 0x9999u, 0x13EF24D5u);
	bool bPassedThrough = false;

	const std::vector<BYTE> emitted = ReplayEncryptedDatagramSendSequence(datagram, {8u, 3u}, true, &bPassedThrough);
	CHECK(bPassedThrough);
	CHECK(emitted.empty());
}

TEST_CASE("Encrypted datagram send sequence ignores zero-budget iterations and still completes deterministically")
{
	const std::vector<BYTE> payload = {0x31, 0x32, 0x33};
	const std::vector<BYTE> datagram = BuildDeterministicEncryptedDatagram(0x05u, false, payload, 0x5151u, 0x395F2EC1u);

	const std::vector<BYTE> emitted = ReplayEncryptedDatagramSendSequence(datagram, {0u, 3u, 0u, 2u, 6u}, false);
	CHECK(emitted == datagram);
}

TEST_CASE("Encrypted datagram send sequence clamps over-budget slices and stays idle after completion")
{
	const std::vector<BYTE> payload = {0x44, 0x55};
	const std::vector<BYTE> datagram = BuildDeterministicEncryptedDatagram(0x05u, false, payload, 0x2222u, 0x395F2EC1u);
	const EncryptedDatagramFrameSnapshot snapshot = InspectEncryptedDatagramFrame(datagram.front(), datagram.size(), false);
	EncryptedDatagramSendSequenceState state = CreateEncryptedDatagramSendSequenceState(payload.size(), snapshot, true);

	const std::vector<BYTE> firstEmit = ReplayEncryptedDatagramSendSequenceWithState(state, datagram, {128u});
	const std::vector<BYTE> secondEmit = ReplayEncryptedDatagramSendSequenceWithState(state, datagram, {8u});

	CHECK(firstEmit == datagram);
	CHECK(secondEmit.empty());
	CHECK(state.bTransmissionComplete);
}

TEST_CASE("Encrypted datagram send sequence can be reset after completion and replay another datagram")
{
	const std::vector<BYTE> first = BuildDeterministicEncryptedDatagram(0x05u, false, {0xA1, 0xA2}, 0x1010u, 0x395F2EC1u);
	const std::vector<BYTE> second = BuildDeterministicEncryptedDatagram(0x12u, true, {0xB1, 0xB2, 0xB3}, 0x2020u, 0x395F2EC1u, 0x01020304u, 0x05060708u);

	const EncryptedDatagramFrameSnapshot firstSnapshot = InspectEncryptedDatagramFrame(first.front(), first.size(), false);
	EncryptedDatagramSendSequenceState state = CreateEncryptedDatagramSendSequenceState(first.size() - firstSnapshot.nExpectedOverhead, firstSnapshot, true);
	const std::vector<BYTE> firstEmit = ReplayEncryptedDatagramSendSequenceWithState(state, first, {3u, 7u});
	CHECK(firstEmit == first);
	CHECK(state.bTransmissionComplete);

	const EncryptedDatagramFrameSnapshot secondSnapshot = InspectEncryptedDatagramFrame(second.front(), second.size(), false);
	state = CreateEncryptedDatagramSendSequenceState(second.size() - secondSnapshot.nExpectedOverhead, secondSnapshot, true);
	const std::vector<BYTE> secondEmit = ReplayEncryptedDatagramSendSequenceWithState(state, second, {5u, 5u, 9u});
	CHECK(secondEmit == second);
	CHECK(state.bTransmissionComplete);
}

TEST_CASE("Encrypted datagram receive loop preserves partial carryover across exact header boundaries")
{
	const std::vector<BYTE> payload = {0x55, 0x66, 0x77};
	const std::vector<EncryptedDatagramScenario> datagrams = {
		{BuildDeterministicEncryptedDatagram(0x05u, false, payload, 0xBEEF, 0x395F2EC1u), false}
	};

	const EncryptedDatagramLoopResult result = ReplayEncryptedDatagramReceiveLoop(datagrams, {4u, 4u, 3u});
	CHECK(result.nPassThroughCount == 0u);
	CHECK(result.payloadLengths == std::vector<size_t>{payload.size()});
}

TEST_CASE("Encrypted datagram receive loop exposes zero-length payload datagrams exactly once")
{
	const std::vector<EncryptedDatagramScenario> datagrams = {
		{BuildDeterministicEncryptedDatagram(0x05u, false, {}, 0xAAAAu, 0x395F2EC1u), false}
	};

	const EncryptedDatagramLoopResult result = ReplayEncryptedDatagramReceiveLoop(datagrams, {3u, 3u, 2u});
	CHECK(result.nPassThroughCount == 0u);
	CHECK(result.payloadLengths == std::vector<size_t>{0u});
}

TEST_CASE("Encrypted datagram receive loop carries state across mixed valid invalid and valid datagrams")
{
	const std::vector<EncryptedDatagramScenario> datagrams = {
		{BuildDeterministicEncryptedDatagram(0x05u, false, {0x01, 0x02}, 0x0102u, 0x395F2EC1u), false},
		{{0x34u, 0x11u, 0x11u, 0xD5u, 0x24u, 0xEFu, 0x13u}, true},
		{BuildDeterministicEncryptedDatagram(0x12u, true, {0x10, 0x20, 0x30}, 0x0304u, 0x395F2EC1u, 0xDEADBEEFu, 0x01020304u), false}
	};

	const EncryptedDatagramLoopResult result = ReplayEncryptedDatagramReceiveLoop(datagrams, {5u, 4u, 3u, 5u, 7u, 8u});
	CHECK(result.nPassThroughCount == 1u);
	CHECK(result.payloadLengths == std::vector<size_t>{2u, 3u});
}

TEST_CASE("Encrypted datagram receive loop recovers when an invalid datagram precedes a valid payload")
{
	const std::vector<EncryptedDatagramScenario> datagrams = {
		{{OP_EMULEPROT, 0x22u, 0x22u, 0xC1u, 0x2Eu, 0x5Fu, 0x39u, 0x00u, 0x99u}, false},
		{BuildDeterministicEncryptedDatagram(0x05u, false, {0xAB, 0xCD, 0xEF}, 0x1212u, 0x395F2EC1u), false}
	};

	const EncryptedDatagramLoopResult result = ReplayEncryptedDatagramReceiveLoop(datagrams, {4u, 5u, 2u, 4u, 5u});
	CHECK(result.nPassThroughCount == 1u);
	CHECK(result.payloadLengths == std::vector<size_t>{3u});
}

TEST_CASE("Encrypted datagram receive loop handles one-byte slices for Kad verify-key packets")
{
	const std::vector<BYTE> datagram = BuildDeterministicEncryptedDatagram(0x12u, true, {0xAA, 0xBB}, 0xCAFEu, 0x395F2EC1u, 0x01010101u, 0x02020202u);
	std::vector<size_t> budgets(datagram.size(), 1u);

	const EncryptedDatagramLoopResult result = ReplayEncryptedDatagramReceiveLoop({{datagram, false}}, budgets);
	CHECK(result.nPassThroughCount == 0u);
	CHECK(result.payloadLengths == std::vector<size_t>{2u});
}

TEST_CASE("Encrypted datagram receive loop can be restarted after a pass-through and after a successful payload emission")
{
	const EncryptedDatagramScenario invalidDatagram = {{0x34u, 0x11u, 0x11u, 0xD5u, 0x24u, 0xEFu, 0x13u}, true};
	const EncryptedDatagramScenario validDatagram = {BuildDeterministicEncryptedDatagram(0x05u, false, {0x91, 0x92, 0x93}, 0x3434u, 0x395F2EC1u), false};
	const EncryptedDatagramScenario nextValidDatagram = {BuildDeterministicEncryptedDatagram(0x12u, true, {0x81, 0x82}, 0x4545u, 0x395F2EC1u, 0x01010101u, 0x02020202u), false};

	const EncryptedDatagramLoopResult firstRun = ReplayEncryptedDatagramReceiveLoop({invalidDatagram, validDatagram}, {4u, 4u, 4u, 7u});
	CHECK(firstRun.nPassThroughCount == 1u);
	CHECK(firstRun.payloadLengths == std::vector<size_t>{3u});

	const EncryptedDatagramLoopResult secondRun = ReplayEncryptedDatagramReceiveLoop({nextValidDatagram}, std::vector<size_t>(nextValidDatagram.bytes.size(), 1u));
	CHECK(secondRun.nPassThroughCount == 0u);
	CHECK(secondRun.payloadLengths == std::vector<size_t>{2u});
}

TEST_CASE("Encrypted datagram receive loop clamps over-budget reads to the remaining datagram size")
{
	const std::vector<BYTE> datagram = BuildDeterministicEncryptedDatagram(0x05u, false, {0xFE, 0xED}, 0x7777u, 0x395F2EC1u);

	const EncryptedDatagramLoopResult result = ReplayEncryptedDatagramReceiveLoop({{datagram, false}}, {128u});
	CHECK(result.nPassThroughCount == 0u);
	CHECK(result.payloadLengths == std::vector<size_t>{2u});
}

TEST_CASE("Encrypted datagram receive loop replays mixed plain and encrypted marker corpora")
{
	const EncryptedDatagramScenario plainProtocol = {{OP_EMULEPROT, 0x44u, 0x44u, 0xC1u, 0x2Eu, 0x5Fu, 0x39u, 0x00u, 0xAAu}, false};
	const EncryptedDatagramScenario encryptedEd2k = {BuildDeterministicEncryptedDatagram(0x05u, false, {0x01, 0x02, 0x03}, 0x1234u, 0x395F2EC1u), false};
	const EncryptedDatagramScenario plainKad = {{OP_KADEMLIAHEADER, 0x10u, 0x10u, 0xC1u, 0x2Eu, 0x5Fu, 0x39u, 0x00u, 0xBBu}, false};
	const EncryptedDatagramScenario encryptedKad = {BuildDeterministicEncryptedDatagram(0x12u, true, {0x11, 0x22}, 0x9999u, 0x395F2EC1u, 0xDEADBEEFu, 0x01020304u), false};

	const EncryptedDatagramLoopResult result = ReplayEncryptedDatagramReceiveLoop(
		{plainProtocol, encryptedEd2k, plainKad, encryptedKad},
		{4u, 5u, 6u, 4u, 6u, 7u, 8u});

	CHECK(result.nPassThroughCount == 2u);
	CHECK(result.payloadLengths == std::vector<size_t>{3u, 2u});
}

TEST_CASE("Encrypted datagram sequence replays multiple datagrams from a temp-file corpus")
{
	const std::vector<BYTE> firstPayload = {0x01, 0x02};
	const std::vector<BYTE> secondPayload = {0x0A, 0x0B, 0x0C};
	const std::vector<BYTE> first = BuildDeterministicEncryptedDatagram(0x05u, false, firstPayload, 0x0102u, 0x395F2EC1u);
	const std::vector<BYTE> second = BuildDeterministicEncryptedDatagram(0x12u, true, secondPayload, 0x0304u, 0x395F2EC1u, 0xDEADBEEFu, 0x01020304u);

	std::vector<BYTE> corpus = first;
	corpus.insert(corpus.end(), second.begin(), second.end());

	const std::filesystem::path fixturePath = CreateEncryptedDatagramTempPath();
	WriteEncryptedDatagramFixture(fixturePath, corpus);

	std::ifstream streamFile(fixturePath, std::ios::binary);
	REQUIRE(streamFile.is_open());
	const std::vector<BYTE> fileBytes((std::istreambuf_iterator<char>(streamFile)), std::istreambuf_iterator<char>());

	const std::vector<BYTE> firstReplay(fileBytes.begin(), fileBytes.begin() + first.size());
	const std::vector<BYTE> secondReplay(fileBytes.begin() + first.size(), fileBytes.end());

	const std::vector<size_t> firstLengths = ReplayEncryptedDatagramSequence(firstReplay, {4u, 6u}, false);
	const std::vector<size_t> secondLengths = ReplayEncryptedDatagramSequence(secondReplay, {6u, 5u, 8u}, false);
	const std::vector<BYTE> firstEmit = ReplayEncryptedDatagramSendSequence(firstReplay, {3u, 2u, 5u}, false);
	const std::vector<BYTE> secondEmit = ReplayEncryptedDatagramSendSequence(secondReplay, {6u, 4u, 9u}, false);

	CHECK(firstLengths == std::vector<size_t>{firstPayload.size()});
	CHECK(secondLengths == std::vector<size_t>{secondPayload.size()});
	CHECK(firstEmit == firstReplay);
	CHECK(secondEmit == secondReplay);

	std::error_code ec;
	std::filesystem::remove(fixturePath, ec);
}

TEST_CASE("Encrypted datagram receive loop replays a mixed temp-file corpus with partial carryover between iterations")
{
	const EncryptedDatagramScenario first = {BuildDeterministicEncryptedDatagram(0x05u, false, {0xA1, 0xA2}, 0x0102u, 0x395F2EC1u), false};
	const EncryptedDatagramScenario second = {{0x34u, 0x11u, 0x11u, 0xD5u, 0x24u, 0xEFu, 0x13u}, true};
	const EncryptedDatagramScenario third = {BuildDeterministicEncryptedDatagram(0x12u, true, {0xB1, 0xB2, 0xB3}, 0x0304u, 0x395F2EC1u, 0xDEADBEEFu, 0x01020304u), false};

	std::vector<BYTE> corpus = first.bytes;
	corpus.insert(corpus.end(), second.bytes.begin(), second.bytes.end());
	corpus.insert(corpus.end(), third.bytes.begin(), third.bytes.end());

	const std::filesystem::path fixturePath = CreateEncryptedDatagramTempPath();
	WriteEncryptedDatagramFixture(fixturePath, corpus);

	std::ifstream streamFile(fixturePath, std::ios::binary);
	REQUIRE(streamFile.is_open());
	const std::vector<BYTE> fileBytes((std::istreambuf_iterator<char>(streamFile)), std::istreambuf_iterator<char>());

	const std::vector<EncryptedDatagramScenario> replayDatagrams = {
		{std::vector<BYTE>(fileBytes.begin(), fileBytes.begin() + first.bytes.size()), first.bServerPacket},
		{std::vector<BYTE>(fileBytes.begin() + first.bytes.size(), fileBytes.begin() + first.bytes.size() + second.bytes.size()), second.bServerPacket},
		{std::vector<BYTE>(fileBytes.begin() + first.bytes.size() + second.bytes.size(), fileBytes.end()), third.bServerPacket}
	};

	const EncryptedDatagramLoopResult result = ReplayEncryptedDatagramReceiveLoop(replayDatagrams, {4u, 5u, 3u, 4u, 8u, 8u});
	CHECK(result.nPassThroughCount == 1u);
	CHECK(result.payloadLengths == std::vector<size_t>{2u, 3u});

	std::error_code ec;
	std::filesystem::remove(fixturePath, ec);
}

TEST_SUITE_END;
