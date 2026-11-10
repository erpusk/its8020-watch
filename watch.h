// SPDX-License-Identifier: MIT
#ifndef WATCH_H
#define WATCH_H

// Repeatedly run command_argv at the requested interval until interrupted
int watch_run(double interval_seconds, char *const command_argv[]);

#endif
