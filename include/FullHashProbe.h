#pragma once

/**
 * @brief Runs the standalone non-UI full hashing probe when the command line requests it.
 *
 * @return Probe exit code when `--full-hash-probe` was supplied, otherwise `-1`.
 */
int RunFullHashProbeIfRequested(int argc, char **argv);
