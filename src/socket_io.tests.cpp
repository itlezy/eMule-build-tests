#include "../third_party/doctest/doctest.h"

#include <cstdint>

#include "SocketIoSeams.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("Socket IO seam accepts bounded receive results and rejects impossible byte counts")
{
	CHECK(HasValidSocketReceiveResult(0, 1024u));
	CHECK(HasValidSocketReceiveResult(512, 1024u));
	CHECK_FALSE(HasValidSocketReceiveResult(-1, 1024u));
	CHECK_FALSE(HasValidSocketReceiveResult(2048, 1024u));
}

TEST_CASE("Socket IO seam clamps receive-budget accounting without unsigned underflow")
{
	CHECK_EQ(ClampSocketReceiveBudget(4096u, 512), static_cast<std::uint32_t>(3584));
	CHECK_EQ(ClampSocketReceiveBudget(4096u, 4096), static_cast<std::uint32_t>(0));
	CHECK_EQ(ClampSocketReceiveBudget(4096u, 8192), static_cast<std::uint32_t>(0));
	CHECK_EQ(ClampSocketReceiveBudget(4096u, 0), static_cast<std::uint32_t>(4096));
}

TEST_CASE("Socket IO seam advances send progress only for positive in-range send results")
{
	std::uint32_t nNextSent = 0;

	CHECK(TryAccumulateSocketSendProgress(128u, 256u, 1024u, 128u, &nNextSent));
	CHECK_EQ(nNextSent, static_cast<std::uint32_t>(256));

	CHECK_FALSE(TryAccumulateSocketSendProgress(128u, 256u, 1024u, 0u, &nNextSent));
	CHECK_FALSE(TryAccumulateSocketSendProgress(128u, 256u, 1024u, 300u, &nNextSent));
	CHECK_FALSE(TryAccumulateSocketSendProgress(900u, 200u, 1024u, 150u, &nNextSent));
}

TEST_SUITE_END;
