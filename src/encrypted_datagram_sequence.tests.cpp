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

TEST_SUITE_END;
