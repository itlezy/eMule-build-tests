#include "../third_party/doctest/doctest.h"

#include "DownloadQueueOverviewSeams.h"

namespace
{
	CString BuildLongPartFileName()
	{
		CString strFileName;
		for (int i = 0; i < _MAX_FNAME + 32; ++i)
			strFileName.AppendChar(_T('a'));
		strFileName += _T(".part");
		return strFileName;
	}
}

TEST_SUITE_BEGIN("parity");

TEST_CASE("Queue overview seam preserves full single-temp part file names without truncation")
{
	const CString strFileName = BuildLongPartFileName();
	const CString strPartFilePath = CString(_T("C:\\Temp\\")) + strFileName;

	CHECK(DownloadQueueOverviewSeams::GetPartMetOverviewDisplayName(strPartFilePath, CString(_T("unused")), true) == strFileName);
}

TEST_CASE("Queue overview seam keeps multi-temp export on the logical full-name column")
{
	const CString strPartFilePath(_T("D:\\Temp\\001.part"));
	const CString strFullName(_T("Visible Release Name.iso"));

	CHECK(DownloadQueueOverviewSeams::GetPartMetOverviewDisplayName(strPartFilePath, strFullName, false) == strFullName);
}

TEST_SUITE_END;
