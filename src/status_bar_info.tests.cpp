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

TEST_CASE("Status bar IP pane surfaces startup bind blocks explicitly")
{
	const CString strReason(_T("Bind interface hide.me is unavailable"));

	CHECK(StatusBarInfo::FormatNetworkAddressPaneText(CString(), 0, true) == CString(_T("B:Blocked|P:Offline")));
	CHECK(StatusBarInfo::FormatNetworkAddressPaneToolTip(CString(), 0, true, strReason)
		== CString(_T("Bind IP: Blocked | Public IP: Offline | Bind interface hide.me is unavailable")));
}

TEST_CASE("Status bar users pane keeps eD2K and Kad contributions readable")
{
	CHECK(StatusBarInfo::FormatUsersPaneText(_T("Users"), _T("Files"), _T("12.3M"), _T("4.5M"), _T("98.7M"), _T("6.5M"), true, true)
		== CString(_T("Users:12.3M+4.5M|Files:98.7M+6.5M")));
	CHECK(StatusBarInfo::FormatUsersPaneToolTip(_T("Users"), _T("Files"), _T("12.3M"), _T("4.5M"), _T("98.7M"), _T("6.5M"), true, true)
		== CString(_T("Users eD2K:12.3M | Kad:4.5M | Files eD2K:98.7M | Kad:6.5M")));
}

TEST_CASE("Status bar users pane falls back cleanly to one active network")
{
	CHECK(StatusBarInfo::FormatUsersPaneText(_T("Users"), _T("Files"), _T("12.3M"), _T("4.5M"), _T("98.7M"), _T("6.5M"), true, false)
		== CString(_T("Users:12.3M|Files:98.7M")));
	CHECK(StatusBarInfo::FormatUsersPaneToolTip(_T("Users"), _T("Files"), _T("12.3M"), _T("4.5M"), _T("98.7M"), _T("6.5M"), false, true)
		== CString(_T("Users Kad:4.5M | Files Kad:6.5M")));
}

TEST_CASE("Status bar connection pane highlights richer live states and server diagnostics")
{
	CHECK(StatusBarInfo::FormatConnectionPaneText(_T("Low ID"), _T("Open")) == CString(_T("eD2K:Low ID|Kad:Open")));
	CHECK(StatusBarInfo::FormatConnectionPaneText(_T("Connecting"), _T("Bootstrap 42%")) == CString(_T("eD2K:Connecting|Kad:Bootstrap 42%")));
	CHECK(StatusBarInfo::FormatConnectionPaneToolTip(_T("High ID"), _T("Firewalled"), _T("Server"), _T("Users"), _T("Razorback"), _T("1,234,567"), _T("42 ms"))
		== CString(_T("eD2K:High ID|Kad:Firewalled | Server: Razorback (Users: 1,234,567, 42 ms)")));
}

TEST_CASE("Status bar transfer pane appends compact activity and expands it in the tooltip")
{
	CHECK(StatusBarInfo::FormatTransferPaneText(_T("Up: 1.0 | Down: 2.0"), 3, 2, 4)
		== CString(_T("Up: 1.0 | Down: 2.0 | D:3 U:2/4")));
	CHECK(StatusBarInfo::FormatTransferPaneText(_T("Up: 0.0 | Down: 0.0"), 0, 0, 0)
		== CString(_T("Up: 0.0 | Down: 0.0 | Idle")));
	CHECK(StatusBarInfo::FormatTransferPaneToolTip(_T("Up: 1.0 | Down: 2.0"), _T("Downloading"), _T("Uploading"), _T("On Queue"), 3, 12, 2, 4, 128)
		== CString(_T("Up: 1.0 | Down: 2.0 | Downloading: 3/12 | Uploading: 2/4 | On Queue: 128")));
}
