#include "../third_party/doctest/doctest.h"

#include "../include/LongPathTestSupport.h"

#include "PartFileCompletionSeams.h"

#include <vector>

TEST_SUITE_BEGIN("parity");

TEST_CASE("Part-file completion seam only warns about disabled long-path support for plausible move failures")
{
	LongPathTestSupport::ScopedLongPathFixture fixture;
	INFO(fixture.LastError());
	REQUIRE(fixture.Initialize(true, 0u, 0x434F4Du));

	const std::wstring stagedPartPath = fixture.MakeDirectoryChildPath(L"001 odd-[part].part");
	const std::wstring finishedPath = fixture.MakeDirectoryChildPath(L"finished odd-[leaf].bin");
	const std::vector<BYTE> payload = LongPathTestSupport::BuildDeterministicPayload(4099u, 0x50415254u);
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::WriteBytes(stagedPartPath, payload));
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::MoveFileReplace(stagedPartPath, finishedPath));

	std::vector<BYTE> roundTrip;
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::ReadBytes(finishedPath, roundTrip));

	CString strLongPath(finishedPath.c_str());

	CHECK(strLongPath.GetLength() > MAX_PATH);
	CHECK(PartFileCompletionSeams::ShouldWarnAboutDisabledLongPathSupport(ERROR_FILENAME_EXCED_RANGE, strLongPath, false));
	CHECK(PartFileCompletionSeams::ShouldWarnAboutDisabledLongPathSupport(ERROR_PATH_NOT_FOUND, strLongPath, false));
	CHECK_FALSE(PartFileCompletionSeams::ShouldWarnAboutDisabledLongPathSupport(ERROR_ACCESS_DENIED, strLongPath, false));
	CHECK(roundTrip == payload);
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::DeleteFilePath(finishedPath));
}

TEST_CASE("Part-file completion seam skips disabled-long-path warnings for supported or short-path cases")
{
	LongPathTestSupport::ScopedLongPathFixture shortFixture;
	INFO(shortFixture.LastError());
	REQUIRE(shortFixture.Initialize(false, 0u, 0x53484F52u));
	const std::wstring shortFinishedPath = shortFixture.MakeDirectoryChildPath(L"finished.bin");
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::WriteBytes(shortFinishedPath, LongPathTestSupport::BuildDeterministicPayload(73u, 0x1111u)));
	const CString strShortPath(shortFinishedPath.c_str());

	LongPathTestSupport::ScopedLongPathFixture longFixture;
	INFO(longFixture.LastError());
	REQUIRE(longFixture.Initialize(true, 0u, 0x4C4F4E47u));
	const std::wstring longFinishedPath = longFixture.MakeDirectoryChildPath(L"finished odd-[enabled].bin");
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::WriteBytes(longFinishedPath, LongPathTestSupport::BuildDeterministicPayload(97u, 0x2222u)));
	const CString strLongPath(longFinishedPath.c_str());

	CHECK_FALSE(PartFileCompletionSeams::ShouldWarnAboutDisabledLongPathSupport(ERROR_FILENAME_EXCED_RANGE, strLongPath, true));
	CHECK_FALSE(PartFileCompletionSeams::ShouldWarnAboutDisabledLongPathSupport(ERROR_FILENAME_EXCED_RANGE, strShortPath, false));
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::DeleteFilePath(shortFinishedPath));
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::DeleteFilePath(longFinishedPath));
}

TEST_SUITE_END;
