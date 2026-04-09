#include "../include/HashProbe.h"
#include "../include/LongPathTestSupport.h"

#include <Windows.h>
#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cwchar>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "MappedFileReader.h"

namespace
{
	constexpr std::uint64_t DEFAULT_PROGRESS_INTERVAL_BYTES = 256ull * 1024ull * 1024ull;
	constexpr std::uint64_t DEFAULT_BUFFER_BYTES = 8ull * 1024ull * 1024ull;

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
		std::uint64_t nByteLimit = 0;
		std::uint64_t nProgressIntervalBytes = DEFAULT_PROGRESS_INTERVAL_BYTES;
	};

	struct CProbeResult
	{
		std::wstring ReaderName;
		bool bSucceeded = false;
		std::uint64_t nBytesProcessed = 0;
		std::uint64_t nFileSize = 0;
		std::uint64_t nDigest = 0;
		std::uint64_t nElapsedMs = 0;
		DWORD dwError = ERROR_SUCCESS;
	};

	/**
	 * @brief Converts wide Win32 paths and labels to UTF-8 so redirected probe output stays readable.
	 */
	std::string ToUtf8(const std::wstring &text)
	{
		if (text.empty())
			return {};

		const int nByteCount = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), NULL, 0, NULL, NULL);
		if (nByteCount <= 0)
			return {};

		std::string utf8(static_cast<size_t>(nByteCount), '\0');
		if (::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &utf8[0], nByteCount, NULL, NULL) != nByteCount)
			return {};
		return utf8;
	}

	/**
	 * @brief Escapes non-ASCII path characters so probe logs remain readable across native stdout capture layers.
	 */
	std::string EscapeForProbeOutput(const std::wstring &text)
	{
		std::ostringstream out;
		out << std::hex << std::uppercase;
		for (wchar_t ch : text) {
			const unsigned int nCodeUnit = static_cast<unsigned int>(ch);
			if (nCodeUnit >= 0x20u && nCodeUnit <= 0x7Eu) {
				out << static_cast<char>(ch);
				continue;
			}

			if (nCodeUnit <= 0xFFFFu)
				out << "\\u" << std::setw(4) << std::setfill('0') << nCodeUnit;
			else
				out << "\\U" << std::setw(8) << std::setfill('0') << nCodeUnit;
			out << std::setfill(' ');
		}
		return out.str();
	}

	/**
	 * @brief Maintains a simple digest while the probe compares reader progress and completion.
	 */
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

	/**
	 * @brief Reports long-running scan progress at deterministic byte intervals.
	 */
	class CProbeProgressReporter
	{
	public:
		CProbeProgressReporter(const std::wstring &readerName, std::uint64_t nTotalBytes, std::uint64_t nIntervalBytes)
			: m_strReaderName(readerName)
			, m_nTotalBytes(nTotalBytes)
			, m_nIntervalBytes((nIntervalBytes != 0) ? nIntervalBytes : DEFAULT_PROGRESS_INTERVAL_BYTES)
			, m_start(std::chrono::steady_clock::now())
		{
			if (m_nIntervalBytes > 0)
				m_nNextReport = m_nIntervalBytes;
		}

		void Report(std::uint64_t nBytesProcessed)
		{
			while (m_nIntervalBytes != 0 && nBytesProcessed >= m_nNextReport) {
				PrintProgress(nBytesProcessed);
				if (std::numeric_limits<std::uint64_t>::max() - m_nNextReport < m_nIntervalBytes)
					break;
				m_nNextReport += m_nIntervalBytes;
			}
		}

		void Flush(std::uint64_t nBytesProcessed) const
		{
			PrintProgress(nBytesProcessed);
		}

	private:
		void PrintProgress(std::uint64_t nBytesProcessed) const
		{
			const auto now = std::chrono::steady_clock::now();
			const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_start).count();
			const double dPercent = (m_nTotalBytes != 0)
				? (100.0 * static_cast<double>(nBytesProcessed) / static_cast<double>(m_nTotalBytes))
				: 100.0;
			std::ostringstream out;
			out
				<< "[" << ToUtf8(m_strReaderName) << "] progress bytes=" << nBytesProcessed
				<< " percent=" << std::fixed << std::setprecision(2) << dPercent
				<< " elapsedMs=" << elapsedMs;
			std::cout << out.str() << std::endl;
		}

		std::wstring m_strReaderName;
		std::uint64_t m_nTotalBytes = 0;
		std::uint64_t m_nIntervalBytes = DEFAULT_PROGRESS_INTERVAL_BYTES;
		std::uint64_t m_nNextReport = 0;
		std::chrono::steady_clock::time_point m_start;
	};

	/**
	 * @brief Tracks byte progress through the mapped reader without pulling any GUI state into the probe.
	 */
	class CMappedProbeVisitor : public IMappedFileRangeVisitor
	{
	public:
		CMappedProbeVisitor(CFnv1a64 &digest, CProbeProgressReporter &progressReporter)
			: m_rDigest(digest)
			, m_rProgressReporter(progressReporter)
		{
		}

		void OnMappedFileBytes(const BYTE *pBytes, size_t nByteCount) override
		{
			m_rDigest.Add(pBytes, nByteCount);
			m_nBytesProcessed += static_cast<std::uint64_t>(nByteCount);
			m_rProgressReporter.Report(m_nBytesProcessed);
		}

		std::uint64_t GetBytesProcessed() const
		{
			return m_nBytesProcessed;
		}

	private:
		CFnv1a64 &m_rDigest;
		CProbeProgressReporter &m_rProgressReporter;
		std::uint64_t m_nBytesProcessed = 0;
	};

	/**
	 * @brief Opens a sequential shared-read handle for the requested file path.
	 */
	HANDLE OpenProbeHandle(const std::wstring &path)
	{
		return ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	}

	/**
	 * @brief Converts the current command line to wide arguments so Unicode path probes stay lossless.
	 */
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

	/**
	 * @brief Parses a positive integer option value, rejecting malformed input.
	 */
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

	/**
	 * @brief Prints probe usage without burying the interesting reader flags.
	 */
	void PrintProbeUsage()
	{
		std::cerr << "Usage: emule-tests.exe --hash-probe <file> [--reader buffered|mapped|both] [--byte-limit N] [--progress-mib N]" << std::endl;
	}

	/**
	 * @brief Parses the standalone probe options from the current command line.
	 */
	bool TryParseProbeOptions(CProbeOptions &options)
	{
		const std::vector<std::wstring> args = GetWideArguments();
		bool bProbeRequested = false;

		for (size_t i = 1; i < args.size(); ++i) {
			const std::wstring &arg = args[i];
			if (arg == L"--hash-probe") {
				bProbeRequested = true;
				if (++i >= args.size()) {
					std::cerr << "Missing file path after --hash-probe" << std::endl;
					return false;
				}
				options.FilePath = args[i];
				continue;
			}

			if (!bProbeRequested)
				continue;

			if (arg == L"--reader") {
				if (++i >= args.size()) {
					std::cerr << "Missing value after --reader" << std::endl;
					return false;
				}

				options.bRunBuffered = false;
				options.bRunMapped = false;
				if (args[i] == L"buffered")
					options.bRunBuffered = true;
				else if (args[i] == L"mapped")
					options.bRunMapped = true;
				else if (args[i] == L"both")
					options.bRunBuffered = options.bRunMapped = true;
				else {
					std::cerr << "Unsupported --reader value: " << EscapeForProbeOutput(args[i]) << std::endl;
					return false;
				}
				continue;
			}

			if (arg == L"--byte-limit") {
				if (++i >= args.size() || !TryParseUint64(args[i], options.nByteLimit)) {
					std::cerr << "Invalid value after --byte-limit" << std::endl;
					return false;
				}
				continue;
			}

			if (arg == L"--progress-mib") {
				std::uint64_t nMiB = 0;
				if (++i >= args.size() || !TryParseUint64(args[i], nMiB)) {
					std::cerr << "Invalid value after --progress-mib" << std::endl;
					return false;
				}
				options.nProgressIntervalBytes = nMiB * 1024ull * 1024ull;
				continue;
			}

			std::cerr << "Unknown hash probe option: " << EscapeForProbeOutput(arg) << std::endl;
			return false;
		}

		if (!bProbeRequested)
			return false;

		if (options.FilePath.empty()) {
			std::cerr << "The --hash-probe mode requires a file path." << std::endl;
			return false;
		}

		if (!options.bRunBuffered && !options.bRunMapped) {
			std::cerr << "At least one reader must be enabled." << std::endl;
			return false;
		}

		return true;
	}

	/**
	 * @brief Obtains the on-disk file size for a probe handle.
	 */
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

	/**
	 * @brief Runs a buffered sequential scan to compare against the mapped reader path.
	 */
	CProbeResult RunBufferedProbe(const std::wstring &preparedPath, std::uint64_t nByteLimit, std::uint64_t nProgressIntervalBytes)
	{
		CProbeResult result;
		result.ReaderName = L"buffered";

		const auto start = std::chrono::steady_clock::now();
		HANDLE hFile = OpenProbeHandle(preparedPath);
		if (hFile == INVALID_HANDLE_VALUE) {
			result.dwError = ::GetLastError();
			return result;
		}

		std::unique_ptr<void, decltype(&::CloseHandle)> fileCloser(hFile, ::CloseHandle);
		if (!TryGetFileSize(hFile, result.nFileSize, result.dwError))
			return result;

		const std::uint64_t nTargetBytes = (nByteLimit != 0) ? std::min(nByteLimit, result.nFileSize) : result.nFileSize;
		std::vector<BYTE> buffer(static_cast<size_t>(DEFAULT_BUFFER_BYTES));
		CFnv1a64 digest;
		CProbeProgressReporter reporter(result.ReaderName, nTargetBytes, nProgressIntervalBytes);

		while (result.nBytesProcessed < nTargetBytes) {
			const std::uint64_t nRemaining = nTargetBytes - result.nBytesProcessed;
			const DWORD dwToRead = static_cast<DWORD>(std::min<std::uint64_t>(nRemaining, buffer.size()));
			DWORD dwRead = 0;
			if (!::ReadFile(hFile, &buffer[0], dwToRead, &dwRead, NULL)) {
				result.dwError = ::GetLastError();
				return result;
			}
			if (dwRead == 0)
				break;

			digest.Add(&buffer[0], dwRead);
			result.nBytesProcessed += static_cast<std::uint64_t>(dwRead);
			reporter.Report(result.nBytesProcessed);
		}

		reporter.Flush(result.nBytesProcessed);
		result.nDigest = digest.GetValue();
		result.dwError = ERROR_SUCCESS;
		result.bSucceeded = (result.nBytesProcessed == nTargetBytes);
		result.nElapsedMs = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
		return result;
	}

	/**
	 * @brief Runs the shared mapped-file reader against the same byte budget as the buffered path.
	 */
	CProbeResult RunMappedProbe(const std::wstring &preparedPath, std::uint64_t nByteLimit, std::uint64_t nProgressIntervalBytes)
	{
		CProbeResult result;
		result.ReaderName = L"mapped";

		const auto start = std::chrono::steady_clock::now();
		HANDLE hFile = OpenProbeHandle(preparedPath);
		if (hFile == INVALID_HANDLE_VALUE) {
			result.dwError = ::GetLastError();
			return result;
		}

		std::unique_ptr<void, decltype(&::CloseHandle)> fileCloser(hFile, ::CloseHandle);
		if (!TryGetFileSize(hFile, result.nFileSize, result.dwError))
			return result;

		const std::uint64_t nTargetBytes = (nByteLimit != 0) ? std::min(nByteLimit, result.nFileSize) : result.nFileSize;
		CFnv1a64 digest;
		CProbeProgressReporter reporter(result.ReaderName, nTargetBytes, nProgressIntervalBytes);
		CMappedProbeVisitor visitor(digest, reporter);

		DWORD dwMappedError = ERROR_SUCCESS;
		const bool bMappedOk = VisitMappedFileRange(hFile, 0, nTargetBytes, visitor, &dwMappedError);
		result.nBytesProcessed = visitor.GetBytesProcessed();
		reporter.Flush(result.nBytesProcessed);
		result.nDigest = digest.GetValue();
		result.dwError = bMappedOk ? ERROR_SUCCESS : dwMappedError;
		result.bSucceeded = bMappedOk && (result.nBytesProcessed == nTargetBytes);
		result.nElapsedMs = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
		return result;
	}

	/**
	 * @brief Emits a compact summary that remains readable when redirected to a log file.
	 */
	void PrintProbeResult(const CProbeResult &result)
	{
		std::ostringstream out;
		out
			<< "[" << ToUtf8(result.ReaderName) << "]"
			<< " success=" << (result.bSucceeded ? "true" : "false")
			<< " bytes=" << result.nBytesProcessed
			<< " fileSize=" << result.nFileSize
			<< " digest=0x" << std::hex << std::uppercase << result.nDigest << std::dec
			<< " elapsedMs=" << result.nElapsedMs
			<< " error=" << result.dwError;
		std::cout << out.str() << std::endl;
	}

	/**
	 * @brief Executes the isolated scan modes and returns a process exit code.
	 */
	int RunProbe(const CProbeOptions &options)
	{
		const std::wstring preparedPath = LongPathTestSupport::PreparePathForLongPath(options.FilePath);
		std::cout
			<< "hash-probe path=" << EscapeForProbeOutput(options.FilePath)
			<< "\nprepared-path=" << EscapeForProbeOutput(preparedPath)
			<< "\npath-length=" << options.FilePath.size()
			<< "\nprepared-path-length=" << preparedPath.size()
			<< std::endl;

		std::vector<CProbeResult> results;
		if (options.bRunBuffered)
			results.push_back(RunBufferedProbe(preparedPath, options.nByteLimit, options.nProgressIntervalBytes));
		if (options.bRunMapped)
			results.push_back(RunMappedProbe(preparedPath, options.nByteLimit, options.nProgressIntervalBytes));

		bool bAllSucceeded = true;
		for (const CProbeResult &result : results) {
			PrintProbeResult(result);
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
}

int RunHashProbeIfRequested(int, char **)
{
	CProbeOptions options;
	if (!TryParseProbeOptions(options)) {
		const std::vector<std::wstring> args = GetWideArguments();
		const bool bProbeRequested = std::find(args.begin(), args.end(), std::wstring(L"--hash-probe")) != args.end();
		if (bProbeRequested) {
			PrintProbeUsage();
			return 1;
		}
		return -1;
	}

	return RunProbe(options);
}
