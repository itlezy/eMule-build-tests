#include "../third_party/doctest/doctest.h"

#include "PartFileCompletionSeams.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("Part-file completion seam only warns about disabled long-path support for plausible move failures")
{
	CString strLongPath(_T("C:\\incoming"));
	for (int i = 0; i < 24; ++i)
		strLongPath += _T("\\segmentsegment");
	strLongPath += _T("\\finished.bin");

	CHECK(strLongPath.GetLength() > MAX_PATH);
	CHECK(PartFileCompletionSeams::ShouldWarnAboutDisabledLongPathSupport(ERROR_FILENAME_EXCED_RANGE, strLongPath, false));
	CHECK(PartFileCompletionSeams::ShouldWarnAboutDisabledLongPathSupport(ERROR_PATH_NOT_FOUND, strLongPath, false));
	CHECK_FALSE(PartFileCompletionSeams::ShouldWarnAboutDisabledLongPathSupport(ERROR_ACCESS_DENIED, strLongPath, false));
}

TEST_CASE("Part-file completion seam skips disabled-long-path warnings for supported or short-path cases")
{
	const CString strShortPath(_T("C:\\incoming\\finished.bin"));
	CString strLongPath(_T("C:\\incoming"));
	for (int i = 0; i < 24; ++i)
		strLongPath += _T("\\segmentsegment");
	strLongPath += _T("\\finished.bin");

	CHECK_FALSE(PartFileCompletionSeams::ShouldWarnAboutDisabledLongPathSupport(ERROR_FILENAME_EXCED_RANGE, strLongPath, true));
	CHECK_FALSE(PartFileCompletionSeams::ShouldWarnAboutDisabledLongPathSupport(ERROR_FILENAME_EXCED_RANGE, strShortPath, false));
}

TEST_SUITE_END;
