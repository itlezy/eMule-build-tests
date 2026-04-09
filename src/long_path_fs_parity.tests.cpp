#include "../third_party/doctest/doctest.h"

#include "../include/LongPathTestSupport.h"

#include <algorithm>
#include <string>
#include <vector>
#include <windows.h>

TEST_SUITE_BEGIN("parity");

TEST_CASE("Long-path reference helper prefixes only fully qualified overlong DOS and UNC paths")
{
	std::wstring longDriveAbsolute = L"C:\\";
	longDriveAbsolute.append(static_cast<size_t>(MAX_PATH), L'a');

	std::wstring longUnc = L"\\\\server\\share\\";
	longUnc.append(static_cast<size_t>(MAX_PATH), L'b');

	std::wstring longRelative(static_cast<size_t>(MAX_PATH) + 16u, L'c');
	std::wstring longDriveRelative = L"C:";
	longDriveRelative.append(static_cast<size_t>(MAX_PATH), L'd');
	std::wstring longRootRelative = L"\\";
	longRootRelative.append(static_cast<size_t>(MAX_PATH), L'e');

	CHECK(LongPathTestSupport::PreparePathForLongPath(longDriveAbsolute).rfind(L"\\\\?\\", 0) == 0);
	CHECK(LongPathTestSupport::PreparePathForLongPath(longUnc).rfind(L"\\\\?\\UNC\\", 0) == 0);
	CHECK(LongPathTestSupport::PreparePathForLongPath(longRelative) == longRelative);
	CHECK(LongPathTestSupport::PreparePathForLongPath(longDriveRelative) == longDriveRelative);
	CHECK(LongPathTestSupport::PreparePathForLongPath(longRootRelative) == longRootRelative);
}

TEST_CASE("Long-path reference filesystem preserves deterministic payloads on short and overlong unicode paths")
{
	for (const bool bMakeLongPath : {false, true}) {
		LongPathTestSupport::ScopedLongPathFixture fixture;
		INFO(fixture.LastError());
		REQUIRE(fixture.Initialize(bMakeLongPath, bMakeLongPath ? 65537u : 4099u, bMakeLongPath ? 0xAABBCCDDu : 0x11223344u));

		std::vector<BYTE> bytes;
		REQUIRE(LongPathTestSupport::ScopedLongPathFixture::ReadBytes(fixture.FilePath(), bytes));
		CHECK(bytes == fixture.Payload());
		CHECK(LongPathTestSupport::ComputeFnv1a64(bytes) == fixture.PayloadFnv1a64());
	}
}

TEST_CASE("Long-path reference rename copy delete and enumeration keep the expected golden vectors")
{
	LongPathTestSupport::ScopedLongPathFixture fixture;
	INFO(fixture.LastError());
	REQUIRE(fixture.Initialize(true, 8192u + 123u, 0x0F1E2D3Cu));

	const std::wstring copiedPath = fixture.MakeSiblingPath(L".copy");
	const std::wstring renamedPath = fixture.MakeSiblingPath(L".renamed");

	REQUIRE(::CopyFileW(LongPathTestSupport::PreparePathForLongPath(fixture.FilePath()).c_str(), LongPathTestSupport::PreparePathForLongPath(copiedPath).c_str(), FALSE) != FALSE);
	REQUIRE(::MoveFileExW(LongPathTestSupport::PreparePathForLongPath(copiedPath).c_str(), LongPathTestSupport::PreparePathForLongPath(renamedPath).c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE);

	std::vector<std::wstring> names;
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::EnumerateFileNames(fixture.DirectoryPath(), names));
	CHECK(std::find(names.begin(), names.end(), std::wstring(L"payload_") + LongPathTestSupport::MakeSpecialSegment() + L".bin") != names.end());
	CHECK(std::find(names.begin(), names.end(), std::wstring(L"payload_") + LongPathTestSupport::MakeSpecialSegment() + L".bin.renamed") != names.end());

	std::vector<BYTE> copiedBytes;
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::ReadBytes(renamedPath, copiedBytes));
	CHECK(copiedBytes == fixture.Payload());
	CHECK(LongPathTestSupport::ComputeFnv1a64(copiedBytes) == fixture.PayloadFnv1a64());

	REQUIRE(::DeleteFileW(LongPathTestSupport::PreparePathForLongPath(renamedPath).c_str()) != FALSE);
	std::vector<BYTE> missingBytes;
	CHECK_FALSE(LongPathTestSupport::ScopedLongPathFixture::ReadBytes(renamedPath, missingBytes));
}

