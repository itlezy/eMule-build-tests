#include "../third_party/doctest/doctest.h"

#include "ServerMetPersistenceSeams.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace
{
	class ScopedTempDir
	{
	public:
		ScopedTempDir()
		{
			WCHAR szTempPath[MAX_PATH] = { 0 };
			REQUIRE(::GetTempPathW(_countof(szTempPath), szTempPath) != 0);

			WCHAR szTempFile[MAX_PATH] = { 0 };
			REQUIRE(::GetTempFileNameW(szTempPath, L"smt", 0, szTempFile) != 0);

			m_root = std::filesystem::path(szTempFile);
			std::error_code ec;
			std::filesystem::remove(m_root, ec);
			std::filesystem::create_directories(m_root, ec);
			REQUIRE_FALSE(ec);
		}

		~ScopedTempDir()
		{
			std::error_code ec;
			std::filesystem::remove_all(m_root, ec);
		}

		const std::filesystem::path &Root() const
		{
			return m_root;
		}

	private:
		std::filesystem::path m_root;
	};

	void WriteBytes(const std::filesystem::path &rPath, const std::vector<unsigned char> &rBytes)
	{
		std::ofstream file(rPath, std::ios::binary | std::ios::trunc);
		REQUIRE(file.good());
		file.write(reinterpret_cast<const char *>(rBytes.data()), static_cast<std::streamsize>(rBytes.size()));
		REQUIRE(file.good());
	}

	void WriteTextFile(const std::filesystem::path &rPath, const char *pszText)
	{
		std::ofstream file(rPath, std::ios::binary | std::ios::trunc);
		REQUIRE(file.good());
		file << pszText;
		REQUIRE(file.good());
	}

	std::string ReadTextFile(const std::filesystem::path &rPath)
	{
		std::ifstream file(rPath, std::ios::binary);
		REQUIRE(file.good());
		return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	}

	void AppendUInt16(std::vector<unsigned char> &rBytes, uint16_t nValue)
	{
		rBytes.push_back(static_cast<unsigned char>(nValue & 0xFFu));
		rBytes.push_back(static_cast<unsigned char>((nValue >> 8) & 0xFFu));
	}

	void AppendUInt32(std::vector<unsigned char> &rBytes, uint32_t nValue)
	{
		rBytes.push_back(static_cast<unsigned char>(nValue & 0xFFu));
		rBytes.push_back(static_cast<unsigned char>((nValue >> 8) & 0xFFu));
		rBytes.push_back(static_cast<unsigned char>((nValue >> 16) & 0xFFu));
		rBytes.push_back(static_cast<unsigned char>((nValue >> 24) & 0xFFu));
	}

	std::vector<unsigned char> MakeServerMetFixture(uint32_t nServerCount)
	{
		std::vector<unsigned char> bytes;
		bytes.push_back(ServerMetPersistenceSeams::kCurrentServerMetHeader);
		AppendUInt32(bytes, nServerCount);
		for (uint32_t nIndex = 0; nIndex < nServerCount; ++nIndex) {
			AppendUInt32(bytes, 0x01020304u + nIndex);
			AppendUInt16(bytes, static_cast<uint16_t>(4661u + nIndex));
			AppendUInt32(bytes, 0u);
		}
		return bytes;
	}

	BOOL WINAPI AlwaysFailMoveFileEx(LPCTSTR, LPCTSTR, DWORD)
	{
		::SetLastError(ERROR_ACCESS_DENIED);
		return FALSE;
	}

	BOOL WINAPI AlwaysFailCopyFile(LPCTSTR, LPCTSTR, BOOL)
	{
		::SetLastError(ERROR_DISK_FULL);
		return FALSE;
	}
}

TEST_SUITE_BEGIN("parity");

TEST_CASE("server.met inspector accepts a structurally valid candidate")
{
	ScopedTempDir tempDir;
	const std::filesystem::path candidatePath = tempDir.Root() / L"server.met.download.new";
	WriteBytes(candidatePath, MakeServerMetFixture(1));

	ServerMetPersistenceSeams::ServerMetCandidateInfo info;
	CHECK(ServerMetPersistenceSeams::InspectServerMetCandidate(candidatePath.c_str(), info));
	CHECK_EQ(info.ServerCount, 1u);
}

