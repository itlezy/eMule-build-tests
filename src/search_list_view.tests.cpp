#include "../third_party/doctest/doctest.h"
#include "SearchListViewSeams.h"

#include <vector>

TEST_SUITE_BEGIN("parity");

TEST_CASE("Search list visible rows omit hidden parents and keep stored parent order")
{
	const std::vector<SearchListViewSeams::SStoredRow> aStoredRows{
		{1u, 0u, false, false},
		{2u, 0u, true, false},
		{3u, 0u, false, false}
	};

	std::vector<SearchListViewSeams::SVisibleRow> aVisibleRows;
	SearchListViewSeams::BuildVisibleRows(aStoredRows, &aVisibleRows);

	REQUIRE_EQ(aVisibleRows.size(), 2u);
	CHECK_EQ(aVisibleRows[0].uRowId, 1u);
	CHECK_EQ(aVisibleRows[0].eKind, SearchListViewSeams::EVisibleRowKind::Parent);
	CHECK_EQ(aVisibleRows[1].uRowId, 3u);
	CHECK_EQ(aVisibleRows[1].eKind, SearchListViewSeams::EVisibleRowKind::Parent);
}

TEST_CASE("Search list visible rows append expanded children directly after the parent")
{
	const std::vector<SearchListViewSeams::SStoredRow> aStoredRows{
		{10u, 0u, false, true},
		{11u, 10u, false, false},
		{12u, 10u, false, false},
		{20u, 0u, false, false}
	};

	std::vector<SearchListViewSeams::SVisibleRow> aVisibleRows;
	SearchListViewSeams::BuildVisibleRows(aStoredRows, &aVisibleRows);

	REQUIRE_EQ(aVisibleRows.size(), 4u);
	CHECK_EQ(aVisibleRows[0].uRowId, 10u);
	CHECK_EQ(aVisibleRows[0].eKind, SearchListViewSeams::EVisibleRowKind::Parent);
	CHECK_EQ(aVisibleRows[1].uRowId, 11u);
	CHECK_EQ(aVisibleRows[1].eKind, SearchListViewSeams::EVisibleRowKind::Child);
	CHECK_EQ(aVisibleRows[2].uRowId, 12u);
	CHECK_EQ(aVisibleRows[2].eKind, SearchListViewSeams::EVisibleRowKind::Child);
	CHECK_EQ(aVisibleRows[3].uRowId, 20u);
	CHECK_EQ(aVisibleRows[3].eKind, SearchListViewSeams::EVisibleRowKind::Parent);
}

TEST_CASE("Search list visible rows drop children when the parent is collapsed")
{
	const std::vector<SearchListViewSeams::SStoredRow> aStoredRows{
		{100u, 0u, false, false},
		{101u, 100u, false, false},
		{102u, 100u, false, false}
	};

	std::vector<SearchListViewSeams::SVisibleRow> aVisibleRows;
	SearchListViewSeams::BuildVisibleRows(aStoredRows, &aVisibleRows);

	REQUIRE_EQ(aVisibleRows.size(), 1u);
	CHECK_EQ(aVisibleRows[0].uRowId, 100u);
	CHECK_EQ(aVisibleRows[0].eKind, SearchListViewSeams::EVisibleRowKind::Parent);
}

TEST_CASE("Search list visible row lookup returns the flattened child index")
{
	const std::vector<SearchListViewSeams::SStoredRow> aStoredRows{
		{1u, 0u, false, true},
		{2u, 1u, false, false},
		{3u, 1u, false, false},
		{4u, 0u, false, false}
	};

	std::vector<SearchListViewSeams::SVisibleRow> aVisibleRows;
	SearchListViewSeams::BuildVisibleRows(aStoredRows, &aVisibleRows);

	CHECK_EQ(SearchListViewSeams::FindVisibleRowIndex(aVisibleRows, 1u), 0);
	CHECK_EQ(SearchListViewSeams::FindVisibleRowIndex(aVisibleRows, 2u), 1);
	CHECK_EQ(SearchListViewSeams::FindVisibleRowIndex(aVisibleRows, 3u), 2);
	CHECK_EQ(SearchListViewSeams::FindVisibleRowIndex(aVisibleRows, 4u), 3);
	CHECK_EQ(SearchListViewSeams::FindVisibleRowIndex(aVisibleRows, 999u), -1);
}

TEST_CASE("Search list owner-data mutations marshal only when a worker thread touches the UI projection")
{
	CHECK(SearchListViewSeams::ShouldMarshalOwnerDataMutation(11u, 22u));
	CHECK_FALSE(SearchListViewSeams::ShouldMarshalOwnerDataMutation(22u, 22u));
	CHECK_FALSE(SearchListViewSeams::ShouldMarshalOwnerDataMutation(0u, 22u));
}

TEST_CASE("Search list owner-data refresh coalescing posts only one wakeup while pending")
{
	bool bRefreshMessagePending = false;

	CHECK(SearchListViewSeams::TryQueueCoalescedOwnerDataRefresh(bRefreshMessagePending));
	CHECK(bRefreshMessagePending);
	CHECK_FALSE(SearchListViewSeams::TryQueueCoalescedOwnerDataRefresh(bRefreshMessagePending));
}
