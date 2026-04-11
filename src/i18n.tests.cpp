#include "../third_party/doctest/doctest.h"

#include "I18nSeams.h"

#include <windows.h>

TEST_SUITE_BEGIN("parity");

TEST_CASE("I18n seam extracts the full language DLL basename without _MAX_FNAME truncation")
{
	CString strFileName(_T("C:\\lang"));
	for (int i = 0; i < 24; ++i)
		strFileName += _T("\\segmentsegment");
	strFileName += _T("\\va_ES_RACV.dll");

	CHECK(strFileName.GetLength() > MAX_PATH);
	CHECK(I18nSeams::ExtractLanguageDllBaseName(strFileName) == CString(_T("va_ES_RACV")));
}

TEST_CASE("I18n seam handles plain filenames and forward slashes")
{
	CHECK(I18nSeams::ExtractLanguageDllBaseName(CString(_T("en_US.dll"))) == CString(_T("en_US")));
	CHECK(I18nSeams::ExtractLanguageDllBaseName(CString(_T("C:/lang/pt_BR.dll"))) == CString(_T("pt_BR")));
}

TEST_SUITE_END;
