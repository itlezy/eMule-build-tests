#include "../third_party/doctest/doctest.h"
#include "AsyncSocketExSeams.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("Async socket seam maps a listening socket to readable poll interest")
{
	CHECK_EQ(GetAsyncSocketPollEvents(listening, FD_ACCEPT | FD_CLOSE), static_cast<short>(POLLIN));
}

TEST_CASE("Async socket seam keeps connecting sockets writable and optionally readable")
{
	CHECK_EQ(GetAsyncSocketPollEvents(connecting, FD_CONNECT), static_cast<short>(POLLOUT));
	CHECK_EQ(GetAsyncSocketPollEvents(connecting, FD_CONNECT | FD_READ), static_cast<short>(POLLIN | POLLOUT));
}

TEST_CASE("Async socket seam only polls connected sockets for the requested steady-state events")
{
	CHECK_EQ(GetAsyncSocketPollEvents(connected, FD_READ | FD_WRITE), static_cast<short>(POLLIN | POLLOUT));
	CHECK_EQ(GetAsyncSocketPollEvents(connected, FD_CLOSE), static_cast<short>(POLLIN));
	CHECK_EQ(GetAsyncSocketPollEvents(unconnected, FD_DEFAULT), static_cast<short>(0));
}

TEST_CASE("Async socket seam recognizes connect completion signals")
{
	CHECK(ShouldCompleteAsyncSocketConnect(connecting, POLLOUT));
	CHECK(ShouldCompleteAsyncSocketConnect(connecting, POLLERR));
	CHECK_FALSE(ShouldCompleteAsyncSocketConnect(connected, POLLOUT));
}

TEST_CASE("Async socket seam classifies WSAPoll hard failures separately from timeouts")
{
	CHECK(HasAsyncSocketPollFailure(SOCKET_ERROR));
	CHECK_FALSE(HasAsyncSocketPollFailure(0));
	CHECK_FALSE(HasAsyncSocketPollFailure(2));
}

TEST_CASE("Async socket seam only yields callback-drain polling while callbacks remain in flight")
{
	CHECK(ShouldYieldForAsyncSocketCallbackDrain(1));
	CHECK(ShouldYieldForAsyncSocketCallbackDrain(7));
	CHECK_FALSE(ShouldYieldForAsyncSocketCallbackDrain(0));
	CHECK_FALSE(ShouldYieldForAsyncSocketCallbackDrain(-1));
}

TEST_CASE("Async socket seam gates accept and read callbacks on state and requested interest")
{
	CHECK(ShouldDispatchAsyncSocketAccept(listening, FD_ACCEPT, POLLIN));
	CHECK_FALSE(ShouldDispatchAsyncSocketAccept(listening, FD_READ, POLLIN));
	CHECK(ShouldDispatchAsyncSocketRead(connected, FD_READ, POLLIN));
	CHECK_FALSE(ShouldDispatchAsyncSocketRead(listening, FD_READ, POLLIN));
}

TEST_CASE("Async socket seam gates write callbacks on state and requested interest")
{
	CHECK(ShouldDispatchAsyncSocketWrite(connected, FD_WRITE, POLLOUT));
	CHECK(ShouldDispatchAsyncSocketWrite(attached, FD_WRITE, POLLOUT));
	CHECK_FALSE(ShouldDispatchAsyncSocketWrite(connecting, FD_WRITE, POLLOUT));
	CHECK_FALSE(ShouldDispatchAsyncSocketWrite(connected, FD_READ, POLLOUT));
}

TEST_CASE("Async socket seam drains unread bytes before closing a readable connected socket")
{
	const AsyncSocketExCloseAction action = ClassifyAsyncSocketClose(connected, FD_READ | FD_CLOSE, POLLHUP, true);
	CHECK(action.bShouldReadDrain);
	CHECK_FALSE(action.bShouldClose);
}

TEST_CASE("Async socket seam closes immediately when no unread bytes remain")
{
	const AsyncSocketExCloseAction action = ClassifyAsyncSocketClose(connected, FD_READ | FD_CLOSE, POLLERR, false);
	CHECK_FALSE(action.bShouldReadDrain);
	CHECK(action.bShouldClose);
}

TEST_CASE("Async socket seam ignores close classification outside connected states")
{
	const AsyncSocketExCloseAction action = ClassifyAsyncSocketClose(listening, FD_CLOSE, POLLHUP, true);
	CHECK_FALSE(action.bShouldReadDrain);
	CHECK_FALSE(action.bShouldClose);
}

TEST_SUITE_END;
