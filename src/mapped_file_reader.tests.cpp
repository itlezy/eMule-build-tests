#include "../third_party/doctest/doctest.h"

#include <cstdio>
#include <string>
#include <vector>

#include "MappedFileReader.h"

namespace
{
	class CCollectMappedBytes : public IMappedFileRangeVisitor
	{
	public:
		void OnMappedFileBytes(const BYTE *pBytes, size_t nByteCount) override
		{
			m_Data.insert(m_Data.end(), pBytes, pBytes + nByteCount);
		}

		std::vector<BYTE> m_Data;
	};

	/**
	 * Creates deterministic byte content so range comparisons stay readable.
	 */
	std::vector<BYTE> CreateMappedReaderFixture(size_t nByteCount)
	{
		std::vector<BYTE> data(nByteCount);
		for (size_t i = 0; i < nByteCount; ++i)
			data[i] = static_cast<BYTE>((i * 37u + 11u) & 0xFFu);
		return data;
	}

	/**
	 * Creates a disposable binary file path for mapped-reader regression tests.
	 */
	std::wstring CreateMappedReaderTempPath()
	{
		WCHAR szTempPath[MAX_PATH] = {};
		WCHAR szTempFile[MAX_PATH] = {};
		REQUIRE(::GetTempPathW(_countof(szTempPath), szTempPath) != 0);
		REQUIRE(::GetTempFileNameW(szTempPath, L"mmr", 0, szTempFile) != 0);
		return std::wstring(szTempFile);
	}

	/**
	 * Writes the supplied binary fixture to disk for mapped file reads.
	 */
	void WriteMappedReaderFixture(const std::wstring &rstrPath, const std::vector<BYTE> &rData)
	{
		FILE *pFile = NULL;
		REQUIRE(_wfopen_s(&pFile, rstrPath.c_str(), L"wb") == 0);
		REQUIRE(pFile != NULL);
		REQUIRE(fwrite(rData.data(), 1, rData.size(), pFile) == rData.size());
		fclose(pFile);
	}

	/**
	 * Returns a byte-exact slice for parity assertions.
	 */
	std::vector<BYTE> SliceMappedReaderFixture(const std::vector<BYTE> &rData, size_t nOffset, size_t nLength)
	{
		return std::vector<BYTE>(rData.begin() + nOffset, rData.begin() + nOffset + nLength);
	}
}

TEST_SUITE_BEGIN("parity");

TEST_CASE("Mapped file reader returns the exact requested slice across allocation boundaries")
{
	const std::vector<BYTE> fixture = CreateMappedReaderFixture(1024 * 1024 + 257);
	const std::wstring tempPath = CreateMappedReaderTempPath();
	WriteMappedReaderFixture(tempPath, fixture);

	HANDLE hFile = ::CreateFileW(tempPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	REQUIRE(hFile != INVALID_HANDLE_VALUE);

	CCollectMappedBytes visitor;
	DWORD dwError = ERROR_SUCCESS;
	const size_t nOffset = 65530;
	const size_t nLength = 400000;
	CHECK(VisitMappedFileRange(hFile, nOffset, nLength, visitor, &dwError));
	CHECK_EQ(dwError, static_cast<DWORD>(ERROR_SUCCESS));
	CHECK(visitor.m_Data == SliceMappedReaderFixture(fixture, nOffset, nLength));

	::CloseHandle(hFile);
	::DeleteFileW(tempPath.c_str());
}

TEST_CASE("Mapped file reader spans multiple mapping windows without dropping bytes")
{
	const std::vector<BYTE> fixture = CreateMappedReaderFixture(9 * 1024 * 1024 + 123);
	const std::wstring tempPath = CreateMappedReaderTempPath();
	WriteMappedReaderFixture(tempPath, fixture);

	HANDLE hFile = ::CreateFileW(tempPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	REQUIRE(hFile != INVALID_HANDLE_VALUE);

	CCollectMappedBytes visitor;
	DWORD dwError = ERROR_SUCCESS;
	const size_t nOffset = 123;
	const size_t nLength = fixture.size() - 246;
	CHECK(VisitMappedFileRange(hFile, nOffset, nLength, visitor, &dwError));
	CHECK_EQ(dwError, static_cast<DWORD>(ERROR_SUCCESS));
	CHECK(visitor.m_Data == SliceMappedReaderFixture(fixture, nOffset, nLength));

	::CloseHandle(hFile);
	::DeleteFileW(tempPath.c_str());
}

TEST_CASE("Mapped file reader accepts zero-length ranges without touching the visitor")
{
	const std::vector<BYTE> fixture = CreateMappedReaderFixture(128);
	const std::wstring tempPath = CreateMappedReaderTempPath();
	WriteMappedReaderFixture(tempPath, fixture);

	HANDLE hFile = ::CreateFileW(tempPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	REQUIRE(hFile != INVALID_HANDLE_VALUE);

	CCollectMappedBytes visitor;
	DWORD dwError = ERROR_SUCCESS;
	CHECK(VisitMappedFileRange(hFile, 0, 0, visitor, &dwError));
	CHECK_EQ(dwError, static_cast<DWORD>(ERROR_SUCCESS));
	CHECK(visitor.m_Data.empty());

	::CloseHandle(hFile);
	::DeleteFileW(tempPath.c_str());
}

TEST_CASE("Mapped file reader reports invalid handles")
{
	CCollectMappedBytes visitor;
	DWORD dwError = ERROR_SUCCESS;
	CHECK_FALSE(VisitMappedFileRange(INVALID_HANDLE_VALUE, 0, 1, visitor, &dwError));
	CHECK_EQ(dwError, static_cast<DWORD>(ERROR_INVALID_HANDLE));
}

TEST_SUITE_END;
