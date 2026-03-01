#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <getopt.h>
#include <pwd.h>
#include <grp.h>
#include <libgen.h>
#include <dirent.h>
#include <fnmatch.h>
#include <limits.h>

// Structure to hold program options
typedef struct {
    int recursive;          // -R option
    int verbose;            // -v option
    int quiet;              // -f, --silent, --quiet option
    int changes_only;       // -c option
    int reference_mode;     // --reference option
    char *reference_file;    // File to reference mode from
    int preserve_root;      // --preserve-root option
    int no_preserve_root;   // --no-preserve-root option
    mode_t mode;            // Mode to apply
    mode_t umask_mask;      // Current umask
    int symbolic_mode;      // 1 if mode is symbolic (like u+x)
    char **files;
    int file_count;
} ChmodOptions;

// Structure for symbolic mode parsing
typedef struct {
    char who[4];            // u, g, o, a combinations
    char op;                // +, -, =
    char perm[4];           // r, w, x, X, s, t combinations
} SymbolicMode;

// Function prototypes
void print_usage(const char *program_name);
void print_version(void);
int parse_options(int argc, char *argv[], ChmodOptions *opts);
mode_t parse_numeric_mode(const char *mode_str);
int parse_symbolic_mode(const char *mode_str, ChmodOptions *opts, mode_t current_mode);
int parse_symbolic_component(const char *comp, SymbolicMode *sym_mode);
int apply_symbolic_operation(mode_t *mode, const SymbolicMode *sym_mode, mode_t umask_mask);
int change_mode(const char *path, ChmodOptions *opts);
int change_mode_recursive(const char *path, ChmodOptions *opts);
mode_t get_file_mode(const char *path);
int is_special_file(const char *path);
mode_t get_reference_mode(const char *ref_file);
int is_root_directory(const char *path);
void mode_to_string(mode_t mode, char *str);

void print_usage(const char *program_name) {
    fprintf(stderr, "Usage: %s [OPTION]... MODE[,MODE]... FILE...\n", program_name);
    fprintf(stderr, "  or:  %s [OPTION]... OCTAL-MODE FILE...\n", program_name);
    fprintf(stderr, "  or:  %s [OPTION]... --reference=RFILE FILE...\n", program_name);
    fprintf(stderr, "Change the mode of each FILE to MODE.\n\n");
    
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -c, --changes          like verbose but report only when a change is made\n");
    fprintf(stderr, "  -f, --silent, --quiet  suppress most error messages\n");
    fprintf(stderr, "  -v, --verbose          output a diagnostic for every file processed\n");
    fprintf(stderr, "      --no-preserve-root  do not treat '/' specially (the default)\n");
    fprintf(stderr, "      --preserve-root    fail to operate recursively on '/'\n");
    fprintf(stderr, "      --reference=RFILE  use RFILE's mode instead of MODE values\n");
    fprintf(stderr, "  -R, --recursive        change files and directories recursively\n");
    fprintf(stderr, "      --help             display this help and exit\n");
    fprintf(stderr, "      --version          output version information and exit\n\n");
    
    fprintf(stderr, "Each MODE is of the form '[ugoa]*([-+=]([rwxXst]*|[ugo]))+'.\n\n");
    
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  %s u+x file          Add execute permission for owner\n", program_name);
    fprintf(stderr, "  %s 755 file           Set rwxr-xr-x\n", program_name);
    fprintf(stderr, "  %s -R u+w,go-w docs   Add write permission for owner, remove for group/other recursively\n", program_name);
}

void print_version(void) {
    fprintf(stderr, "chmod (custom implementation) 1.0\n");
    fprintf(stderr, "Copyright (C) 2024 Free Software Foundation, Inc.\n");
    fprintf(stderr, "License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>.\n");
    fprintf(stderr, "This is free software: you are free to change and redistribute it.\n");
    fprintf(stderr, "There is NO WARRANTY, to the extent permitted by law.\n\n");
    fprintf(stderr, "Written by Custom Implementation.\n");
}

