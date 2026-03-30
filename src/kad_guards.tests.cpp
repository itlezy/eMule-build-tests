#include "../third_party/doctest/doctest.h"
#include "../include/TestSupport.h"

#include "kademlia/utils/FastKad.h"
#include "kademlia/utils/KadPublishGuard.h"
#include "kademlia/utils/SafeKad.h"

TEST_SUITE_BEGIN("kad");

TEST_CASE("Fast Kad raises its pending timeout estimate after repeated slow replies")
{
	Kademlia::CFastKad fastKad;
	const clock_t clkDefaultEstimate = fastKad.GetEstMaxResponseTime();
	for (uint32 uIndex = 1; uIndex <= 20; ++uIndex)
		fastKad.AddResponseTime(uIndex, 2 * CLOCKS_PER_SEC);

	CHECK(fastKad.GetEstMaxResponseTime() > clkDefaultEstimate);
	CHECK(fastKad.GetEstMaxResponseTime() <= 3 * CLOCKS_PER_SEC);
}

TEST_CASE("Safe Kad bans a verified node that flips identities too quickly when strict mode is enabled")
{
	Kademlia::CSafeKad safeKad;
	safeKad.TrackNode(0x01020304u, 4665, Kademlia::CUInt128(1ul), true, true);

	CHECK(safeKad.IsBadNode(0x01020304u, 4665, Kademlia::CUInt128(2ul), KADEMLIA_VERSION8_49b, true, false, true));
	CHECK(safeKad.IsBanned(0x01020304u));
}

TEST_CASE("Safe Kad keeps problematic nodes only until the runtime cache is reset")
{
	Kademlia::CSafeKad safeKad;
	safeKad.TrackProblematicNode(0x0A000001u, 4672);
	CHECK(safeKad.IsProblematic(0x0A000001u, 4672));

	safeKad.ShutdownCleanup();
	CHECK_FALSE(safeKad.IsProblematic(0x0A000001u, 4672));
}

TEST_CASE("Kad publish guard accepts a valid high-ID source publish")
{
	Kademlia::PublishSourceMetadata metadata;
	metadata.m_bHasSourceType = true;
	metadata.m_uSourceType = 1;
	metadata.m_bHasSourcePort = true;
	metadata.m_uSourcePort = 4662;

	CHECK(Kademlia::ValidatePublishSourceMetadata(metadata));
}

TEST_CASE("Kad publish guard rejects low-ID source publishes without complete buddy information")
{
	Kademlia::PublishSourceMetadata metadata;
	metadata.m_bHasSourceType = true;
	metadata.m_uSourceType = 3;
	metadata.m_bHasSourcePort = true;
	metadata.m_uSourcePort = 4662;
	metadata.m_bHasBuddyIP = true;
	metadata.m_uBuddyIP = 0x05060708u;

	CHECK_FALSE(Kademlia::ValidatePublishSourceMetadata(metadata));
}

TEST_CASE("Kad publish guard escalates repeated publish-source requests from the same IP")
{
	Kademlia::CKadPublishSourceThrottle throttle;
	uint32 dwNow = 1000;

	for (uint32 uCount = 0; uCount < 10; ++uCount)
		CHECK_EQ(throttle.TrackRequest(0x11223344u, dwNow, 10), Kademlia::KPUBLISH_ALLOW);

	CHECK_EQ(throttle.TrackRequest(0x11223344u, dwNow, 10), Kademlia::KPUBLISH_DROP);

	for (uint32 uCount = 0; uCount < 20; ++uCount)
		throttle.TrackRequest(0x11223344u, dwNow, 10);

	CHECK_EQ(throttle.TrackRequest(0x11223344u, dwNow, 10), Kademlia::KPUBLISH_BAN);
}

TEST_SUITE_END;
