#include "../third_party/doctest/doctest.h"
#include "PipeApiServerPolicy.h"
#include "PipeApiSurfaceSeams.h"

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

TEST_CASE("Pipe API exposes stable server priority names for the remote surface")
{
	CHECK_EQ(PipeApiSurfaceSeams::GetServerPriorityName(2), "low");
	CHECK_EQ(PipeApiSurfaceSeams::GetServerPriorityName(0), "normal");
	CHECK_EQ(PipeApiSurfaceSeams::GetServerPriorityName(1), "high");
	CHECK_EQ(PipeApiSurfaceSeams::GetServerPriorityName(99), "normal");
}

TEST_CASE("Pipe API exposes stable upload state names for the remote surface")
{
	CHECK_EQ(PipeApiSurfaceSeams::GetUploadStateName(0), "uploading");
	CHECK_EQ(PipeApiSurfaceSeams::GetUploadStateName(1), "queued");
	CHECK_EQ(PipeApiSurfaceSeams::GetUploadStateName(2), "connecting");
	CHECK_EQ(PipeApiSurfaceSeams::GetUploadStateName(3), "banned");
	CHECK_EQ(PipeApiSurfaceSeams::GetUploadStateName(4), "idle");
	CHECK_EQ(PipeApiSurfaceSeams::GetUploadStateName(255), "idle");
}

TEST_CASE("Pipe API parses the final transfer priority vocabulary")
{
	CHECK_EQ(PipeApiSurfaceSeams::ParseTransferPriorityName("auto"), PipeApiSurfaceSeams::ETransferPriority::Auto);
	CHECK_EQ(PipeApiSurfaceSeams::ParseTransferPriorityName("very_low"), PipeApiSurfaceSeams::ETransferPriority::VeryLow);
	CHECK_EQ(PipeApiSurfaceSeams::ParseTransferPriorityName("low"), PipeApiSurfaceSeams::ETransferPriority::Low);
	CHECK_EQ(PipeApiSurfaceSeams::ParseTransferPriorityName("normal"), PipeApiSurfaceSeams::ETransferPriority::Normal);
	CHECK_EQ(PipeApiSurfaceSeams::ParseTransferPriorityName("high"), PipeApiSurfaceSeams::ETransferPriority::High);
	CHECK_EQ(PipeApiSurfaceSeams::ParseTransferPriorityName("very_high"), PipeApiSurfaceSeams::ETransferPriority::VeryHigh);
	CHECK_EQ(PipeApiSurfaceSeams::ParseTransferPriorityName("invalid"), PipeApiSurfaceSeams::ETransferPriority::Invalid);
	CHECK_EQ(PipeApiSurfaceSeams::ParseTransferPriorityName(nullptr), PipeApiSurfaceSeams::ETransferPriority::Invalid);
}
