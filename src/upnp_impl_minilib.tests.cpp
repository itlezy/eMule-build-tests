#include "../third_party/doctest/doctest.h"

#include "UPnPImplMiniLibSeams.h"

TEST_SUITE_BEGIN("parity");

#if defined(EMULE_TEST_HAVE_UPNP_MINILIB_SEAMS)
TEST_CASE("MiniUPnP seam accepts an existing mapping that already targets the requested LAN endpoint")
{
	CHECK(DoesMiniUPnPMappingMatchRequest("10.54.224.185", "27198", "10.54.224.185", 27198));
	CHECK(DoesMiniUPnPMappingMatchRequest("10.54.224.185", "27208", "10.54.224.185", 27208));
}

TEST_CASE("MiniUPnP seam rejects missing or mismatched mapping targets")
{
	CHECK_FALSE(DoesMiniUPnPMappingMatchRequest("", "27198", "10.54.224.185", 27198));
	CHECK_FALSE(DoesMiniUPnPMappingMatchRequest("10.54.224.185", "", "10.54.224.185", 27198));
	CHECK_FALSE(DoesMiniUPnPMappingMatchRequest("10.54.224.186", "27198", "10.54.224.185", 27198));
	CHECK_FALSE(DoesMiniUPnPMappingMatchRequest("10.54.224.185", "27199", "10.54.224.185", 27198));
}
#endif

TEST_SUITE_END;
