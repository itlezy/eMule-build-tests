#include "../third_party/doctest/doctest.h"
#include "PipeApiCommandSeams.h"
#include "PipeApiServerPolicy.h"
#include "PipeApiSurfaceSeams.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("Pipe API command queue accepts work until the configured saturation limit")
{
	CHECK(PipeApiPolicy::CanQueueCommand(PipeApiPolicy::kMaxPendingCommands - 1));
	CHECK_FALSE(PipeApiPolicy::CanQueueCommand(PipeApiPolicy::kMaxPendingCommands));
}

TEST_CASE("Pipe API drops duplicate stats updates when one stats event is already queued")
{
	CHECK_EQ(
		PipeApiPolicy::GetWriteAction(
			PipeApiPolicy::EWriteKind::Stats,
			0,
			0,
			128,
			true),
		PipeApiPolicy::EWriteAction::Drop);
}

TEST_CASE("Pipe API drops stats updates instead of disconnecting on outbound saturation")
{
	CHECK_EQ(
		PipeApiPolicy::GetWriteAction(
			PipeApiPolicy::EWriteKind::Stats,
			PipeApiPolicy::kMaxPendingWrites,
			PipeApiPolicy::kMaxPendingWriteBytes,
			128,
			false),
		PipeApiPolicy::EWriteAction::Drop);
}

TEST_CASE("Pipe API disconnects a slow client when structural events exceed outbound limits")
{
	CHECK_EQ(
		PipeApiPolicy::GetWriteAction(
			PipeApiPolicy::EWriteKind::Structural,
			PipeApiPolicy::kMaxPendingWrites,
			PipeApiPolicy::kMaxPendingWriteBytes,
			128,
			false),
		PipeApiPolicy::EWriteAction::Disconnect);
}

TEST_CASE("Pipe API queues structural events while remaining under outbound limits")
{
	CHECK_EQ(
		PipeApiPolicy::GetWriteAction(
			PipeApiPolicy::EWriteKind::Structural,
			PipeApiPolicy::kMaxPendingWrites - 1,
			PipeApiPolicy::kMaxPendingWriteBytes - 1024,
			512,
			false),
		PipeApiPolicy::EWriteAction::Queue);
}

TEST_CASE("Pipe API exposes stable server priority names for the remote surface")
{
	CHECK_EQ(PipeApiSurfaceSeams::GetServerPriorityName(2), "low");
	CHECK_EQ(PipeApiSurfaceSeams::GetServerPriorityName(0), "normal");
	CHECK_EQ(PipeApiSurfaceSeams::GetServerPriorityName(1), "high");
	CHECK_EQ(PipeApiSurfaceSeams::GetServerPriorityName(99), "normal");
}

TEST_CASE("Pipe API exposes stable upload state names for the remote surface")
{
	CHECK_EQ(PipeApiSurfaceSeams::GetUploadStateName(0), "uploading");
	CHECK_EQ(PipeApiSurfaceSeams::GetUploadStateName(1), "queued");
	CHECK_EQ(PipeApiSurfaceSeams::GetUploadStateName(2), "connecting");
	CHECK_EQ(PipeApiSurfaceSeams::GetUploadStateName(3), "banned");
	CHECK_EQ(PipeApiSurfaceSeams::GetUploadStateName(4), "idle");
	CHECK_EQ(PipeApiSurfaceSeams::GetUploadStateName(255), "idle");
}