// Convert mode to string representation (like ls -l)
void mode_to_string(mode_t mode, char *str) {
    strcpy(str, "----------");
    
    // Owner permissions
    if (mode & S_IRUSR) str[1] = 'r';
    if (mode & S_IWUSR) str[2] = 'w';
    if (mode & S_IXUSR) str[3] = 'x';
    
    // Group permissions
    if (mode & S_IRGRP) str[4] = 'r';
    if (mode & S_IWGRP) str[5] = 'w';
    if (mode & S_IXGRP) str[6] = 'x';
    
    // Other permissions
    if (mode & S_IROTH) str[7] = 'r';
    if (mode & S_IWOTH) str[8] = 'w';
    if (mode & S_IXOTH) str[9] = 'x';
    
    // Special bits
    if (mode & S_ISUID) {
        str[3] = (str[3] == 'x') ? 's' : 'S';
    }
    if (mode & S_ISGID) {
        str[6] = (str[6] == 'x') ? 's' : 'S';
    }
    if (mode & S_ISVTX) {
        str[9] = (str[9] == 'x') ? 't' : 'T';
    }
}

// Check if path is root directory
int is_root_directory(const char *path) {
    char *resolved_path = realpath(path, NULL);
    if (!resolved_path) return 0;
    
    int is_root = (strcmp(resolved_path, "/") == 0);
    free(resolved_path);
    return is_root;
}

// Get mode from reference file
mode_t get_reference_mode(const char *ref_file) {
    struct stat st;
    if (stat(ref_file, &st) == -1) {
        fprintf(stderr, "chmod: cannot access '%s': %s\n", ref_file, strerror(errno));
        exit(EXIT_FAILURE);
    }
    return st.st_mode & 07777;  // Return only permission bits
}

// Parse numeric mode (octal)
mode_t parse_numeric_mode(const char *mode_str) {
    char *endptr;
    long mode = strtol(mode_str, &endptr, 8);
    
    if (*endptr != '\0' || mode < 0 || mode > 07777) {
        return (mode_t)-1;
    }
    
    return (mode_t)mode;
}

// Parse symbolic mode component
int parse_symbolic_component(const char *comp, SymbolicMode *sym_mode) {
    int i = 0;
    
    // Initialize
    memset(sym_mode->who, 0, sizeof(sym_mode->who));
    memset(sym_mode->perm, 0, sizeof(sym_mode->perm));
    sym_mode->op = 0;
    
    // Parse 'who' part (ugoa combinations)
    while (comp[i] && strchr("ugoa", comp[i])) {
        if (strlen(sym_mode->who) < 3) {
            char who_str[2] = {comp[i], '\0'};
            strcat(sym_mode->who, who_str);
        }
        i++;
    }
    
    // If no 'who' specified, default to 'a'
    if (i == 0) {
        strcpy(sym_mode->who, "a");
    }
    
    // Parse operator
    if (comp[i] && strchr("+-=", comp[i])) {
        sym_mode->op = comp[i];
        i++;
    } else {
        return -1;  // Invalid operator
    }
    
    // Parse permissions
    while (comp[i] && strchr("rwxXstugo", comp[i])) {
        if (strlen(sym_mode->perm) < 3) {
            char perm_str[2] = {comp[i], '\0'};
            strcat(sym_mode->perm, perm_str);
        }
        i++;
    }
    
    // Check if we've parsed the entire component
    if (comp[i] != '\0') {
        return -1;  // Extra characters
    }
    
    return 0;
}

