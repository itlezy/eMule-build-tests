#include "../third_party/doctest/doctest.h"

#include "../include/LongPathTestSupport.h"

#include "KnownFileMetadataSeams.h"

#include <io.h>
#include <vector>

namespace
{
	std::vector<BYTE> ReadBytesFromFd(const int fd)
	{
		std::vector<BYTE> bytes;
		BYTE buffer[4096] = {};
		int nRead = 0;
		while ((nRead = _read(fd, buffer, sizeof buffer)) > 0)
			bytes.insert(bytes.end(), buffer, buffer + nRead);
		return bytes;
	}
}

TEST_SUITE_BEGIN("parity");

TEST_CASE("Known-file metadata seam preserves full assembled paths without MAX_PATH truncation")
{
	LongPathTestSupport::ScopedLongPathFixture fixture;
	INFO(fixture.LastError());
	REQUIRE(fixture.Initialize(true, 0u, 0x4B4E4Fu));

	const std::wstring fileName = L"release.track.name.with.metadata odd-[leaf].flac";
	const std::wstring rawFullPath = fixture.MakeDirectoryChildPath(fileName.c_str());
	const std::vector<BYTE> payload = LongPathTestSupport::BuildDeterministicPayload(2049u, 0x4D455441u);
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::WriteBytes(rawFullPath, payload));

	const CString strDirectory(fixture.DirectoryPath().c_str());
	const CString strFileName(fileName.c_str());

	const CString strFullPath = KnownFileMetadataSeams::BuildMetadataFilePath(strDirectory, strFileName);
	CHECK(strFullPath.GetLength() > MAX_PATH);
	CHECK(strFullPath == strDirectory + _T("\\") + strFileName);
	CHECK(KnownFileMetadataSeams::BuildMetadataFilePath(strDirectory + _T("\\"), strFileName) == strDirectory + _T("\\") + strFileName);

	const int fd = KnownFileMetadataSeams::OpenMetadataReadOnlyDescriptor(strFullPath);
	REQUIRE(fd != -1);
	const std::vector<BYTE> bytes = ReadBytesFromFd(fd);
	REQUIRE(_close(fd) == 0);
	CHECK(bytes == payload);
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::DeleteFilePath(rawFullPath));
}

TEST_CASE("Known-file metadata seam opens overlong files for reader-side metadata probes")
{
	LongPathTestSupport::ScopedLongPathFixture fixture;
	INFO(fixture.LastError());
	REQUIRE(fixture.Initialize(true, 24577u, 0x6B6E6Fu));

	const int fd = KnownFileMetadataSeams::OpenMetadataReadOnlyDescriptor(fixture.FilePath().c_str());
	REQUIRE(fd != -1);
	const std::vector<BYTE> bytes = ReadBytesFromFd(fd);
	REQUIRE(_close(fd) == 0);

	CHECK(bytes == fixture.Payload());
	CHECK(LongPathTestSupport::ComputeFnv1a64(bytes) == fixture.PayloadFnv1a64());
}

TEST_CASE("Known-file metadata seam keeps MPEG audio fallback extensions explicit")
{
	CHECK(KnownFileMetadataSeams::IsMpegAudioMetadataExtension(_T("track.mp3")));
	CHECK(KnownFileMetadataSeams::IsMpegAudioMetadataExtension(_T("track.MP2")));
	CHECK(KnownFileMetadataSeams::IsMpegAudioMetadataExtension(_T("track.mp1")));
	CHECK(KnownFileMetadataSeams::IsMpegAudioMetadataExtension(_T("track.mpa")));
	CHECK_FALSE(KnownFileMetadataSeams::IsMpegAudioMetadataExtension(_T("track.flac")));
	CHECK_FALSE(KnownFileMetadataSeams::IsMpegAudioMetadataExtension(_T("track")));
}

TEST_SUITE_END;