TEST_CASE("Pipe API parses the final transfer priority vocabulary")
{
	CHECK_EQ(PipeApiSurfaceSeams::ParseTransferPriorityName("auto"), PipeApiSurfaceSeams::ETransferPriority::Auto);
	CHECK_EQ(PipeApiSurfaceSeams::ParseTransferPriorityName("very_low"), PipeApiSurfaceSeams::ETransferPriority::VeryLow);
	CHECK_EQ(PipeApiSurfaceSeams::ParseTransferPriorityName("low"), PipeApiSurfaceSeams::ETransferPriority::Low);
	CHECK_EQ(PipeApiSurfaceSeams::ParseTransferPriorityName("normal"), PipeApiSurfaceSeams::ETransferPriority::Normal);
	CHECK_EQ(PipeApiSurfaceSeams::ParseTransferPriorityName("high"), PipeApiSurfaceSeams::ETransferPriority::High);
	CHECK_EQ(PipeApiSurfaceSeams::ParseTransferPriorityName("very_high"), PipeApiSurfaceSeams::ETransferPriority::VeryHigh);
	CHECK_EQ(PipeApiSurfaceSeams::ParseTransferPriorityName("invalid"), PipeApiSurfaceSeams::ETransferPriority::Invalid);
	CHECK_EQ(PipeApiSurfaceSeams::ParseTransferPriorityName(nullptr), PipeApiSurfaceSeams::ETransferPriority::Invalid);
}

TEST_CASE("Pipe API parses the expanded mutable preference vocabulary")
{
	CHECK_EQ(PipeApiSurfaceSeams::ParseMutablePreferenceName("maxUploadKiB"), PipeApiSurfaceSeams::EMutablePreference::MaxUploadKiB);
	CHECK_EQ(PipeApiSurfaceSeams::ParseMutablePreferenceName("maxDownloadKiB"), PipeApiSurfaceSeams::EMutablePreference::MaxDownloadKiB);
	CHECK_EQ(PipeApiSurfaceSeams::ParseMutablePreferenceName("maxConnections"), PipeApiSurfaceSeams::EMutablePreference::MaxConnections);
	CHECK_EQ(PipeApiSurfaceSeams::ParseMutablePreferenceName("maxConPerFive"), PipeApiSurfaceSeams::EMutablePreference::MaxConPerFive);
	CHECK_EQ(PipeApiSurfaceSeams::ParseMutablePreferenceName("maxSourcesPerFile"), PipeApiSurfaceSeams::EMutablePreference::MaxSourcesPerFile);
	CHECK_EQ(PipeApiSurfaceSeams::ParseMutablePreferenceName("uploadClientDataRate"), PipeApiSurfaceSeams::EMutablePreference::UploadClientDataRate);
	CHECK_EQ(PipeApiSurfaceSeams::ParseMutablePreferenceName("maxUploadSlots"), PipeApiSurfaceSeams::EMutablePreference::MaxUploadSlots);
	CHECK_EQ(PipeApiSurfaceSeams::ParseMutablePreferenceName("queueSize"), PipeApiSurfaceSeams::EMutablePreference::QueueSize);
	CHECK_EQ(PipeApiSurfaceSeams::ParseMutablePreferenceName("autoConnect"), PipeApiSurfaceSeams::EMutablePreference::AutoConnect);
	CHECK_EQ(PipeApiSurfaceSeams::ParseMutablePreferenceName("newAutoUp"), PipeApiSurfaceSeams::EMutablePreference::NewAutoUp);
	CHECK_EQ(PipeApiSurfaceSeams::ParseMutablePreferenceName("newAutoDown"), PipeApiSurfaceSeams::EMutablePreference::NewAutoDown);
	CHECK_EQ(PipeApiSurfaceSeams::ParseMutablePreferenceName("creditSystem"), PipeApiSurfaceSeams::EMutablePreference::CreditSystem);
	CHECK_EQ(PipeApiSurfaceSeams::ParseMutablePreferenceName("safeServerConnect"), PipeApiSurfaceSeams::EMutablePreference::SafeServerConnect);
	CHECK_EQ(PipeApiSurfaceSeams::ParseMutablePreferenceName("networkKademlia"), PipeApiSurfaceSeams::EMutablePreference::NetworkKademlia);
	CHECK_EQ(PipeApiSurfaceSeams::ParseMutablePreferenceName("networkEd2k"), PipeApiSurfaceSeams::EMutablePreference::NetworkEd2K);
	CHECK_EQ(PipeApiSurfaceSeams::ParseMutablePreferenceName("unsupported"), PipeApiSurfaceSeams::EMutablePreference::Invalid);
	CHECK_EQ(PipeApiSurfaceSeams::ParseMutablePreferenceName(nullptr), PipeApiSurfaceSeams::EMutablePreference::Invalid);
}

