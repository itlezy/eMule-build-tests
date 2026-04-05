#include "../third_party/doctest/doctest.h"
#include "../include/TestSupport.h"

#include "kademlia/kademlia/Defines.h"
#include "kademlia/utils/KadClientSearcher.h"

namespace
{
	class RecordingKadClientSearcher : public Kademlia::CKadClientSearcher
	{
	public:
		void KadSearchNodeIDByIPResult(Kademlia::EKadClientSearchRes eStatus, const uchar *pachNodeID) override
		{
			m_eLastNodeStatus = eStatus;
			m_pLastNodeID = pachNodeID;
		}

		void KadSearchIPByNodeIDResult(Kademlia::EKadClientSearchRes eStatus, uint32 dwIP, uint16 nPort) override
		{
			m_eLastIpStatus = eStatus;
			m_dwLastIP = dwIP;
			m_uLastPort = nPort;
		}

		Kademlia::EKadClientSearchRes m_eLastNodeStatus = Kademlia::KCSR_TIMEOUT;
		Kademlia::EKadClientSearchRes m_eLastIpStatus = Kademlia::KCSR_TIMEOUT;
		const uchar *m_pLastNodeID = nullptr;
		uint32 m_dwLastIP = 0;
		uint16 m_uLastPort = 0;
	};
}

TEST_SUITE_BEGIN("kad-base");

TEST_CASE("Kad base constants keep the expected query and bucket sizing contract")
{
	CHECK_EQ(static_cast<unsigned>(K), 10u);
	CHECK_EQ(static_cast<unsigned>(KBASE), 4u);
	CHECK_EQ(static_cast<unsigned>(KK), 5u);
	CHECK_EQ(static_cast<unsigned>(ALPHA_QUERY), 3u);
	CHECK(SEARCHFILE_LIFETIME >= SEARCHNODECOMP_LIFETIME);
	CHECK(SEARCHFINDSOURCE_TOTAL >= SEARCHSTOREFILE_TOTAL);
}

TEST_CASE("Kad client searcher reports the stable result vocabulary ordering")
{
	CHECK_EQ(static_cast<int>(Kademlia::KCSR_SUCCEEDED), 0);
	CHECK_EQ(static_cast<int>(Kademlia::KCSR_NOTFOUND), 1);
	CHECK_EQ(static_cast<int>(Kademlia::KCSR_TIMEOUT), 2);
}

TEST_CASE("Kad client searcher callbacks preserve the delivered status and endpoint snapshot")
{
	RecordingKadClientSearcher searcher;
	const uchar byNodeID[16] = {};

	searcher.KadSearchNodeIDByIPResult(Kademlia::KCSR_SUCCEEDED, byNodeID);
	searcher.KadSearchIPByNodeIDResult(Kademlia::KCSR_NOTFOUND, 0x01020304u, 4672u);

	CHECK_EQ(searcher.m_eLastNodeStatus, Kademlia::KCSR_SUCCEEDED);
	CHECK_EQ(searcher.m_pLastNodeID, byNodeID);
	CHECK_EQ(searcher.m_eLastIpStatus, Kademlia::KCSR_NOTFOUND);
	CHECK_EQ(searcher.m_dwLastIP, static_cast<uint32>(0x01020304u));
	CHECK_EQ(searcher.m_uLastPort, static_cast<uint16>(4672u));
}

TEST_CASE("Kad client searcher preserves timeout and null-node responses")
{
	RecordingKadClientSearcher searcher;

	searcher.KadSearchNodeIDByIPResult(Kademlia::KCSR_TIMEOUT, nullptr);
	searcher.KadSearchIPByNodeIDResult(Kademlia::KCSR_TIMEOUT, 0u, 0u);

	CHECK_EQ(searcher.m_eLastNodeStatus, Kademlia::KCSR_TIMEOUT);
	CHECK_EQ(searcher.m_pLastNodeID, nullptr);
	CHECK_EQ(searcher.m_eLastIpStatus, Kademlia::KCSR_TIMEOUT);
	CHECK_EQ(searcher.m_dwLastIP, static_cast<uint32>(0u));
	CHECK_EQ(searcher.m_uLastPort, static_cast<uint16>(0u));
}

TEST_CASE("Kad base timing constants remain monotonically ordered for search expiry logic")
{
	CHECK(SEARCHKEYWORD_LIFETIME >= SEARCHNODE_LIFETIME);
	CHECK(SEARCHFINDSOURCE_LIFETIME >= SEARCHNODECOMP_LIFETIME);
	CHECK(SEARCHSTOREFILE_LIFETIME >= SEARCHSTOREFILE_TOTAL);
}

TEST_SUITE_END;
