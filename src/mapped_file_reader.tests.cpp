#include "../third_party/doctest/doctest.h"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <vector>

#if defined(__has_include)
#if __has_include("MappedFileReader.h")
#include "MappedFileReader.h"
#define EMULE_TEST_HAVE_MAPPED_FILE_READER 1
#else
#define EMULE_TEST_HAVE_MAPPED_FILE_READER 0
#endif
#else
#define EMULE_TEST_HAVE_MAPPED_FILE_READER 0
#endif

namespace
{
	constexpr size_t HASH_BUFFER_SIZE = 8192u;
	constexpr size_t DEFAULT_SAMPLE_COUNT = 5u;
	constexpr unsigned long long DEFAULT_SAMPLE_MAX_BYTES = 96ull * 1024ull * 1024ull;
	constexpr unsigned long long DEFAULT_STABLE_AGE_MS = 60000ull;
	constexpr unsigned long long PARTSIZE_BYTES = 9728000ull;
	constexpr size_t EMBLOCKSIZE_BYTES = 184320u;

	using CMd4Digest = std::array<BYTE, 16>;
	using CSha1Digest = std::array<BYTE, 20>;

	enum class EReaderMode
	{
		Buffered,
		Win32,
		Mapped
	};

	struct CSampledFile
	{
		std::wstring Path;
		unsigned long long Length = 0;
	};

	struct CFileHashArtifacts
	{
		unsigned long long Length = 0;
		CMd4Digest FileMd4 = {};
		CSha1Digest AichMaster = {};
		std::vector<CMd4Digest> Md4PartHashes;
		std::vector<CSha1Digest> AichPartHashes;

		bool operator==(const CFileHashArtifacts &rhs) const
		{
			return Length == rhs.Length
				&& FileMd4 == rhs.FileMd4
				&& AichMaster == rhs.AichMaster
				&& Md4PartHashes == rhs.Md4PartHashes
				&& AichPartHashes == rhs.AichPartHashes;
		}
	};

	/**
	 * @brief Updates a simple byte-for-byte checksum while the tests compare reader paths.
	 */
	class CFnv1a64
	{
	public:
		void Add(const BYTE *pBytes, size_t nByteCount)
		{
			for (size_t i = 0; i < nByteCount; ++i) {
				m_nValue ^= static_cast<unsigned long long>(pBytes[i]);
				m_nValue *= 1099511628211ull;
			}
		}

		unsigned long long GetValue() const
		{
			return m_nValue;
		}

	private:
		unsigned long long m_nValue = 1469598103934665603ull;
	};

#if EMULE_TEST_HAVE_MAPPED_FILE_READER
	class CFnvMappedVisitor : public IMappedFileRangeVisitor
	{
	public:
		void OnMappedFileBytes(const BYTE *pBytes, size_t nByteCount) override
		{
			m_Hash.Add(pBytes, nByteCount);
		}

		unsigned long long GetDigest() const
		{
			return m_Hash.GetValue();
		}

	private:
		CFnv1a64 m_Hash;
	};
#endif

	/**
	 * @brief Reads a small integer override from the environment when present.
	 */
	unsigned long long GetEnvironmentUint64(LPCWSTR pszName, unsigned long long nDefaultValue)
	{
		WCHAR szValue[64] = {};
		const DWORD dwLength = ::GetEnvironmentVariableW(pszName, szValue, _countof(szValue));
		if (dwLength == 0 || dwLength >= _countof(szValue))
			return nDefaultValue;

		WCHAR *pEnd = NULL;
		const unsigned long long nValue = _wcstoui64(szValue, &pEnd, 10);
		return (pEnd != szValue) ? nValue : nDefaultValue;
	}

	/**
	 * @brief Uses a caller-provided sample seed when present so dev and oracle can benchmark the same files.
	 */
	unsigned long long GetRuntimeSampleSeed()
	{
		const unsigned long long nConfiguredSeed = GetEnvironmentUint64(L"EMULE_TEST_SAMPLE_SEED", 0ull);
		if (nConfiguredSeed != 0ull)
			return nConfiguredSeed;

		return static_cast<unsigned long long>(::GetTickCount64()) ^ static_cast<unsigned long long>(::GetCurrentProcessId());
	}

	/**
	 * @brief Keeps actual-file sampling rooted in C:\tmp unless a caller overrides it explicitly.
	 */
	std::wstring GetRuntimeSampleRoot()
	{
		WCHAR szValue[MAX_PATH] = {};
		const DWORD dwLength = ::GetEnvironmentVariableW(L"EMULE_TEST_FILE_ROOT", szValue, _countof(szValue));
		if (dwLength == 0 || dwLength >= _countof(szValue))
			return L"C:\\tmp";

		return std::wstring(szValue);
	}

