#include "../third_party/doctest/doctest.h"
#include "EncryptedStreamSocketSeams.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("Delayed server send completes once the pending negotiation buffer is fully flushed")
{
	CHECK(EncryptedStreamSocketSeams::ShouldCompleteDelayedServerSendAfterFlush(true, false));
}

TEST_CASE("Delayed server send stays pending while buffered negotiation data remains")
{
	CHECK_FALSE(EncryptedStreamSocketSeams::ShouldCompleteDelayedServerSendAfterFlush(true, true));
}

TEST_CASE("Non-delayed send states do not complete through the delayed-flush helper")
{
	CHECK_FALSE(EncryptedStreamSocketSeams::ShouldCompleteDelayedServerSendAfterFlush(false, false));
}
