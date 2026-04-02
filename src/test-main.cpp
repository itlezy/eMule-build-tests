#define DOCTEST_CONFIG_IMPLEMENT
#include "../third_party/doctest/doctest.h"

#include "../include/FullHashProbe.h"
#include "../include/HashProbe.h"

#if defined(__has_include)
#if !__has_include("MappedFileReader.h")
/**
 * @brief Returns the standard "not requested" sentinel when the full hash probe is unavailable in this workspace.
 */
int RunFullHashProbeIfRequested(int, char **)
{
	return -1;
}

/**
 * @brief Returns the standard "not requested" sentinel when the hash probe is unavailable in this workspace.
 */
int RunHashProbeIfRequested(int, char **)
{
	return -1;
}
#endif
#endif

/**
 * @brief Runs the standalone hash probe when explicitly requested, otherwise executes doctest.
 */
int main(int argc, char **argv)
{
	const int nFullHashProbeResult = RunFullHashProbeIfRequested(argc, argv);
	if (nFullHashProbeResult >= 0)
		return nFullHashProbeResult;

	const int nProbeResult = RunHashProbeIfRequested(argc, argv);
	if (nProbeResult >= 0)
		return nProbeResult;

	doctest::Context context;
	context.applyCommandLine(argc, argv);
	return context.run();
}
