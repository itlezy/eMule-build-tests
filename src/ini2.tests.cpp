#include "../third_party/doctest/doctest.h"

#include "../include/LongPathTestSupport.h"

#include "Ini2Helpers.h"
#include "PathHelpers.h"

#include <vector>
#include <windows.h>

TEST_SUITE_BEGIN("parity");

namespace
{
CString RepeatCString(LPCTSTR pszFragment, const int nCount)
{
	CString strRepeated;
	for (int i = 0; i < nCount; ++i)
		strRepeated += pszFragment;
	return strRepeated;
}
}

TEST_CASE("Ini2 seam prefixes relative ini paths from long base directories without truncation")
{
	LongPathTestSupport::ScopedLongPathFixture fixture;
	INFO(fixture.LastError());
	REQUIRE(fixture.Initialize(true, 0u, 0x494E4932u));

	const CString strBase(fixture.DirectoryPath().c_str());
	const CString strRelative(_T("prefs\\user.ini"));

	const CString strResolved = Ini2Helpers::BuildPathFromBaseDirectory(strBase, strRelative);
	const std::wstring prefsDirectory = fixture.MakeDirectoryChildPath(L"prefs");
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::EnsureDirectoryTree(fixture.DirectoryPath(), prefsDirectory));
	const std::wstring resolvedPath(strResolved.GetString());
	const std::vector<BYTE> payload = LongPathTestSupport::BuildDeterministicPayload(515u, 0x1A2Bu);
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::WriteBytes(resolvedPath, payload));

	std::vector<BYTE> roundTrip;
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::ReadBytes(resolvedPath, roundTrip));

	CHECK(strResolved.GetLength() > MAX_PATH);
	CHECK(Ini2Helpers::NeedsBaseDirectoryPrefix(strRelative));
	CHECK_FALSE(Ini2Helpers::NeedsBaseDirectoryPrefix(CString(_T("C:\\prefs\\user.ini"))));
	CHECK_FALSE(Ini2Helpers::NeedsBaseDirectoryPrefix(CString(_T("\\\\server\\share\\user.ini"))));
	CHECK(strResolved == PathHelpers::EnsureTrailingSeparator(strBase) + strRelative);
	CHECK(roundTrip == payload);
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::DeleteFilePath(resolvedPath));
	REQUIRE(::RemoveDirectoryW(LongPathTestSupport::PreparePathForLongPath(prefsDirectory).c_str()) != FALSE);
}

