#include "../third_party/doctest/doctest.h"

#include "../include/TestSupport.h"

#include <atomic>

#include "AppStateSeams.h"
#include "AtomicStateSeams.h"
#include "DisplayRefreshSeams.h"
#include "SharedFileListSeams.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("Display refresh mask atomically merges new bits without losing the previous state")
{
	std::atomic<LONG> nPendingMask(0);

	CHECK(AccumulatePendingDisplayMask(nPendingMask, DISPLAY_REFRESH_DOWNLOAD_LIST) == 0);
	CHECK(nPendingMask.load() == DISPLAY_REFRESH_DOWNLOAD_LIST);

	CHECK(AccumulatePendingDisplayMask(nPendingMask, DISPLAY_REFRESH_CLIENT_LIST) == DISPLAY_REFRESH_DOWNLOAD_LIST);
	CHECK(nPendingMask.load() == (DISPLAY_REFRESH_DOWNLOAD_LIST | DISPLAY_REFRESH_CLIENT_LIST));

	CHECK(AccumulatePendingDisplayMask(nPendingMask, DISPLAY_REFRESH_DOWNLOAD_LIST) == (DISPLAY_REFRESH_DOWNLOAD_LIST | DISPLAY_REFRESH_CLIENT_LIST));
	CHECK(nPendingMask.load() == (DISPLAY_REFRESH_DOWNLOAD_LIST | DISPLAY_REFRESH_CLIENT_LIST));
}

TEST_CASE("Display refresh mask exchange drains the queued bits and clears the pending state")
{
	std::atomic<LONG> nPendingMask(0);

	AccumulatePendingDisplayMask(nPendingMask, DISPLAY_REFRESH_UPLOAD_LIST);
	AccumulatePendingDisplayMask(nPendingMask, DISPLAY_REFRESH_QUEUE_LIST);

	CHECK(nPendingMask.exchange(0) == (DISPLAY_REFRESH_UPLOAD_LIST | DISPLAY_REFRESH_QUEUE_LIST));
	CHECK(nPendingMask.load() == 0);
}

TEST_CASE("App state helpers preserve the running and closing classifications")
{
	CHECK_FALSE(IsAppStateRunning(APP_STATE_STARTING));
	CHECK_FALSE(IsAppStateClosing(APP_STATE_STARTING));
	CHECK(IsAppStateRunning(APP_STATE_RUNNING));
	CHECK(IsAppStateRunning(APP_STATE_ASKCLOSE));
	CHECK_FALSE(IsAppStateClosing(APP_STATE_ASKCLOSE));
	CHECK(IsAppStateClosing(APP_STATE_SHUTTINGDOWN));
	CHECK(IsAppStateClosing(APP_STATE_DONE));
}

TEST_CASE("Atomic long flag helpers consume one raised request and then reset cleanly")
{
	std::atomic<LONG> nFlag(0);

	CHECK_FALSE(ConsumeAtomicLongFlag(nFlag));

	SetAtomicLongFlag(nFlag, TRUE);
	CHECK(ConsumeAtomicLongFlag(nFlag));
	CHECK_FALSE(ConsumeAtomicLongFlag(nFlag));
}

TEST_CASE("Shared file auto-rescan dirty flag reports dirty after set and clean after clear")
{
	std::atomic<LONG> nDirtyFlag(0);

	CHECK_FALSE(SharedFileListSeams::IsAutoRescanDirtyFlagSet(nDirtyFlag));
	SharedFileListSeams::MarkAutoRescanDirtyFlag(nDirtyFlag);
	CHECK(SharedFileListSeams::IsAutoRescanDirtyFlagSet(nDirtyFlag));
	SharedFileListSeams::ClearAutoRescanDirtyFlag(nDirtyFlag);
	CHECK_FALSE(SharedFileListSeams::IsAutoRescanDirtyFlagSet(nDirtyFlag));
}
