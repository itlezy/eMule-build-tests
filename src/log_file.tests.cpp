#include "../third_party/doctest/doctest.h"

#include "LogFileSeams.h"

#include <windows.h>

TEST_SUITE_BEGIN("parity");

TEST_CASE("Log-file seam preserves long rotated backup names without MAX_PATH truncation")
{
	CString strLogPath(_T("C:\\logs"));
	CString strExpectedPrefix(_T("C:\\logs"));
	for (int i = 0; i < 24; ++i) {
		strLogPath += _T("\\segmentsegment");
		strExpectedPrefix += _T("\\segmentsegment");
	}
	strLogPath += _T("\\downloads.log");
	strExpectedPrefix += _T("\\downloads");

	const CString strRotatedPath = LogFileSeams::BuildRotatedLogFilePath(strLogPath, _T("2026.04.11 15.00.00"));
	CHECK(strLogPath.GetLength() > MAX_PATH);
	CHECK(strRotatedPath == strExpectedPrefix + CString(_T(" - 2026.04.11 15.00.00.log")));
}

TEST_CASE("Log-file seam preserves extensionless and slash-separated rotation targets")
{
	CHECK(LogFileSeams::BuildRotatedLogFilePath(CString(_T("downloads")), _T("ts")) == CString(_T("downloads - ts")));
	CHECK(LogFileSeams::BuildRotatedLogFilePath(CString(_T("C:/logs/downloads.log")), _T("ts")) == CString(_T("C:/logs/downloads - ts.log")));
}

TEST_SUITE_END;
