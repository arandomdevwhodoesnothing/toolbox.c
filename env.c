#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

extern char **environ;

static void print_usage(FILE *stream, const char *progname) {
    fprintf(stream, "Usage: %s [OPTION]... [-] [NAME=VALUE]... [COMMAND [ARG]...]\n", progname);
    fprintf(stream, "Set each NAME to VALUE in the environment and run COMMAND.\n\n");
    fprintf(stream, "  -i, --ignore-environment   start with an empty environment\n");
    fprintf(stream, "  -0, --null           end each output line with NUL, not newline\n");
    fprintf(stream, "  -u, --unset=NAME     remove variable from the environment\n");
    fprintf(stream, "  -C, --chdir=DIR      change working directory to DIR\n");
    fprintf(stream, "  -v, --debug          print verbose information for each processing step\n");
    fprintf(stream, "      --help     display this help and exit\n");
    fprintf(stream, "      --version  output version information and exit\n\n");
    fprintf(stream, "A mere - implies -i.  If no COMMAND, print the resulting environment.\n");
}

static void print_version(void) {
    printf("env (custom implementation) 1.0\n");
    printf("Copyright (C) 2026\n");
    printf("This is free software: you are free to change and redistribute it.\n");
}

static void print_environment(char null_terminated) {
    char **env = environ;
    char separator = null_terminated ? '\0' : '\n';
    
    while (*env != NULL) {
        printf("%s%c", *env, separator);
        env++;
    }
}

static void unset_variable(const char *name, int debug) {
    if (debug) {
        fprintf(stderr, "env: unsetting '%s'\n", name);
    }
    
    if (unsetenv(name) != 0) {
        fprintf(stderr, "env: failed to unset '%s'\n", name);
        exit(EXIT_FAILURE);
    }
}

static void set_variable(const char *assignment, int debug) {
    char *equals = strchr(assignment, '=');
    if (equals == NULL) {
        fprintf(stderr, "env: invalid argument '%s': must be in format NAME=VALUE\n", assignment);
        exit(EXIT_FAILURE);
    }
    
    // Split into name and value
    char *name = strndup(assignment, equals - assignment);
    char *value = equals + 1;
    
    if (debug) {
        fprintf(stderr, "env: setting '%s' to '%s'\n", name, value);
    }
    
    if (setenv(name, value, 1) != 0) {
        fprintf(stderr, "env: failed to set '%s'\n", assignment);
        free(name);
        exit(EXIT_FAILURE);
    }
    
    free(name);
}

int main(int argc, char *argv[]) {
    int opt;
    int ignore_environment = 0;
    int null_terminated = 0;
    int debug = 0;
    char *chdir_dir = NULL;
    
    // Arrays to store unset operations and assignments
    char **unset_names = NULL;
    size_t unset_count = 0;
    
    char **assignments = NULL;
    size_t assign_count = 0;
    
    struct option long_options[] = {
        {"ignore-environment", no_argument, 0, 'i'},
        {"null", no_argument, 0, '0'},
        {"unset", required_argument, 0, 'u'},
        {"chdir", required_argument, 0, 'C'},
        {"debug", no_argument, 0, 'v'},
        {"help", no_argument, 0, 1},
        {"version", no_argument, 0, 2},
        {0, 0, 0, 0}
    };
    
    // Parse options
    while ((opt = getopt_long(argc, argv, "+i0u:C:v", long_options, NULL)) != -1) {
        switch (opt) {
            case 'i':
                ignore_environment = 1;
                if (debug) fprintf(stderr, "env: ignoring environment\n");
                break;
            case '0':
                null_terminated = 1;
                if (debug) fprintf(stderr, "env: using null termination\n");
                break;
            case 'u':
                unset_names = realloc(unset_names, (unset_count + 1) * sizeof(char *));
                unset_names[unset_count++] = strdup(optarg);
                break;
            case 'C':
                chdir_dir = optarg;
                if (debug) fprintf(stderr, "env: will chdir to '%s'\n", chdir_dir);
                break;
            case 'v':
                debug = 1;
                break;
            case 1: // --help
                print_usage(stdout, argv[0]);
                return EXIT_SUCCESS;
            case 2: // --version
                print_version();
                return EXIT_SUCCESS;
            case '?':
                // getopt already printed an error
                return EXIT_FAILURE;
            default:
                fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
                return EXIT_FAILURE;
        }
    }
    
    // Check for '-' argument which implies -i
    if (optind < argc && strcmp(argv[optind], "-") == 0) {
        ignore_environment = 1;
        if (debug) fprintf(stderr, "env: ignoring environment due to '-'\n");
        optind++;
    }
    
    // Handle ignore-environment: clear or set to empty
    if (ignore_environment) {
        // Clear the entire environment
        environ = NULL;
        if (debug) fprintf(stderr, "env: cleared environment\n");
    }
    
    // Process remaining arguments: first NAME=VALUE pairs, then COMMAND
    int command_start = optind;
    
    // First, collect all NAME=VALUE assignments
    while (command_start < argc && strchr(argv[command_start], '=') != NULL) {
        assignments = realloc(assignments, (assign_count + 1) * sizeof(char *));
        assignments[assign_count++] = strdup(argv[command_start]);
        command_start++;
    }
    
    // Apply unset operations first
    for (size_t i = 0; i < unset_count; i++) {
        unset_variable(unset_names[i], debug);
        free(unset_names[i]);
    }
    free(unset_names);
    
    // Then apply assignments (these will overwrite if they were unset)
    for (size_t i = 0; i < assign_count; i++) {
        set_variable(assignments[i], debug);
        free(assignments[i]);
    }
    free(assignments);
    
    // Change directory if requested
    if (chdir_dir != NULL) {
        if (chdir(chdir_dir) != 0) {
            fprintf(stderr, "env: cannot change directory to '%s': ", chdir_dir);
            perror("");
            return EXIT_FAILURE;
        }
        if (debug) fprintf(stderr, "env: changed to directory '%s'\n", chdir_dir);
    }
    
    // Check if we have a command to run
    if (command_start < argc) {
        // Execute the command with its arguments
        if (debug) {
            fprintf(stderr, "env: executing: ");
            for (int i = command_start; i < argc; i++) {
                fprintf(stderr, "%s ", argv[i]);
            }
            fprintf(stderr, "\n");
        }
        
        execvp(argv[command_start], &argv[command_start]);
        
        // If we get here, execvp failed
        fprintf(stderr, "env: failed to execute '%s': ", argv[command_start]);
        perror("");
        return EXIT_FAILURE;
    } else {
        // No command, just print the environment
        print_environment(null_terminated);
        return EXIT_SUCCESS;
    }
}
