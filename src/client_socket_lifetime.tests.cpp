#include "../third_party/doctest/doctest.h"

#include "ClientSocketLifetimeSeams.h"

namespace
{
	struct FakeSocket;

	struct FakeClient
	{
		FakeSocket *socket;
	};

	struct FakeSocket
	{
		FakeClient *client;
	};
}

TEST_SUITE_BEGIN("parity");

TEST_CASE("Client/socket detach clears both sides and stays safe when repeated")
{
	FakeClient client = {};
	FakeSocket socket = {};

	LinkClientSocketPair(&client, &socket);
	REQUIRE(client.socket == &socket);
	REQUIRE(socket.client == &client);

	DetachClientSocketPair(&client, &socket);
	CHECK(IsClientSocketPairDetached(&client, &socket));

	DetachClientSocketPair(&client, &socket);
	CHECK(IsClientSocketPairDetached(&client, &socket));
}

TEST_CASE("Client/socket relink detaches stale peers before reassigning the accepted socket")
{
	FakeClient existingClient = {};
	FakeClient temporaryClient = {};
	FakeSocket existingSocket = {};
	FakeSocket acceptedSocket = {};

	LinkClientSocketPair(&existingClient, &existingSocket);
	LinkClientSocketPair(&temporaryClient, &acceptedSocket);

	LinkClientSocketPair(&existingClient, &acceptedSocket);

	CHECK(existingClient.socket == &acceptedSocket);
	CHECK(acceptedSocket.client == &existingClient);
	CHECK(existingSocket.client == nullptr);
	CHECK(temporaryClient.socket == nullptr);
}

TEST_SUITE_END;
