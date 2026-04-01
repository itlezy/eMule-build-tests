#include "../third_party/doctest/doctest.h"
#include "PipeApiServerPolicy.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("Pipe API command queue accepts work until the configured saturation limit")
{
	CHECK(PipeApiPolicy::CanQueueCommand(PipeApiPolicy::kMaxPendingCommands - 1));
	CHECK_FALSE(PipeApiPolicy::CanQueueCommand(PipeApiPolicy::kMaxPendingCommands));
}

TEST_CASE("Pipe API drops duplicate stats updates when one stats event is already queued")
{
	CHECK_EQ(
		PipeApiPolicy::GetWriteAction(
			PipeApiPolicy::EWriteKind::Stats,
			0,
			0,
			128,
			true),
		PipeApiPolicy::EWriteAction::Drop);
}

TEST_CASE("Pipe API drops stats updates instead of disconnecting on outbound saturation")
{
	CHECK_EQ(
		PipeApiPolicy::GetWriteAction(
			PipeApiPolicy::EWriteKind::Stats,
			PipeApiPolicy::kMaxPendingWrites,
			PipeApiPolicy::kMaxPendingWriteBytes,
			128,
			false),
		PipeApiPolicy::EWriteAction::Drop);
}

TEST_CASE("Pipe API disconnects a slow client when structural events exceed outbound limits")
{
	CHECK_EQ(
		PipeApiPolicy::GetWriteAction(
			PipeApiPolicy::EWriteKind::Structural,
			PipeApiPolicy::kMaxPendingWrites,
			PipeApiPolicy::kMaxPendingWriteBytes,
			128,
			false),
		PipeApiPolicy::EWriteAction::Disconnect);
}

TEST_CASE("Pipe API queues structural events while remaining under outbound limits")
{
	CHECK_EQ(
		PipeApiPolicy::GetWriteAction(
			PipeApiPolicy::EWriteKind::Structural,
			PipeApiPolicy::kMaxPendingWrites - 1,
			PipeApiPolicy::kMaxPendingWriteBytes - 1024,
			512,
			false),
		PipeApiPolicy::EWriteAction::Queue);
}
