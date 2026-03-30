#include "../third_party/doctest/doctest.h"
#include "MenuCmds.h"
#include "Resource.h"

namespace
{
#if __has_include("Scheduler.h")
	constexpr bool kHasSchedulerHeader = true;
#else
	constexpr bool kHasSchedulerHeader = false;
#endif

#if __has_include("PPgScheduler.h")
	constexpr bool kHasSchedulerPageHeader = true;
#else
	constexpr bool kHasSchedulerPageHeader = false;
#endif

#ifdef MP_HM_SCHEDONOFF
	constexpr bool kHasSchedulerHotMenuCommand = true;
#else
	constexpr bool kHasSchedulerHotMenuCommand = false;
#endif

#ifdef MP_SCHACTIONS
	constexpr bool kHasSchedulerActionRange = true;
#else
	constexpr bool kHasSchedulerActionRange = false;
#endif

#ifdef IDS_SCHEDULER
	constexpr bool kHasSchedulerStringId = true;
#else
	constexpr bool kHasSchedulerStringId = false;
#endif

#ifdef IDS_HM_SCHED_ON
	constexpr bool kHasSchedulerHotMenuOnStringId = true;
#else
	constexpr bool kHasSchedulerHotMenuOnStringId = false;
#endif

#ifdef IDS_HM_SCHED_OFF
	constexpr bool kHasSchedulerHotMenuOffStringId = true;
#else
	constexpr bool kHasSchedulerHotMenuOffStringId = false;
#endif

#ifdef IDD_PPG_SCHEDULER
	constexpr bool kHasSchedulerDialogId = true;
#else
	constexpr bool kHasSchedulerDialogId = false;
#endif
}

TEST_SUITE_BEGIN("divergence");

TEST_CASE("Legacy scheduler headers are absent from the target workspace")
{
	CHECK_FALSE(kHasSchedulerHeader);
	CHECK_FALSE(kHasSchedulerPageHeader);
}

TEST_CASE("Legacy scheduler command ids are absent from the target workspace")
{
	CHECK_FALSE(kHasSchedulerHotMenuCommand);
	CHECK_FALSE(kHasSchedulerActionRange);
}

TEST_CASE("Legacy scheduler resource ids are absent from the target workspace")
{
	CHECK_FALSE(kHasSchedulerStringId);
	CHECK_FALSE(kHasSchedulerHotMenuOnStringId);
	CHECK_FALSE(kHasSchedulerHotMenuOffStringId);
	CHECK_FALSE(kHasSchedulerDialogId);
}

TEST_SUITE_END;
