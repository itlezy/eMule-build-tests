#include "../third_party/doctest/doctest.h"

#include "Ini2Seams.h"

#include <windows.h>

TEST_SUITE_BEGIN("parity");

TEST_CASE("Ini2 seam prefixes relative ini paths from long base directories without truncation")
{
	CString strBase(_T("C:\\config"));
	for (int i = 0; i < 24; ++i)
		strBase += _T("\\segmentsegment");
	const CString strRelative(_T("prefs\\user.ini"));

	const CString strResolved = Ini2Seams::BuildPathFromBaseDirectory(strBase, strRelative);
	CHECK(strResolved.GetLength() > MAX_PATH);
	CHECK(Ini2Seams::NeedsBaseDirectoryPrefix(strRelative));
	CHECK_FALSE(Ini2Seams::NeedsBaseDirectoryPrefix(CString(_T("C:\\prefs\\user.ini"))));
	CHECK_FALSE(Ini2Seams::NeedsBaseDirectoryPrefix(CString(_T("\\\\server\\share\\user.ini"))));
	CHECK(strResolved == Ini2Seams::EnsureTrailingSlash(strBase) + strRelative);
}

TEST_CASE("Ini2 seam builds default ini file paths from module and current directories")
{
	CString strModulePath(_T("C:\\apps\\eMule.exe"));
	CString strCurrentDirectory(_T("D:\\profiles"));
	CHECK(Ini2Seams::BuildDefaultIniFilePath(strModulePath, strCurrentDirectory, true) == CString(_T("C:\\apps\\eMule.ini")));
	CHECK(Ini2Seams::BuildDefaultIniFilePath(strModulePath, strCurrentDirectory, false) == CString(_T("D:\\profiles\\eMule.ini")));

	CString strLongModulePath(_T("C:\\apps"));
	for (int i = 0; i < 24; ++i)
		strLongModulePath += _T("\\segmentsegment");
	strLongModulePath += _T("\\client.build.exe");
	const CString strLongIniPath = Ini2Seams::BuildDefaultIniFilePath(strLongModulePath, strCurrentDirectory, true);
	CHECK(strLongIniPath.GetLength() > MAX_PATH);
	CHECK(strLongIniPath == Ini2Seams::ExtractDirectoryPath(strLongModulePath) + CString(_T("client.build.ini")));
}

TEST_SUITE_END;
