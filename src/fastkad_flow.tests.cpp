#include "../third_party/doctest/doctest.h"
#include "../include/TestSupport.h"

#include <ctime>
#include <vector>

#include "kademlia/utils/FastKad.h"

namespace
{
	CString CreateFastKadFlowTempPath()
	{
		WCHAR szTempPath[MAX_PATH] = {};
		WCHAR szTempFile[MAX_PATH] = {};
		REQUIRE(::GetTempPathW(_countof(szTempPath), szTempPath) != 0);
		REQUIRE(::GetTempFileNameW(szTempPath, L"fkf", 0, szTempFile) != 0);
		::DeleteFileW(szTempFile);
		return CString(szTempFile);
	}
}

TEST_SUITE_BEGIN("kad-broadband");

TEST_CASE("Fast Kad flow demotes repeated failures and recovers when a node becomes reachable again")
{
	Kademlia::CFastKad fastKad;
	const Kademlia::CUInt128 uNodeID(901ul);

	fastKad.TrackNodeReachable(uNodeID, 4665);
	const uint64 uReachablePriority = fastKad.GetBootstrapPriority(uNodeID, 4665);
	CHECK(uReachablePriority > 0);

	fastKad.TrackNodeFailure(uNodeID, 4665);
	const uint64 uOneFailurePriority = fastKad.GetBootstrapPriority(uNodeID, 4665);
	CHECK(uOneFailurePriority < uReachablePriority);

	fastKad.TrackNodeFailure(uNodeID, 4665);
	const uint64 uTwoFailuresPriority = fastKad.GetBootstrapPriority(uNodeID, 4665);
	CHECK(uTwoFailuresPriority <= uOneFailurePriority);

	fastKad.TrackNodeReachable(uNodeID, 4665);
	const uint64 uRecoveredPriority = fastKad.GetBootstrapPriority(uNodeID, 4665);
	CHECK(uRecoveredPriority > uTwoFailuresPriority);
}

TEST_CASE("Fast Kad flow keeps node identity partitioned by UDP port through save and reload")
{
	Kademlia::CFastKad writer;
	const Kademlia::CUInt128 uNodeID(902ul);
	writer.TrackNodeReachable(uNodeID, 4665);
	writer.TrackNodeFailure(uNodeID, 4672);

	std::vector<Kademlia::CFastKad::NodeKey> knownNodes;
	knownNodes.push_back(Kademlia::CFastKad::NodeKey(uNodeID, 4665));
	knownNodes.push_back(Kademlia::CFastKad::NodeKey(uNodeID, 4672));

	const CString strPath = CreateFastKadFlowTempPath();
	writer.SaveNodesMetadata(strPath, knownNodes);

	Kademlia::CFastKad reader;
	reader.LoadNodesMetadata(strPath);
	const uint64 uReachablePriority = reader.GetBootstrapPriority(uNodeID, 4665);
	const uint64 uFailedPriority = reader.GetBootstrapPriority(uNodeID, 4672);
	CHECK(uReachablePriority > 0);
	CHECK(uFailedPriority > 0);
	CHECK(uReachablePriority > uFailedPriority);

	::DeleteFile(strPath);
}

TEST_CASE("Fast Kad flow resets runtime ranking state completely on shutdown cleanup")
{
	Kademlia::CFastKad fastKad;
	const Kademlia::CUInt128 uNodeID(903ul);

	fastKad.TrackNodeResponse(uNodeID, 4665, 0x01020304u, 2 * CLOCKS_PER_SEC);
	CHECK(fastKad.GetBootstrapPriority(uNodeID, 4665) > 0);

	fastKad.ShutdownCleanup();
	CHECK_EQ(fastKad.GetBootstrapPriority(uNodeID, 4665), 0ull);
}

TEST_SUITE_END;
