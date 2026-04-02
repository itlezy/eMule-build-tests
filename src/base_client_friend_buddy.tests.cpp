#include "../third_party/doctest/doctest.h"

#include "TestSupport.h"
#include "BaseClientFriendBuddySeams.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("Friend transition reports userhash failure only for mismatched hashed friends that are still connecting")
{
	const FriendLinkSnapshot connectingMismatch = {true, true, true, false, false};
	const FriendLinkSnapshot linkedMismatch = {true, true, false, false, false};
	const FriendLinkSnapshot matchingHashedFriend = {true, true, false, true, false};

	CHECK_EQ(ClassifyFriendLinkTransition(connectingMismatch), friendLinkTransitionUserhashFailed);
	CHECK_EQ(ClassifyFriendLinkTransition(linkedMismatch), friendLinkTransitionUnlink);
	CHECK_EQ(ClassifyFriendLinkTransition(matchingHashedFriend), friendLinkTransitionNone);
}

TEST_CASE("Friend replacement policy preserves the IP-only friend exception")
{
	const FriendLinkSnapshot matchingIpOnlyFriend = {true, false, false, true, true};
	const FriendLinkSnapshot mismatchedEndpoint = {true, false, false, true, false};
	const FriendLinkSnapshot hashedFriend = {true, true, false, true, true};
	const FriendLinkSnapshot noFriend = {false, false, false, false, false};

	CHECK_FALSE(ShouldSearchReplacementFriend(matchingIpOnlyFriend));
	CHECK(ShouldSearchReplacementFriend(mismatchedEndpoint));
	CHECK(ShouldSearchReplacementFriend(hashedFriend));
	CHECK(ShouldSearchReplacementFriend(noFriend));
}

TEST_CASE("Buddy hello snapshot advertises tags only when firewalled mode has a buddy snapshot")
{
	const BuddyHelloSnapshot advertisedBuddy = BuildBuddyHelloSnapshot(true, true, 0x01020304u, 4662u);
	const BuddyHelloSnapshot noBuddy = BuildBuddyHelloSnapshot(true, false, 0u, 0u);
	const BuddyHelloSnapshot notFirewalled = BuildBuddyHelloSnapshot(false, true, 0x01020304u, 4662u);

	CHECK(advertisedBuddy.bShouldAdvertise);
	CHECK_EQ(advertisedBuddy.dwBuddyIP, static_cast<uint32>(0x01020304u));
	CHECK_EQ(advertisedBuddy.nBuddyPort, static_cast<uint16>(4662u));
	CHECK_FALSE(noBuddy.bShouldAdvertise);
	CHECK_FALSE(notFirewalled.bShouldAdvertise);
}

TEST_CASE("Hello tag count stays aligned with the buddy advertisement snapshot")
{
	const BuddyHelloSnapshot advertisedBuddy = BuildBuddyHelloSnapshot(true, true, 1u, 2u);
	const BuddyHelloSnapshot noBuddy = BuildBuddyHelloSnapshot(true, false, 0u, 0u);

	CHECK_EQ(GetHelloTagCount(advertisedBuddy), static_cast<uint32>(8u));
	CHECK_EQ(GetHelloTagCount(noBuddy), static_cast<uint32>(6u));
}

TEST_SUITE_END;
