#include "../third_party/doctest/doctest.h"
#include "../include/TestSupport.h"

#include "FormatSafetySeams.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("Format safety seam keeps stored IPv4 endpoint text stable for ordinary addresses")
{
	const uint32 dwIp = 203u | (0u << 8) | (113u << 16) | (7u << 24);
	CHECK(FormatSafetySeams::FormatStoredIPv4Endpoint(dwIp, 4662) == CString(_T("203.0.113.7:4662")));
}

TEST_CASE("Format safety seam formats the highest endpoint port without truncation")
{
	const uint32 dwIp = 10u | (54u << 8) | (218u << 16) | (144u << 24);
	CHECK(FormatSafetySeams::FormatStoredIPv4Endpoint(dwIp, 65535) == CString(_T("10.54.218.144:65535")));
}

TEST_CASE("Format safety seam formats decimal ASCII port values for miniupnpc calls")
{
	CHECK(FormatSafetySeams::FormatDecimalPortValueA(0) == CStringA("0"));
	CHECK(FormatSafetySeams::FormatDecimalPortValueA(4662) == CStringA("4662"));
	CHECK(FormatSafetySeams::FormatDecimalPortValueA(65535) == CStringA("65535"));
}

TEST_SUITE_END;
