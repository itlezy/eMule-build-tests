#include "../third_party/doctest/doctest.h"

#include "../include/LongPathTestSupport.h"

#include "LogFileSeams.h"

#include <algorithm>
#include <vector>
#include <windows.h>

TEST_SUITE_BEGIN("parity");

TEST_CASE("Log-file seam preserves long rotated backup names without MAX_PATH truncation")
{
	LongPathTestSupport::ScopedLongPathFixture fixture;
	INFO(fixture.LastError());
	REQUIRE(fixture.Initialize(true, 0u, 0x4C4F4721u));

	const std::wstring rawLogPath = fixture.MakeDirectoryChildPath(L"downloads odd-[log].log");
	const std::vector<BYTE> payload = LongPathTestSupport::BuildDeterministicPayload(1231u, 0xABCDu);
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::WriteBytes(rawLogPath, payload));

	CString strLogPath(rawLogPath.c_str());
	const CString strExpectedPrefix = strLogPath.Left(strLogPath.ReverseFind(_T('.')));

	const CString strRotatedPath = LogFileSeams::BuildRotatedLogFilePath(strLogPath, _T("2026.04.11 15.00.00"));
	const std::wstring rotatedPath(strRotatedPath.GetString());
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::MoveFileReplace(rawLogPath, rotatedPath));

	std::vector<BYTE> roundTrip;
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::ReadBytes(rotatedPath, roundTrip));
	std::vector<std::wstring> names;
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::EnumerateFileNames(fixture.DirectoryPath(), names));

	CHECK(strLogPath.GetLength() > MAX_PATH);
	CHECK(strRotatedPath == strExpectedPrefix + CString(_T(" - 2026.04.11 15.00.00.log")));
	CHECK(roundTrip == payload);
	CHECK(std::find(names.begin(), names.end(), std::wstring(L"downloads odd-[log] - 2026.04.11 15.00.00.log")) != names.end());
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::DeleteFilePath(rotatedPath));
}

TEST_CASE("Log-file seam preserves extensionless and slash-separated rotation targets")
{
	CHECK(LogFileSeams::BuildRotatedLogFilePath(CString(_T("downloads")), _T("ts")) == CString(_T("downloads - ts")));
	CHECK(LogFileSeams::BuildRotatedLogFilePath(CString(_T("C:/logs/downloads.log")), _T("ts")) == CString(_T("C:/logs/downloads - ts.log")));
}

TEST_SUITE_END;
