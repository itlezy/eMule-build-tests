#include "../third_party/doctest/doctest.h"

#ifndef ASSERT
#define ASSERT(expr) ((void)0)
#endif

#include "StatusBarInfo.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("Status bar IP pane uses compact placeholders for default bind and unknown public IP")
{
	CHECK(StatusBarInfo::FormatNetworkAddressPaneText(CString(), 0) == CString(_T("B:Any|P:?")));
	CHECK(StatusBarInfo::FormatNetworkAddressPaneToolTip(CString(), 0) == CString(_T("Bind IP: Any interface | Public IP: Unknown")));
}

TEST_CASE("Status bar IP pane formats both bind and public IPv4 addresses")
{
	const CString strBindAddress(_T("10.54.218.144"));
	const uint32 dwPublicIp = 203u | (0u << 8) | (113u << 16) | (7u << 24);

	CHECK(StatusBarInfo::Detail::FormatStoredIPv4Address(dwPublicIp) == CString(_T("203.0.113.7")));
	CHECK(StatusBarInfo::FormatNetworkAddressPaneText(strBindAddress, dwPublicIp) == CString(_T("B:10.54.218.144|P:203.0.113.7")));
	CHECK(StatusBarInfo::FormatNetworkAddressPaneToolTip(strBindAddress, dwPublicIp) == CString(_T("Bind IP: 10.54.218.144 | Public IP: 203.0.113.7")));
}

TEST_CASE("Status bar IP pane keeps a known bind address when public IP is still pending")
{
	const CString strBindAddress(_T("192.168.50.12"));

	CHECK(StatusBarInfo::FormatNetworkAddressPaneText(strBindAddress, 0) == CString(_T("B:192.168.50.12|P:?")));
	CHECK(StatusBarInfo::FormatNetworkAddressPaneToolTip(strBindAddress, 0) == CString(_T("Bind IP: 192.168.50.12 | Public IP: Unknown")));
}