TEST_CASE("Long-path reference temp-file lifecycle preserves golden vectors across append move and backup flows")
{
	LongPathTestSupport::ScopedLongPathFixture fixture;
	INFO(fixture.LastError());
	REQUIRE(fixture.Initialize(true, 0u, 0x8086u));

	const std::wstring tempPath = fixture.MakeSiblingPath(L".tmp");
	const std::wstring finalPath = fixture.MakeSiblingPath(L".final");
	const std::wstring backupPath = fixture.MakeSiblingPath(L".bak");
	const std::vector<BYTE> firstPayload = LongPathTestSupport::BuildDeterministicPayload(6001u, 0x1234ABCDu);
	const std::vector<BYTE> secondPayload = LongPathTestSupport::BuildDeterministicPayload(4097u, 0x55AA7711u);

	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::AppendBytes(tempPath, firstPayload));
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::AppendBytes(tempPath, secondPayload));
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::MoveFileReplace(tempPath, finalPath));
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::CopyFilePath(finalPath, backupPath, false));

	std::vector<BYTE> expected = firstPayload;
	expected.insert(expected.end(), secondPayload.begin(), secondPayload.end());

	std::vector<BYTE> finalBytes;
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::ReadBytes(finalPath, finalBytes));
	CHECK(finalBytes == expected);
	CHECK(LongPathTestSupport::ComputeFnv1a64(finalBytes) == LongPathTestSupport::ComputeFnv1a64(expected));

	std::vector<BYTE> backupBytes;
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::ReadBytes(backupPath, backupBytes));
	CHECK(backupBytes == expected);
	CHECK(LongPathTestSupport::ComputeFnv1a64(backupBytes) == LongPathTestSupport::ComputeFnv1a64(expected));

	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::DeleteFilePath(finalPath));
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::DeleteFilePath(backupPath));
}

TEST_CASE("Long-path reference directory-create headroom supports payload round-trips in deep unicode trees")
{
	LongPathTestSupport::ScopedLongPathFixture fixture;
	INFO(fixture.LastError());
	REQUIRE(fixture.Initialize(false, 0u, 0x9090u));

	std::wstring longDir = fixture.DirectoryPath() + L"\\reference_" + LongPathTestSupport::MakeSpecialSegment();
	while (longDir.size() < MAX_PATH + 40u) {
		longDir += L"\\segment_";
		longDir += LongPathTestSupport::MakeSpecialSegment();
	}
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::EnsureDirectoryTree(fixture.DirectoryPath(), longDir));

	const std::wstring payloadPath = longDir + L"\\roundtrip_" + LongPathTestSupport::MakeSpecialSegment() + L".bin";
	const std::vector<BYTE> payload = LongPathTestSupport::BuildDeterministicPayload(5432u, 0xCAFEBABEu);
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::WriteBytes(payloadPath, payload));

	std::vector<BYTE> loaded;
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::ReadBytes(payloadPath, loaded));
	CHECK(loaded == payload);
	CHECK(LongPathTestSupport::ComputeFnv1a64(loaded) == LongPathTestSupport::ComputeFnv1a64(payload));

	std::vector<std::wstring> names;
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::EnumerateFileNames(longDir, names));
	CHECK(std::find(names.begin(), names.end(), std::wstring(L"roundtrip_") + LongPathTestSupport::MakeSpecialSegment() + L".bin") != names.end());

	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::DeleteFilePath(payloadPath));
	for (std::wstring current = longDir; current.size() > fixture.DirectoryPath().size(); ) {
		REQUIRE(::RemoveDirectoryW(LongPathTestSupport::PreparePathForLongPath(current).c_str()) != FALSE);
		const size_t nLastSlash = current.find_last_of(L'\\');
		REQUIRE(nLastSlash != std::wstring::npos);
		current.erase(nLastSlash);
	}
}

TEST_SUITE_END;
