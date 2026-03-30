#include "../third_party/doctest/doctest.h"
#include "../include/TestSupport.h"

#include <ctime>
#include <cstdio>
#include <vector>

#include "kademlia/utils/FastKad.h"
#include "kademlia/utils/KadPublishGuard.h"
#include "kademlia/utils/SafeKad.h"

namespace
{
	/**
	 * Creates a disposable sidecar path for Fast Kad persistence tests.
	 */
	CString CreateFastKadTempPath()
	{
		WCHAR szTempPath[MAX_PATH] = {};
		WCHAR szTempFile[MAX_PATH] = {};
		REQUIRE(::GetTempPathW(_countof(szTempPath), szTempPath) != 0);
		REQUIRE(::GetTempFileNameW(szTempPath, L"fkd", 0, szTempFile) != 0);
		::DeleteFileW(szTempFile);
		return CString(szTempFile);
	}

	/**
	 * Writes a deterministic Fast Kad sidecar fixture so ranking tests can control recency and health.
	 */
	void WriteFastKadFixture(LPCTSTR pszPath, const Kademlia::CUInt128 &uID, uint16 uUDPPort, uint64 uLastSuccessTime, uint32 uResponseTimeMs, sint16 nHealthScore)
	{
		const uint32 uMagic = 0x3146444Bu;
		const uint32 uVersion = 1;
		const uint32 uCount = 1;
		FILE *pFile = NULL;
		REQUIRE(_wfopen_s(&pFile, pszPath, L"wb") == 0);
		REQUIRE(pFile != NULL);
		REQUIRE(fwrite(&uMagic, 1, sizeof uMagic, pFile) == sizeof uMagic);
		REQUIRE(fwrite(&uVersion, 1, sizeof uVersion, pFile) == sizeof uVersion);
		REQUIRE(fwrite(&uCount, 1, sizeof uCount, pFile) == sizeof uCount);
		REQUIRE(fwrite(uID.GetData(), 1, 16, pFile) == 16);
		REQUIRE(fwrite(&uUDPPort, 1, sizeof uUDPPort, pFile) == sizeof uUDPPort);
		REQUIRE(fwrite(&uLastSuccessTime, 1, sizeof uLastSuccessTime, pFile) == sizeof uLastSuccessTime);
		REQUIRE(fwrite(&uResponseTimeMs, 1, sizeof uResponseTimeMs, pFile) == sizeof uResponseTimeMs);
		REQUIRE(fwrite(&nHealthScore, 1, sizeof nHealthScore, pFile) == sizeof nHealthScore);
		fclose(pFile);
	}
}

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

TEST_CASE("Fast Kad sidecar round-trips bootstrap metadata for known nodes")
{
	const CString strPath = CreateFastKadTempPath();
	Kademlia::CFastKad writer;
	const Kademlia::CUInt128 uFastNode(11ul);
	const Kademlia::CUInt128 uWarmNode(12ul);
	writer.TrackNodeResponse(uFastNode, 4665, 0x01020304u, 2 * CLOCKS_PER_SEC);
	writer.TrackNodeReachable(uWarmNode, 4666);

	std::vector<Kademlia::CFastKad::NodeKey> knownNodes;
	knownNodes.push_back(Kademlia::CFastKad::NodeKey(uFastNode, 4665));
	knownNodes.push_back(Kademlia::CFastKad::NodeKey(uWarmNode, 4666));
	writer.SaveNodesMetadata(strPath, knownNodes);

	Kademlia::CFastKad reader;
	reader.LoadNodesMetadata(strPath);
	CHECK(reader.GetBootstrapPriority(uFastNode, 4665) > 0);
	CHECK(reader.GetBootstrapPriority(uWarmNode, 4666) > 0);
	CHECK(reader.GetBootstrapPriority(uFastNode, 4665) > reader.GetBootstrapPriority(uWarmNode, 4666));

	::DeleteFile(strPath);
}

TEST_CASE("Fast Kad sidecar ignores invalid files without poisoning bootstrap priority state")
{
	const CString strPath = CreateFastKadTempPath();
	FILE *pFile = NULL;
	const uint32 uBadMagic = 0x0u;
	const uint32 uVersion = 1;
	const uint32 uCount = 1;
	REQUIRE(_wfopen_s(&pFile, strPath, L"wb") == 0);
	REQUIRE(pFile != NULL);
	REQUIRE(fwrite(&uBadMagic, 1, sizeof uBadMagic, pFile) == sizeof uBadMagic);
	REQUIRE(fwrite(&uVersion, 1, sizeof uVersion, pFile) == sizeof uVersion);
	REQUIRE(fwrite(&uCount, 1, sizeof uCount, pFile) == sizeof uCount);
	fclose(pFile);

	Kademlia::CFastKad fastKad;
	CHECK_EQ(fastKad.GetBootstrapPriority(Kademlia::CUInt128(21ul), 4672), 0ull);
	fastKad.LoadNodesMetadata(strPath);
	CHECK_EQ(fastKad.GetBootstrapPriority(Kademlia::CUInt128(21ul), 4672), 0ull);

	::DeleteFile(strPath);
}

TEST_CASE("Fast Kad bootstrap priority prefers recent healthy nodes and keys metadata by Kad ID plus UDP port")
{
	const CString strRecentPath = CreateFastKadTempPath();
	const CString strStalePath = CreateFastKadTempPath();
	const Kademlia::CUInt128 uNodeID(33ul);
	const uint64 uNow = static_cast<uint64>(time(NULL));

	WriteFastKadFixture(strRecentPath, uNodeID, 4665, uNow, 1200, 40);
	WriteFastKadFixture(strStalePath, uNodeID, 4666, uNow - (120ull * 24ull * 60ull * 60ull), 1200, -40);

	Kademlia::CFastKad fastKad;
	fastKad.LoadNodesMetadata(strRecentPath);
	const uint64 uRecentPriority = fastKad.GetBootstrapPriority(uNodeID, 4665);
	CHECK(uRecentPriority > 0);
	CHECK_EQ(fastKad.GetBootstrapPriority(uNodeID, 4666), 0ull);

	fastKad.LoadNodesMetadata(strStalePath);
	const uint64 uStalePriority = fastKad.GetBootstrapPriority(uNodeID, 4666);
	CHECK(uStalePriority > 0);
	CHECK(uRecentPriority > uStalePriority);

	::DeleteFile(strRecentPath);
	::DeleteFile(strStalePath);
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
