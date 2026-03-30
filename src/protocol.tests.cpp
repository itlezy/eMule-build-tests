#include "../third_party/doctest/doctest.h"
#include "../include/TestSupport.h"
#include "..\\eMule\\srchybrid\\ProtocolGuards.h"

namespace
{
	static const size_t TCP_READ_BUFFER_SIZE = 2000000u;
	static const uint32 MAX_PROTOCOL_TAGS = 256u;
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

TEST_SUITE_END;