TEST_CASE("server.met inspector rejects malformed and empty downloaded candidates")
{
	ScopedTempDir tempDir;
	const std::filesystem::path truncatedPath = tempDir.Root() / L"server.met.truncated";
	const std::filesystem::path emptyListPath = tempDir.Root() / L"server.met.empty";

	WriteBytes(truncatedPath, { ServerMetPersistenceSeams::kCurrentServerMetHeader, 1, 0, 0, 0 });
	WriteBytes(emptyListPath, MakeServerMetFixture(0));

	ServerMetPersistenceSeams::ServerMetCandidateInfo info;
	CHECK_FALSE(ServerMetPersistenceSeams::InspectServerMetCandidate(truncatedPath.c_str(), info));
	CHECK_FALSE(ServerMetPersistenceSeams::InspectServerMetCandidate(emptyListPath.c_str(), info));
	CHECK(ServerMetPersistenceSeams::InspectServerMetCandidate(emptyListPath.c_str(), info, false));
	CHECK_EQ(info.ServerCount, 0u);
}

TEST_CASE("server.met downloaded candidate install rejects invalid input without mutating the target")
{
	ScopedTempDir tempDir;
	const std::filesystem::path candidatePath = tempDir.Root() / L"server_met.download.new";
	const std::filesystem::path targetPath = tempDir.Root() / L"server_met.download";

	WriteBytes(candidatePath, { 0x01, 1, 0, 0, 0 });
	WriteTextFile(targetPath, "previous");

	DWORD dwLastError = ERROR_SUCCESS;
	CHECK_FALSE(ServerMetPersistenceSeams::InstallDownloadedServerMetCandidate(candidatePath.c_str(), targetPath.c_str(), &dwLastError));
	CHECK_EQ(dwLastError, static_cast<DWORD>(ERROR_INVALID_DATA));
	CHECK_EQ(ReadTextFile(targetPath), std::string("previous"));
	CHECK(std::filesystem::exists(candidatePath));
}

TEST_CASE("server.met promotion preserves the destination when replacement fails")
{
	ScopedTempDir tempDir;
	const std::filesystem::path sourcePath = tempDir.Root() / L"server.met.new";
	const std::filesystem::path targetPath = tempDir.Root() / L"server.met";

	WriteTextFile(sourcePath, "after");
	WriteTextFile(targetPath, "before");

	PartFilePersistenceSeams::FileSystemOps ops = PartFilePersistenceSeams::GetDefaultFileSystemOps();
	ops.MoveFileEx = &AlwaysFailMoveFileEx;

	DWORD dwLastError = ERROR_SUCCESS;
	CHECK_FALSE(ServerMetPersistenceSeams::PromotePreparedServerMetWithOps(sourcePath.c_str(), targetPath.c_str(), &dwLastError, ops));
	CHECK_EQ(dwLastError, static_cast<DWORD>(ERROR_ACCESS_DENIED));
	CHECK_EQ(ReadTextFile(targetPath), std::string("before"));
	CHECK(std::filesystem::exists(sourcePath));
}

TEST_CASE("server.met backup refresh preserves the previous backup when staging fails")
{
	ScopedTempDir tempDir;
	const std::filesystem::path livePath = tempDir.Root() / L"server.met";
	const std::filesystem::path backupPath = tempDir.Root() / L"server_met.old";
	const std::filesystem::path backupTempPath = tempDir.Root() / L"server_met.old.tmp";

	WriteTextFile(livePath, "current");
	WriteTextFile(backupPath, "previous");

	PartFilePersistenceSeams::FileSystemOps ops = PartFilePersistenceSeams::GetDefaultFileSystemOps();
	ops.CopyFile = &AlwaysFailCopyFile;

	DWORD dwLastError = ERROR_SUCCESS;
	CHECK_FALSE(ServerMetPersistenceSeams::RefreshServerMetBackupWithOps(livePath.c_str(), backupPath.c_str(), backupTempPath.c_str(), &dwLastError, ops));
	CHECK_EQ(dwLastError, static_cast<DWORD>(ERROR_DISK_FULL));
	CHECK_EQ(ReadTextFile(backupPath), std::string("previous"));
	CHECK_FALSE(std::filesystem::exists(backupTempPath));
}

TEST_SUITE_END();