TEST_CASE("Pipe API normalizes search method and type names case-insensitively")
{
	CHECK_EQ(PipeApiCommandSeams::ParseSearchMethodName("AUTOMATIC"), PipeApiCommandSeams::ESearchMethod::Automatic);
	CHECK_EQ(PipeApiCommandSeams::ParseSearchMethodName("gLoBaL"), PipeApiCommandSeams::ESearchMethod::Global);
	CHECK_EQ(PipeApiCommandSeams::ParseSearchMethodName(""), PipeApiCommandSeams::ESearchMethod::Invalid);
	CHECK_EQ(PipeApiCommandSeams::ParseSearchMethodName(nullptr), PipeApiCommandSeams::ESearchMethod::Invalid);
	CHECK_EQ(PipeApiCommandSeams::ParseSearchFileTypeName("VIDEO"), PipeApiCommandSeams::ESearchFileType::Video);
	CHECK_EQ(PipeApiCommandSeams::ParseSearchFileTypeName("emulecollection"), PipeApiCommandSeams::ESearchFileType::EmuleCollection);
	CHECK_EQ(PipeApiCommandSeams::ParseSearchFileTypeName(nullptr), PipeApiCommandSeams::ESearchFileType::Invalid);
}

TEST_CASE("Pipe API only allows shared-file removal for files that are shared and not mandatory")
{
	CHECK(PipeApiSurfaceSeams::CanRemoveSharedFile(true, false));
	CHECK_FALSE(PipeApiSurfaceSeams::CanRemoveSharedFile(false, false));
	CHECK_FALSE(PipeApiSurfaceSeams::CanRemoveSharedFile(true, true));
}

TEST_CASE("Pipe API parses the search start command vocabulary and trims the query")
{
	PipeApiCommandSeams::SSearchStartRequest request;
	std::string error;
	const PipeApiCommandSeams::json params = {
		{"query", " 1080p "},
		{"method", "KaD"},
		{"type", "ISO"},
		{"ext", ".mkv"},
		{"min_size", 700u},
		{"max_size", 4096u}
	};

	CHECK(PipeApiCommandSeams::TryParseSearchStartRequest(params, request, error));
	CHECK(error.empty());
	CHECK_EQ(request.strQuery, "1080p");
	CHECK_EQ(request.eMethod, PipeApiCommandSeams::ESearchMethod::Kad);
	CHECK_EQ(request.eFileType, PipeApiCommandSeams::ESearchFileType::CdImage);
	CHECK_EQ(request.strExtension, ".mkv");
	CHECK(request.bHasMinSize);
	CHECK(request.bHasMaxSize);
	CHECK_EQ(request.ullMinSize, 700u);
	CHECK_EQ(request.ullMaxSize, 4096u);
}

