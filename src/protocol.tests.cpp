#include "../third_party/doctest/doctest.h"
#include "../include/TestSupport.h"
#include <limits>
#include "ProtocolGuards.h"
#include "ProtocolParsers.h"
#include "ServerConnectionGuards.h"

/**
 * @brief Enables the target-only protocol guard coverage that depends on newer helper APIs.
 */
#if defined(__has_include)
#if __has_include("MappedFileReader.h")
#define EMULE_HAS_EXTENDED_PROTOCOL_GUARDS 1
#endif
#endif

namespace
{
	static const size_t TCP_READ_BUFFER_SIZE = 2000000u;
	static const uint32 MAX_PROTOCOL_TAGS = 256u;
	static const uint32 IPV4_BROADCAST = 0xFFFFFFFFu;

	/**
	 * Keeps the packet fixtures readable when asserting serialized header decode paths.
	 */
	ProtocolPacketHeader ParsePacketHeaderFixture(const BYTE *pFixture, size_t nFixtureSize)
	{
		ProtocolPacketHeader header = {};
		REQUIRE(TryParsePacketHeader(pFixture, nFixtureSize, &header));
		return header;
	}

	/**
	 * Keeps the tag fixtures readable when asserting serialized tag span decode paths.
	 */
	ProtocolTagSpan ParseTagFixture(const BYTE *pFixture, size_t nFixtureSize)
	{
		ProtocolTagSpan span = {};
		REQUIRE(TryParseTagSpan(pFixture, nFixtureSize, &span));
		return span;
	}
}

TEST_SUITE_BEGIN("parity");

TEST_CASE("Protocol guard rejects oversized TCP payloads")
{
	CHECK_FALSE(CanReadTcpPacketPayload(static_cast<uint32>(TCP_READ_BUFFER_SIZE + 2), TCP_READ_BUFFER_SIZE));
}

TEST_CASE("Protocol guard accepts the largest TCP payload that fits the receive buffer")
{
	CHECK(CanReadTcpPacketPayload(static_cast<uint32>(TCP_READ_BUFFER_SIZE + 1), TCP_READ_BUFFER_SIZE));
}

TEST_CASE("Protocol guard rejects zero-length TCP headers at the receive gate")
{
	CHECK_FALSE(CanReadTcpPacketPayload(0, TCP_READ_BUFFER_SIZE));
}

TEST_CASE("Protocol guard accepts a reasonable tag count for the remaining packet bytes")
{
	CHECK(HasSaneTagCount(8, 16, 4, MAX_PROTOCOL_TAGS));
}

TEST_CASE("Protocol guard accepts compressed UDP payloads that include at least one body byte")
{
	CHECK(HasCompressedUdpPayload(3));
}

TEST_CASE("Protocol guard accepts exact-fit packet spans and bounded size arithmetic")
{
#if defined(EMULE_HAS_EXTENDED_PROTOCOL_GUARDS)
	size_t nCombinedSize = 0;
	size_t nExpandedSize = 0;
	CHECK(CanReadPacketSpan(24, 16, 8));
	CHECK(TryAddSize(32, 10, &nCombinedSize));
	CHECK_EQ(nCombinedSize, static_cast<size_t>(42));
	CHECK(TryMultiplyAddSize(32, 10, 300, &nExpandedSize));
	CHECK_EQ(nExpandedSize, static_cast<size_t>(620));
#else
	MESSAGE("Extended protocol guard helpers are not available in this workspace.");
#endif
}

TEST_CASE("Protocol guard accepts restorable TCP header fragments and minimal callback or block packet headers")
{
#if defined(EMULE_HAS_EXTENDED_PROTOCOL_GUARDS)
	CHECK(CanRestoreTcpPendingHeader(PROTOCOL_PACKET_HEADER_SIZE - 1, PROTOCOL_PACKET_HEADER_SIZE, TCP_READ_BUFFER_SIZE));
	CHECK(CanStoreTcpPendingHeader(PROTOCOL_PACKET_HEADER_SIZE - 1, PROTOCOL_PACKET_HEADER_SIZE));
	CHECK(CanContinuePacketAssembly(100, 100));
	CHECK(HasUdpCallbackPayload(17));
	CHECK(HasDownloadBlockPacketHeader(24, false, false));
#else
	MESSAGE("Extended protocol guard helpers are not available in this workspace.");
#endif
}

