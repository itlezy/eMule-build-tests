#include "../third_party/doctest/doctest.h"

#include "SocketPolicySeams.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("Disconnect cleanup marks connecting clients as dead sources")
{
	CHECK(ShouldMarkClientAsDeadSource(true, false));
	CHECK(ShouldMarkClientAsDeadSource(false, true));
	CHECK_FALSE(ShouldMarkClientAsDeadSource(false, false));
}

TEST_CASE("Verbose connect logging ignores expected refusal and timeout failures")
{
	CHECK_FALSE(ShouldLogVerboseClientConnectError(WSAECONNREFUSED));
	CHECK_FALSE(ShouldLogVerboseClientConnectError(WSAETIMEDOUT));
	CHECK(ShouldLogVerboseClientConnectError(WSAECONNRESET));
}

TEST_SUITE_END;
