#include "../third_party/doctest/doctest.h"

#include "../include/LongPathTestSupport.h"

#include "Ini2Seams.h"

#include <vector>
#include <windows.h>

TEST_SUITE_BEGIN("parity");

TEST_CASE("Ini2 seam prefixes relative ini paths from long base directories without truncation")
{
	LongPathTestSupport::ScopedLongPathFixture fixture;
	INFO(fixture.LastError());
	REQUIRE(fixture.Initialize(true, 0u, 0x494E4932u));

	const CString strBase(fixture.DirectoryPath().c_str());
	const CString strRelative(_T("prefs\\user.ini"));

	const CString strResolved = Ini2Seams::BuildPathFromBaseDirectory(strBase, strRelative);
	const std::wstring prefsDirectory = fixture.MakeDirectoryChildPath(L"prefs");
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::EnsureDirectoryTree(fixture.DirectoryPath(), prefsDirectory));
	const std::wstring resolvedPath(strResolved.GetString());
	const std::vector<BYTE> payload = LongPathTestSupport::BuildDeterministicPayload(515u, 0x1A2Bu);
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::WriteBytes(resolvedPath, payload));

	std::vector<BYTE> roundTrip;
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::ReadBytes(resolvedPath, roundTrip));

	CHECK(strResolved.GetLength() > MAX_PATH);
	CHECK(Ini2Seams::NeedsBaseDirectoryPrefix(strRelative));
	CHECK_FALSE(Ini2Seams::NeedsBaseDirectoryPrefix(CString(_T("C:\\prefs\\user.ini"))));
	CHECK_FALSE(Ini2Seams::NeedsBaseDirectoryPrefix(CString(_T("\\\\server\\share\\user.ini"))));
	CHECK(strResolved == Ini2Seams::EnsureTrailingSlash(strBase) + strRelative);
	CHECK(roundTrip == payload);
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::DeleteFilePath(resolvedPath));
	REQUIRE(::RemoveDirectoryW(LongPathTestSupport::PreparePathForLongPath(prefsDirectory).c_str()) != FALSE);
}

TEST_CASE("Ini2 seam builds default ini file paths from module and current directories")
{
	CString strModulePath(_T("C:\\apps\\eMule.exe"));
	CString strCurrentDirectory(_T("D:\\profiles"));
	CHECK(Ini2Seams::BuildDefaultIniFilePath(strModulePath, strCurrentDirectory, true) == CString(_T("C:\\apps\\eMule.ini")));
	CHECK(Ini2Seams::BuildDefaultIniFilePath(strModulePath, strCurrentDirectory, false) == CString(_T("D:\\profiles\\eMule.ini")));

	LongPathTestSupport::ScopedLongPathFixture fixture;
	INFO(fixture.LastError());
	REQUIRE(fixture.Initialize(true, 0u, 0x32494E49u));

	const std::wstring modulePath = fixture.MakeDirectoryChildPath(L"client.build.exe");
	const std::wstring currentDirectory = fixture.MakeDirectoryChildPath(L"profiles");
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::EnsureDirectoryTree(fixture.DirectoryPath(), currentDirectory));
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::WriteBytes(modulePath, LongPathTestSupport::BuildDeterministicPayload(271u, 0xE0E0u)));

	const CString strLongModulePath(modulePath.c_str());
	const CString strLongCurrentDirectory(currentDirectory.c_str());
	const CString strLongIniPath = Ini2Seams::BuildDefaultIniFilePath(strLongModulePath, strLongCurrentDirectory, true);
	const CString strCurrentIniPath = Ini2Seams::BuildDefaultIniFilePath(strLongModulePath, strLongCurrentDirectory, false);
	CHECK(strLongIniPath.GetLength() > MAX_PATH);
	CHECK(strLongIniPath == Ini2Seams::ExtractDirectoryPath(strLongModulePath) + CString(_T("client.build.ini")));
	CHECK(strCurrentIniPath == Ini2Seams::EnsureTrailingSlash(strLongCurrentDirectory) + CString(_T("client.build.ini")));

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

TEST_SUITE_END;
