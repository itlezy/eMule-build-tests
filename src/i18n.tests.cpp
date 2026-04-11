#include "../third_party/doctest/doctest.h"

#include "../include/LongPathTestSupport.h"

#include "I18nSeams.h"

#include <vector>
#include <windows.h>

TEST_SUITE_BEGIN("parity");

TEST_CASE("I18n seam extracts the full language DLL basename without _MAX_FNAME truncation")
{
	LongPathTestSupport::ScopedLongPathFixture fixture;
	INFO(fixture.LastError());
	REQUIRE(fixture.Initialize(true, 0u, 0x4931384Eu));

	const std::wstring dllPath = fixture.MakeDirectoryChildPath(L"va_ES_RACV.dll");
	const std::vector<BYTE> payload = LongPathTestSupport::BuildDeterministicPayload(333u, 0xD11u);
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::WriteBytes(dllPath, payload));

	CString strFileName(dllPath.c_str());
	std::vector<BYTE> roundTrip;
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::ReadBytes(dllPath, roundTrip));

	CHECK(strFileName.GetLength() > MAX_PATH);
	CHECK(I18nSeams::ExtractLanguageDllBaseName(strFileName) == CString(_T("va_ES_RACV")));
	CHECK(roundTrip == payload);
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::DeleteFilePath(dllPath));
}

TEST_CASE("I18n seam handles plain filenames and forward slashes")
{
	CHECK(I18nSeams::ExtractLanguageDllBaseName(CString(_T("en_US.dll"))) == CString(_T("en_US")));
	CHECK(I18nSeams::ExtractLanguageDllBaseName(CString(_T("C:/lang/pt_BR.dll"))) == CString(_T("pt_BR")));
}

TEST_SUITE_END;
