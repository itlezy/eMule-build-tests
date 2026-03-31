#pragma once

/**
 * @brief Runs the standalone non-UI hash probe when the current command line requests it.
 *
 * @return Probe exit code when `--hash-probe` was supplied, otherwise `-1` so callers can
 *         continue with their default startup flow.
 */
int RunHashProbeIfRequested(int argc, char **argv);
