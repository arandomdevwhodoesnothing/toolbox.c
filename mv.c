#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <libgen.h>
#include <getopt.h>
#include <time.h>

#define BUFFER_SIZE 65536  // 64KB buffer for efficient copying
#define MAX_PATH 4096

// Structure for command line options
typedef struct {
    int interactive;      // -i: prompt before overwrite
    int force;           // -f: do not prompt before overwriting
    int verbose;         // -v: explain what is being done
    int no_clobber;      // -n: do not overwrite an existing file
    int backup;          // -b: make backup of existing files
    int update;          // -u: move only when source is newer
    int strip_trailing_slashes; // --strip-trailing-slashes
    int target_directory; // -t: specify target directory
    int no_target_directory; // -T: treat target as normal file
    char *suffix;        // -S: backup suffix (default ~)
    char *backup_dir;    // --backup-dir: directory for backups
} mv_options_t;

// Initialize default options
void init_options(mv_options_t *opts) {
    opts->interactive = 0;
    opts->force = 0;
    opts->verbose = 0;
    opts->no_clobber = 0;
    opts->backup = 0;
    opts->update = 0;
    opts->strip_trailing_slashes = 0;
    opts->target_directory = 0;
    opts->no_target_directory = 0;
    opts->suffix = "~";
    opts->backup_dir = NULL;
}

// Error handling function
void error_exit(const char *msg) {
    fprintf(stderr, "mv: %s: %s\n", msg, strerror(errno));
    exit(EXIT_FAILURE);
}

// Check if file exists
int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

// Check if path is directory
int is_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0)
        return 0;
    return S_ISDIR(st.st_mode);
}

// Get file modification time
time_t get_mtime(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0)
        return 0;
    return st.st_mtime;
}

// Create backup filename
char *create_backup_name(const char *filename, mv_options_t *opts) {
    char *backup_name = malloc(strlen(filename) + strlen(opts->suffix) + 1);
    if (!backup_name) error_exit("malloc failed");
    
    if (opts->backup_dir) {
        // Create backup in specified directory
        char *base = basename((char *)filename);
        snprintf(backup_name, MAX_PATH, "%s/%s%s", opts->backup_dir, base, opts->suffix);
    } else {
        // Simple backup with suffix
        sprintf(backup_name, "%s%s", filename, opts->suffix);
    }
    
    return backup_name;
}

// Create numbered backup if needed
char *create_numbered_backup(const char *filename) {
    static char backup[MAX_PATH];
    int n = 1;
    
    while (1) {
        snprintf(backup, sizeof(backup), "%s.%d", filename, n);
        if (!file_exists(backup))
            return backup;
        n++;
    }
}

// Prompt user for confirmation
int confirm_overwrite(const char *dest) {
    char response[10];
    printf("mv: overwrite '%s'? ", dest);
    if (fgets(response, sizeof(response), stdin)) {
        return (response[0] == 'y' || response[0] == 'Y');
    }
    return 0;
}

// Copy file contents
int copy_file_contents(const char *src, const char *dest) {
    int src_fd, dest_fd;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read, bytes_written;
    struct stat src_st;
    
    // Get source file permissions
    if (stat(src, &src_st) != 0)
        return -1;
    
    // Open source file
    src_fd = open(src, O_RDONLY);
    if (src_fd < 0)
        return -1;
    
    // Open destination file with appropriate permissions
    dest_fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, src_st.st_mode);
    if (dest_fd < 0) {
        close(src_fd);
        return -1;
    }
    
    // Copy data
    while ((bytes_read = read(src_fd, buffer, sizeof(buffer))) > 0) {
        char *p = buffer;
        ssize_t remaining = bytes_read;
        
        while (remaining > 0) {
            bytes_written = write(dest_fd, p, remaining);
            if (bytes_written < 0) {
                close(src_fd);
                close(dest_fd);
                return -1;
            }
            remaining -= bytes_written;
            p += bytes_written;
        }
    }
    
    // Copy file metadata
    struct timespec times[2];
    times[0] = src_st.st_atim;
    times[1] = src_st.st_mtim;
    futimens(dest_fd, times);
    
    close(src_fd);
    close(dest_fd);
    
    return 0;
}

// Remove source file/directory
int remove_source(const char *src) {
    if (is_directory(src)) {
        return rmdir(src);
    } else {
        return unlink(src);
    }
}