TEST_CASE("Protocol guard accepts blob payloads that stay within the remaining packet bytes")
{
	CHECK(CanReadBlobPayload(8, 16, 4));
}

TEST_CASE("Protocol guard keeps integer progress percentages stable for valid denominators")
{
	CHECK_EQ(CalculateProgressPercent(25, 100), static_cast<uint32>(25));
}

TEST_CASE("Protocol guard keeps floating-point progress ratios stable for valid denominators")
{
	CHECK_EQ(CalculateProgressRatio(25.0f, 100.0f), doctest::Approx(0.25f));
}

TEST_CASE("Protocol guard parses ordinary dotted IPv4 literals")
{
	uint32 nAddress = 0;
	CHECK(TryParseDottedIPv4Literal("1.2.3.4", &nAddress));
	CHECK_EQ(nAddress, static_cast<uint32>(0x04030201u));
}

TEST_CASE("Protocol parser decodes the smallest legal packet header from serialized bytes")
{
	const BYTE fixture[] = {
		OP_EDONKEYPROT,
		0x01, 0x00, 0x00, 0x00,
		OP_HELLO
	};

	const ProtocolPacketHeader header = ParsePacketHeaderFixture(fixture, sizeof(fixture));
	CHECK_EQ(header.nProtocol, static_cast<uint8>(OP_EDONKEYPROT));
	CHECK_EQ(header.nOpcode, static_cast<uint8>(OP_HELLO));
	CHECK_EQ(header.nPacketLength, static_cast<uint32>(1));
	CHECK_EQ(header.nPayloadLength, static_cast<uint32>(0));
}

TEST_CASE("Protocol parser decodes a named-by-id uint32 tag from serialized bytes")
{
	const BYTE fixture[] = {
		static_cast<BYTE>(TAGTYPE_UINT32 | 0x80),
		CT_VERSION,
		0x34, 0x12, 0x00, 0x00
	};

	const ProtocolTagSpan span = ParseTagFixture(fixture, sizeof(fixture));
	CHECK_EQ(span.Header.nType, static_cast<uint8>(TAGTYPE_UINT32));
	CHECK(span.Header.bUsesNameId);
	CHECK_EQ(span.Header.nNameId, static_cast<uint8>(CT_VERSION));
	CHECK_EQ(span.Header.nHeaderSize, static_cast<size_t>(2));
	CHECK_EQ(span.nValueSize, static_cast<size_t>(4));
	CHECK_EQ(span.nTotalSize, sizeof(fixture));
}

TEST_CASE("Protocol parser decodes an explicit-name string tag whose payload exactly fits the serialized bytes")
{
	const BYTE fixture[] = {
		TAGTYPE_STRING,
		0x03, 0x00,
		'e', 'd', '2',
		0x04, 0x00,
		'l', 'i', 'n', 'k'
	};

	const ProtocolTagSpan span = ParseTagFixture(fixture, sizeof(fixture));
	CHECK_EQ(span.Header.nType, static_cast<uint8>(TAGTYPE_STRING));
	CHECK_FALSE(span.Header.bUsesNameId);
	CHECK_EQ(span.Header.nNameLength, static_cast<uint16>(3));
	CHECK_EQ(span.Header.nHeaderSize, static_cast<size_t>(2 + 3 + 1));
	CHECK_EQ(span.nValueSize, static_cast<size_t>(2 + 4));
	CHECK_EQ(span.nTotalSize, sizeof(fixture));
}

TEST_CASE("Protocol parser accepts a blob tag whose payload exactly fits the serialized bytes")
{
	const BYTE fixture[] = {
		static_cast<BYTE>(TAGTYPE_BLOB | 0x80),
		FT_MEDIA_ARTIST,
		0x03, 0x00, 0x00, 0x00,
		0xAA, 0xBB, 0xCC
	};

	const ProtocolTagSpan span = ParseTagFixture(fixture, sizeof(fixture));
	CHECK_EQ(span.Header.nType, static_cast<uint8>(TAGTYPE_BLOB));
	CHECK_EQ(span.nBlobSize, static_cast<uint32>(3));
	CHECK_EQ(span.nTotalSize, sizeof(fixture));
}

TEST_CASE("Protocol guard bounds zero-length packet payloads instead of underflowing")
{
	CHECK_EQ(GetPacketPayloadSize(0), static_cast<uint32>(0));
}

