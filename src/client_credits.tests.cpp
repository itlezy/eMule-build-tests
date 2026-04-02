#include "../third_party/doctest/doctest.h"
#include "../include/TestSupport.h"

#include "ClientCreditsSeams.h"

#include <limits>

TEST_SUITE_BEGIN("parity");

TEST_CASE("Client credits seam builds bounded secure-ident challenge layouts")
{
	ClientCreditsChallengeLayout layout = {};
	CHECK(TryBuildClientCreditsChallengeLayout(kClientCreditsMaxPublicKeySize, false, layout));
	CHECK(layout.nMessageLength == kClientCreditsMaxPublicKeySize + 4u);
	CHECK(layout.nChallengeIpLength == 0u);

	CHECK(TryBuildClientCreditsChallengeLayout(kClientCreditsMaxPublicKeySize, true, layout));
	CHECK(layout.nMessageLength == kClientCreditsMaxPublicKeySize + 9u);
	CHECK(layout.nChallengeIpLength == 5u);

	CHECK_FALSE(TryBuildClientCreditsChallengeLayout(kClientCreditsMaxPublicKeySize + 1u, true, layout));
}

TEST_CASE("Client credits seam validates signature buffer capacity")
{
	CHECK(CanStoreClientCreditsSignature(0u, 0));
	CHECK(CanStoreClientCreditsSignature(80u, 80));
	CHECK_FALSE(CanStoreClientCreditsSignature(81u, 80));
}

TEST_CASE("Client credits seam keeps the existing verify-failure state transition")
{
	CHECK(GetClientCreditsStateAfterVerifyFailure(1, 1, 3) == 3);
	CHECK(GetClientCreditsStateAfterVerifyFailure(2, 1, 3) == 2);
	CHECK(GetClientCreditsStateAfterVerifyFailure(4, 1, 3) == 4);
}

TEST_SUITE_END;