TEST_CASE("Pipe API rejects invalid search start payloads before they touch the UI")
{
	PipeApiCommandSeams::SSearchStartRequest request;
	std::string error;

	CHECK_FALSE(PipeApiCommandSeams::TryParseSearchStartRequest(PipeApiCommandSeams::json{{"query", "   "}}, request, error));
	CHECK_EQ(error, "query must not be empty");

	error.clear();
	CHECK_FALSE(PipeApiCommandSeams::TryParseSearchStartRequest(PipeApiCommandSeams::json{{"query", "1080p"}, {"method", "contentdb"}}, request, error));
	CHECK_EQ(error, "method must be one of automatic, server, global, kad");

	error.clear();
	CHECK_FALSE(PipeApiCommandSeams::TryParseSearchStartRequest(PipeApiCommandSeams::json{{"query", "1080p"}, {"type", "ebook"}}, request, error));
	CHECK_EQ(error, "type is not supported");

	error.clear();
	CHECK_FALSE(PipeApiCommandSeams::TryParseSearchStartRequest(PipeApiCommandSeams::json{{"query", "1080p"}, {"min_size", -1}}, request, error));
	CHECK_EQ(error, "min_size must be an unsigned number");

	error.clear();
	CHECK_FALSE(PipeApiCommandSeams::TryParseSearchStartRequest(PipeApiCommandSeams::json{{"query", 7}}, request, error));
	CHECK_EQ(error, "query must be a string");

	error.clear();
	CHECK_FALSE(PipeApiCommandSeams::TryParseSearchStartRequest(PipeApiCommandSeams::json{{"query", "1080p"}, {"method", 7}}, request, error));
	CHECK_EQ(error, "method must be a string");

	error.clear();
	CHECK_FALSE(PipeApiCommandSeams::TryParseSearchStartRequest(PipeApiCommandSeams::json{{"query", "1080p"}, {"type", 7}}, request, error));
	CHECK_EQ(error, "type must be a string");

	error.clear();
	CHECK_FALSE(PipeApiCommandSeams::TryParseSearchStartRequest(PipeApiCommandSeams::json{{"query", "1080p"}, {"ext", 7}}, request, error));
	CHECK_EQ(error, "ext must be a string");

	error.clear();
	CHECK_FALSE(PipeApiCommandSeams::TryParseSearchStartRequest(PipeApiCommandSeams::json{{"query", "1080p"}, {"max_size", -1}}, request, error));
	CHECK_EQ(error, "max_size must be an unsigned number");
}

TEST_CASE("Pipe API parses search identifiers as decimal uint32 strings")
{
	uint32_t uSearchID = 0;
	std::string error;

	CHECK(PipeApiCommandSeams::TryParseSearchId(PipeApiCommandSeams::json("12345"), uSearchID, error));
	CHECK_EQ(uSearchID, 12345u);

	error.clear();
	CHECK_FALSE(PipeApiCommandSeams::TryParseSearchId(PipeApiCommandSeams::json(""), uSearchID, error));
	CHECK_EQ(error, "search_id must not be empty");

	error.clear();
	CHECK_FALSE(PipeApiCommandSeams::TryParseSearchId(PipeApiCommandSeams::json("12x"), uSearchID, error));
	CHECK_EQ(error, "search_id must be a valid uint32 decimal string");

	error.clear();
	CHECK_FALSE(PipeApiCommandSeams::TryParseSearchId(PipeApiCommandSeams::json(7), uSearchID, error));
	CHECK_EQ(error, "search_id must be a decimal string");

	error.clear();
	CHECK_FALSE(PipeApiCommandSeams::TryParseSearchId(PipeApiCommandSeams::json("4294967296"), uSearchID, error));
	CHECK_EQ(error, "search_id must be a valid uint32 decimal string");
}

TEST_CASE("Pipe API parses transfer list filters and validates categories")
{
	PipeApiCommandSeams::STransfersListRequest request;
	std::string error;

	CHECK(PipeApiCommandSeams::TryParseTransfersListRequest(PipeApiCommandSeams::json{{"filter", "DoWnLoAdInG"}, {"category", 3}}, request, error));
	CHECK_EQ(request.strFilterLower, "downloading");
	CHECK(request.bHasCategory);
	CHECK_EQ(request.uCategory, 3u);

	error.clear();
	CHECK_FALSE(PipeApiCommandSeams::TryParseTransfersListRequest(PipeApiCommandSeams::json{{"filter", 7}}, request, error));
	CHECK_EQ(error, "filter must be a string when provided");

	error.clear();
	CHECK_FALSE(PipeApiCommandSeams::TryParseTransfersListRequest(PipeApiCommandSeams::json{{"category", -1}}, request, error));
	CHECK_EQ(error, "category must be an unsigned number");
}

