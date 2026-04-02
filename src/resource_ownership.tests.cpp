#include "../third_party/doctest/doctest.h"

#include "ResourceOwnershipSeams.h"

#include <vector>
#include <windows.h>

namespace
{
	struct FakeOwnedObject
	{
		int value;
	};

	struct FakeRequestedBlock
	{
		int id;
	};

	struct FakePendingBlock
	{
		FakeRequestedBlock *block;
	};

	struct FakePendingList
	{
		std::vector<FakePendingBlock*> items;

		~FakePendingList()
		{
			for (FakePendingBlock *pItem : items)
				delete pItem;
		}

		void AddTail(FakePendingBlock *pItem)
		{
			items.push_back(pItem);
		}
	};
}

TEST_SUITE_BEGIN("parity");

TEST_CASE("ScopedHandle closes the wrapped Win32 handle on scope exit")
{
	HANDLE hEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL);
	REQUIRE(hEvent != NULL);

	{
		ScopedHandle hOwnedEvent(hEvent);
		REQUIRE(hOwnedEvent.IsValid());
		CHECK(hOwnedEvent.Get() == hEvent);
	}

	::SetLastError(ERROR_SUCCESS);
	CHECK(::WaitForSingleObject(hEvent, 0) == WAIT_FAILED);
	CHECK(::GetLastError() == ERROR_INVALID_HANDLE);
}

TEST_CASE("ScopedHandle reset closes the previously owned handle before replacing it")
{
	HANDLE hFirstEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL);
	HANDLE hSecondEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL);
	REQUIRE(hFirstEvent != NULL);
	REQUIRE(hSecondEvent != NULL);

	ScopedHandle hOwnedEvent(hFirstEvent);
	hOwnedEvent.Reset(hSecondEvent);

	CHECK(hOwnedEvent.IsValid());
	CHECK(hOwnedEvent.Get() == hSecondEvent);

	::SetLastError(ERROR_SUCCESS);
	CHECK(::WaitForSingleObject(hFirstEvent, 0) == WAIT_FAILED);
	CHECK(::GetLastError() == ERROR_INVALID_HANDLE);

	HANDLE hReleasedEvent = hOwnedEvent.Release();
	CHECK(hReleasedEvent == hSecondEvent);
	CHECK_FALSE(hOwnedEvent.IsValid());
	CHECK(::CloseHandle(hReleasedEvent) != 0);
}

TEST_CASE("ScopedHandle release keeps the raw handle alive for the next owner")
{
	HANDLE hEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL);
	REQUIRE(hEvent != NULL);

	ScopedHandle hOwnedEvent(hEvent);
	HANDLE hReleasedEvent = hOwnedEvent.Release();

	CHECK(hReleasedEvent == hEvent);
	CHECK_FALSE(hOwnedEvent.IsValid());
	CHECK(::WaitForSingleObject(hReleasedEvent, 0) == WAIT_TIMEOUT);
	CHECK(::CloseHandle(hReleasedEvent) != 0);
}

TEST_CASE("ReleaseOwnedObjectIfMatched only releases ownership for the accepted object")
{
	{
		std::unique_ptr<FakeOwnedObject> pOwnedObject(new FakeOwnedObject{1});
		FakeOwnedObject *pAcceptedObject = pOwnedObject.get();

		ReleaseOwnedObjectIfMatched(pOwnedObject, pAcceptedObject);

		CHECK(pOwnedObject.get() == nullptr);
		delete pAcceptedObject;
	}

	{
		std::unique_ptr<FakeOwnedObject> pOwnedObject(new FakeOwnedObject{2});
		FakeOwnedObject otherObject = {3};

		ReleaseOwnedObjectIfMatched(pOwnedObject, &otherObject);

		CHECK(pOwnedObject.get() != nullptr);
		CHECK(pOwnedObject->value == 2);
	}
}

TEST_CASE("AppendPendingBlocksFromStage preserves staged pointer order")
{
	FakeRequestedBlock firstBlock = {1};
	FakeRequestedBlock secondBlock = {2};
	FakeRequestedBlock *stagedBlocks[] = {&firstBlock, &secondBlock};
	FakePendingList pendingList;

	AppendPendingBlocksFromStage<FakePendingList, FakePendingBlock>(pendingList, stagedBlocks, 2);

	REQUIRE(pendingList.items.size() == 2);
	CHECK(pendingList.items[0]->block == &firstBlock);
	CHECK(pendingList.items[1]->block == &secondBlock);
}

TEST_SUITE_END;