// Apply symbolic operation to mode
int apply_symbolic_operation(mode_t *mode, const SymbolicMode *sym_mode, mode_t umask_mask) {
    mode_t who_mask = 0;
    mode_t perm_mask = 0;
    mode_t new_mode = *mode;
    
    // Determine 'who' mask
    for (int i = 0; sym_mode->who[i]; i++) {
        switch (sym_mode->who[i]) {
            case 'u': who_mask |= S_IRWXU | S_ISUID; break;
            case 'g': who_mask |= S_IRWXG | S_ISGID; break;
            case 'o': who_mask |= S_IRWXO; break;
            case 'a': who_mask |= 07777; break;  // All permission bits
        }
    }
    
    // Determine permission mask
    for (int i = 0; sym_mode->perm[i]; i++) {
        switch (sym_mode->perm[i]) {
            case 'r':
                perm_mask |= (who_mask & (S_IRUSR | S_IRGRP | S_IROTH));
                break;
            case 'w':
                perm_mask |= (who_mask & (S_IWUSR | S_IWGRP | S_IWOTH));
                break;
            case 'x':
                perm_mask |= (who_mask & (S_IXUSR | S_IXGRP | S_IXOTH));
                break;
            case 'X':
                // 'X' is handled separately - needs file context
                // For now, treat as 'x' if any execute bit is set
                if (*mode & (S_IXUSR | S_IXGRP | S_IXOTH)) {
                    perm_mask |= (who_mask & (S_IXUSR | S_IXGRP | S_IXOTH));
                }
                break;
            case 's':
                if (who_mask & S_IRWXU) perm_mask |= S_ISUID;
                if (who_mask & S_IRWXG) perm_mask |= S_ISGID;
                break;
            case 't':
                if (who_mask & S_IRWXO) perm_mask |= S_ISVTX;
                break;
            case 'u':
                perm_mask |= (*mode & S_IRWXU) ? (who_mask & S_IRWXU) : 0;
                perm_mask |= (*mode & S_ISUID) ? (who_mask & S_ISUID) : 0;
                break;
            case 'g':
                perm_mask |= (*mode & S_IRWXG) ? (who_mask & S_IRWXG) : 0;
                perm_mask |= (*mode & S_ISGID) ? (who_mask & S_ISGID) : 0;
                break;
            case 'o':
                perm_mask |= (*mode & S_IRWXO) ? (who_mask & S_IRWXO) : 0;
                break;
        }
    }
    
    // Apply operation
    switch (sym_mode->op) {
        case '+':
            new_mode |= perm_mask;
            break;
        case '-':
            new_mode &= ~perm_mask;
            break;
        case '=':
            // Clear the 'who' bits first
            new_mode &= ~who_mask;
            new_mode |= perm_mask;
            break;
    }
    
    *mode = new_mode;
    return 0;
}

// Parse symbolic mode string (may contain multiple comma-separated operations)
int parse_symbolic_mode(const char *mode_str, ChmodOptions *opts, mode_t current_mode) {
    char *mode_copy = strdup(mode_str);
    if (!mode_copy) return -1;
    
    char *saveptr;
    char *token = strtok_r(mode_copy, ",", &saveptr);
    mode_t new_mode = current_mode;
    
    while (token != NULL) {
        SymbolicMode sym_mode;
        if (parse_symbolic_component(token, &sym_mode) != 0) {
            fprintf(stderr, "chmod: invalid mode: '%s'\n", token);
            free(mode_copy);
            return -1;
        }
        
        apply_symbolic_operation(&new_mode, &sym_mode, opts->umask_mask);
        token = strtok_r(NULL, ",", &saveptr);
    }
    
    opts->mode = new_mode;
    free(mode_copy);
    return 0;
}

