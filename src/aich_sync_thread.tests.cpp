#include "../third_party/doctest/doctest.h"
#include "../include/TestSupport.h"

#include "AICHSyncThreadSeams.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("AICH sync seam waits while foreground hash work is still active")
{
	CHECK(ShouldWaitForAICHSyncForegroundHashing({false, 1, false}));
	CHECK(ShouldWaitForAICHSyncForegroundHashing({false, 0, true}));
	CHECK_FALSE(ShouldWaitForAICHSyncForegroundHashing({false, 0, false}));
	CHECK_FALSE(ShouldWaitForAICHSyncForegroundHashing({true, 4, true}));
}

TEST_CASE("AICH sync seam hashes only live shared candidates while the app is still running")
{
	CHECK(ShouldCreateAICHSyncHash(false, true));
	CHECK_FALSE(ShouldCreateAICHSyncHash(false, false));
	CHECK_FALSE(ShouldCreateAICHSyncHash(true, true));
}

TEST_CASE("AICH sync seam validates only non-negative UI progress counts")
{
	CHECK(HasValidAICHSyncProgressCount(0));
	CHECK(HasValidAICHSyncProgressCount(7));
	CHECK_FALSE(HasValidAICHSyncProgressCount(-1));
}

TEST_SUITE_END;
