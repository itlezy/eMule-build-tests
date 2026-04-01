#include "../third_party/doctest/doctest.h"

#include "StartupConfigOverride.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("Startup config override parses an absolute drive path and normalizes it")
{
	TCHAR *argv[] = {
		const_cast<TCHAR*>(_T("emule.exe")),
		const_cast<TCHAR*>(_T("-c")),
		const_cast<TCHAR*>(_T("C:/profiles/test-root"))
	};

	CString strBaseDir;
	CString strError;
	CHECK(StartupConfigOverride::TryParseConfigBaseDirOverride(_countof(argv), argv, strBaseDir, strError));
	CHECK(strError.IsEmpty());
	CHECK(strBaseDir == CString(_T("C:\\profiles\\test-root\\")));
	CHECK(StartupConfigOverride::GetConfigDirectoryFromBaseDir(strBaseDir) == CString(_T("C:\\profiles\\test-root\\config\\")));
	CHECK(StartupConfigOverride::GetLogDirectoryFromBaseDir(strBaseDir) == CString(_T("C:\\profiles\\test-root\\logs\\")));
	CHECK(StartupConfigOverride::GetPreferencesIniPathFromBaseDir(strBaseDir) == CString(_T("C:\\profiles\\test-root\\config\\preferences.ini")));
}

TEST_CASE("Startup config override accepts an absolute UNC base path")
{
	TCHAR *argv[] = {
		const_cast<TCHAR*>(_T("emule.exe")),
		const_cast<TCHAR*>(_T("/c")),
		const_cast<TCHAR*>(_T("\\\\server\\share\\profile"))
	};

	CString strBaseDir;
	CString strError;
	CHECK(StartupConfigOverride::TryParseConfigBaseDirOverride(_countof(argv), argv, strBaseDir, strError));
	CHECK(strError.IsEmpty());
	CHECK(strBaseDir == CString(_T("\\\\server\\share\\profile\\")));
}

TEST_CASE("Startup config override rejects a relative base path")
{
	TCHAR *argv[] = {
		const_cast<TCHAR*>(_T("emule.exe")),
		const_cast<TCHAR*>(_T("-c")),
		const_cast<TCHAR*>(_T("relative\\profile"))
	};

	CString strBaseDir;
	CString strError;
	CHECK_FALSE(StartupConfigOverride::TryParseConfigBaseDirOverride(_countof(argv), argv, strBaseDir, strError));
	CHECK(strError == CString(_T("The -c option requires an absolute eMule base directory.")));
}

TEST_CASE("Startup config override rejects a missing value")
{
	TCHAR *argv[] = {
		const_cast<TCHAR*>(_T("emule.exe")),
		const_cast<TCHAR*>(_T("-c"))
	};

	CString strBaseDir;
	CString strError;
	CHECK_FALSE(StartupConfigOverride::TryParseConfigBaseDirOverride(_countof(argv), argv, strBaseDir, strError));
	CHECK(strError == CString(_T("The -c option requires an absolute eMule base directory.")));
}

TEST_CASE("Startup config override rejects duplicate options")
{
	TCHAR *argv[] = {
		const_cast<TCHAR*>(_T("emule.exe")),
		const_cast<TCHAR*>(_T("-c")),
		const_cast<TCHAR*>(_T("C:\\one")),
		const_cast<TCHAR*>(_T("/c")),
		const_cast<TCHAR*>(_T("C:\\two"))
	};

	CString strBaseDir;
	CString strError;
	CHECK_FALSE(StartupConfigOverride::TryParseConfigBaseDirOverride(_countof(argv), argv, strBaseDir, strError));
	CHECK(strError == CString(_T("The -c option may be specified only once.")));
}
