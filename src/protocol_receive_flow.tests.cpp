#include "../third_party/doctest/doctest.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>
#include <windows.h>

#include "TestSupport.h"
#include "ProtocolParsers.h"
#include "ProtocolReceiveFlowSeams.h"
#include "opcodes.h"

namespace
{
	std::filesystem::path CreateProtocolReplayTempPath()
	{
		WCHAR szTempPath[MAX_PATH] = {};
		WCHAR szTempFile[MAX_PATH] = {};
		REQUIRE(::GetTempPathW(_countof(szTempPath), szTempPath) != 0);
		REQUIRE(::GetTempFileNameW(szTempPath, L"prf", 0, szTempFile) != 0);
		return std::filesystem::path(szTempFile);
	}

	void WriteProtocolReplayFixture(const std::filesystem::path &rPath, const std::vector<BYTE> &rBytes)
	{
		std::ofstream stream(rPath, std::ios::binary | std::ios::trunc);
		REQUIRE(stream.is_open());
		stream.write(reinterpret_cast<const char*>(rBytes.data()), static_cast<std::streamsize>(rBytes.size()));
		REQUIRE(stream.good());
	}

	std::vector<size_t> ReplayProtocolStream(const std::vector<BYTE> &rStream, const std::vector<size_t> &rChunkSizes, bool *pbRejected = nullptr)
	{
		std::vector<size_t> payloadLengths;
		std::vector<BYTE> pendingHeaderBytes;
		pendingHeaderBytes.reserve(6u);
		ProtocolReceiveFlowState state = CreateProtocolReceiveFlowState();

		size_t nStreamOffset = 0;
		for (size_t nChunkSize : rChunkSizes) {
			if (nStreamOffset >= rStream.size())
				break;

			size_t nChunkOffset = 0;
			const size_t nReadableChunkSize = (nStreamOffset + nChunkSize <= rStream.size()) ? nChunkSize : (rStream.size() - nStreamOffset);
			while (nChunkOffset < nReadableChunkSize) {
				bool bHeaderValid = false;
				size_t nPayloadLength = 0;
				if (!state.bHeaderDecoded) {
					const size_t nHeaderBytesNeeded = 6u - state.nHeaderBytesBuffered;
					const size_t nProbeBytes = (nReadableChunkSize - nChunkOffset < nHeaderBytesNeeded) ? (nReadableChunkSize - nChunkOffset) : nHeaderBytesNeeded;
					for (size_t i = 0; i < nProbeBytes; ++i)
						pendingHeaderBytes.push_back(rStream[nStreamOffset + nChunkOffset + i]);

					if (pendingHeaderBytes.size() == 6u) {
						ProtocolPacketHeader header = {};
						bHeaderValid = TryParsePacketHeader(pendingHeaderBytes.data(), pendingHeaderBytes.size(), &header);
						if (bHeaderValid)
							nPayloadLength = header.nPayloadLength;
					}
				}

				const ProtocolReceiveFlowAction action = AdvanceProtocolReceiveFlow(state, nReadableChunkSize - nChunkOffset, bHeaderValid, nPayloadLength);
				nChunkOffset += action.nBytesConsumed;

				if (action.bShouldAttemptHeaderParse && !action.bShouldRejectPacket)
					pendingHeaderBytes.clear();
				if (action.bShouldRejectPacket) {
					if (pbRejected != nullptr)
						*pbRejected = true;
					return payloadLengths;
				}
				if (action.bShouldEmitPacket) {
					payloadLengths.push_back(state.nPayloadBytesExpected);
					ResetProtocolReceiveFlow(state);
					pendingHeaderBytes.clear();
				}
				if (action.nBytesConsumed == 0)
					break;
			}
			nStreamOffset += nReadableChunkSize;
		}

		if (pbRejected != nullptr)
			*pbRejected = state.bRejected;
		return payloadLengths;
	}
}

TEST_SUITE_BEGIN("parity");

TEST_CASE("Protocol receive flow reconstructs fragmented packets from a temp-file byte stream")
{
	const std::vector<BYTE> stream = {
		OP_EDONKEYPROT, 0x04, 0x00, 0x00, 0x00, OP_HELLO, 0xAA, 0xBB, 0xCC,
		OP_EDONKEYPROT, 0x03, 0x00, 0x00, 0x00, OP_MESSAGE, 0xDD, 0xEE
	};
	const std::filesystem::path fixturePath = CreateProtocolReplayTempPath();
	WriteProtocolReplayFixture(fixturePath, stream);

	std::ifstream streamFile(fixturePath, std::ios::binary);
	REQUIRE(streamFile.is_open());
	const std::vector<BYTE> fileBytes((std::istreambuf_iterator<char>(streamFile)), std::istreambuf_iterator<char>());
	const std::vector<size_t> payloadLengths = ReplayProtocolStream(fileBytes, {2u, 4u, 3u, 2u, 6u});

	CHECK(payloadLengths == std::vector<size_t>{3u, 2u});
	std::error_code ec;
	std::filesystem::remove(fixturePath, ec);
}

TEST_CASE("Protocol receive flow emits zero-payload packets when the header completes")
{
	const std::vector<BYTE> stream = {
		OP_EDONKEYPROT, 0x01, 0x00, 0x00, 0x00, OP_HELLO
	};

	const std::vector<size_t> payloadLengths = ReplayProtocolStream(stream, {1u, 2u, 3u});
	CHECK(payloadLengths == std::vector<size_t>{0u});
}

TEST_CASE("Protocol receive flow rejects malformed zero-length packet headers")
{
	const std::vector<BYTE> stream = {
		OP_EDONKEYPROT, 0x00, 0x00, 0x00, 0x00, OP_HELLO
	};

	bool bRejected = false;
	const std::vector<size_t> payloadLengths = ReplayProtocolStream(stream, {6u}, &bRejected);
	CHECK(payloadLengths.empty());
	CHECK(bRejected);
}

TEST_CASE("Protocol receive flow emits multiple packets from one coalesced buffer")
{
	const std::vector<BYTE> stream = {
		OP_EDONKEYPROT, 0x02, 0x00, 0x00, 0x00, OP_HELLO, 0xAA,
		OP_EDONKEYPROT, 0x03, 0x00, 0x00, 0x00, OP_MESSAGE, 0xBB, 0xCC
	};

	const std::vector<size_t> payloadLengths = ReplayProtocolStream(stream, {stream.size()});
	CHECK(payloadLengths == std::vector<size_t>{1u, 2u});
}

TEST_CASE("Protocol receive flow leaves a truncated trailing header unresolved instead of inventing a packet")
{
	const std::vector<BYTE> stream = {
		OP_EDONKEYPROT, 0x02, 0x00, 0x00, 0x00, OP_HELLO, 0xAA,
		OP_EDONKEYPROT, 0x03, 0x00
	};

	bool bRejected = false;
	const std::vector<size_t> payloadLengths = ReplayProtocolStream(stream, {stream.size()}, &bRejected);
	CHECK(payloadLengths == std::vector<size_t>{1u});
	CHECK_FALSE(bRejected);
}

TEST_SUITE_END;