// Parse command line options
int parse_options(int argc, char *argv[], ChmodOptions *opts) {
    int c;
    int option_index = 0;
    
    static struct option long_options[] = {
        {"recursive", no_argument, 0, 'R'},
        {"changes", no_argument, 0, 'c'},
        {"verbose", no_argument, 0, 'v'},
        {"quiet", no_argument, 0, 'f'},
        {"silent", no_argument, 0, 'f'},
        {"reference", required_argument, 0, 0},
        {"preserve-root", no_argument, 0, 0},
        {"no-preserve-root", no_argument, 0, 0},
        {"help", no_argument, 0, 0},
        {"version", no_argument, 0, 0},
        {0, 0, 0, 0}
    };
    
    // Set defaults
    opts->recursive = 0;
    opts->verbose = 0;
    opts->quiet = 0;
    opts->changes_only = 0;
    opts->reference_mode = 0;
    opts->reference_file = NULL;
    opts->preserve_root = 0;
    opts->no_preserve_root = 0;
    opts->mode = 0;
    opts->symbolic_mode = 0;
    opts->files = NULL;
    opts->file_count = 0;
    
    // Get current umask
    opts->umask_mask = umask(0);
    umask(opts->umask_mask);  // Restore umask
    
    while ((c = getopt_long(argc, argv, "Rcvf", long_options, &option_index)) != -1) {
        switch (c) {
            case 'R':
                opts->recursive = 1;
                break;
            case 'c':
                opts->changes_only = 1;
                opts->verbose = 1;
                break;
            case 'v':
                opts->verbose = 1;
                break;
            case 'f':
                opts->quiet = 1;
                break;
            case 0:
                if (strcmp(long_options[option_index].name, "reference") == 0) {
                    opts->reference_mode = 1;
                    opts->reference_file = optarg;
                } else if (strcmp(long_options[option_index].name, "preserve-root") == 0) {
                    opts->preserve_root = 1;
                } else if (strcmp(long_options[option_index].name, "no-preserve-root") == 0) {
                    opts->no_preserve_root = 1;
                } else if (strcmp(long_options[option_index].name, "help") == 0) {
                    print_usage(argv[0]);
                    exit(EXIT_SUCCESS);
                } else if (strcmp(long_options[option_index].name, "version") == 0) {
                    print_version();
                    exit(EXIT_SUCCESS);
                }
                break;
            default:
                return -1;
        }
    }
    
    return optind;
}

// Get current file mode
mode_t get_file_mode(const char *path) {
    struct stat st;
    if (stat(path, &st) == -1) {
        return (mode_t)-1;
    }
    return st.st_mode;
}

// Check if file is special (device, pipe, socket, etc.)
int is_special_file(const char *path) {
    struct stat st;
    if (stat(path, &st) == -1) {
        return 0;
    }
    return (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode) || 
            S_ISFIFO(st.st_mode) || S_ISSOCK(st.st_mode));
}

// Change mode of a single file
int change_mode(const char *path, ChmodOptions *opts) {
    mode_t old_mode;
    mode_t new_mode;
    struct stat st;
    
    // Get current mode
    if (stat(path, &st) == -1) {
        if (!opts->quiet) {
            fprintf(stderr, "chmod: cannot access '%s': %s\n", path, strerror(errno));
        }
        return -1;
    }
    
    old_mode = st.st_mode;
    
    if (opts->reference_mode) {
        // Use reference file mode
        new_mode = (old_mode & ~07777) | (opts->mode & 07777);
    } else {
        new_mode = opts->mode;
        
        // For symbolic mode, we need to preserve certain bits
        if (opts->symbolic_mode) {
            // Preserve file type bits
            new_mode = (old_mode & ~07777) | (new_mode & 07777);
        }
    }
    
    // Apply the mode change
    if (chmod(path, new_mode) == -1) {
        if (!opts->quiet) {
            fprintf(stderr, "chmod: changing permissions of '%s': %s\n", 
                    path, strerror(errno));
        }
        return -1;
    }
    
    // Report changes if verbose
    if (opts->verbose) {
        if (!opts->changes_only || (old_mode & 07777) != (new_mode & 07777)) {
            char old_perm[11], new_perm[11];
            mode_to_string(old_mode & 07777, old_perm);
            mode_to_string(new_mode & 07777, new_perm);
            
            if ((old_mode & 07777) != (new_mode & 07777)) {
                printf("mode of '%s' changed from %04o (%s) to %04o (%s)\n",
                       path, old_mode & 07777, old_perm, 
                       new_mode & 07777, new_perm);
            } else {
                printf("mode of '%s' retained as %04o (%s)\n",
                       path, new_mode & 07777, new_perm);
            }
        }
    }
    
    return 0;
}