// Move file or directory
int move_item(const char *src, const char *dest, mv_options_t *opts) {
    struct stat src_st, dest_st;
    int dest_exists = stat(dest, &dest_st) == 0;
    int src_is_dir = is_directory(src);
    char *backup_name = NULL;
    int result = 0;
    
    // Check if source exists
    if (stat(src, &src_st) != 0) {
        fprintf(stderr, "mv: cannot stat '%s': %s\n", src, strerror(errno));
        return -1;
    }
    
    // Check if source and destination are the same
    if (dest_exists && src_st.st_dev == dest_st.st_dev && src_st.st_ino == dest_st.st_ino) {
        fprintf(stderr, "mv: '%s' and '%s' are the same file\n", src, dest);
        return 0;  // Not an error, just nothing to do
    }
    
    // Handle update option
    if (opts->update && dest_exists) {
        time_t src_mtime = get_mtime(src);
        time_t dest_mtime = get_mtime(dest);
        if (src_mtime <= dest_mtime) {
            if (opts->verbose)
                printf("mv: skipping '%s' (destination newer)\n", src);
            return 0;
        }
    }
    
    // Handle backup option
    if (opts->backup && dest_exists) {
        backup_name = create_backup_name(dest, opts);
        if (rename(dest, backup_name) != 0) {
            free(backup_name);
            error_exit("cannot create backup");
        }
        if (opts->verbose)
            printf("mv: backing up '%s' to '%s'\n", dest, backup_name);
        free(backup_name);
    }
    
    // Check for overwrite conditions
    if (dest_exists && !opts->backup) {
        if (opts->no_clobber) {
            if (opts->verbose)
                printf("mv: not overwriting '%s'\n", dest);
            return 0;
        }
        
        if (opts->interactive && !opts->force) {
            if (!confirm_overwrite(dest))
                return 0;
        }
        
        if (opts->force) {
            // Remove destination if it exists
            if (remove_source(dest) != 0) {
                error_exit("cannot remove destination");
            }
        }
    }
    
    // Try to rename first (fast path)
    if (!opts->backup && rename(src, dest) == 0) {
        if (opts->verbose)
            printf("mv: renamed '%s' -> '%s'\n", src, dest);
        return 0;
    }
    
    // If rename fails (cross-device or other error), copy and delete
    if (errno == EXDEV || errno == EINVAL) {
        if (opts->verbose)
            printf("mv: copying '%s' -> '%s'\n", src, dest);
        
        // Copy contents
        if (src_is_dir) {
            // For directories, we need recursive copy
            fprintf(stderr, "mv: cannot move directory across devices\n");
            return -1;
        } else {
            if (copy_file_contents(src, dest) == 0) {
                // Copy successful, remove source
                if (remove_source(src) == 0) {
                    if (opts->verbose)
                        printf("mv: removed '%s'\n", src);
                    return 0;
                } else {
                    fprintf(stderr, "mv: cannot remove '%s' after copy\n", src);
                    return -1;
                }
            } else {
                fprintf(stderr, "mv: cannot copy '%s' to '%s'\n", src, dest);
                return -1;
            }
        }
    } else {
        // Some other error
        fprintf(stderr, "mv: cannot move '%s' to '%s': %s\n", src, dest, strerror(errno));
        return -1;
    }
}

// Process multiple sources with a target directory
int move_multiple(char **sources, int num_sources, const char *target_dir, mv_options_t *opts) {
    int success = 0;
    
    // Check if target is a directory
    if (!is_directory(target_dir)) {
        fprintf(stderr, "mv: target '%s' is not a directory\n", target_dir);
        return -1;
    }
    
    // Move each source into the target directory
    for (int i = 0; i < num_sources; i++) {
        char dest_path[MAX_PATH];
        char *base = basename(sources[i]);
        
        // Remove trailing slashes if requested
        if (opts->strip_trailing_slashes) {
            char *p = sources[i] + strlen(sources[i]) - 1;
            while (p > sources[i] && *p == '/')
                *p-- = '\0';
        }
        
        snprintf(dest_path, sizeof(dest_path), "%s/%s", target_dir, base);
        
        if (move_item(sources[i], dest_path, opts) == 0)
            success++;
    }
    
    return (success == num_sources) ? 0 : -1;
}

