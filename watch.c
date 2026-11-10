// SPDX-License-Identifier: MIT
#define _POSIX_C_SOURCE 200809L

#include "watch.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stop_requested = 0;

// Ctrl+C sets this; the main code checks it and stops
static void handle_interrupt(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

// Use our handler for Ctrl+C and save the old setting
static int install_interrupt_handler(struct sigaction *old_action)
{
    struct sigaction action;

    action.sa_handler = handle_interrupt;

    // Start with no extra signals blocked during the handler
    if (sigemptyset(&action.sa_mask) == -1) {
        perror("watch: sigemptyset");
        return -1;
    }
    action.sa_flags = 0;
    if (sigaction(SIGINT, &action, old_action) == -1) {
        perror("watch: sigaction");
        return -1;
    }
    return 0;
}

static int get_time(struct timespec *time_value)
{
    // Clock that moves forwards steadily, used here to measure elapsed time
    if (clock_gettime(CLOCK_MONOTONIC, time_value) == -1) {
        perror("watch: clock_gettime");
        return -1;
    }
    return 0;
}

// Find the time between two clock readings
static double elapsed_seconds(const struct timespec *start, const struct timespec *end)
{
    return (double)(end->tv_sec - start->tv_sec) + (double)(end->tv_nsec - start->tv_nsec) / 1000000000.0;
}

// Wait for the requested time
static int wait_for_interval(double seconds)
{
    struct timespec delay;

    delay.tv_sec = (time_t)seconds;
    delay.tv_nsec = (long)((seconds - (double)delay.tv_sec) * 1000000000.0);
    if (delay.tv_nsec < 0) {
        delay.tv_nsec = 0;
    }

    // Sleep for requested time. Interrupted sleep other than Ctrl+C: try again for the time left
    while (nanosleep(&delay, &delay) == -1) {
        if (errno == EINTR && stop_requested) {
            return 1;
        }
        if (errno != EINTR) {
            perror("watch: nanosleep");
            return -1;
        }
    }
    return 0;
}

static int set_close_on_exec(int file_descriptor)
{
    int flags = fcntl(file_descriptor, F_GETFD);

    // Close this pipe end when the command starts
    if (flags == -1 || fcntl(file_descriptor, F_SETFD, flags | FD_CLOEXEC) == -1) {
        return -1;
    }
    return 0;
}

static int report_exec_failure(int error_pipe[2], int *exec_error)
{
    size_t bytes_read = 0;

    // No error bytes means it started; an error number means it did not
    *exec_error = 0;
    while (bytes_read < sizeof(*exec_error)) {
        // Read the child's error number, if it sent one
        ssize_t result = read(error_pipe[0], (char *)exec_error + bytes_read, sizeof(*exec_error) - bytes_read);
        if (result > 0) {
            bytes_read += (size_t)result;
        } else if (result == 0) {
            break;
        } else if (errno != EINTR) {
            return -1;
        }
    }
    if (bytes_read != 0 && bytes_read != sizeof(*exec_error)) {
        errno = EIO;
        return -1;
    }
    return 0;
}

// Tell the parent why the command could not start, then exit
static void child_report_error(int error_pipe, int child_error)
{
    const char *bytes = (const char *)&child_error;
    size_t bytes_written = 0;

    while (bytes_written < sizeof(child_error)) {
        // Write any remaining bytes of the error number
        ssize_t result = write(error_pipe, bytes + bytes_written, sizeof(child_error) - bytes_written);
        if (result > 0) {
            bytes_written += (size_t)result;
        } else if (result == -1 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
    _exit(127);
}

static int wait_for_child(pid_t child, int *status)
{
    // Wait here so the next run won't start before this one ends
    if (!stop_requested) {
        for (;;) {
            if (waitpid(child, status, 0) != -1) { // Child finished
                return 0;
            }
            if (errno == EINTR && !stop_requested) {
                continue;
            }
            if (errno != EINTR) {
                perror("watch: waitpid");
                return -1;
            }
            break;
        }
    }

    // Ask the child to stop after Ctrl+C
    if (kill(child, SIGTERM) == -1 && errno != ESRCH) {
        perror("watch: kill");
        return -1;
    }
    while (waitpid(child, status, 0) == -1) {
        if (errno != EINTR) {
            perror("watch: waitpid");
            return -1;
        }
    }
    return 0;
}

// 0 = done, -1 = error, 1 = Ctrl+C interruption
static int run_command(char *const command_argv[])
{
    int error_pipe[2];
    int child_status;
    int exec_error;
    pid_t child;

    // Pipe for sending a start-up error from child to parent
    if (pipe(error_pipe) == -1) {
        perror("watch: pipe");
        return -1;
    }
    if (set_close_on_exec(error_pipe[0]) == -1 || set_close_on_exec(error_pipe[1]) == -1) {
        perror("watch: fcntl");
        (void)close(error_pipe[0]);
        (void)close(error_pipe[1]);
        return -1;
    }

    // Child runs the requested command; parent keeps watch running
    child = fork();
    if (child == -1) {
        perror("watch: fork");
        (void)close(error_pipe[0]);
        (void)close(error_pipe[1]);
        return -1;
    }
    if (child == 0) {
        struct sigaction default_action;
        int child_error;

        // Closes child's read end: child only writes errors, the parents reads them
        (void)close(error_pipe[0]);
        default_action.sa_handler = SIG_DFL;
        if (sigemptyset(&default_action.sa_mask) == -1) {
            child_error = errno;
            child_report_error(error_pipe[1], child_error);
        }
        default_action.sa_flags = 0;
        if (sigaction(SIGINT, &default_action, NULL) == -1) {
            child_error = errno;
            child_report_error(error_pipe[1], child_error);
        }

        // Successful exec replaces this child with the requested command
        execvp(command_argv[0], command_argv);
        child_error = errno;
        child_report_error(error_pipe[1], child_error);
    }

    // Parent won't write, so close its write end
    (void)close(error_pipe[1]);
    if (report_exec_failure(error_pipe, &exec_error) == -1) {
        perror("watch: read");
        kill(child, SIGTERM);
        (void)close(error_pipe[0]);
        wait_for_child(child, &child_status);
        return -1;
    }
    (void)close(error_pipe[0]);

    if (wait_for_child(child, &child_status) == -1) {
        return -1;
    }
    if (stop_requested) {
        return 1;
    }
    if (exec_error != 0) {
        errno = exec_error;
        perror("watch: execvp");
        return -1;
    }
    // Command finished; watch will repeat it even if it returned an error
    return 0;
}

int watch_run(double interval_seconds, char *const command_argv[])
{
    struct sigaction old_action;
    int final_status = EXIT_SUCCESS;

    stop_requested = 0;
    if (install_interrupt_handler(&old_action) == -1) {
        return EXIT_FAILURE;
    }

    // Repeat the command until Ctrl+C or an error
    while (!stop_requested) {
        struct timespec start;
        struct timespec end;
        double remaining;
        int run_result;
        int sleep_result;

        if (fputs("\033[H\033[2J", stdout) == EOF || fflush(stdout) == EOF) {
            perror("watch: terminal refresh");
            final_status = EXIT_FAILURE;
            break;
        }
        if (get_time(&start) == -1) {
            final_status = EXIT_FAILURE;
            break;
        }

        run_result = run_command(command_argv);
        if (run_result == 1 || stop_requested) {
            final_status = 128 + SIGINT;
            break;
        }
        if (run_result == -1) {
            final_status = EXIT_FAILURE;
            break;
        }

        if (get_time(&end) == -1) {
            final_status = EXIT_FAILURE;
            break;
        }
        // Subtract run time from the interval to get the remaining wait
        remaining = interval_seconds - elapsed_seconds(&start, &end);
        if (remaining > 0.0) {
            sleep_result = wait_for_interval(remaining);
            if (sleep_result == 1) {
                final_status = 128 + SIGINT;
                break;
            }
            if (sleep_result == -1) {
                final_status = EXIT_FAILURE;
                break;
            }
        }
    }

    // Put Ctrl+C back the way it was before watch started
    if (sigaction(SIGINT, &old_action, NULL) == -1) {
        perror("watch: restore sigaction");
        return EXIT_FAILURE;
    }
    return final_status;
}
