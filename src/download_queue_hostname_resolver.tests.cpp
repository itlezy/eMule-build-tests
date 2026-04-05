#include "../third_party/doctest/doctest.h"

#include "DownloadQueueHostnameResolverSeams.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("Hostname resolution completions drop missing files and failed lookups")
{
	CHECK(GetDownloadHostnameResolveDispatch(false, true, true, false) == EDownloadHostnameResolveDispatch::Drop);
	CHECK(GetDownloadHostnameResolveDispatch(true, false, true, false) == EDownloadHostnameResolveDispatch::Drop);
	CHECK(GetDownloadHostnameResolveDispatch(true, true, false, false) == EDownloadHostnameResolveDispatch::Drop);
}

TEST_CASE("Hostname resolution completions preserve the packed-source versus URL path split")
{
	CHECK(GetDownloadHostnameResolveDispatch(true, true, true, false) == EDownloadHostnameResolveDispatch::AddPackedSource);
	CHECK(GetDownloadHostnameResolveDispatch(true, true, true, true) == EDownloadHostnameResolveDispatch::AddUrlSource);
}

TEST_CASE("Hostname resolution keeps URL sources distinct even when every other source gate is satisfied")
{
	CHECK(GetDownloadHostnameResolveDispatch(true, true, true, true) == EDownloadHostnameResolveDispatch::AddUrlSource);
	CHECK(GetDownloadHostnameResolveDispatch(true, true, false, true) == EDownloadHostnameResolveDispatch::Drop);
}

TEST_SUITE_END;
