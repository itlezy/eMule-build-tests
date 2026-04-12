#include "../third_party/doctest/doctest.h"

#include "../include/LongPathTestSupport.h"

#include "LongPathSeams.h"
#include "OtherFunctionsSeams.h"
#include "PathHelperSeams.h"

#include <windows.h>

TEST_SUITE_BEGIN("parity");

namespace
{
CString RepeatPathFragment(LPCTSTR pszFragment, const int nCount)
{
	CString strRepeated;
	for (int i = 0; i < nCount; ++i)
		strRepeated += pszFragment;
	return strRepeated;
}
}

TEST_CASE("Other-functions seam strips Win32 long-path prefixes before shell parsing")
{
	CHECK(OtherFunctionsSeams::PreparePathForShellOperation(CString(_T("\\\\?\\C:\\deep\\leaf.bin"))) == CString(_T("C:\\deep\\leaf.bin")));
	CHECK(OtherFunctionsSeams::PreparePathForShellOperation(CString(_T("\\\\?\\UNC\\server\\share\\leaf.bin"))) == CString(_T("\\\\server\\share\\leaf.bin")));
	CHECK(OtherFunctionsSeams::PreparePathForShellOperation(CString(_T("C:\\short\\leaf.bin"))) == CString(_T("C:\\short\\leaf.bin")));
}

TEST_CASE("Other-functions seam routes deep unicode deletes through the direct long-path path when recycle-bin delete is disabled")
{
	LongPathTestSupport::ScopedLongPathFixture fixture;
	INFO(fixture.LastError());
	REQUIRE(fixture.Initialize(true, 4097u, 0x0B2201u));

	int nRecycleDeleteCalls = 0;
	int nDirectDeleteCalls = 0;
	const std::wstring filePath = fixture.FilePath();

	const bool bDeleted = OtherFunctionsSeams::ExecuteShellDelete(
		filePath.c_str(),
		false,
		NULL,
		[](LPCTSTR pszPath) { return LongPathSeams::PathExists(pszPath) != FALSE; },
		[&](LPCTSTR, HWND) {
			++nRecycleDeleteCalls;
			return false;
		},
		[&](LPCTSTR pszPath) {
			++nDirectDeleteCalls;
			return LongPathSeams::DeleteFile(pszPath) != FALSE;
		});

	CHECK(bDeleted);
	CHECK_EQ(nRecycleDeleteCalls, 0);
	CHECK_EQ(nDirectDeleteCalls, 1);
	CHECK_FALSE(LongPathSeams::PathExists(filePath.c_str()));
}

TEST_CASE("Other-functions seam routes deep unicode deletes through the recycle-bin path when recycle-bin delete is enabled")
{
	LongPathTestSupport::ScopedLongPathFixture fixture;
	INFO(fixture.LastError());
	REQUIRE(fixture.Initialize(true, 2049u, 0x0B2202u));

	int nRecycleDeleteCalls = 0;
	int nDirectDeleteCalls = 0;
	const std::wstring filePath = fixture.FilePath();

	const bool bDeleted = OtherFunctionsSeams::ExecuteShellDelete(
		filePath.c_str(),
		true,
		reinterpret_cast<HWND>(static_cast<INT_PTR>(0x1234)),
		[](LPCTSTR pszPath) { return LongPathSeams::PathExists(pszPath) != FALSE; },
		[&](LPCTSTR pszPath, HWND hOwnerWindow) {
			++nRecycleDeleteCalls;
			CHECK(CString(pszPath) == CString(filePath.c_str()));
			CHECK(hOwnerWindow == reinterpret_cast<HWND>(static_cast<INT_PTR>(0x1234)));
			return true;
		},
		[&](LPCTSTR) {
			++nDirectDeleteCalls;
			return false;
		});

	CHECK(bDeleted);
	CHECK_EQ(nRecycleDeleteCalls, 1);
	CHECK_EQ(nDirectDeleteCalls, 0);
	CHECK(LongPathSeams::PathExists(filePath.c_str()));
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::DeleteFilePath(filePath));
}

