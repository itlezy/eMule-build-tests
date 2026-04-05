#include "../third_party/doctest/doctest.h"

#include <vector>

#include "TestSupport.h"
#include "BaseClientFriendBuddySeams.h"
#include "DownloadQueueHostnameResolverSeams.h"

namespace
{
	enum class ESourceReplayStep
	{
		AdvertiseBuddy,
		AddPackedSource,
		SearchReplacementFriend,
		UnlinkFriend,
		AddUrlSource,
		DropSource
	};
}

TEST_SUITE_BEGIN("parity");

TEST_CASE("Source exchange replay keeps packed-source, friend drift, and URL-source decisions in stable order")
{
	std::vector<ESourceReplayStep> trace;

	const BuddyHelloSnapshot buddySnapshot = BuildBuddyHelloSnapshot(true, true, 0x0A0B0C0Du, 4662u);
	if (buddySnapshot.bShouldAdvertise)
		trace.push_back(ESourceReplayStep::AdvertiseBuddy);

	if (GetDownloadHostnameResolveDispatch(true, true, true, false) == EDownloadHostnameResolveDispatch::AddPackedSource)
		trace.push_back(ESourceReplayStep::AddPackedSource);

	const FriendLinkSnapshot driftedFriend = {true, true, false, false, false};
	if (ShouldSearchReplacementFriend(driftedFriend))
		trace.push_back(ESourceReplayStep::SearchReplacementFriend);
	if (ClassifyFriendLinkTransition(driftedFriend) == friendLinkTransitionUnlink)
		trace.push_back(ESourceReplayStep::UnlinkFriend);

	if (GetDownloadHostnameResolveDispatch(true, true, true, true) == EDownloadHostnameResolveDispatch::AddUrlSource)
		trace.push_back(ESourceReplayStep::AddUrlSource);

	CHECK(trace == std::vector<ESourceReplayStep>{
		ESourceReplayStep::AdvertiseBuddy,
		ESourceReplayStep::AddPackedSource,
		ESourceReplayStep::SearchReplacementFriend,
		ESourceReplayStep::UnlinkFriend,
		ESourceReplayStep::AddUrlSource
	});
}

TEST_CASE("Source exchange replay drops stale hostname completions without disturbing stable friends")
{
	std::vector<ESourceReplayStep> trace;

	const FriendLinkSnapshot stableFriend = {true, false, false, false, true};
	CHECK_FALSE(ShouldSearchReplacementFriend(stableFriend));
	CHECK_EQ(ClassifyFriendLinkTransition(stableFriend), friendLinkTransitionNone);

	if (GetDownloadHostnameResolveDispatch(false, true, true, false) == EDownloadHostnameResolveDispatch::Drop)
		trace.push_back(ESourceReplayStep::DropSource);
	if (GetDownloadHostnameResolveDispatch(true, false, true, true) == EDownloadHostnameResolveDispatch::Drop)
		trace.push_back(ESourceReplayStep::DropSource);
	if (GetDownloadHostnameResolveDispatch(true, true, false, false) == EDownloadHostnameResolveDispatch::Drop)
		trace.push_back(ESourceReplayStep::DropSource);

	CHECK(trace == std::vector<ESourceReplayStep>{
		ESourceReplayStep::DropSource,
		ESourceReplayStep::DropSource,
		ESourceReplayStep::DropSource
	});
}

TEST_SUITE_END;