// Recursively change mode
int change_mode_recursive(const char *path, ChmodOptions *opts) {
    DIR *dir;
    struct dirent *entry;
    char fullpath[PATH_MAX];
    int ret = 0;
    
    // First, change mode of current directory/file
    if (change_mode(path, opts) != 0) {
        ret = -1;
    }
    
    // If it's a directory and recursive, process contents
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        dir = opendir(path);
        if (!dir) {
            if (!opts->quiet) {
                fprintf(stderr, "chmod: cannot open directory '%s': %s\n", 
                        path, strerror(errno));
            }
            return -1;
        }
        
        while ((entry = readdir(dir)) != NULL) {
            // Skip . and ..
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            
            snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);
            
            if (change_mode_recursive(fullpath, opts) != 0) {
                ret = -1;
            }
        }
        
        closedir(dir);
    }
    
    return ret;
}

int main(int argc, char *argv[]) {
    ChmodOptions opts;
    int opt_index;
    int exit_status = 0;
    
    if (argc < 2) {
        print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }
    
    // Parse options
    opt_index = parse_options(argc, argv, &opts);
    if (opt_index < 0) {
        print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }
    
    // Check for --reference mode
    if (opts.reference_mode) {
        opts.mode = get_reference_mode(opts.reference_file);
        
        // The remaining arguments are files
        if (opt_index >= argc) {
            fprintf(stderr, "chmod: missing operand after '--reference=%s'\n", 
                    opts.reference_file);
            exit(EXIT_FAILURE);
        }
        opts.files = &argv[opt_index];
        opts.file_count = argc - opt_index;
    } else {
        // Need at least mode and one file
        if (opt_index >= argc - 1) {
            fprintf(stderr, "chmod: missing operand\n");
            print_usage(argv[0]);
            exit(EXIT_FAILURE);
        }
        
        char *mode_str = argv[opt_index];
        
        // Check if it's a symbolic mode (contains letters or special chars)
        if (strpbrk(mode_str, "ugoa+-=rwxXst")) {
            opts.symbolic_mode = 1;
            
            // For symbolic mode, we need a dummy file to get current mode
            // We'll process each file individually
            char *first_file = argv[opt_index + 1];
            mode_t current_mode = get_file_mode(first_file);
            
            if (current_mode == (mode_t)-1) {
                // Can't stat file, use default mode
                current_mode = 0777 & ~opts.umask_mask;
            }
            
            if (parse_symbolic_mode(mode_str, &opts, current_mode) != 0) {
                exit(EXIT_FAILURE);
            }
        } else {
            // Numeric mode
            opts.symbolic_mode = 0;
            opts.mode = parse_numeric_mode(mode_str);
            if (opts.mode == (mode_t)-1) {
                fprintf(stderr, "chmod: invalid mode: '%s'\n", mode_str);
                exit(EXIT_FAILURE);
            }
        }
        
        opts.files = &argv[opt_index + 1];
        opts.file_count = argc - opt_index - 1;
    }
    
    // Check preserve-root
    if (opts.preserve_root && !opts.no_preserve_root) {
        for (int i = 0; i < opts.file_count; i++) {
            if (is_root_directory(opts.files[i])) {
                fprintf(stderr, "chmod: it is dangerous to operate recursively on '/'\n");
                fprintf(stderr, "chmod: use --no-preserve-root to override this failsafe\n");
                exit(EXIT_FAILURE);
            }
        }
    }
    
    // Process each file
    for (int i = 0; i < opts.file_count; i++) {
        int result;
        
        if (opts.recursive) {
            result = change_mode_recursive(opts.files[i], &opts);
        } else {
            result = change_mode(opts.files[i], &opts);
        }
        
        if (result != 0) {
            exit_status = 1;
        }
    }
    
    return exit_status;
}
