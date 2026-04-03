#include "../third_party/doctest/doctest.h"
#include "../include/TestSupport.h"

#include "NullGuardSeams.h"

#include <tchar.h>
#include <cstring>
#include <limits>
#include <memory>

namespace
{
	TCHAR *DuplicateFixtureString(LPCTSTR pszSource)
	{
		const size_t nLength = _tcslen(pszSource) + 1u;
		std::unique_ptr<TCHAR[]> pCopy(new TCHAR[nLength]);
		memcpy(pCopy.get(), pszSource, nLength * sizeof(TCHAR));
		return pCopy.release();
	}

	TCHAR *FailToDuplicateFixtureString(LPCTSTR)
	{
		return NULL;
	}
}

TEST_SUITE_BEGIN("parity");

TEST_CASE("Null guard duplicates hello usernames through the supplied allocator")
{
	TCHAR *pszDuplicate = NULL;

	REQUIRE(TryDuplicateCString(_T("alice"), &pszDuplicate, DuplicateFixtureString));
	REQUIRE(pszDuplicate != NULL);
	CHECK(_tcscmp(pszDuplicate, _T("alice")) == 0);

	delete[] pszDuplicate;
}

TEST_CASE("Null guard reports the packed byte count for an exact bitfield")
{
	size_t nPackedByteCount = 0;
	REQUIRE(TryGetPartStatusPackedByteCount(9u, &nPackedByteCount));
	CHECK_EQ(nPackedByteCount, static_cast<size_t>(2));

	size_t nDisplayLength = 0;
	REQUIRE(TryGetPartStatusDisplayLength(9u, &nDisplayLength));
	CHECK_EQ(nDisplayLength, static_cast<size_t>(10));
}

TEST_CASE("Null guard decodes packed part-status bits without overrunning the destination")
{
	const BYTE packedBits[] = {0x0Du, 0x02u};
	uint8 partStatus[9] = {};

	REQUIRE(TryDecodePartStatusBits(partStatus, 9u, packedBits, _countof(packedBits)));
	CHECK_EQ(partStatus[0], static_cast<uint8>(1));
	CHECK_EQ(partStatus[1], static_cast<uint8>(0));
	CHECK_EQ(partStatus[2], static_cast<uint8>(1));
	CHECK_EQ(partStatus[3], static_cast<uint8>(1));
	CHECK_EQ(partStatus[8], static_cast<uint8>(0));
}

TEST_CASE("Null guard rejects username duplication when the allocator fails")
{
	TCHAR *pszDuplicate = reinterpret_cast<TCHAR*>(1);
	CHECK_FALSE(TryDuplicateCString(_T("alice"), &pszDuplicate, FailToDuplicateFixtureString));
	CHECK(pszDuplicate == NULL);
}

TEST_CASE("Null guard rejects truncated packed part-status buffers and overflowed debug lengths")
{
	uint8 partStatus[9] = {};
	const BYTE packedBits[] = {0x0Du};
	size_t nDisplayLength = 0;

	CHECK_FALSE(HasPackedPartStatusBytes(9u, 1u));
	CHECK_FALSE(TryDecodePartStatusBits(partStatus, 9u, packedBits, _countof(packedBits)));
	CHECK_FALSE(TryGetPartStatusDisplayLength((std::numeric_limits<size_t>::max)(), &nDisplayLength));
}

TEST_CASE("Null guard rejects null duplication inputs and missing packed-count outputs")
{
	TCHAR *pszDuplicate = reinterpret_cast<TCHAR*>(1);

	CHECK_FALSE(TryDuplicateCString(NULL, &pszDuplicate, DuplicateFixtureString));
	CHECK(pszDuplicate == reinterpret_cast<TCHAR*>(1));
	CHECK_FALSE(TryDuplicateCString(_T("alice"), NULL, DuplicateFixtureString));
	CHECK_FALSE(TryGetPartStatusPackedByteCount(9u, NULL));
}

TEST_CASE("Null guard handles empty part-status spans without touching the destination")
{
	size_t nPackedByteCount = 1;
	size_t nDisplayLength = 0;
	uint8 partStatus = 0x5Au;
	const BYTE packedBits[] = {0xFFu};

	REQUIRE(TryGetPartStatusPackedByteCount(0u, &nPackedByteCount));
	CHECK_EQ(nPackedByteCount, static_cast<size_t>(0));
	REQUIRE(TryGetPartStatusDisplayLength(0u, &nDisplayLength));
	CHECK_EQ(nDisplayLength, static_cast<size_t>(1));
	CHECK(HasPackedPartStatusBytes(0u, 0u));
	REQUIRE(TryDecodePartStatusBits(&partStatus, 0u, packedBits, 0u));
	CHECK_EQ(partStatus, static_cast<uint8>(0x5Au));
}

TEST_SUITE_END;
