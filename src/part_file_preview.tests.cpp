#include "../third_party/doctest/doctest.h"

#include "PartFilePreviewSeams.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("Part-file preview seam extracts VLC from a long configured player path without truncation")
{
	CString strLongPath(_T("C:\\players"));
	for (int i = 0; i < 24; ++i)
		strLongPath += _T("\\segmentsegment");
	strLongPath += _T("\\vlc.exe");

	CHECK(strLongPath.GetLength() > MAX_PATH);
	CHECK(PartFilePreviewSeams::ExtractConfiguredVideoPlayerBaseName(strLongPath) == CString(_T("vlc")));
}

TEST_CASE("Part-file preview seam handles slash variants and extensionless commands")
{
	CHECK(PartFilePreviewSeams::ExtractConfiguredVideoPlayerBaseName(CString(_T("C:/apps/VideoLAN/VLC.EXE"))) == CString(_T("VLC")));
	CHECK(PartFilePreviewSeams::ExtractConfiguredVideoPlayerBaseName(CString(_T("vlc"))) == CString(_T("vlc")));
	CHECK(PartFilePreviewSeams::ExtractConfiguredVideoPlayerBaseName(CString(_T("C:\\tools\\player.with.dots\\mpv.com"))) == CString(_T("mpv")));
}

TEST_SUITE_END;