TEST_CASE("Path-helper seam grows module-path buffers past MAX_PATH")
{
	const CString strExpected = CString(_T("C:\\module-root\\")) + RepeatPathFragment(_T("segment\\"), 80) + CString(_T("emule.exe"));

	const CString strActual = PathHelperSeams::GetModuleFilePath(
		reinterpret_cast<HMODULE>(static_cast<INT_PTR>(0x7777)),
		[&](HMODULE hModule, LPTSTR pszBuffer, DWORD cchBuffer) -> DWORD {
			CHECK(hModule == reinterpret_cast<HMODULE>(static_cast<INT_PTR>(0x7777)));
			if (cchBuffer == 0)
				return 0;

			const DWORD cchRequired = static_cast<DWORD>(strExpected.GetLength());
			if (cchBuffer <= cchRequired) {
				const DWORD cchToCopy = cchBuffer - 1;
				for (DWORD i = 0; i < cchToCopy; ++i)
					pszBuffer[i] = strExpected[i];
				pszBuffer[cchToCopy] = _T('\0');
				return cchBuffer;
			}

			for (DWORD i = 0; i < cchRequired; ++i)
				pszBuffer[i] = strExpected[i];
			pszBuffer[cchRequired] = _T('\0');
			return cchRequired;
		});

	CHECK(strActual == strExpected);
	CHECK(strActual.GetLength() > MAX_PATH);
}

TEST_CASE("Path-helper seam joins MediaInfo DLL candidates without MAX_PATH truncation")
{
	const CString strBase = CString(_T("C:\\Program Files\\")) + RepeatPathFragment(_T("MediaInfo\\segment\\"), 40) + CString(_T("bin"));
	const CString strJoined = PathHelperSeams::AppendPathComponent(strBase, _T("MEDIAINFO.DLL"));

	CHECK(strJoined == strBase + CString(_T("\\MEDIAINFO.DLL")));
	CHECK(strJoined.GetLength() > MAX_PATH);
}

TEST_CASE("Path-helper seam canonicalizes overlong paths lexically")
{
	const CString strPrefix = CString(_T("C:\\skins\\")) + RepeatPathFragment(_T("segment\\"), 60);
	const CString strInput = strPrefix + CString(_T(".\\theme\\..\\icons\\logo.gif"));
	const CString strExpected = strPrefix + CString(_T("icons\\logo.gif"));

	CHECK(PathHelperSeams::CanonicalizePath(strInput) == strExpected);
	CHECK(strInput.GetLength() > MAX_PATH);
}

TEST_CASE("Path-helper seam formats MiniMule resource URLs from overlong module paths")
{
	const CString strModulePath = CString(_T("C:\\Program Files\\eMule\\")) + RepeatPathFragment(_T("segment\\"), 70) + CString(_T("emule.exe"));
	const CString strResourceUrl = PathHelperSeams::BuildModuleResourceBaseUrl(
		reinterpret_cast<HMODULE>(static_cast<INT_PTR>(0x2222)),
		[&](HMODULE hModule, LPTSTR pszBuffer, DWORD cchBuffer) -> DWORD {
			CHECK(hModule == reinterpret_cast<HMODULE>(static_cast<INT_PTR>(0x2222)));
			if (cchBuffer == 0)
				return 0;

			const DWORD cchRequired = static_cast<DWORD>(strModulePath.GetLength());
			if (cchBuffer <= cchRequired) {
				const DWORD cchToCopy = cchBuffer - 1;
				for (DWORD i = 0; i < cchToCopy; ++i)
					pszBuffer[i] = strModulePath[i];
				pszBuffer[cchToCopy] = _T('\0');
				return cchBuffer;
			}

			for (DWORD i = 0; i < cchRequired; ++i)
				pszBuffer[i] = strModulePath[i];
			pszBuffer[cchRequired] = _T('\0');
			return cchRequired;
		});

	CHECK(strResourceUrl == CString(_T("res://")) + strModulePath);
	CHECK(strResourceUrl.GetLength() > MAX_PATH);
}

TEST_SUITE_END;
