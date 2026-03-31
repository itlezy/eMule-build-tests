#include "../third_party/doctest/doctest.h"
#include "FileBufferSlider.h"
#include "ModernLimits.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("Modern limits default timeouts match the FEAT_018 targets")
{
	CHECK_EQ(ModernLimits::kDefaultConnectionTimeoutSeconds, 30u);
	CHECK_EQ(ModernLimits::kDefaultDownloadTimeoutSeconds, 75u);
	CHECK_EQ(ModernLimits::kDefaultUdpMaxQueueTimeSeconds, 20u);
	CHECK_EQ(ModernLimits::kDefaultConnectionLatencyMs, 15000u);
}

TEST_CASE("Modern limits preference defaults match the FEAT_019 targets")
{
	CHECK_EQ(ModernLimits::kDefaultMaxConnections, 500u);
	CHECK_EQ(ModernLimits::kDefaultMaxHalfOpenConnections, 50u);
	CHECK_EQ(ModernLimits::kDefaultMaxConnectionsPerFiveSeconds, 50u);
	CHECK_EQ(ModernLimits::kDefaultUdpReceiveBufferSize, 512u * 1024u);
	CHECK_EQ(ModernLimits::kDefaultTcpSendBufferSize, 512u * 1024u);
	CHECK_EQ(ModernLimits::kDefaultFileBufferSize, 64u * 1024u * 1024u);
	CHECK_EQ(ModernLimits::kMaxFileBufferSize, 512u * 1024u * 1024u);
	CHECK_EQ(ModernLimits::kDefaultFileBufferTimeLimitSeconds, 120u);
	CHECK_EQ(ModernLimits::kDefaultQueueSize, 10000);
	CHECK_EQ(ModernLimits::kDefaultMaxSourcesPerFile, 600u);
	CHECK_EQ(ModernLimits::kDefaultMaxSourcesPerFileSoft, 1000u);
	CHECK_EQ(ModernLimits::kDefaultMaxSourcesPerFileUdp, 100u);
	CHECK_EQ(ModernLimits::kDefaultUploadClientDataRate, 8u * 1024u * 1024u);
}

TEST_CASE("Modern limits timeout normalization keeps invalid values bounded")
{
	CHECK_EQ(ModernLimits::NormalizeTimeoutSeconds(0u, ModernLimits::kDefaultConnectionTimeoutSeconds), 30000ul);
	CHECK_EQ(ModernLimits::NormalizeTimeoutSeconds(1u, ModernLimits::kDefaultConnectionTimeoutSeconds), 5000ul);
	CHECK_EQ(ModernLimits::NormalizeTimeoutSeconds(75u, ModernLimits::kDefaultDownloadTimeoutSeconds), 75000ul);
}

TEST_CASE("Modern limits timeout serialization round-trips whole seconds")
{
	CHECK_EQ(ModernLimits::TimeoutMsToSeconds(ModernLimits::NormalizeTimeoutSeconds(30u, ModernLimits::kDefaultConnectionTimeoutSeconds)), 30u);
	CHECK_EQ(ModernLimits::TimeoutMsToSeconds(ModernLimits::NormalizeTimeoutSeconds(75u, ModernLimits::kDefaultDownloadTimeoutSeconds)), 75u);
}

TEST_CASE("Modern limits upload ceiling clamps oversized slot targets only")
{
	CHECK_EQ(ModernLimits::ApplyUploadClientDataRateCap(256u * 1024u, 8u * 1024u * 1024u), 256u * 1024u);
	CHECK_EQ(ModernLimits::ApplyUploadClientDataRateCap(16u * 1024u * 1024u, 8u * 1024u * 1024u), 8u * 1024u * 1024u);
}

TEST_CASE("File buffer slider preserves small KiB values and reaches the larger MiB range")
{
	CHECK_EQ(FileBufferSlider::PositionToBytes(FileBufferSlider::kMinPosition), 16u * 1024u);
	CHECK_EQ(FileBufferSlider::PositionToBytes(FileBufferSlider::BytesToPosition(256u * 1024u)), 256u * 1024u);
	CHECK_EQ(FileBufferSlider::PositionToBytes(FileBufferSlider::BytesToPosition(1024u * 1024u)), 1024u * 1024u);
	CHECK_EQ(FileBufferSlider::PositionToBytes(FileBufferSlider::BytesToPosition(ModernLimits::kDefaultFileBufferSize)), ModernLimits::kDefaultFileBufferSize);
	CHECK_EQ(FileBufferSlider::PositionToBytes(FileBufferSlider::kMaxPosition), ModernLimits::kMaxFileBufferSize);
}