// Print usage information
void print_usage(const char *progname) {
    fprintf(stderr, "Usage: %s [OPTION]... SOURCE DEST\n", progname);
    fprintf(stderr, "  or:  %s [OPTION]... SOURCE... DIRECTORY\n", progname);
    fprintf(stderr, "  or:  %s [OPTION]... -t DIRECTORY SOURCE...\n", progname);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -f, --force                 do not prompt before overwriting\n");
    fprintf(stderr, "  -i, --interactive            prompt before overwrite\n");
    fprintf(stderr, "  -n, --no-clobber             do not overwrite an existing file\n");
    fprintf(stderr, "  -v, --verbose                explain what is being done\n");
    fprintf(stderr, "  -u, --update                 move only when the SOURCE file is newer\n");
    fprintf(stderr, "  -b, --backup                 make a backup of each existing destination file\n");
    fprintf(stderr, "  -S, --suffix=SUFFIX          override the usual backup suffix\n");
    fprintf(stderr, "  -t, --target-directory=DIR   move all SOURCE arguments into DIR\n");
    fprintf(stderr, "  -T, --no-target-directory    treat DEST as a normal file\n");
    fprintf(stderr, "      --strip-trailing-slashes remove any trailing slashes from each SOURCE\n");
    fprintf(stderr, "  -h, --help                   display this help and exit\n");
}

int main(int argc, char *argv[]) {
    mv_options_t opts;
    init_options(&opts);
    
    // Long options
    static struct option long_options[] = {
        {"force", no_argument, 0, 'f'},
        {"interactive", no_argument, 0, 'i'},
        {"verbose", no_argument, 0, 'v'},
        {"no-clobber", no_argument, 0, 'n'},
        {"update", no_argument, 0, 'u'},
        {"backup", no_argument, 0, 'b'},
        {"suffix", required_argument, 0, 'S'},
        {"target-directory", required_argument, 0, 't'},
        {"no-target-directory", no_argument, 0, 'T'},
        {"strip-trailing-slashes", no_argument, 0, 1},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    char *target_dir = NULL;
    int target_dir_specified = 0;
    
    // Parse command line options
    while ((opt = getopt_long(argc, argv, "fivnubS:t:Th", long_options, NULL)) != -1) {
        switch (opt) {
            case 'f':
                opts.force = 1;
                opts.interactive = 0;  // -f overrides -i
                break;
            case 'i':
                opts.interactive = 1;
                opts.force = 0;  // -i overrides -f
                break;
            case 'v':
                opts.verbose = 1;
                break;
            case 'n':
                opts.no_clobber = 1;
                break;
            case 'u':
                opts.update = 1;
                break;
            case 'b':
                opts.backup = 1;
                break;
            case 'S':
                opts.suffix = optarg;
                break;
            case 't':
                target_dir = optarg;
                target_dir_specified = 1;
                opts.target_directory = 1;
                break;
            case 'T':
                opts.no_target_directory = 1;
                break;
            case 1:  // --strip-trailing-slashes
                opts.strip_trailing_slashes = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                return EXIT_SUCCESS;
            default:
                print_usage(argv[0]);
                return EXIT_FAILURE;
        }
    }
    
    // Check for conflicting options
    if (opts.interactive && opts.force) {
        fprintf(stderr, "mv: cannot combine -i and -f options\n");
        return EXIT_FAILURE;
    }
    
    if (opts.no_clobber && (opts.interactive || opts.force || opts.backup)) {
        fprintf(stderr, "mv: cannot combine -n with -i, -f, or -b\n");
        return EXIT_FAILURE;
    }
    
    // Calculate number of remaining arguments
    int num_args = argc - optind;
    char **args = argv + optind;
    
    // Check argument count
    if (num_args < 1) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    
    // Handle different usage patterns
    if (target_dir_specified) {
        // -t option: all sources go to target directory
        if (num_args < 1) {
            fprintf(stderr, "mv: missing file operand after '-t'\n");
            return EXIT_FAILURE;
        }
        return move_multiple(args, num_args, target_dir, &opts);
    } else if (num_args == 2 && !opts.no_target_directory) {
        // Standard SOURCE DEST pattern
        const char *src = args[0];
        const char *dest = args[1];
        
        // Check if destination is a directory and we should move into it
        if (is_directory(dest) && !opts.no_target_directory) {
            char dest_path[MAX_PATH];
            char *base = basename((char *)src);
            snprintf(dest_path, sizeof(dest_path), "%s/%s", dest, base);
            return move_item(src, dest_path, &opts);
        } else {
            return move_item(src, dest, &opts);
        }
    } else if (num_args >= 2 && is_directory(args[num_args - 1]) && !opts.no_target_directory) {
        // Multiple sources, last argument is directory
        const char *target_dir = args[num_args - 1];
        return move_multiple(args, num_args - 1, target_dir, &opts);
    } else if (num_args == 2 && opts.no_target_directory) {
        // -T option: treat destination as file even if it's a directory
        return move_item(args[0], args[1], &opts);
    } else {
        fprintf(stderr, "mv: invalid arguments\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}
