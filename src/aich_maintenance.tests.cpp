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

TEST_CASE("AICH maintenance seam keeps the newest stored duplicate hash position")
{
	const StoredAICHHashUpdate olderDuplicate = AICHMaintenanceSeams::ResolveStoredAICHHashUpdate(40u, 30u);
	CHECK_FALSE(olderDuplicate.bShouldReplaceExisting);
	CHECK_EQ(olderDuplicate.nReplacedFilePos, static_cast<ULONGLONG>(0u));

	const StoredAICHHashUpdate newerDuplicate = AICHMaintenanceSeams::ResolveStoredAICHHashUpdate(40u, 55u);
	CHECK(newerDuplicate.bShouldReplaceExisting);
	CHECK_EQ(newerDuplicate.nReplacedFilePos, static_cast<ULONGLONG>(40u));
}

TEST_CASE("AICH maintenance seam invalidates malformed one-sided tree nodes")
{
	const IncompleteAICHTreeNodeAction incompleteLeft = AICHMaintenanceSeams::GetIncompleteAICHTreeNodeAction(true, false);
	CHECK(incompleteLeft.bHasIncompleteChildren);
	CHECK(incompleteLeft.bShouldInvalidateNodeHash);

	const IncompleteAICHTreeNodeAction incompleteRight = AICHMaintenanceSeams::GetIncompleteAICHTreeNodeAction(false, true);
	CHECK(incompleteRight.bHasIncompleteChildren);
	CHECK(incompleteRight.bShouldInvalidateNodeHash);

	const IncompleteAICHTreeNodeAction completeNode = AICHMaintenanceSeams::GetIncompleteAICHTreeNodeAction(true, true);
	CHECK_FALSE(completeNode.bHasIncompleteChildren);
	CHECK_FALSE(completeNode.bShouldInvalidateNodeHash);
}

TEST_SUITE_END;
