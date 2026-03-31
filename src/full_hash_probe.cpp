#include "../include/FullHashProbe.h"

#include <Windows.h>
#include <bcrypt.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cwchar>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "MappedFileReader.h"

namespace
{
	constexpr std::uint64_t PARTSIZE_BYTES = 9728000ull;
	constexpr size_t EMBLOCKSIZE_BYTES = 184320u;
	constexpr std::uint64_t DEFAULT_PROGRESS_INTERVAL_BYTES = 256ull * 1024ull * 1024ull;
	constexpr std::uint64_t BUFFER_BYTES = 8ull * 1024ull * 1024ull;

	using CMd4Digest = std::array<BYTE, 16>;
	using CSha1Digest = std::array<BYTE, 20>;

	enum class EProbeReader
	{
		Buffered,
		Mapped
	};

	struct CProbeOptions
	{
		std::wstring FilePath;
		bool bRunBuffered = true;
		bool bRunMapped = true;
		std::uint64_t nProgressIntervalBytes = DEFAULT_PROGRESS_INTERVAL_BYTES;
	};

	struct CProbeResult
	{
		const char *pszReaderName = "";
		bool bSucceeded = false;
		std::uint64_t nBytesProcessed = 0;
		std::uint64_t nFileSize = 0;
		std::uint64_t nDigest = 0;
		std::uint64_t nElapsedMs = 0;
		std::uint64_t nPartsProcessed = 0;
		DWORD dwError = ERROR_SUCCESS;
	};

	struct CHashAlgorithmProvider
	{
		BCRYPT_ALG_HANDLE Handle = NULL;
		DWORD ObjectLength = 0;
		DWORD HashLength = 0;
	};

	struct CPartHashData
	{
		CMd4Digest Md4 = {};
		std::vector<CSha1Digest> AichBlockHashes;
	};

	struct CFileHashArtifacts
	{
		std::uint64_t Length = 0;
		CMd4Digest FileMd4 = {};
		CSha1Digest AichMaster = {};
		std::vector<CMd4Digest> Md4PartHashes;
		std::vector<CSha1Digest> AichPartHashes;
	};

	class CFnv1a64
	{
	public:
		void Add(const BYTE *pBytes, size_t nByteCount)
		{
			for (size_t i = 0; i < nByteCount; ++i) {
				m_nValue ^= static_cast<std::uint64_t>(pBytes[i]);
				m_nValue *= 1099511628211ull;
			}
		}

		std::uint64_t GetValue() const
		{
			return m_nValue;
		}

	private:
		std::uint64_t m_nValue = 1469598103934665603ull;
	};

	class CIncrementalHash
	{
	public:
		explicit CIncrementalHash(const CHashAlgorithmProvider &rProvider)
			: m_rProvider(rProvider)
			, m_HashObject(rProvider.ObjectLength)
		{
			if (::BCryptCreateHash(m_rProvider.Handle, &m_hHash, m_HashObject.data(), static_cast<ULONG>(m_HashObject.size()), NULL, 0, 0) != 0)
				throw std::runtime_error("BCryptCreateHash failed");
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
			if (::BCryptHashData(m_hHash, const_cast<PUCHAR>(pBytes), static_cast<ULONG>(nByteCount), 0) != 0)
				throw std::runtime_error("BCryptHashData failed");
		}

		template <size_t N>
		std::array<BYTE, N> Finish()
		{
			if (m_rProvider.HashLength != N)
				throw std::runtime_error("Unexpected hash length");

			std::array<BYTE, N> digest = {};
			if (::BCryptFinishHash(m_hHash, digest.data(), static_cast<ULONG>(digest.size()), 0) != 0)
				throw std::runtime_error("BCryptFinishHash failed");
			return digest;
		}

	private:
		const CHashAlgorithmProvider &m_rProvider;
		std::vector<UCHAR> m_HashObject;
		BCRYPT_HASH_HANDLE m_hHash = NULL;
	};

	template <typename TVisitor>
	class CMappedLambdaVisitor : public IMappedFileRangeVisitor
	{
	public:
		explicit CMappedLambdaVisitor(TVisitor &rVisitor)
			: m_rVisitor(rVisitor)
		{
		}

		void OnMappedFileBytes(const BYTE *pBytes, size_t nByteCount) override
		{
			m_rVisitor(pBytes, nByteCount);
		}