	/**
	 * @brief Rejects files that are still being updated so parity runs stay stable on temp directories.
	 */
	bool IsStableEnoughForSampling(const std::filesystem::directory_entry &entry, unsigned long long nStableAgeMs)
	{
		std::error_code ec;
		const auto lastWriteTime = entry.last_write_time(ec);
		if (ec)
			return true;

		const auto now = std::filesystem::file_time_type::clock::now();
		if (now < lastWriteTime)
			return false;

		const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastWriteTime).count();
		return static_cast<unsigned long long>(age) >= nStableAgeMs;
	}

	/**
	 * @brief Collects a runtime-random, name-agnostic sample of readable files from C:\tmp.
	 */
	std::vector<CSampledFile> SelectRuntimeSampleFiles()
	{
		const std::wstring sampleRoot = GetRuntimeSampleRoot();
		const size_t nSampleCount = static_cast<size_t>(GetEnvironmentUint64(L"EMULE_TEST_SAMPLE_COUNT", DEFAULT_SAMPLE_COUNT));
		const unsigned long long nMaxBytes = GetEnvironmentUint64(L"EMULE_TEST_SAMPLE_MAX_BYTES", DEFAULT_SAMPLE_MAX_BYTES);
		const unsigned long long nStableAgeMs = GetEnvironmentUint64(L"EMULE_TEST_SAMPLE_STABLE_AGE_MS", DEFAULT_STABLE_AGE_MS);

		std::vector<CSampledFile> candidates;
		std::error_code ec;
		const std::filesystem::path root(sampleRoot);
		if (!std::filesystem::exists(root, ec))
			return {};

		for (const auto &entry : std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, ec)) {
			if (ec) {
				ec.clear();
				continue;
			}
			if (!entry.is_regular_file(ec) || ec) {
				ec.clear();
				continue;
			}
			if (!IsStableEnoughForSampling(entry, nStableAgeMs))
				continue;

			const auto fileSize = entry.file_size(ec);
			if (ec) {
				ec.clear();
				continue;
			}
			if (fileSize == 0)
				continue;

			candidates.push_back(CSampledFile{ entry.path().wstring(), static_cast<unsigned long long>(fileSize) });
		}

		std::sort(candidates.begin(), candidates.end(), [](const CSampledFile &lhs, const CSampledFile &rhs) {
			return lhs.Path < rhs.Path;
		});

		std::mt19937_64 randomizer(GetRuntimeSampleSeed());
		std::shuffle(candidates.begin(), candidates.end(), randomizer);

		std::vector<CSampledFile> sample;
		unsigned long long nTotalBytes = 0;
		for (const CSampledFile &candidate : candidates) {
			if (!sample.empty() && (sample.size() >= nSampleCount || nTotalBytes + candidate.Length > nMaxBytes))
				continue;

			sample.push_back(candidate);
			nTotalBytes += candidate.Length;
			if (sample.size() >= nSampleCount || nTotalBytes >= nMaxBytes)
				break;
		}

		if (sample.empty() && !candidates.empty())
			sample.push_back(candidates.front());

		return sample;
	}

	/**
	 * @brief Creates deterministic byte content so synthetic range comparisons stay readable.
	 */
	std::vector<BYTE> CreateMappedReaderFixture(size_t nByteCount)
	{
		std::vector<BYTE> data(nByteCount);
		for (size_t i = 0; i < nByteCount; ++i)
			data[i] = static_cast<BYTE>((i * 37u + 11u) & 0xFFu);
		return data;
	}

	/**
	 * @brief Creates a disposable binary file path for mapped-reader regression tests.
	 */
	std::wstring CreateMappedReaderTempPath()
	{
		WCHAR szTempPath[MAX_PATH] = {};
		WCHAR szTempFile[MAX_PATH] = {};
		REQUIRE(::GetTempPathW(_countof(szTempPath), szTempPath) != 0);
		REQUIRE(::GetTempFileNameW(szTempPath, L"mmr", 0, szTempFile) != 0);
		return std::wstring(szTempFile);
	}

	/**
	 * @brief Writes the supplied binary fixture to disk for mapped file reads.
	 */
	void WriteMappedReaderFixture(const std::wstring &rstrPath, const std::vector<BYTE> &rData)
	{
		FILE *pFile = NULL;
		REQUIRE(_wfopen_s(&pFile, rstrPath.c_str(), L"wb") == 0);
		REQUIRE(pFile != NULL);
		REQUIRE(fwrite(rData.data(), 1, rData.size(), pFile) == rData.size());
		fclose(pFile);
	}

	/**
	 * @brief Returns a byte-exact slice for synthetic parity assertions.
	 */
	std::vector<BYTE> SliceMappedReaderFixture(const std::vector<BYTE> &rData, size_t nOffset, size_t nLength)
	{
		return std::vector<BYTE>(rData.begin() + nOffset, rData.begin() + nOffset + nLength);
	}

	/**
	 * @brief Reads a file through a CRT stream to mirror the legacy buffered path.
	 */
	unsigned long long ComputeBufferedDigest(const std::wstring &rstrPath)
	{
		FILE *pFile = NULL;
		REQUIRE(_wfopen_s(&pFile, rstrPath.c_str(), L"rb") == 0);
		REQUIRE(pFile != NULL);

		CFnv1a64 hash;
		BYTE buffer[HASH_BUFFER_SIZE] = {};
		for (;;) {
			const size_t nRead = fread(buffer, 1, sizeof buffer, pFile);
			if (nRead != 0)
				hash.Add(buffer, nRead);
			if (nRead < sizeof buffer) {
				REQUIRE(feof(pFile) != 0);
				break;
			}
		}

		fclose(pFile);
		return hash.GetValue();
	}

	/**
	 * @brief Reads a file through explicit Win32 reads to provide an independent reference path.
	 */
	unsigned long long ComputeWin32Digest(const std::wstring &rstrPath)
	{
		HANDLE hFile = ::CreateFileW(rstrPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
		REQUIRE(hFile != INVALID_HANDLE_VALUE);

		CFnv1a64 hash;
		BYTE buffer[HASH_BUFFER_SIZE] = {};
		for (;;) {
			DWORD dwRead = 0;
			REQUIRE(::ReadFile(hFile, buffer, sizeof buffer, &dwRead, NULL) != FALSE);
			if (dwRead == 0)
				break;
			hash.Add(buffer, dwRead);
		}

		::CloseHandle(hFile);
		return hash.GetValue();
	}

#if EMULE_TEST_HAVE_MAPPED_FILE_READER
	/**
	 * @brief Reads a whole file through the workspace mapped reader implementation.
	 */
	unsigned long long ComputeMappedDigest(const std::wstring &rstrPath)
	{
		HANDLE hFile = ::CreateFileW(rstrPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
		REQUIRE(hFile != INVALID_HANDLE_VALUE);

		LARGE_INTEGER fileSize = {};
		REQUIRE(::GetFileSizeEx(hFile, &fileSize) != FALSE);

		CFnvMappedVisitor visitor;
		DWORD dwError = ERROR_SUCCESS;
		CHECK(VisitMappedFileRange(hFile, 0, static_cast<ULONGLONG>(fileSize.QuadPart), visitor, &dwError));
		CHECK_EQ(dwError, static_cast<DWORD>(ERROR_SUCCESS));
		::CloseHandle(hFile);
		return visitor.GetDigest();
	}
#endif

	template <typename TDigestFn>
	double MeasureDigestPassMs(const std::vector<CSampledFile> &sample, TDigestFn DigestFn, unsigned long long *pnDigestOut)
	{
		const auto start = std::chrono::steady_clock::now();
		unsigned long long nDigest = 0;
		for (const CSampledFile &file : sample)
			nDigest ^= DigestFn(file.Path);
		const auto stop = std::chrono::steady_clock::now();

		if (pnDigestOut != NULL)
			*pnDigestOut = nDigest;

		return std::chrono::duration<double, std::milli>(stop - start).count();
	}

	unsigned long long GetSampleTotalBytes(const std::vector<CSampledFile> &sample)
	{
		unsigned long long nTotalBytes = 0;
		for (const CSampledFile &file : sample)
			nTotalBytes += file.Length;
		return nTotalBytes;
	}

	/**
	 * @brief Names the active reader path in benchmark output.
	 */
	const wchar_t* GetReaderModeName(EReaderMode eMode)
	{
		switch (eMode) {
		case EReaderMode::Buffered:
			return L"buffered";
		case EReaderMode::Win32:
			return L"win32";
		case EReaderMode::Mapped:
			return L"mapped";
		default:
			return L"unknown";
		}
	}

	/**
	 * @brief Chooses the workspace-preferred reader so dev uses mapped I/O while oracle keeps the buffered reference path.
	 */
	EReaderMode GetWorkspacePreferredReaderMode()
	{
#if EMULE_TEST_HAVE_MAPPED_FILE_READER
		return EReaderMode::Mapped;
#else
		return EReaderMode::Win32;
#endif
	}

	struct CHashAlgorithmProvider
	{
		BCRYPT_ALG_HANDLE Handle = NULL;
		DWORD ObjectLength = 0;
		DWORD HashLength = 0;
	};

	/**
	 * @brief Opens a reusable CNG provider for the requested digest algorithm.
	 */
	const CHashAlgorithmProvider& GetHashAlgorithmProvider(LPCWSTR pszAlgorithmId)
	{
		static CHashAlgorithmProvider s_md4Provider = {};
		static CHashAlgorithmProvider s_sha1Provider = {};
		CHashAlgorithmProvider *pProvider = (wcscmp(pszAlgorithmId, BCRYPT_MD4_ALGORITHM) == 0) ? &s_md4Provider : &s_sha1Provider;
		if (pProvider->Handle != NULL)
			return *pProvider;

		ULONG nResultLength = 0;
		REQUIRE(::BCryptOpenAlgorithmProvider(&pProvider->Handle, pszAlgorithmId, NULL, 0) == 0);
		REQUIRE(::BCryptGetProperty(pProvider->Handle, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&pProvider->ObjectLength), sizeof pProvider->ObjectLength, &nResultLength, 0) == 0);
		REQUIRE(nResultLength == sizeof pProvider->ObjectLength);
		REQUIRE(::BCryptGetProperty(pProvider->Handle, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&pProvider->HashLength), sizeof pProvider->HashLength, &nResultLength, 0) == 0);
		REQUIRE(nResultLength == sizeof pProvider->HashLength);
		return *pProvider;
	}

	/**
	 * @brief Manages a single incremental CNG hash instance for MD4 and SHA1 parity checks.
	 */
	class CIncrementalHash
	{
	public:
		explicit CIncrementalHash(const CHashAlgorithmProvider &rProvider)
			: m_rProvider(rProvider)
			, m_HashObject(rProvider.ObjectLength)
		{
			REQUIRE(::BCryptCreateHash(m_rProvider.Handle, &m_hHash, m_HashObject.data(), static_cast<ULONG>(m_HashObject.size()), NULL, 0, 0) == 0);
		}

		~CIncrementalHash()
		{
			if (m_hHash != NULL)
				::BCryptDestroyHash(m_hHash);
		}

		void Add(const BYTE *pBytes, size_t nByteCount)
		{
			if (nByteCount == 0)
				return;
			REQUIRE(::BCryptHashData(m_hHash, const_cast<PUCHAR>(pBytes), static_cast<ULONG>(nByteCount), 0) == 0);
		}

		template <size_t N>
		std::array<BYTE, N> Finish()
		{
			REQUIRE(m_rProvider.HashLength == N);
			std::array<BYTE, N> digest = {};
			REQUIRE(::BCryptFinishHash(m_hHash, digest.data(), static_cast<ULONG>(digest.size()), 0) == 0);
			return digest;
		}

	private:
		const CHashAlgorithmProvider &m_rProvider;
		std::vector<UCHAR> m_HashObject;
		BCRYPT_HASH_HANDLE m_hHash = NULL;
	};

	/**
	 * @brief Visits a file range through the legacy CRT buffered path.
	 */
	template <typename TVisitor>
	void VisitFileRangeBuffered(const std::wstring &rstrPath, unsigned long long nOffset, unsigned long long nLength, TVisitor &&Visitor)
	{
		FILE *pFile = NULL;
		REQUIRE(_wfopen_s(&pFile, rstrPath.c_str(), L"rb") == 0);
		REQUIRE(pFile != NULL);
		REQUIRE(_fseeki64(pFile, static_cast<__int64>(nOffset), SEEK_SET) == 0);

		BYTE buffer[HASH_BUFFER_SIZE] = {};
		unsigned long long nRemaining = nLength;
		while (nRemaining != 0) {
			const size_t nRequested = static_cast<size_t>(std::min<unsigned long long>(nRemaining, sizeof buffer));
			const size_t nRead = fread(buffer, 1, nRequested, pFile);
			REQUIRE(nRead == nRequested);
			Visitor(buffer, nRead);
			nRemaining -= nRead;
		}

		fclose(pFile);
	}

	/**
	 * @brief Visits a file range through explicit Win32 reads.
	 */
	template <typename TVisitor>
	void VisitFileRangeWin32(const std::wstring &rstrPath, unsigned long long nOffset, unsigned long long nLength, TVisitor &&Visitor)
	{
		HANDLE hFile = ::CreateFileW(rstrPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
		REQUIRE(hFile != INVALID_HANDLE_VALUE);

		LARGE_INTEGER liOffset = {};
		liOffset.QuadPart = static_cast<LONGLONG>(nOffset);
		REQUIRE(::SetFilePointerEx(hFile, liOffset, NULL, FILE_BEGIN) != FALSE);

		BYTE buffer[HASH_BUFFER_SIZE] = {};
		unsigned long long nRemaining = nLength;
		while (nRemaining != 0) {
			DWORD dwRead = 0;
			const DWORD dwRequested = static_cast<DWORD>(std::min<unsigned long long>(nRemaining, sizeof buffer));
			REQUIRE(::ReadFile(hFile, buffer, dwRequested, &dwRead, NULL) != FALSE);
			REQUIRE(dwRead == dwRequested);
			Visitor(buffer, dwRead);
			nRemaining -= dwRead;
		}

		::CloseHandle(hFile);
	}

#if EMULE_TEST_HAVE_MAPPED_FILE_READER
	/**
	 * @brief Adapts a lambda visitor to the production mapped reader interface.
	 */
	template <typename TVisitor>
	class CMappedLambdaVisitor : public IMappedFileRangeVisitor
	{
	public:
		explicit CMappedLambdaVisitor(TVisitor &Visitor)
			: m_Visitor(Visitor)
		{
		}

		void OnMappedFileBytes(const BYTE *pBytes, size_t nByteCount) override
		{
			m_Visitor(pBytes, nByteCount);
		}

	private:
		TVisitor &m_Visitor;
	};
#endif

	/**
	 * @brief Visits a file range through the workspace mapped reader implementation.
	 */
	template <typename TVisitor>
	void VisitFileRangeMapped(const std::wstring &rstrPath, unsigned long long nOffset, unsigned long long nLength, TVisitor &&Visitor)
	{
#if EMULE_TEST_HAVE_MAPPED_FILE_READER
		HANDLE hFile = ::CreateFileW(rstrPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
		REQUIRE(hFile != INVALID_HANDLE_VALUE);

		DWORD dwError = ERROR_SUCCESS;
		CMappedLambdaVisitor<TVisitor> mappedVisitor(Visitor);
		REQUIRE(VisitMappedFileRange(hFile, nOffset, nLength, mappedVisitor, &dwError));
		REQUIRE(dwError == ERROR_SUCCESS);

		::CloseHandle(hFile);
#else
		(void)rstrPath;
		(void)nOffset;
		(void)nLength;
		(void)Visitor;
		CHECK_MESSAGE(false, "MappedFileReader unavailable in this workspace");
#endif
	}

	/**
	 * @brief Dispatches the byte-stream visitor through the requested file reader path.
	 */
	template <typename TVisitor>
	void VisitFileRange(const std::wstring &rstrPath, unsigned long long nOffset, unsigned long long nLength, EReaderMode eMode, TVisitor &&Visitor)
	{
		switch (eMode) {
		case EReaderMode::Buffered:
			VisitFileRangeBuffered(rstrPath, nOffset, nLength, std::forward<TVisitor>(Visitor));
			return;
		case EReaderMode::Win32:
			VisitFileRangeWin32(rstrPath, nOffset, nLength, std::forward<TVisitor>(Visitor));
			return;
		case EReaderMode::Mapped:
			VisitFileRangeMapped(rstrPath, nOffset, nLength, std::forward<TVisitor>(Visitor));
			return;
		default:
			CHECK_MESSAGE(false, "Unknown reader mode");
			return;
		}
	}

	/**
	 * @brief Hashes the buffered bytes into MD4 plus ordered SHA1 block hashes for one eMule part.
	 */
	struct CPartHashData
	{
		CMd4Digest Md4 = {};
		std::vector<CSha1Digest> AichBlockHashes;
	};

	/**
	 * @brief Streams one eMule part exactly once and records the raw MD4 plus AICH block hashes.
	 */
	CPartHashData ComputePartHashData(const std::wstring &rstrPath, unsigned long long nOffset, unsigned long long nLength, EReaderMode eMode)
	{
		CPartHashData result;
		CIncrementalHash md4Hasher(GetHashAlgorithmProvider(BCRYPT_MD4_ALGORITHM));
		std::unique_ptr<CIncrementalHash> pAichBlockHasher = std::make_unique<CIncrementalHash>(GetHashAlgorithmProvider(BCRYPT_SHA1_ALGORITHM));
		size_t nBytesInCurrentBlock = 0;

		auto FlushAichBlock = [&]() {
			result.AichBlockHashes.push_back(pAichBlockHasher->Finish<20>());
			pAichBlockHasher = std::make_unique<CIncrementalHash>(GetHashAlgorithmProvider(BCRYPT_SHA1_ALGORITHM));
			nBytesInCurrentBlock = 0;
		};

		VisitFileRange(rstrPath, nOffset, nLength, eMode, [&](const BYTE *pBytes, size_t nByteCount) {
			md4Hasher.Add(pBytes, nByteCount);
			while (nByteCount != 0) {
				const size_t nChunk = std::min(nByteCount, EMBLOCKSIZE_BYTES - nBytesInCurrentBlock);
				pAichBlockHasher->Add(pBytes, nChunk);
				nBytesInCurrentBlock += nChunk;
				pBytes += nChunk;
				nByteCount -= nChunk;

				if (nBytesInCurrentBlock == EMBLOCKSIZE_BYTES)
					FlushAichBlock();
			}
		});

		if (nLength != 0 && nBytesInCurrentBlock != 0)
			FlushAichBlock();

		result.Md4 = md4Hasher.Finish<16>();
		return result;
	}

	/**
	 * @brief Reduces ordered SHA1 leaves with the production left/right AICH balancing rules.
	 */
	CSha1Digest ReduceAichHashes(const std::vector<CSha1Digest> &rLeaves, size_t nStart, size_t nCount, bool bIsLeftBranch)
	{
		REQUIRE(nCount != 0);
		if (nCount == 1)
			return rLeaves[nStart];

		const size_t nLeftCount = (nCount + static_cast<size_t>(bIsLeftBranch)) / 2u;
		const size_t nRightCount = nCount - nLeftCount;
		REQUIRE(nRightCount != 0);

		const CSha1Digest leftHash = ReduceAichHashes(rLeaves, nStart, nLeftCount, true);
		const CSha1Digest rightHash = ReduceAichHashes(rLeaves, nStart + nLeftCount, nRightCount, false);

		CIncrementalHash parentHasher(GetHashAlgorithmProvider(BCRYPT_SHA1_ALGORITHM));
		parentHasher.Add(leftHash.data(), leftHash.size());
		parentHasher.Add(rightHash.data(), rightHash.size());
		return parentHasher.Finish<20>();
	}

	/**
	 * @brief Expands the part-root orientation sequence used by the production AICH tree.
	 */
	void CollectPartOrientations(size_t nPartCount, bool bIsLeftBranch, std::vector<bool> &rOrientations)
	{
		REQUIRE(nPartCount != 0);
		if (nPartCount == 1) {
			rOrientations.push_back(bIsLeftBranch);
			return;
		}

		const size_t nLeftCount = (nPartCount + static_cast<size_t>(bIsLeftBranch)) / 2u;
		CollectPartOrientations(nLeftCount, true, rOrientations);
		CollectPartOrientations(nPartCount - nLeftCount, false, rOrientations);
	}

	/**
	 * @brief Returns the MD4 of zero bytes for the special exact-partsize ed2k tail hash.
	 */
	const CMd4Digest& GetEmptyMd4Digest()
	{
		static const CMd4Digest s_EmptyMd4Digest = []() {
			CIncrementalHash hasher(GetHashAlgorithmProvider(BCRYPT_MD4_ALGORITHM));
			return hasher.Finish<16>();
		}();
		return s_EmptyMd4Digest;
	}

	/**
	 * @brief Reproduces the full eMule MD4 and AICH artifact pipeline for one file.
	 */
	CFileHashArtifacts ComputeFullHashArtifacts(const CSampledFile &rFile, EReaderMode eMode)
	{
		REQUIRE(rFile.Length != 0);

		CFileHashArtifacts artifacts = {};
		artifacts.Length = rFile.Length;
		const size_t nRealPartCount = static_cast<size_t>((rFile.Length + (PARTSIZE_BYTES - 1ull)) / PARTSIZE_BYTES);
		std::vector<bool> aichPartOrientations;
		if (rFile.Length > PARTSIZE_BYTES) {
			aichPartOrientations.reserve(nRealPartCount);
			CollectPartOrientations(nRealPartCount, true, aichPartOrientations);
			REQUIRE(aichPartOrientations.size() == nRealPartCount);
		}

		for (size_t nPart = 0; nPart < nRealPartCount; ++nPart) {
			const unsigned long long nOffset = static_cast<unsigned long long>(nPart) * PARTSIZE_BYTES;
			const unsigned long long nLength = std::min<unsigned long long>(PARTSIZE_BYTES, rFile.Length - nOffset);
			const CPartHashData partHashes = ComputePartHashData(rFile.Path, nOffset, nLength, eMode);

			if (rFile.Length < PARTSIZE_BYTES)
				artifacts.FileMd4 = partHashes.Md4;
			else
				artifacts.Md4PartHashes.push_back(partHashes.Md4);

			if (rFile.Length <= PARTSIZE_BYTES)
				artifacts.AichMaster = ReduceAichHashes(partHashes.AichBlockHashes, 0, partHashes.AichBlockHashes.size(), true);
			else {
				const CSha1Digest partHash = ReduceAichHashes(partHashes.AichBlockHashes, 0, partHashes.AichBlockHashes.size(), aichPartOrientations[nPart]);
				artifacts.AichPartHashes.push_back(partHash);
			}
		}

		if (rFile.Length >= PARTSIZE_BYTES) {
			if (rFile.Length % PARTSIZE_BYTES == 0ull)
				artifacts.Md4PartHashes.push_back(GetEmptyMd4Digest());

			CIncrementalHash fileMd4Hasher(GetHashAlgorithmProvider(BCRYPT_MD4_ALGORITHM));
			for (const CMd4Digest &partHash : artifacts.Md4PartHashes)
				fileMd4Hasher.Add(partHash.data(), partHash.size());
			artifacts.FileMd4 = fileMd4Hasher.Finish<16>();
		}

		if (rFile.Length > PARTSIZE_BYTES)
			artifacts.AichMaster = ReduceAichHashes(artifacts.AichPartHashes, 0, artifacts.AichPartHashes.size(), true);

		return artifacts;
	}

	/**
	 * @brief Feeds the full hashing artifacts into an aggregate digest for dev-vs-oracle comparisons.
	 */
	void AddArtifactsToDigest(CFnv1a64 &rDigest, const CFileHashArtifacts &rArtifacts)
	{
		rDigest.Add(reinterpret_cast<const BYTE*>(&rArtifacts.Length), sizeof rArtifacts.Length);
		rDigest.Add(rArtifacts.FileMd4.data(), rArtifacts.FileMd4.size());
		rDigest.Add(rArtifacts.AichMaster.data(), rArtifacts.AichMaster.size());

		const size_t nMd4Count = rArtifacts.Md4PartHashes.size();
		rDigest.Add(reinterpret_cast<const BYTE*>(&nMd4Count), sizeof nMd4Count);
		for (const CMd4Digest &hash : rArtifacts.Md4PartHashes)
			rDigest.Add(hash.data(), hash.size());

		const size_t nAichCount = rArtifacts.AichPartHashes.size();
		rDigest.Add(reinterpret_cast<const BYTE*>(&nAichCount), sizeof nAichCount);
		for (const CSha1Digest &hash : rArtifacts.AichPartHashes)
			rDigest.Add(hash.data(), hash.size());
	}

	/**
	 * @brief Measures the full-file eMule hashing pipeline over the sampled corpus.
	 */
	double MeasureFullPipelineMs(const std::vector<CSampledFile> &sample, EReaderMode eMode, unsigned long long *pnAggregateDigestOut)
	{
		const auto start = std::chrono::steady_clock::now();
		CFnv1a64 aggregateDigest;
		for (const CSampledFile &file : sample)
			AddArtifactsToDigest(aggregateDigest, ComputeFullHashArtifacts(file, eMode));
		const auto stop = std::chrono::steady_clock::now();

		if (pnAggregateDigestOut != NULL)
			*pnAggregateDigestOut = aggregateDigest.GetValue();

		return std::chrono::duration<double, std::milli>(stop - start).count();
	}
}

TEST_SUITE_BEGIN("parity");

TEST_CASE("Actual temp-file buffered and Win32 digests match on sampled files")
{
	const std::vector<CSampledFile> sample = SelectRuntimeSampleFiles();
	REQUIRE_FALSE(sample.empty());

	for (const CSampledFile &file : sample)
		CHECK_EQ(ComputeBufferedDigest(file.Path), ComputeWin32Digest(file.Path));
}

TEST_SUITE_END;

TEST_SUITE_BEGIN("divergence");

TEST_CASE("Mapped file reader returns the exact requested slice across allocation boundaries")
{
#if EMULE_TEST_HAVE_MAPPED_FILE_READER
	const std::vector<BYTE> fixture = CreateMappedReaderFixture(1024 * 1024 + 257);
	const std::wstring tempPath = CreateMappedReaderTempPath();
	WriteMappedReaderFixture(tempPath, fixture);

	HANDLE hFile = ::CreateFileW(tempPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	REQUIRE(hFile != INVALID_HANDLE_VALUE);

	CFnvMappedVisitor visitor;
	DWORD dwError = ERROR_SUCCESS;
	const size_t nOffset = 65530;
	const size_t nLength = 400000;
	CHECK(VisitMappedFileRange(hFile, nOffset, nLength, visitor, &dwError));
	CHECK_EQ(dwError, static_cast<DWORD>(ERROR_SUCCESS));

	CFnv1a64 referenceHash;
	const std::vector<BYTE> reference = SliceMappedReaderFixture(fixture, nOffset, nLength);
	referenceHash.Add(reference.data(), reference.size());
	CHECK_EQ(visitor.GetDigest(), referenceHash.GetValue());

	::CloseHandle(hFile);
	::DeleteFileW(tempPath.c_str());
#else
	CHECK_MESSAGE(false, "MappedFileReader unavailable in this workspace");
#endif
}

TEST_CASE("Mapped file reader spans multiple mapping windows without dropping bytes")
{
#if EMULE_TEST_HAVE_MAPPED_FILE_READER
	const std::vector<BYTE> fixture = CreateMappedReaderFixture(9 * 1024 * 1024 + 123);
	const std::wstring tempPath = CreateMappedReaderTempPath();
	WriteMappedReaderFixture(tempPath, fixture);

	HANDLE hFile = ::CreateFileW(tempPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	REQUIRE(hFile != INVALID_HANDLE_VALUE);

	CFnvMappedVisitor visitor;
	DWORD dwError = ERROR_SUCCESS;
	const size_t nOffset = 123;
	const size_t nLength = fixture.size() - 246;
	CHECK(VisitMappedFileRange(hFile, nOffset, nLength, visitor, &dwError));
	CHECK_EQ(dwError, static_cast<DWORD>(ERROR_SUCCESS));

	CFnv1a64 referenceHash;
	const std::vector<BYTE> reference = SliceMappedReaderFixture(fixture, nOffset, nLength);
	referenceHash.Add(reference.data(), reference.size());
	CHECK_EQ(visitor.GetDigest(), referenceHash.GetValue());

	::CloseHandle(hFile);
	::DeleteFileW(tempPath.c_str());
#else
	CHECK_MESSAGE(false, "MappedFileReader unavailable in this workspace");
#endif
}

TEST_CASE("Mapped file reader accepts zero-length ranges without touching the visitor")
{
#if EMULE_TEST_HAVE_MAPPED_FILE_READER
	const std::vector<BYTE> fixture = CreateMappedReaderFixture(128);
	const std::wstring tempPath = CreateMappedReaderTempPath();
	WriteMappedReaderFixture(tempPath, fixture);

	HANDLE hFile = ::CreateFileW(tempPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	REQUIRE(hFile != INVALID_HANDLE_VALUE);

	CFnvMappedVisitor visitor;
	DWORD dwError = ERROR_SUCCESS;
	CHECK(VisitMappedFileRange(hFile, 0, 0, visitor, &dwError));
	CHECK_EQ(dwError, static_cast<DWORD>(ERROR_SUCCESS));
	CHECK_EQ(visitor.GetDigest(), static_cast<unsigned long long>(1469598103934665603ull));

	::CloseHandle(hFile);
	::DeleteFileW(tempPath.c_str());
#else
	CHECK_MESSAGE(false, "MappedFileReader unavailable in this workspace");
#endif
}

TEST_CASE("Mapped file reader reports invalid handles")
{
#if EMULE_TEST_HAVE_MAPPED_FILE_READER
	class CNoopVisitor : public IMappedFileRangeVisitor
	{
	public:
		void OnMappedFileBytes(const BYTE *, size_t) override
		{
		}
	} visitor;

	DWORD dwError = ERROR_SUCCESS;
	CHECK_FALSE(VisitMappedFileRange(INVALID_HANDLE_VALUE, 0, 1, visitor, &dwError));
	CHECK_EQ(dwError, static_cast<DWORD>(ERROR_INVALID_HANDLE));
#else
	CHECK_MESSAGE(false, "MappedFileReader unavailable in this workspace");
#endif
}

TEST_CASE("Mapped file reader matches buffered digest on sampled actual temp files")
{
#if EMULE_TEST_HAVE_MAPPED_FILE_READER
	const std::vector<CSampledFile> sample = SelectRuntimeSampleFiles();
	REQUIRE_FALSE(sample.empty());

	for (const CSampledFile &file : sample)
		CHECK_EQ(ComputeMappedDigest(file.Path), ComputeBufferedDigest(file.Path));
#else
	CHECK_MESSAGE(false, "MappedFileReader unavailable in this workspace");
#endif
}

TEST_SUITE_END;

TEST_SUITE_BEGIN("benchmark");

TEST_CASE("File reader benchmark reports sampled temp-file throughput")
{
	const std::vector<CSampledFile> sample = SelectRuntimeSampleFiles();
	REQUIRE_FALSE(sample.empty());

	const unsigned long long nSeed = GetRuntimeSampleSeed();
	const unsigned long long nTotalBytes = GetSampleTotalBytes(sample);
	unsigned long long nBufferedDigest = 0;
	unsigned long long nWin32Digest = 0;
	const double dBufferedMs = MeasureDigestPassMs(sample, ComputeBufferedDigest, &nBufferedDigest);
	const double dWin32Ms = MeasureDigestPassMs(sample, ComputeWin32Digest, &nWin32Digest);
	CHECK_EQ(nBufferedDigest, nWin32Digest);

	std::printf("BENCHMARK sample_root=%ls seed=%llu file_count=%zu total_bytes=%llu buffered_ms=%.3f win32_ms=%.3f buffered_mib_s=%.3f win32_mib_s=%.3f"
		, GetRuntimeSampleRoot().c_str()
		, nSeed
		, sample.size()
		, nTotalBytes
		, dBufferedMs
		, dWin32Ms
		, dBufferedMs > 0.0 ? (static_cast<double>(nTotalBytes) / (1024.0 * 1024.0)) / (dBufferedMs / 1000.0) : 0.0
		, dWin32Ms > 0.0 ? (static_cast<double>(nTotalBytes) / (1024.0 * 1024.0)) / (dWin32Ms / 1000.0) : 0.0);

#if EMULE_TEST_HAVE_MAPPED_FILE_READER
	unsigned long long nMappedDigest = 0;
	const double dMappedMs = MeasureDigestPassMs(sample, ComputeMappedDigest, &nMappedDigest);
	CHECK_EQ(nBufferedDigest, nMappedDigest);
	std::printf(" mapped_ms=%.3f mapped_mib_s=%.3f mapped_vs_buffered=%.3fx"
		, dMappedMs
		, dMappedMs > 0.0 ? (static_cast<double>(nTotalBytes) / (1024.0 * 1024.0)) / (dMappedMs / 1000.0) : 0.0
		, dMappedMs > 0.0 ? dBufferedMs / dMappedMs : 0.0);
#endif

	std::printf("\n");
}

TEST_SUITE_END;

TEST_SUITE_BEGIN("pipeline");

TEST_CASE("Actual temp-file full hashing artifacts match between buffered and preferred readers")
{
	const std::vector<CSampledFile> sample = SelectRuntimeSampleFiles();
	REQUIRE_FALSE(sample.empty());

	const EReaderMode ePreferredMode = GetWorkspacePreferredReaderMode();
	for (const CSampledFile &file : sample)
		CHECK(ComputeFullHashArtifacts(file, EReaderMode::Buffered) == ComputeFullHashArtifacts(file, ePreferredMode));
}

TEST_SUITE_END;

TEST_SUITE_BEGIN("pipeline-benchmark");

TEST_CASE("Full hashing pipeline benchmark reports sampled temp-file parity and throughput")
{
	const std::vector<CSampledFile> sample = SelectRuntimeSampleFiles();
	REQUIRE_FALSE(sample.empty());

	const unsigned long long nSeed = GetRuntimeSampleSeed();
	const unsigned long long nTotalBytes = GetSampleTotalBytes(sample);
	const EReaderMode ePreferredMode = GetWorkspacePreferredReaderMode();
	unsigned long long nBufferedDigest = 0;
	unsigned long long nPreferredDigest = 0;
	const double dBufferedMs = MeasureFullPipelineMs(sample, EReaderMode::Buffered, &nBufferedDigest);
	const double dPreferredMs = MeasureFullPipelineMs(sample, ePreferredMode, &nPreferredDigest);
	CHECK_EQ(nBufferedDigest, nPreferredDigest);

	std::printf("PIPELINE_BENCHMARK sample_root=%ls seed=%llu file_count=%zu total_bytes=%llu buffered_ms=%.3f preferred_ms=%.3f preferred_kind=%ls buffered_mib_s=%.3f preferred_mib_s=%.3f preferred_vs_buffered=%.3fx aggregate_digest=%llu\n"
		, GetRuntimeSampleRoot().c_str()
		, nSeed
		, sample.size()
		, nTotalBytes
		, dBufferedMs
		, dPreferredMs
		, GetReaderModeName(ePreferredMode)
		, dBufferedMs > 0.0 ? (static_cast<double>(nTotalBytes) / (1024.0 * 1024.0)) / (dBufferedMs / 1000.0) : 0.0
		, dPreferredMs > 0.0 ? (static_cast<double>(nTotalBytes) / (1024.0 * 1024.0)) / (dPreferredMs / 1000.0) : 0.0
		, dPreferredMs > 0.0 ? dBufferedMs / dPreferredMs : 0.0
		, nPreferredDigest);
}

TEST_SUITE_END;
