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
	CString strDirectory(_T("C:\\shared"));
	for (int i = 0; i < 24; ++i)
		strDirectory += _T("\\segmentsegment");
	const CString strFileName(_T("release.track.name.with.metadata.flac"));

	const CString strFullPath = KnownFileMetadataSeams::BuildMetadataFilePath(strDirectory, strFileName);
	CHECK(strFullPath.GetLength() > MAX_PATH);
	CHECK(strFullPath == strDirectory + _T("\\") + strFileName);
	CHECK(KnownFileMetadataSeams::BuildMetadataFilePath(strDirectory + _T("\\"), strFileName) == strDirectory + _T("\\") + strFileName);
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

TEST_SUITE_END;
