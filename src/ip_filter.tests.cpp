#include "../third_party/doctest/doctest.h"

#include "../include/LongPathTestSupport.h"

#include "IPFilterSeams.h"

#include <vector>

TEST_SUITE_BEGIN("parity");

TEST_CASE("IP-filter seam classifies long path names without fixed-buffer truncation")
{
	LongPathTestSupport::ScopedLongPathFixture fixture;
	INFO(fixture.LastError());
	REQUIRE(fixture.Initialize(true, 0u, 0x49504649u));

	const std::wstring guardingPath = fixture.MakeDirectoryChildPath(L"guarding.p2p.txt");
	const std::wstring prefixPath = fixture.MakeDirectoryChildPath(L"custom odd-[filter].prefix");
	const std::wstring pgPath = fixture.MakeDirectoryChildPath(L"custom odd-[guard].p2p");
	const std::vector<BYTE> payload = LongPathTestSupport::BuildDeterministicPayload(211u, 0x505047u);
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::WriteBytes(guardingPath, payload));
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::WriteBytes(prefixPath, payload));
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::WriteBytes(pgPath, payload));

	CHECK(IPFilterSeams::DetectFileTypeFromPath(CString(guardingPath.c_str())) == IPFilterSeams::PathHintPeerGuardian);
	CHECK(IPFilterSeams::DetectFileTypeFromPath(CString(prefixPath.c_str())) == IPFilterSeams::PathHintFilterDat);
	CHECK(IPFilterSeams::DetectFileTypeFromPath(CString(pgPath.c_str())) == IPFilterSeams::PathHintPeerGuardian);

	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::DeleteFilePath(guardingPath));
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::DeleteFilePath(prefixPath));
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::DeleteFilePath(pgPath));
}

TEST_CASE("IP-filter seam leaves unknown file hints to content sniffing")
{
	LongPathTestSupport::ScopedLongPathFixture fixture;
	INFO(fixture.LastError());
	REQUIRE(fixture.Initialize(true, 0u, 0x554E4Bu));

	const std::wstring unknownPath = fixture.MakeDirectoryChildPath(L"list odd-[unknown].txt");
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::WriteBytes(unknownPath, LongPathTestSupport::BuildDeterministicPayload(77u, 0x123u)));

	CHECK(IPFilterSeams::DetectFileTypeFromPath(CString(_T("ipfilter.dat"))) == IPFilterSeams::PathHintUnknown);
	CHECK(IPFilterSeams::DetectFileTypeFromPath(CString(unknownPath.c_str())) == IPFilterSeams::PathHintUnknown);
	REQUIRE(LongPathTestSupport::ScopedLongPathFixture::DeleteFilePath(unknownPath));
}

TEST_SUITE_END;