TEST_CASE("Protocol guard rejects hostile hello tag counts before tag parsing starts")
{
	CHECK_FALSE(HasSaneTagCount(8, 16, MAX_PROTOCOL_TAGS + 1, MAX_PROTOCOL_TAGS));
}

TEST_CASE("Protocol guard rejects short server UDP payloads before reading the header bytes")
{
	CHECK_FALSE(HasUdpPayloadHeader(1));
}

TEST_CASE("Protocol guard rejects compressed UDP payloads that have no compressed body bytes")
{
	CHECK_FALSE(HasCompressedUdpPayload(2));
}

TEST_CASE("Protocol guard rejects truncated packet spans and impossible TCP partial-header states")
{
#if defined(EMULE_HAS_EXTENDED_PROTOCOL_GUARDS)
	CHECK_FALSE(CanReadPacketSpan(23, 16, 8));
	CHECK_FALSE(CanRestoreTcpPendingHeader(PROTOCOL_PACKET_HEADER_SIZE + 1, PROTOCOL_PACKET_HEADER_SIZE, TCP_READ_BUFFER_SIZE));
	CHECK_FALSE(CanStoreTcpPendingHeader(PROTOCOL_PACKET_HEADER_SIZE, PROTOCOL_PACKET_HEADER_SIZE));
	CHECK_FALSE(CanContinuePacketAssembly(100, 101));
#else
	MESSAGE("Extended protocol guard helpers are not available in this workspace.");
#endif
}

TEST_CASE("Protocol guard rejects overflowed size arithmetic and truncated callback or block packet headers")
{
#if defined(EMULE_HAS_EXTENDED_PROTOCOL_GUARDS)
	size_t nIgnoredSize = 0;
	CHECK_FALSE(TryAddSize(std::numeric_limits<size_t>::max(), 1, &nIgnoredSize));
	CHECK_FALSE(TryMultiplyAddSize(std::numeric_limits<size_t>::max(), 2, 1, &nIgnoredSize));
	CHECK_FALSE(HasUdpCallbackPayload(16));
	CHECK_FALSE(HasDownloadBlockPacketHeader(23, false, false));
#else
	MESSAGE("Extended protocol guard helpers are not available in this workspace.");
#endif
}

TEST_CASE("Protocol guard rejects blob reads once the parser position has moved past the packet length")
{
	CHECK_FALSE(CanReadBlobPayload(17, 16, 1));
}

TEST_CASE("Protocol guard treats zero-denominator integer progress as 0 instead of an ASSERT-only contract")
{
	CHECK_EQ(CalculateProgressPercent(5, 0), static_cast<uint32>(0));
}

TEST_CASE("Protocol guard treats zero-denominator floating-point progress as empty progress")
{
	CHECK_EQ(CalculateProgressRatio(5.0f, 0.0f), doctest::Approx(0.0f));
}

TEST_CASE("Protocol guard accepts the dotted broadcast IPv4 literal as a valid parse")
{
	uint32 nAddress = 0;
	CHECK(TryParseDottedIPv4Literal("255.255.255.255", &nAddress));
	CHECK_EQ(nAddress, IPV4_BROADCAST);
}

TEST_CASE("Protocol guard rejects invalid IPv4 parser inputs and clamps bounded progress")
{
	size_t nExpandedSize = 0;
	uint32 nAddress = 0;

	CHECK_FALSE(TryMultiplyAddSize(4u, 2u, 3u, NULL));
	CHECK_EQ(CalculateProgressPercent(150u, 100u), static_cast<uint32>(100));
	CHECK_EQ(CalculateProgressRatio(-5.0f, 10.0f), doctest::Approx(0.0f));
	CHECK_EQ(CalculateProgressRatio(15.0f, 10.0f), doctest::Approx(1.0f));
	REQUIRE(TryMultiplyAddSize(4u, 0u, 7u, &nExpandedSize));
	CHECK_EQ(nExpandedSize, static_cast<size_t>(7));
	CHECK_FALSE(TryParseDottedIPv4Literal(NULL, &nAddress));
	CHECK_FALSE(TryParseDottedIPv4Literal("1.2.3.4", NULL));
	CHECK_FALSE(TryParseDottedIPv4Literal("a.2.3.4", &nAddress));
	CHECK_FALSE(TryParseDottedIPv4Literal("256.2.3.4", &nAddress));
	CHECK_FALSE(TryParseDottedIPv4Literal("1-2.3.4", &nAddress));
	CHECK_FALSE(TryParseDottedIPv4Literal("1.2.3.4x", &nAddress));
}