TEST_CASE("Pipe API trims transfer add links and rejects empty payloads")
{
	std::string link;
	std::string error;

	CHECK(PipeApiCommandSeams::TryParseTransferAddLink(PipeApiCommandSeams::json{{"link", " ed2k://|file|ubuntu.iso|1|0123456789abcdef0123456789abcdef|/ "}}, link, error));
	CHECK_EQ(link, "ed2k://|file|ubuntu.iso|1|0123456789abcdef0123456789abcdef|/");

	error.clear();
	CHECK(PipeApiCommandSeams::TryParseTransferAddLink(PipeApiCommandSeams::json{{"link", " ed2k://|server|1.2.3.4|4661|/ "}}, link, error));
	CHECK_EQ(link, "ed2k://|server|1.2.3.4|4661|/");

	error.clear();
	CHECK_FALSE(PipeApiCommandSeams::TryParseTransferAddLink(PipeApiCommandSeams::json{{"link", "   "}}, link, error));
	CHECK_EQ(error, "link must not be empty");

	error.clear();
	CHECK_FALSE(PipeApiCommandSeams::TryParseTransferAddLink(PipeApiCommandSeams::json{{"link", 7}}, link, error));
	CHECK_EQ(error, "link must be a string");

	error.clear();
	CHECK_FALSE(PipeApiCommandSeams::TryParseTransferAddLink(PipeApiCommandSeams::json::object(), link, error));
	CHECK_EQ(error, "link must be a string");
}

TEST_CASE("Pipe API preserves source-oriented ed2k links after trimming transport whitespace")
{
	std::string link;
	std::string error;

	CHECK(PipeApiCommandSeams::TryParseTransferAddLink(
		PipeApiCommandSeams::json{{"link", "\n\ted2k://|file|ubuntu.iso|1|0123456789abcdef0123456789abcdef|sources,1.2.3.4:4662|/\r\n"}},
		link,
		error));
	CHECK_EQ(link, "ed2k://|file|ubuntu.iso|1|0123456789abcdef0123456789abcdef|sources,1.2.3.4:4662|/");
}

TEST_CASE("Pipe API parses bulk transfer mutations and the delete-files aliases")
{
	PipeApiCommandSeams::STransferBulkMutationRequest request;
	std::string error;

	CHECK(PipeApiCommandSeams::TryParseTransferBulkMutationRequest(
		PipeApiCommandSeams::json{
			{"hashes", PipeApiCommandSeams::json::array({"a", "b"})},
			{"delete_files", true}
		},
		request,
		error));
	CHECK_EQ(request.hashes.size(), 2u);
	CHECK(request.bDeleteFiles);
	CHECK_EQ(request.hashes[0].get<std::string>(), "a");
	CHECK_EQ(request.hashes[1].get<std::string>(), "b");

	error.clear();
	CHECK_FALSE(PipeApiCommandSeams::TryParseTransferBulkMutationRequest(PipeApiCommandSeams::json{{"hashes", "abc"}}, request, error));
	CHECK_EQ(error, "hashes must be a string array");
}

TEST_CASE("Pipe API accepts both delete-file aliases and rejects missing hash arrays")
{
	PipeApiCommandSeams::STransferBulkMutationRequest request;
	std::string error;

	CHECK(PipeApiCommandSeams::TryParseTransferBulkMutationRequest(
		PipeApiCommandSeams::json{
			{"hashes", PipeApiCommandSeams::json::array()},
			{"deleteFiles", true}
		},
		request,
		error));
	CHECK(request.bDeleteFiles);
	CHECK_EQ(request.hashes.size(), 0u);

	error.clear();
	CHECK_FALSE(PipeApiCommandSeams::TryParseTransferBulkMutationRequest(PipeApiCommandSeams::json::object(), request, error));
	CHECK_EQ(error, "hashes must be a string array");
}
