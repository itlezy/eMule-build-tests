#include "../third_party/doctest/doctest.h"

#include "../include/LongPathTestSupport.h"

#include "PerfLogSeams.h"

#include <algorithm>
#include <vector>

TEST_SUITE_BEGIN("parity");

TEST_CASE("Perf-log seam preserves long MRTG base paths without MAX_PATH truncation")
{
	LongPathTestSupport::ScopedLongPathFixture fixture;
	INFO(fixture.LastError());
	REQUIRE(fixture.Initialize(true, 0u, 0x50455246u));

	const std::wstring configuredPath = fixture.MakeDirectoryChildPath(L"perf odd-[mrtg].mrtg");
	const std::vector<BYTE> configuredPayload = LongPathTestSupport::BuildDeterministicPayload(257u, 0xC0DEu);
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::WriteBytes(configuredPath, configuredPayload));

	CString strConfiguredPath(configuredPath.c_str());

	const CString strDataPath = PerfLogSeams::BuildMrtgSidecarPath(strConfiguredPath, _T("_data.mrtg"));
	const CString strOverheadPath = PerfLogSeams::BuildMrtgSidecarPath(strConfiguredPath, _T("_overhead.mrtg"));
	const std::wstring dataPath(strDataPath.GetString());
	const std::wstring overheadPath(strOverheadPath.GetString());
	const std::vector<BYTE> dataPayload = LongPathTestSupport::BuildDeterministicPayload(513u, 0xDA7Au);
	const std::vector<BYTE> overheadPayload = LongPathTestSupport::BuildDeterministicPayload(777u, 0x0BEEu);

	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::WriteBytes(dataPath, dataPayload));
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::WriteBytes(overheadPath, overheadPayload));

	std::vector<BYTE> dataRoundTrip;
	std::vector<BYTE> overheadRoundTrip;
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::ReadBytes(dataPath, dataRoundTrip));
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::ReadBytes(overheadPath, overheadRoundTrip));

	std::vector<std::wstring> names;
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::EnumerateFileNames(fixture.DirectoryPath(), names));

	CHECK(strConfiguredPath.GetLength() > MAX_PATH);
	CHECK(strDataPath == strConfiguredPath.Left(strConfiguredPath.ReverseFind(_T('.'))) + CString(_T("_data.mrtg")));
	CHECK(strOverheadPath == strConfiguredPath.Left(strConfiguredPath.ReverseFind(_T('.'))) + CString(_T("_overhead.mrtg")));
	CHECK(dataRoundTrip == dataPayload);
	CHECK(overheadRoundTrip == overheadPayload);
	CHECK(std::find(names.begin(), names.end(), std::wstring(L"perf odd-[mrtg]_data.mrtg")) != names.end());
	CHECK(std::find(names.begin(), names.end(), std::wstring(L"perf odd-[mrtg]_overhead.mrtg")) != names.end());

	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::DeleteFilePath(configuredPath));
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::DeleteFilePath(dataPath));
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::DeleteFilePath(overheadPath));
}

TEST_CASE("Perf-log seam handles extensionless and slash-separated MRTG inputs")
{
	CHECK(PerfLogSeams::BuildMrtgSidecarPath(CString(_T("perflog")), _T("_data.mrtg")) == CString(_T("perflog_data.mrtg")));
	CHECK(PerfLogSeams::BuildMrtgSidecarPath(CString(_T("C:/stats/perflog.mrtg")), _T("_overhead.mrtg")) == CString(_T("C:/stats/perflog_overhead.mrtg")));
	CHECK(PerfLogSeams::BuildMrtgSidecarPath(CString(_T("C:\\stats.name\\perf.log")), _T("_data.mrtg")) == CString(_T("C:\\stats.name\\perf_data.mrtg")));
}

TEST_SUITE_END;