TEST_CASE("Connected-server seam accepts a cached current-server snapshot when connected")
{
	const void *pCurrentServer = reinterpret_cast<const void*>(1);
	CHECK(HasConnectedServerSnapshot(true, pCurrentServer));
	CHECK(HasConnectedServerCapability(true, pCurrentServer, true));
	CHECK(MatchesConnectedServerEndpoint(true, pCurrentServer, 0x01020304u, 4661, 0x01020304u, 4661));
}

TEST_CASE("Connected-server seam rejects missing capability bits and mismatched endpoints even with a snapshot")
{
	const void *pCurrentServer = reinterpret_cast<const void*>(1);
	CHECK_FALSE(HasConnectedServerCapability(true, pCurrentServer, false));
	CHECK_FALSE(MatchesConnectedServerEndpoint(true, pCurrentServer, 0x01020304u, 4661, 0x05060708u, 4661));
	CHECK_FALSE(MatchesConnectedServerEndpoint(true, pCurrentServer, 0x01020304u, 4661, 0x01020304u, 4662));
}

TEST_SUITE_END;

TEST_SUITE_BEGIN("divergence");

TEST_CASE("Protocol parser rejects zero-length packet headers before payload math underflows")
{
	const BYTE fixture[] = {
		OP_EDONKEYPROT,
		0x00, 0x00, 0x00, 0x00,
		OP_HELLO
	};

	ProtocolPacketHeader header = {};
	CHECK_FALSE(TryParsePacketHeader(fixture, sizeof(fixture), &header));
}

TEST_CASE("Protocol parser rejects truncated serialized packet headers before the opcode read")
{
	const BYTE fixture[] = {
		OP_EDONKEYPROT,
		0x01, 0x00, 0x00, 0x00
	};

	ProtocolPacketHeader header = {};
	CHECK_FALSE(TryParsePacketHeader(fixture, sizeof(fixture), &header));
}

TEST_CASE("Protocol parser rejects explicit-name tags whose serialized name bytes are truncated")
{
	const BYTE fixture[] = {
		TAGTYPE_UINT8,
		0x03, 0x00,
		'A', 'B'
	};

	ProtocolTagHeader header = {};
	CHECK_FALSE(TryParseTagHeader(fixture, sizeof(fixture), &header));
}

TEST_CASE("Protocol parser rejects named-by-id uint32 tags whose serialized value bytes are truncated")
{
	const BYTE fixture[] = {
		static_cast<BYTE>(TAGTYPE_UINT32 | 0x80),
		CT_VERSION,
		0x34, 0x12, 0x00
	};

	ProtocolTagSpan span = {};
	CHECK_FALSE(TryParseTagSpan(fixture, sizeof(fixture), &span));
}

TEST_CASE("Protocol parser rejects string tags whose serialized payload is truncated")
{
	const BYTE fixture[] = {
		static_cast<BYTE>(TAGTYPE_STRING | 0x80),
		CT_NAME,
		0x03, 0x00,
		'A', 'B'
	};

	ProtocolTagSpan span = {};
	CHECK_FALSE(TryParseTagSpan(fixture, sizeof(fixture), &span));
}

TEST_CASE("Protocol parser rejects blob tags whose serialized payload exceeds the remaining bytes")
{
	const BYTE fixture[] = {
		static_cast<BYTE>(TAGTYPE_BLOB | 0x80),
		FT_MEDIA_ARTIST,
		0x04, 0x00, 0x00, 0x00,
		0xAA, 0xBB, 0xCC
	};

	ProtocolTagSpan span = {};
	CHECK_FALSE(TryParseTagSpan(fixture, sizeof(fixture), &span));
}

TEST_CASE("Connected-server seam rejects a connected session that lost its current-server snapshot")
{
	CHECK_FALSE(HasConnectedServerSnapshot(true, NULL));
}

TEST_CASE("Connected-server seam rejects capability checks once the current-server snapshot is missing")
{
	CHECK_FALSE(HasConnectedServerCapability(true, NULL, true));
}

TEST_CASE("Connected-server seam rejects endpoint matches once the current-server snapshot is missing")
{
	CHECK_FALSE(MatchesConnectedServerEndpoint(true, NULL, 0x01020304u, 4661, 0x01020304u, 4661));
}

TEST_SUITE_END;