	private:
		TVisitor &m_rVisitor;
	};

	/**
	 * @brief Escapes non-ASCII path characters so redirected probe logs stay stable.
	 */
	std::string EscapeForProbeOutput(const std::wstring &text)
	{
		std::ostringstream out;
		out << std::hex << std::uppercase;
		for (wchar_t ch : text) {
			const unsigned int nCodeUnit = static_cast<unsigned int>(ch);
			if (nCodeUnit >= 0x20u && nCodeUnit <= 0x7Eu)
				out << static_cast<char>(ch);
			else
				out << "\\u" << std::setw(4) << std::setfill('0') << nCodeUnit << std::setfill(' ');
		}
		return out.str();
	}

	/**
	 * @brief Applies the Win32 long-path prefix when the incoming path needs it.
	 */
	std::wstring PreparePathForLongPath(const std::wstring &path)
	{
		if (path.empty() || path.size() < MAX_PATH)
			return path;
		if (path.rfind(L"\\\\?\\", 0) == 0)
			return path;
		if (path.rfind(L"\\\\", 0) == 0)
			return std::wstring(L"\\\\?\\UNC\\") + path.substr(2);
		return std::wstring(L"\\\\?\\") + path;
	}

	const CHashAlgorithmProvider& GetHashAlgorithmProvider(LPCWSTR pszAlgorithmId)
	{
		static CHashAlgorithmProvider s_md4Provider = {};
		static CHashAlgorithmProvider s_sha1Provider = {};
		CHashAlgorithmProvider *pProvider = (wcscmp(pszAlgorithmId, BCRYPT_MD4_ALGORITHM) == 0) ? &s_md4Provider : &s_sha1Provider;
		if (pProvider->Handle != NULL)
			return *pProvider;

		ULONG nResultLength = 0;
		if (::BCryptOpenAlgorithmProvider(&pProvider->Handle, pszAlgorithmId, NULL, 0) != 0)
			throw std::runtime_error("BCryptOpenAlgorithmProvider failed");
		if (::BCryptGetProperty(pProvider->Handle, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&pProvider->ObjectLength), sizeof pProvider->ObjectLength, &nResultLength, 0) != 0)
			throw std::runtime_error("BCryptGetProperty(object length) failed");
		if (::BCryptGetProperty(pProvider->Handle, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&pProvider->HashLength), sizeof pProvider->HashLength, &nResultLength, 0) != 0)
			throw std::runtime_error("BCryptGetProperty(hash length) failed");
		return *pProvider;
	}

	std::vector<std::wstring> GetWideArguments()
	{
		int nArgCount = 0;
		LPWSTR *argv = ::CommandLineToArgvW(::GetCommandLineW(), &nArgCount);
		if (argv == NULL)
			return {};

		std::vector<std::wstring> args;
		args.reserve(static_cast<size_t>(nArgCount));
		for (int i = 0; i < nArgCount; ++i)
			args.push_back(argv[i]);
		::LocalFree(argv);
		return args;
	}

	bool TryParseUint64(const std::wstring &text, std::uint64_t &nValue)
	{
		if (text.empty())
			return false;
		wchar_t *pEnd = NULL;
		errno = 0;
		const unsigned long long nParsed = _wcstoui64(text.c_str(), &pEnd, 10);
		if (errno != 0 || pEnd == text.c_str() || *pEnd != L'\0')
			return false;
		nValue = static_cast<std::uint64_t>(nParsed);
		return true;
	}

	bool TryGetFileSize(HANDLE hFile, std::uint64_t &nFileSize, DWORD &dwError)
	{
		LARGE_INTEGER fileSize = {};
		if (!::GetFileSizeEx(hFile, &fileSize)) {
			dwError = ::GetLastError();
			return false;
		}

		nFileSize = static_cast<std::uint64_t>(fileSize.QuadPart);
		dwError = ERROR_SUCCESS;
		return true;
	}

	template <typename TVisitor>
	void VisitFileRange(HANDLE hFile, std::uint64_t nOffset, std::uint64_t nLength, EProbeReader eReader, TVisitor &&visitor)
	{
		if (eReader == EProbeReader::Buffered) {
			LARGE_INTEGER liOffset = {};
			liOffset.QuadPart = static_cast<LONGLONG>(nOffset);
			if (::SetFilePointerEx(hFile, liOffset, NULL, FILE_BEGIN) == FALSE)
				throw std::runtime_error("SetFilePointerEx failed");

			std::vector<BYTE> buffer(static_cast<size_t>(BUFFER_BYTES));
			std::uint64_t nRemaining = nLength;
			while (nRemaining != 0) {
				DWORD dwRead = 0;
				const DWORD dwRequested = static_cast<DWORD>(std::min<std::uint64_t>(nRemaining, buffer.size()));
				if (::ReadFile(hFile, &buffer[0], dwRequested, &dwRead, NULL) == FALSE || dwRead != dwRequested)
					throw std::runtime_error("ReadFile failed");
				visitor(&buffer[0], dwRead);
				nRemaining -= dwRead;
			}
			return;
		}

		DWORD dwError = ERROR_SUCCESS;
		CMappedLambdaVisitor<TVisitor> mappedVisitor(visitor);
		if (!VisitMappedFileRange(hFile, nOffset, nLength, mappedVisitor, &dwError) || dwError != ERROR_SUCCESS)
			throw std::runtime_error("VisitMappedFileRange failed");
	}

	CPartHashData ComputePartHashData(HANDLE hFile, std::uint64_t nOffset, std::uint64_t nLength, EProbeReader eReader)
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

		VisitFileRange(hFile, nOffset, nLength, eReader, [&](const BYTE *pBytes, size_t nByteCount) {
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

	CSha1Digest ReduceAichHashes(const std::vector<CSha1Digest> &rLeaves, size_t nStart, size_t nCount, bool bIsLeftBranch)
	{
		if (nCount == 1)
			return rLeaves[nStart];

		const size_t nLeftCount = (nCount + static_cast<size_t>(bIsLeftBranch)) / 2u;
		const size_t nRightCount = nCount - nLeftCount;
		const CSha1Digest leftHash = ReduceAichHashes(rLeaves, nStart, nLeftCount, true);
		const CSha1Digest rightHash = ReduceAichHashes(rLeaves, nStart + nLeftCount, nRightCount, false);

		CIncrementalHash parentHasher(GetHashAlgorithmProvider(BCRYPT_SHA1_ALGORITHM));
		parentHasher.Add(leftHash.data(), leftHash.size());
		parentHasher.Add(rightHash.data(), rightHash.size());
		return parentHasher.Finish<20>();
	}

	void CollectPartOrientations(size_t nPartCount, bool bIsLeftBranch, std::vector<bool> &rOrientations)
	{
		if (nPartCount == 1) {
			rOrientations.push_back(bIsLeftBranch);
			return;
		}

		const size_t nLeftCount = (nPartCount + static_cast<size_t>(bIsLeftBranch)) / 2u;
		CollectPartOrientations(nLeftCount, true, rOrientations);
		CollectPartOrientations(nPartCount - nLeftCount, false, rOrientations);
	}

	const CMd4Digest& GetEmptyMd4Digest()
	{
		static const CMd4Digest s_EmptyMd4Digest = []() {
			CIncrementalHash hasher(GetHashAlgorithmProvider(BCRYPT_MD4_ALGORITHM));
			return hasher.Finish<16>();
		}();
		return s_EmptyMd4Digest;
	}

	void AddArtifactsToDigest(CFnv1a64 &rDigest, const CFileHashArtifacts &rArtifacts)
	{
		rDigest.Add(reinterpret_cast<const BYTE*>(&rArtifacts.Length), sizeof rArtifacts.Length);
		rDigest.Add(rArtifacts.FileMd4.data(), rArtifacts.FileMd4.size());
		rDigest.Add(rArtifacts.AichMaster.data(), rArtifacts.AichMaster.size());
		for (const CMd4Digest &hash : rArtifacts.Md4PartHashes)
			rDigest.Add(hash.data(), hash.size());
		for (const CSha1Digest &hash : rArtifacts.AichPartHashes)
			rDigest.Add(hash.data(), hash.size());
	}

	void PrintProgress(const char *pszReaderName, std::uint64_t nBytesProcessed, std::uint64_t nTotalBytes, const std::chrono::steady_clock::time_point &start)
	{
		const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
		const double dPercent = (nTotalBytes != 0) ? (100.0 * static_cast<double>(nBytesProcessed) / static_cast<double>(nTotalBytes)) : 100.0;
		std::cout << "[" << pszReaderName << "] progress bytes=" << nBytesProcessed << " percent=" << std::fixed << std::setprecision(2) << dPercent << " elapsedMs=" << elapsedMs << std::endl;
	}

	/**
	 * @brief Replays the non-UI MD4 plus AICH hashing pipeline for one file and one reader strategy.
	 */
	CProbeResult RunFullHashProbe(const std::wstring &preparedPath, EProbeReader eReader, std::uint64_t nProgressIntervalBytes)
	{
		CProbeResult result;
		result.pszReaderName = (eReader == EProbeReader::Buffered) ? "buffered-full" : "mapped-full";
		const auto start = std::chrono::steady_clock::now();

		try {
			HANDLE hFile = ::CreateFileW(preparedPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
			if (hFile == INVALID_HANDLE_VALUE) {
				result.dwError = ::GetLastError();
				return result;
			}

			std::unique_ptr<void, decltype(&::CloseHandle)> fileCloser(hFile, ::CloseHandle);
			if (!TryGetFileSize(hFile, result.nFileSize, result.dwError))
				return result;

			CFileHashArtifacts artifacts = {};
			artifacts.Length = result.nFileSize;
			const size_t nRealPartCount = static_cast<size_t>((result.nFileSize + (PARTSIZE_BYTES - 1ull)) / PARTSIZE_BYTES);
			std::vector<bool> aichPartOrientations;
			if (result.nFileSize > PARTSIZE_BYTES)
				CollectPartOrientations(nRealPartCount, true, aichPartOrientations);

			std::uint64_t nNextProgressBytes = (nProgressIntervalBytes != 0) ? nProgressIntervalBytes : DEFAULT_PROGRESS_INTERVAL_BYTES;
			for (size_t nPart = 0; nPart < nRealPartCount; ++nPart) {
				const std::uint64_t nOffset = static_cast<std::uint64_t>(nPart) * PARTSIZE_BYTES;
				const std::uint64_t nLength = std::min<std::uint64_t>(PARTSIZE_BYTES, result.nFileSize - nOffset);
				const CPartHashData partHashes = ComputePartHashData(hFile, nOffset, nLength, eReader);

				if (result.nFileSize < PARTSIZE_BYTES)
					artifacts.FileMd4 = partHashes.Md4;
				else
					artifacts.Md4PartHashes.push_back(partHashes.Md4);

				if (result.nFileSize <= PARTSIZE_BYTES)
					artifacts.AichMaster = ReduceAichHashes(partHashes.AichBlockHashes, 0, partHashes.AichBlockHashes.size(), true);
				else
					artifacts.AichPartHashes.push_back(ReduceAichHashes(partHashes.AichBlockHashes, 0, partHashes.AichBlockHashes.size(), aichPartOrientations[nPart]));

				result.nBytesProcessed = nOffset + nLength;
				result.nPartsProcessed = static_cast<std::uint64_t>(nPart + 1);
				while (nNextProgressBytes != 0 && result.nBytesProcessed >= nNextProgressBytes) {
					PrintProgress(result.pszReaderName, result.nBytesProcessed, result.nFileSize, start);
					if (nNextProgressBytes > _UI64_MAX - nProgressIntervalBytes) {
						nNextProgressBytes = 0;
						break;
					}
					nNextProgressBytes += nProgressIntervalBytes;
				}
			}

			if (result.nFileSize >= PARTSIZE_BYTES) {
				if (result.nFileSize % PARTSIZE_BYTES == 0ull)
					artifacts.Md4PartHashes.push_back(GetEmptyMd4Digest());

				CIncrementalHash fileMd4Hasher(GetHashAlgorithmProvider(BCRYPT_MD4_ALGORITHM));
				for (const CMd4Digest &partHash : artifacts.Md4PartHashes)
					fileMd4Hasher.Add(partHash.data(), partHash.size());
				artifacts.FileMd4 = fileMd4Hasher.Finish<16>();
			}

			if (result.nFileSize > PARTSIZE_BYTES)
				artifacts.AichMaster = ReduceAichHashes(artifacts.AichPartHashes, 0, artifacts.AichPartHashes.size(), true);

			PrintProgress(result.pszReaderName, result.nBytesProcessed, result.nFileSize, start);
			CFnv1a64 digest;
			AddArtifactsToDigest(digest, artifacts);
			result.nDigest = digest.GetValue();
			result.dwError = ERROR_SUCCESS;
			result.bSucceeded = (result.nBytesProcessed == result.nFileSize);
		} catch (...) {
			result.dwError = ERROR_GEN_FAILURE;
		}

		result.nElapsedMs = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
		return result;
	}
}

int RunFullHashProbeIfRequested(int, char **)
{
	const std::vector<std::wstring> args = GetWideArguments();
	CProbeOptions options;
	bool bProbeRequested = false;

	for (size_t i = 1; i < args.size(); ++i) {
		const std::wstring &arg = args[i];
		if (arg == L"--full-hash-probe") {
			bProbeRequested = true;
			if (++i >= args.size()) {
				std::cerr << "Missing file path after --full-hash-probe" << std::endl;
				return 1;
			}
			options.FilePath = args[i];
			continue;
		}

		if (!bProbeRequested)
			continue;

		if (arg == L"--reader") {
			if (++i >= args.size()) {
				std::cerr << "Missing value after --reader" << std::endl;
				return 1;
			}
			options.bRunBuffered = options.bRunMapped = false;
			if (args[i] == L"buffered")
				options.bRunBuffered = true;
			else if (args[i] == L"mapped")
				options.bRunMapped = true;
			else if (args[i] == L"both")
				options.bRunBuffered = options.bRunMapped = true;
			else {
				std::cerr << "Unsupported --reader value: " << EscapeForProbeOutput(args[i]) << std::endl;
				return 1;
			}
			continue;
		}

		if (arg == L"--progress-mib") {
			std::uint64_t nMiB = 0;
			if (++i >= args.size() || !TryParseUint64(args[i], nMiB)) {
				std::cerr << "Invalid value after --progress-mib" << std::endl;
				return 1;
			}
			options.nProgressIntervalBytes = nMiB * 1024ull * 1024ull;
			continue;
		}

		std::cerr << "Unknown full-hash-probe option: " << EscapeForProbeOutput(arg) << std::endl;
		return 1;
	}

	if (!bProbeRequested)
		return -1;
	if (options.FilePath.empty()) {
		std::cerr << "The --full-hash-probe mode requires a file path." << std::endl;
		return 1;
	}

	const std::wstring preparedPath = PreparePathForLongPath(options.FilePath);
	std::cout
		<< "full-hash-probe path=" << EscapeForProbeOutput(options.FilePath)
		<< "\nprepared-path=" << EscapeForProbeOutput(preparedPath)
		<< "\npath-length=" << options.FilePath.size()
		<< "\nprepared-path-length=" << preparedPath.size()
		<< std::endl;

	std::vector<CProbeResult> results;
	if (options.bRunBuffered)
		results.push_back(RunFullHashProbe(preparedPath, EProbeReader::Buffered, options.nProgressIntervalBytes));
	if (options.bRunMapped)
		results.push_back(RunFullHashProbe(preparedPath, EProbeReader::Mapped, options.nProgressIntervalBytes));

	bool bAllSucceeded = true;
	for (const CProbeResult &result : results) {
		std::cout
			<< "[" << result.pszReaderName << "] success=" << (result.bSucceeded ? "true" : "false")
			<< " bytes=" << result.nBytesProcessed
			<< " fileSize=" << result.nFileSize
			<< " digest=0x" << std::hex << std::uppercase << result.nDigest << std::dec
			<< " elapsedMs=" << result.nElapsedMs
			<< " parts=" << result.nPartsProcessed
			<< " error=" << result.dwError
			<< std::endl;
		bAllSucceeded = bAllSucceeded && result.bSucceeded;
	}

	if (results.size() == 2 && results[0].bSucceeded && results[1].bSucceeded) {
		const bool bDigestsMatch = (results[0].nDigest == results[1].nDigest) && (results[0].nBytesProcessed == results[1].nBytesProcessed);
		std::cout << "digest-match=" << (bDigestsMatch ? "true" : "false") << std::endl;
		if (!bDigestsMatch)
			return 3;
	}

	return bAllSucceeded ? 0 : 2;
}
