// SPDX-License-Identifier: MIT

#include "watch.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_INTERVAL_SECONDS 1000000000.0

// Print the usage and option help
static void print_usage(FILE *stream, const char *program) {
  fprintf(stream, "Usage: %s [--interval SECS] COMMAND [ARGS...]\n", program);
  fprintf(stream, "       %s [-n SECS] COMMAND [ARGS...]\n", program);
  fputs("Run COMMAND repeatedly, waiting between runs.\n"
        "  --interval SECS  seconds between command starts (default: 2)\n"
        "  -n SECS          short form of --interval\n"
        "  --help           show this help\n",
        stream);
}

// Turn the interval text into a checked number
static int parse_interval(const char *text, double *interval) {
  char *end = NULL;
  double value;

  errno = 0;
  value = strtod(text, &end); // Convert text; end points after the number
  if (errno != 0 || end == text || *end != '\0' || !isfinite(value) ||
      value <= 0.0 || value > MAX_INTERVAL_SECONDS) {
    return -1;
  }

  *interval = value;
  return 0;
}

int main(int argc, char **argv) {
  double interval = 2.0;
  int command_index = 1;

  while (command_index < argc) {
    const char *option = argv[command_index];

    if (strcmp(option, "--help") == 0) { // Help goes to stdout
      print_usage(stdout, argv[0]);
      return EXIT_SUCCESS;
    }
    if (strcmp(option, "--") == 0) {
      command_index++;
      break;
    }
    if (strcmp(option, "-n") == 0 || strcmp(option, "--interval") == 0) {
      // The interval option needs a number right after it
      if (command_index + 1 >= argc ||
          parse_interval(argv[command_index + 1], &interval) != 0) {
        fprintf(stderr,
                "%s: %s requires a number greater than 0 and at most "
                "1000000000 seconds\n",
                argv[0], option);
        print_usage(stderr, argv[0]);
        return EXIT_FAILURE;
      }
      command_index += 2;
      continue;
    }
    break;
  }

  if (command_index >= argc) {
    fprintf(stderr, "%s: missing command\n", argv[0]);
    print_usage(stderr, argv[0]);
    return EXIT_FAILURE;
  }

  // Pass the command and its arguments to the repeat loop
  return watch_run(interval, &argv[command_index]);
}