TEST_CASE("Ini2 seam builds default ini file paths from module and current directories")
{
	CString strModulePath(_T("C:\\apps\\eMule.exe"));
	CString strCurrentDirectory(_T("D:\\profiles"));
	CHECK(Ini2Helpers::BuildDefaultIniFilePath(strModulePath, strCurrentDirectory, true) == CString(_T("C:\\apps\\eMule.ini")));
	CHECK(Ini2Helpers::BuildDefaultIniFilePath(strModulePath, strCurrentDirectory, false) == CString(_T("D:\\profiles\\eMule.ini")));

	LongPathTestSupport::ScopedLongPathFixture fixture;
	INFO(fixture.LastError());
	REQUIRE(fixture.Initialize(true, 0u, 0x32494E49u));

	const std::wstring modulePath = fixture.MakeDirectoryChildPath(L"client.build.exe");
	const std::wstring currentDirectory = fixture.MakeDirectoryChildPath(L"profiles");
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::EnsureDirectoryTree(fixture.DirectoryPath(), currentDirectory));
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::WriteBytes(modulePath, LongPathTestSupport::BuildDeterministicPayload(271u, 0xE0E0u)));

	const CString strLongModulePath(modulePath.c_str());
	const CString strLongCurrentDirectory(currentDirectory.c_str());
	const CString strLongIniPath = Ini2Helpers::BuildDefaultIniFilePath(strLongModulePath, strLongCurrentDirectory, true);
	const CString strCurrentIniPath = Ini2Helpers::BuildDefaultIniFilePath(strLongModulePath, strLongCurrentDirectory, false);
	CHECK(strLongIniPath.GetLength() > MAX_PATH);
	CHECK(strLongIniPath == PathHelpers::EnsureTrailingSeparator(PathHelpers::GetDirectoryPath(strLongModulePath)) + CString(_T("client.build.ini")));
	CHECK(strCurrentIniPath == PathHelpers::EnsureTrailingSeparator(strLongCurrentDirectory) + CString(_T("client.build.ini")));

	const std::wstring moduleIniPath(strLongIniPath.GetString());
	const std::wstring currentIniPath(strCurrentIniPath.GetString());
	const std::vector<BYTE> moduleIniPayload = LongPathTestSupport::BuildDeterministicPayload(129u, 0xAAAAu);
	const std::vector<BYTE> currentIniPayload = LongPathTestSupport::BuildDeterministicPayload(131u, 0xBBBBu);
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::WriteBytes(moduleIniPath, moduleIniPayload));
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::WriteBytes(currentIniPath, currentIniPayload));

	std::vector<BYTE> moduleIniRoundTrip;
	std::vector<BYTE> currentIniRoundTrip;
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::ReadBytes(moduleIniPath, moduleIniRoundTrip));
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::ReadBytes(currentIniPath, currentIniRoundTrip));
	CHECK(moduleIniRoundTrip == moduleIniPayload);
	CHECK(currentIniRoundTrip == currentIniPayload);

	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::DeleteFilePath(modulePath));
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::DeleteFilePath(moduleIniPath));
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::DeleteFilePath(currentIniPath));
	REQUIRE(::RemoveDirectoryW(LongPathTestSupport::PreparePathForLongPath(currentDirectory).c_str()) != FALSE);
}

TEST_CASE("Ini2 seam grows TCHAR profile-string buffers past the old 256 character limit")
{
	const CString strExpected = CString(_T("C:\\profiles\\")) + RepeatCString(_T("segment\\"), 70) + CString(_T("incoming\\"));
	const CString strActual = Ini2Helpers::ReadProfileStringDynamic<CString>(
		[&](LPTSTR pszBuffer, DWORD dwCapacity) -> DWORD {
			const DWORD dwRequired = static_cast<DWORD>(strExpected.GetLength());
			const DWORD dwToCopy = (dwCapacity > 0)
				? (dwRequired < (dwCapacity - 1) ? dwRequired : (dwCapacity - 1))
				: 0;
			for (DWORD i = 0; i < dwToCopy; ++i)
				pszBuffer[i] = strExpected[i];
			if (dwCapacity > 0)
				pszBuffer[dwToCopy] = _T('\0');
			return (dwToCopy == dwRequired) ? dwRequired : (dwCapacity - 1);
		});

	CHECK(strActual == strExpected);
	CHECK(strActual.GetLength() > 256);
}

TEST_CASE("Ini2 seam grows UTF-8 profile-string buffers past the old 256 character limit")
{
	const CStringA strExpected("Category-");
	CStringA strRepeated;
	for (int i = 0; i < 40; ++i)
		strRepeated += "naive-utf8-";
	const CStringA strFullExpected = strExpected + strRepeated + CStringA("fin");
	const CStringA strActual = Ini2Helpers::ReadProfileStringDynamic<CStringA>(
		[&](LPSTR pszBuffer, DWORD dwCapacity) -> DWORD {
			const DWORD dwRequired = static_cast<DWORD>(strFullExpected.GetLength());
			const DWORD dwToCopy = (dwCapacity > 0)
				? (dwRequired < (dwCapacity - 1) ? dwRequired : (dwCapacity - 1))
				: 0;
			for (DWORD i = 0; i < dwToCopy; ++i)
				pszBuffer[i] = strFullExpected[i];
			if (dwCapacity > 0)
				pszBuffer[dwToCopy] = '\0';
			return (dwToCopy == dwRequired) ? dwRequired : (dwCapacity - 1);
		});

	CHECK(strActual == strFullExpected);
	CHECK(strActual.GetLength() > 256);
}

TEST_SUITE_END;
