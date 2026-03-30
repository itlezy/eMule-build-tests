#include "../third_party/doctest/doctest.h"
#include "../include/TestSupport.h"
#include "ProtocolGuards.h"

namespace
{
	static const size_t TCP_READ_BUFFER_SIZE = 2000000u;
	static const uint32 MAX_PROTOCOL_TAGS = 256u;
	static const uint32 IPV4_BROADCAST = 0xFFFFFFFFu;
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

TEST_SUITE_END;

TEST_SUITE_BEGIN("divergence");

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

TEST_SUITE_END;
