#include "../third_party/doctest/doctest.h"

#include "PerfLogSeams.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("Perf-log seam preserves long MRTG base paths without MAX_PATH truncation")
{
	CString strConfiguredPath(_T("C:\\logs"));
	for (int i = 0; i < 24; ++i)
		strConfiguredPath += _T("\\segmentsegment");
	strConfiguredPath += _T("\\perflog.mrtg");

	const CString strDataPath = PerfLogSeams::BuildMrtgSidecarPath(strConfiguredPath, _T("_data.mrtg"));
	const CString strOverheadPath = PerfLogSeams::BuildMrtgSidecarPath(strConfiguredPath, _T("_overhead.mrtg"));

	CHECK(strConfiguredPath.GetLength() > MAX_PATH);
	CHECK(strDataPath == strConfiguredPath.Left(strConfiguredPath.ReverseFind(_T('.'))) + CString(_T("_data.mrtg")));
	CHECK(strOverheadPath == strConfiguredPath.Left(strConfiguredPath.ReverseFind(_T('.'))) + CString(_T("_overhead.mrtg")));
}

TEST_CASE("Perf-log seam handles extensionless and slash-separated MRTG inputs")
{
	CHECK(PerfLogSeams::BuildMrtgSidecarPath(CString(_T("perflog")), _T("_data.mrtg")) == CString(_T("perflog_data.mrtg")));
	CHECK(PerfLogSeams::BuildMrtgSidecarPath(CString(_T("C:/stats/perflog.mrtg")), _T("_overhead.mrtg")) == CString(_T("C:/stats/perflog_overhead.mrtg")));
	CHECK(PerfLogSeams::BuildMrtgSidecarPath(CString(_T("C:\\stats.name\\perf.log")), _T("_data.mrtg")) == CString(_T("C:\\stats.name\\perf_data.mrtg")));
}

TEST_SUITE_END;
