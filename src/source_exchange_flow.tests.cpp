#include "../third_party/doctest/doctest.h"

#include "TestSupport.h"
#include "BaseClientFriendBuddySeams.h"
#include "DownloadQueueHostnameResolverSeams.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("Source flow keeps buddy advertisement and hostname dispatch decisions independent across a sequence")
{
	const BuddyHelloSnapshot buddySnapshot = BuildBuddyHelloSnapshot(true, true, 0x01020304u, 4662u);
	CHECK(buddySnapshot.bShouldAdvertise);
	CHECK_EQ(GetHelloTagCount(buddySnapshot), static_cast<uint32>(8u));

	const EDownloadHostnameResolveDispatch firstDispatch = GetDownloadHostnameResolveDispatch(true, true, true, false);
	const EDownloadHostnameResolveDispatch secondDispatch = GetDownloadHostnameResolveDispatch(true, true, true, true);
	const EDownloadHostnameResolveDispatch thirdDispatch = GetDownloadHostnameResolveDispatch(true, false, true, true);

	CHECK(firstDispatch == EDownloadHostnameResolveDispatch::AddPackedSource);
	CHECK(secondDispatch == EDownloadHostnameResolveDispatch::AddUrlSource);
	CHECK(thirdDispatch == EDownloadHostnameResolveDispatch::Drop);
}

TEST_CASE("Source flow keeps packed and URL hostname completions isolated across repeated transitions")
{
	CHECK(GetDownloadHostnameResolveDispatch(true, true, true, false) == EDownloadHostnameResolveDispatch::AddPackedSource);
	CHECK(GetDownloadHostnameResolveDispatch(true, true, true, true) == EDownloadHostnameResolveDispatch::AddUrlSource);
	CHECK(GetDownloadHostnameResolveDispatch(false, true, true, false) == EDownloadHostnameResolveDispatch::Drop);
	CHECK(GetDownloadHostnameResolveDispatch(true, false, true, false) == EDownloadHostnameResolveDispatch::Drop);
	CHECK(GetDownloadHostnameResolveDispatch(true, true, false, true) == EDownloadHostnameResolveDispatch::Drop);
}

TEST_CASE("Source flow only searches for replacement friends when the link snapshot really drifted")
{
	const FriendLinkSnapshot stableIpOnlyFriend = {true, false, false, false, true};
	const FriendLinkSnapshot hashedMismatchFriend = {true, true, false, false, false};
	const FriendLinkSnapshot missingFriend = {false, false, false, false, false};

	CHECK_FALSE(ShouldSearchReplacementFriend(stableIpOnlyFriend));
	CHECK(ShouldSearchReplacementFriend(hashedMismatchFriend));
	CHECK(ShouldSearchReplacementFriend(missingFriend));
}

TEST_SUITE_END;
