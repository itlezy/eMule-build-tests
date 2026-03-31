#include "../third_party/doctest/doctest.h"

#ifndef ASSERT
#define ASSERT(expr) ((void)0)
#endif

#include "BindStartupPolicy.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("Startup bind policy allows the default interface selection")
{
	CHECK_FALSE(BindStartupPolicy::HasExplicitBindSelection(CString(), CString()));
	CHECK_FALSE(BindStartupPolicy::ShouldBlockSessionNetworking(CString(), CString(), BARR_Default));
	CHECK(BindStartupPolicy::FormatStartupBlockReason(CString(), CString(), CString(), BARR_Default).IsEmpty());
}

TEST_CASE("Startup bind policy blocks an explicit interface when it disappears")
{
	const CString strInterfaceId(_T("vpn-if-guid"));
	const CString strInterfaceName(_T("My VPN"));

	CHECK(BindStartupPolicy::HasExplicitBindSelection(strInterfaceId, CString()));
	CHECK(BindStartupPolicy::ShouldBlockSessionNetworking(strInterfaceId, CString(), BARR_InterfaceNotFound));
	CHECK(BindStartupPolicy::FormatStartupBlockReason(strInterfaceName, strInterfaceId, CString(), BARR_InterfaceNotFound)
		== CString(_T("Networking disabled for this session because the selected bind interface is no longer available: My VPN")));
}

TEST_CASE("Startup bind policy blocks a selected IP that vanished from the chosen interface")
{
	const CString strInterfaceId(_T("vpn-if-guid"));
	const CString strInterfaceName(_T("My VPN"));
	const CString strAddress(_T("10.54.218.144"));

	CHECK(BindStartupPolicy::ShouldBlockSessionNetworking(strInterfaceId, strAddress, BARR_AddressNotFoundOnInterface));
	CHECK(BindStartupPolicy::FormatStartupBlockReason(strInterfaceName, strInterfaceId, strAddress, BARR_AddressNotFoundOnInterface)
		== CString(_T("Networking disabled for this session because the selected bind IP is no longer present on the selected interface: My VPN / 10.54.218.144")));
}

TEST_CASE("Startup bind policy blocks an address-only selection that is missing everywhere")
{
	const CString strAddress(_T("10.54.218.144"));

	CHECK(BindStartupPolicy::ShouldBlockSessionNetworking(CString(), strAddress, BARR_AddressNotFound));
	CHECK(BindStartupPolicy::FormatStartupBlockReason(CString(), CString(), strAddress, BARR_AddressNotFound)
		== CString(_T("Networking disabled for this session because the selected bind IP is no longer present on any live interface: Any interface / 10.54.218.144")));
}
