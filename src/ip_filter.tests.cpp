#include "../third_party/doctest/doctest.h"

#include "IPFilterSeams.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("IP-filter seam classifies long path names without fixed-buffer truncation")
{
	CString strBase(_T("C:\\filters"));
	for (int i = 0; i < 24; ++i)
		strBase += _T("\\segmentsegment");

	CHECK(IPFilterSeams::DetectFileTypeFromPath(strBase + _T("\\guarding.p2p.txt")) == IPFilterSeams::PathHintPeerGuardian);
	CHECK(IPFilterSeams::DetectFileTypeFromPath(strBase + _T("\\custom.prefix")) == IPFilterSeams::PathHintFilterDat);
	CHECK(IPFilterSeams::DetectFileTypeFromPath(strBase + _T("\\custom.p2p")) == IPFilterSeams::PathHintPeerGuardian);
}

TEST_CASE("IP-filter seam leaves unknown file hints to content sniffing")
{
	CHECK(IPFilterSeams::DetectFileTypeFromPath(CString(_T("ipfilter.dat"))) == IPFilterSeams::PathHintUnknown);
	CHECK(IPFilterSeams::DetectFileTypeFromPath(CString(_T("C:/filters/list.txt"))) == IPFilterSeams::PathHintUnknown);
}

TEST_SUITE_END;
