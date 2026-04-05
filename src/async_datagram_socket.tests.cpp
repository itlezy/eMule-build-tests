#include "../third_party/doctest/doctest.h"

#include "AsyncDatagramSocketSeams.h"
#include "AsyncSocketExSeams.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("Async datagram seam keeps steady-state UDP sockets read-only until backpressure occurs")
{
	CHECK_EQ(GetAsyncDatagramEventMask(false), static_cast<long>(FD_READ));
	CHECK_EQ(GetAsyncDatagramEventMask(true), static_cast<long>(FD_READ | FD_WRITE));
}

TEST_CASE("Async datagram seam maps UDP read and write interest onto poll events without implicit close polling")
{
	CHECK_EQ(GetAsyncSocketPollEvents(attached, GetAsyncDatagramEventMask(false)), static_cast<short>(POLLIN));
	CHECK_EQ(GetAsyncSocketPollEvents(attached, GetAsyncDatagramEventMask(true)), static_cast<short>(POLLIN | POLLOUT));
}

TEST_CASE("Async datagram seam does not arm UDP write polling for detached or unconnected states")
{
	CHECK_EQ(GetAsyncSocketPollEvents(unconnected, GetAsyncDatagramEventMask(true)), static_cast<short>(0));
	CHECK_EQ(GetAsyncSocketPollEvents(closed, GetAsyncDatagramEventMask(true)), static_cast<short>(0));
	CHECK_EQ(GetAsyncSocketPollEvents(attached, GetAsyncDatagramEventMask(false)), static_cast<short>(POLLIN));
}

TEST_SUITE_END;
