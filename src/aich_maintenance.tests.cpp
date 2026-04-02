#include "../third_party/doctest/doctest.h"
#include "../include/TestSupport.h"

#include <limits>

#include "AICHMaintenanceSeams.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("AICH maintenance seam derives ordinary raw hash payload sizes without overflow")
{
	uint32 nPayloadSize = 0;

	CHECK(AICHMaintenanceSeams::TryDeriveAICHHashPayloadSize(20u, 4u, &nPayloadSize));
	CHECK_EQ(nPayloadSize, static_cast<uint32>(80u));
}

TEST_CASE("AICH maintenance seam rejects raw hash payload sizes that overflow 32-bit file IO spans")
{
	uint32 nPayloadSize = 0;

	CHECK_FALSE(AICHMaintenanceSeams::TryDeriveAICHHashPayloadSize((std::numeric_limits<size_t>::max)(), 2u, &nPayloadSize));
	CHECK_FALSE(AICHMaintenanceSeams::TryDeriveAICHHashPayloadSize(static_cast<size_t>((std::numeric_limits<uint32>::max)()), 2u, &nPayloadSize));
}

TEST_CASE("AICH maintenance seam yields briefly for foreground hashing and exits immediately on shutdown")
{
	const AICHSyncForegroundWaitAction waitingAction = AICHMaintenanceSeams::GetForegroundHashWaitAction({false, 1, false});
	CHECK_FALSE(waitingAction.bShouldExit);
	CHECK_EQ(waitingAction.dwSleepMilliseconds, static_cast<DWORD>(AICHMaintenanceSeams::kForegroundHashYieldDelayMs));

	const AICHSyncForegroundWaitAction idleAction = AICHMaintenanceSeams::GetForegroundHashWaitAction({false, 0, false});
	CHECK_FALSE(idleAction.bShouldExit);
	CHECK_EQ(idleAction.dwSleepMilliseconds, static_cast<DWORD>(0u));

	const AICHSyncForegroundWaitAction shutdownAction = AICHMaintenanceSeams::GetForegroundHashWaitAction({true, 3, true});
	CHECK(shutdownAction.bShouldExit);
	CHECK_EQ(shutdownAction.dwSleepMilliseconds, static_cast<DWORD>(0u));
}

TEST_SUITE_END;
