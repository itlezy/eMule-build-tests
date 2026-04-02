#include "../third_party/doctest/doctest.h"

#include <list>
#include <vector>

#include "UploadBandwidthThrottlerSeams.h"

namespace
{
	struct FakeSocket
	{
		int id;
	};

	template <typename TSocket>
	std::vector<int> GetSocketIds(const std::list<TSocket*> &queue)
	{
		std::vector<int> ids;
		for (TSocket *socket : queue)
			ids.push_back(socket != NULL ? socket->id : -1);
		return ids;
	}
}

TEST_SUITE_BEGIN("parity");

TEST_CASE("Upload throttler seam merges pending control queues without disturbing existing priority order")
{
	FakeSocket liveFirstA{1};
	FakeSocket liveA{2};
	FakeSocket pendingFirstA{3};
	FakeSocket pendingFirstB{4};
	FakeSocket pendingA{5};
	FakeSocket pendingB{6};

	std::list<FakeSocket*> controlQueueFirst{&liveFirstA};
	std::list<FakeSocket*> controlQueue{&liveA};
	std::list<FakeSocket*> tempControlQueueFirst{&pendingFirstA, &pendingFirstB};
	std::list<FakeSocket*> tempControlQueue{&pendingA, &pendingB};

	UploadBandwidthThrottlerSeams::MergePendingControlQueues(controlQueueFirst, controlQueue, tempControlQueueFirst, tempControlQueue);

	CHECK(GetSocketIds(controlQueueFirst) == std::vector<int>{1, 3, 4});
	CHECK(GetSocketIds(controlQueue) == std::vector<int>{2, 5, 6});
	CHECK(tempControlQueueFirst.empty());
	CHECK(tempControlQueue.empty());
}

TEST_CASE("Upload throttler seam removes a socket from every control queue domain")
{
	FakeSocket keepA{1};
	FakeSocket removeMe{2};
	FakeSocket keepB{3};

	std::list<FakeSocket*> controlQueueFirst{&keepA, &removeMe};
	std::list<FakeSocket*> controlQueue{&removeMe, &keepB};
	std::list<FakeSocket*> tempControlQueueFirst{&removeMe};
	std::list<FakeSocket*> tempControlQueue{&keepA, &removeMe, &keepB};

	CHECK(UploadBandwidthThrottlerSeams::RemoveSocketFromAllControlQueues(
		controlQueueFirst,
		controlQueue,
		tempControlQueueFirst,
		tempControlQueue,
		&removeMe));

	CHECK(GetSocketIds(controlQueueFirst) == std::vector<int>{1});
	CHECK(GetSocketIds(controlQueue) == std::vector<int>{3});
	CHECK(tempControlQueueFirst.empty());
	CHECK(GetSocketIds(tempControlQueue) == std::vector<int>{1, 3});
	CHECK_FALSE(UploadBandwidthThrottlerSeams::RemoveSocketFromAllControlQueues(
		controlQueueFirst,
		controlQueue,
		tempControlQueueFirst,
		tempControlQueue,
		&removeMe));
}

TEST_CASE("Upload throttler seam clears all queue domains during shutdown cleanup")
{
	FakeSocket socketA{1};
	FakeSocket socketB{2};

	std::list<FakeSocket*> controlQueueFirst{&socketA};
	std::list<FakeSocket*> controlQueue{&socketB};
	std::list<FakeSocket*> tempControlQueueFirst{&socketB};
	std::list<FakeSocket*> tempControlQueue{&socketA};

	UploadBandwidthThrottlerSeams::ClearAllControlQueues(controlQueueFirst, controlQueue, tempControlQueueFirst, tempControlQueue);

	CHECK(controlQueueFirst.empty());
	CHECK(controlQueue.empty());
	CHECK(tempControlQueueFirst.empty());
	CHECK(tempControlQueue.empty());
}

TEST_SUITE_END;
