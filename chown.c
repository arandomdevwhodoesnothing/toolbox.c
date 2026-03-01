#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>
#include <dirent.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// Version information
#define VERSION "1.0"

// Function prototypes
void print_usage(const char *program_name);
void print_version(void);
void print_help(const char *program_name);
int parse_user_group(const char *spec, uid_t *uid, gid_t *gid, int *has_user, int *has_group, int silent);
int change_ownership(const char *path, uid_t uid, gid_t gid, int flags, int silent, int verbose);
int process_path(const char *path, uid_t uid, gid_t gid, int flags, int recursive, int silent, int verbose);
const char* get_username(uid_t uid);
const char* get_groupname(gid_t gid);

// Global options
int dereference = 0;           // -h option: affect symlinks themselves
int preserve_root = 0;         // --preserve-root option
int no_preserve_root = 0;      // --no-preserve-root option

// Flags for ownership change
#define CHOWN_NOFLAG     0
#define CHOWN_DEREF      (1 << 0)  // Follow symlinks (default)
#define CHOWN_NO_DEREF   (1 << 1)  // Don't follow symlinks (-h)
#define CHOWN_CHANGE_UID (1 << 2)  // Change user
#define CHOWN_CHANGE_GID (1 << 3)  // Change group

int main(int argc, char *argv[]) {
    int opt;
    int verbose = 0;
    int recursive = 0;
    int silent = 0;
    int reference_mode = 0;
    char *reference_file = NULL;
    int option_index = 0;
    
    // Long options
    static struct option long_options[] = {
        {"changes",       no_argument,       0, 'c'},
        {"dereference",   no_argument,       0, 'L'},
        {"no-dereference",no_argument,       0, 'h'},
        {"quiet",         no_argument,       0, 'f'},
        {"silent",        no_argument,       0, 'f'},
        {"verbose",       no_argument,       0, 'v'},
        {"recursive",     no_argument,       0, 'R'},
        {"preserve-root", no_argument,       &preserve_root, 1},
        {"no-preserve-root", no_argument,    &no_preserve_root, 1},
        {"reference",     required_argument, 0, 'r'},
        {"help",          no_argument,       0, 'H'},
        {"version",       no_argument,       0, 'V'},
        {0, 0, 0, 0}
    };
    
    // Parse command line options
    while ((opt = getopt_long(argc, argv, "chLfvRr:HV", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'c':  // --changes
                verbose = 2;  // Show only changes
                break;
            case 'h':  // --no-dereference (affect symlinks)
                dereference = 0;
                break;
            case 'L':  // --dereference (follow symlinks)
                dereference = 1;
                break;
            case 'f':  // --silent, --quiet
                silent = 1;
                break;
            case 'v':  // --verbose
                verbose = 1;
                break;
            case 'R':  // --recursive
                recursive = 1;
                break;
            case 'r':  // --reference
                reference_mode = 1;
                reference_file = optarg;
                break;
            case 'H':  // --help
                print_help(argv[0]);
                return 0;
            case 'V':  // --version
                print_version();
                return 0;
            case 0:    // Flag set by long option
                break;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    // Handle --preserve-root and --no-preserve-root
    if (preserve_root && no_preserve_root) {
        fprintf(stderr, "Error: --preserve-root and --no-preserve-root are mutually exclusive\n");
        return 1;
    }
    
    // Parse user:group specification
    uid_t uid = -1;
    gid_t gid = -1;
    int has_user = 0;
    int has_group = 0;
    
    if (reference_mode) {
        // Get ownership from reference file
        struct stat ref_stat;
        if (stat(reference_file, &ref_stat) == -1) {
            fprintf(stderr, "Error: cannot stat reference file '%s': %s\n", 
                    reference_file, strerror(errno));
            return 1;
        }
        uid = ref_stat.st_uid;
        gid = ref_stat.st_gid;
        has_user = 1;
        has_group = 1;
    } else {
        // Check if we have enough arguments
        if (optind >= argc) {
            fprintf(stderr, "Error: missing operand\n");
            print_usage(argv[0]);
            return 1;
        }
        
        // Parse user:group
        if (parse_user_group(argv[optind], &uid, &gid, &has_user, &has_group, silent) != 0) {
            return 1;
        }
        optind++;
    }
    
    // Check if we have files to process
    if (optind >= argc) {
        fprintf(stderr, "Error: missing file operand\n");
        print_usage(argv[0]);
        return 1;
    }
    
    // Set flags
    int flags = CHOWN_NOFLAG;
    if (dereference) {
        flags |= CHOWN_DEREF;
    } else {
        flags |= CHOWN_NO_DEREF;
    }
    if (has_user) {
        flags |= CHOWN_CHANGE_UID;
    }
    if (has_group) {
        flags |= CHOWN_CHANGE_GID;
    }
    
    // Process each file/directory
    int errors = 0;
    for (int i = optind; i < argc; i++) {
        if (process_path(argv[i], uid, gid, flags, recursive, silent, verbose) != 0) {
            errors++;
        }
    }
    
    return errors ? 1 : 0;
}

void print_usage(const char *program_name) {
    fprintf(stderr, "Usage: %s [OPTION]... [OWNER][:[GROUP]] FILE...\n", program_name);
    fprintf(stderr, "  or:  %s [OPTION]... --reference=RFILE FILE...\n", program_name);
    fprintf(stderr, "Try '%s --help' for more information.\n", program_name);
}

void print_help(const char *program_name) {
    printf("Usage: %s [OPTION]... [OWNER][:[GROUP]] FILE...\n", program_name);
    printf("  or:  %s [OPTION]... --reference=RFILE FILE...\n", program_name);
    printf("Change the owner and/or group of each FILE to OWNER and/or GROUP.\n");
    printf("\nOptions:\n");
    printf("  -c, --changes          like verbose but report only when a change is made\n");
    printf("  -f, --silent, --quiet  suppress most error messages\n");
    printf("  -v, --verbose          output a diagnostic for every file processed\n");
    printf("  -h, --no-dereference   affect symbolic links instead of any referenced file\n");
    printf("  -L, --dereference      follow symbolic links (default)\n");
    printf("  -R, --recursive        operate on files and directories recursively\n");
    printf("      --preserve-root    fail to operate recursively on '/'\n");
    printf("      --no-preserve-root do not treat '/' specially (the default)\n");
    printf("      --reference=RFILE  use RFILE's owner and group rather than specifying\n");
    printf("                         OWNER:GROUP values\n");
    printf("      --help     display this help and exit\n");
    printf("      --version  output version information and exit\n");
    printf("\nOwner and group specification:\n");
    printf("  OWNER and GROUP may be numeric or symbolic.\n");
    printf("  If GROUP is omitted, only the owner is changed.\n");
    printf("  If OWNER is omitted, only the group is changed.\n");
    printf("  If ':' but no GROUP, change owner and change group to login group.\n");
    printf("  If ':' with GROUP, change owner and group.\n");
}

void print_version(void) {
    printf("chown (custom implementation) %s\n", VERSION);
    printf("This is free software: you are free to change and redistribute it.\n");
}

const char* get_username(uid_t uid) {
    struct passwd *pwd = getpwuid(uid);
    static char buf[32];
    if (pwd) {
        return pwd->pw_name;
    } else {
        snprintf(buf, sizeof(buf), "%d", uid);
        return buf;
    }
}

const char* get_groupname(gid_t gid) {
    struct group *grp = getgrgid(gid);
    static char buf[32];
    if (grp) {
        return grp->gr_name;
    } else {
        snprintf(buf, sizeof(buf), "%d", gid);
        return buf;
    }
}

int parse_user_group(const char *spec, uid_t *uid, gid_t *gid, int *has_user, int *has_group, int silent) {
    char *colon = strchr(spec, ':');
    struct passwd *pwd;
    struct group *grp;
    
    *has_user = 0;
    *has_group = 0;
    
    // Handle empty spec (should not happen, but just in case)
    if (strlen(spec) == 0) {
        if (!silent) {
            fprintf(stderr, "Error: empty user/group specification\n");
        }
        return -1;
    }
    
    if (colon == NULL) {
        // Only user specified
        // Try numeric UID first
        char *endptr;
        long num_uid = strtol(spec, &endptr, 10);
        if (*endptr == '\0') {
            // Valid number
            *uid = (uid_t)num_uid;
            *has_user = 1;
            
            // Verify the UID exists (optional, but good practice)
            if (getpwuid(*uid) == NULL && !silent) {
                fprintf(stderr, "Warning: UID %d does not exist in password database\n", *uid);
            }
        } else {
            // Try username
            pwd = getpwnam(spec);
            if (pwd == NULL) {
                if (!silent) {
                    fprintf(stderr, "Error: invalid user '%s'\n", spec);
                }
                return -1;
            }
            *uid = pwd->pw_uid;
            *has_user = 1;
        }
    } else {
        // User:Group format
        size_t user_len = colon - spec;
        
        if (user_len > 0) {
            // User part specified
            char user_str[256];
            strncpy(user_str, spec, user_len);
            user_str[user_len] = '\0';
            
            // Try numeric UID first
            char *endptr;
            long num_uid = strtol(user_str, &endptr, 10);
            if (*endptr == '\0') {
                *uid = (uid_t)num_uid;
                // Verify the UID exists
                if (getpwuid(*uid) == NULL && !silent) {
                    fprintf(stderr, "Warning: UID %d does not exist in password database\n", *uid);
                }
            } else {
                pwd = getpwnam(user_str);
                if (pwd == NULL) {
                    if (!silent) {
                        fprintf(stderr, "Error: invalid user '%s'\n", user_str);
                    }
                    return -1;
                }
                *uid = pwd->pw_uid;
            }
            *has_user = 1;
        }
        
        // Group part
        if (*(colon + 1) != '\0') {
            // Group specified
            char *group_str = colon + 1;
            
            // Try numeric GID first
            char *endptr;
            long num_gid = strtol(group_str, &endptr, 10);
            if (*endptr == '\0') {
                *gid = (gid_t)num_gid;
                // Verify the GID exists
                if (getgrgid(*gid) == NULL && !silent) {
                    fprintf(stderr, "Warning: GID %d does not exist in group database\n", *gid);
                }
            } else {
                grp = getgrnam(group_str);
                if (grp == NULL) {
                    if (!silent) {
                        fprintf(stderr, "Error: invalid group '%s'\n", group_str);
                    }
                    return -1;
                }
                *gid = grp->gr_gid;
            }
            *has_group = 1;
        } else {
            // Colon but no group - change owner and group to login group
            if (*has_user) {
                // Get the user's primary group
                pwd = getpwuid(*uid);
                if (pwd != NULL) {
                    *gid = pwd->pw_gid;
                    *has_group = 1;
                } else if (!silent) {
                    fprintf(stderr, "Warning: Cannot determine login group for user\n");
                }
            }
        }
    }
    
    return 0;
}

int change_ownership(const char *path, uid_t uid, gid_t gid, int flags, int silent, int verbose) {
    struct stat st;
    int result;
    int saved_errno;
    
    // Check for root directory protection
    if (preserve_root && strcmp(path, "/") == 0) {
        if (!silent) {
            fprintf(stderr, "chown: refusing to change ownership of '/' (use --no-preserve-root to override)\n");
        }
        return -1;
    }
    
    // Get current ownership
    int stat_result;
    if (flags & CHOWN_DEREF) {
        stat_result = stat(path, &st);
    } else {
        stat_result = lstat(path, &st);
    }
    
    if (stat_result == -1) {
        if (!silent) {
            fprintf(stderr, "chown: cannot access '%s': %s\n", path, strerror(errno));
        }
        return -1;
    }
    
    // Determine what to change
    uid_t new_uid = (flags & CHOWN_CHANGE_UID) ? uid : st.st_uid;
    gid_t new_gid = (flags & CHOWN_CHANGE_GID) ? gid : st.st_gid;
    
    // Check if anything needs to change
    if (new_uid == st.st_uid && new_gid == st.st_gid) {
        if (verbose == 1) {
            printf("ownership of '%s' retained as ", path);
            if (flags & CHOWN_CHANGE_UID) {
                printf("%s", get_username(st.st_uid));
            }
            if ((flags & CHOWN_CHANGE_UID) && (flags & CHOWN_CHANGE_GID)) {
                printf(":");
            }
            if (flags & CHOWN_CHANGE_GID) {
                printf("%s", get_groupname(st.st_gid));
            }
            printf("\n");
        }
        return 0;
    }
    
    // Check permissions (useful for debugging)
    if (geteuid() != 0 && geteuid() != st.st_uid) {
        if (!silent && verbose) {
            fprintf(stderr, "chown: warning: operation not permitted (not owner of '%s')\n", path);
        }
    }
    
    // Change ownership
    if (flags & CHOWN_DEREF) {
        result = chown(path, new_uid, new_gid);
    } else {
#ifdef HAVE_LCHOWN
        result = lchown(path, new_uid, new_gid);
#else
        // Fallback if lchown not available
        result = chown(path, new_uid, new_gid);
        if (result == -1 && errno == ENOENT) {
            // Try to handle symlinks without lchown
            if (!silent) {
                fprintf(stderr, "chown: warning: cannot change ownership of symlink '%s' (lchown not available)\n", path);
            }
        }
#endif
    }
    
    saved_errno = errno;
    
    // Handle results
    if (result == -1) {
        if (!silent) {
            fprintf(stderr, "chown: changing ownership of '%s': %s\n", path, strerror(saved_errno));
        }
        return -1;
    }
    
    // Verbose output
    if (verbose == 1 || (verbose == 2 && (new_uid != st.st_uid || new_gid != st.st_gid))) {
        printf("changed ownership of '%s' from %s:%s to ", 
               path, get_username(st.st_uid), get_groupname(st.st_gid));
        
        if (flags & CHOWN_CHANGE_UID) {
            printf("%s", get_username(new_uid));
        } else {
            printf("%s", get_username(st.st_uid));
        }
        
        printf(":");
        
        if (flags & CHOWN_CHANGE_GID) {
            printf("%s", get_groupname(new_gid));
        } else {
            printf("%s", get_groupname(st.st_gid));
        }
        printf("\n");
    }
    
    return 0;
}

int process_path(const char *path, uid_t uid, gid_t gid, int flags, int recursive, int silent, int verbose) {
    struct stat st;
    int ret = 0;
    
    // Check if it's a directory for recursive processing
    if (recursive && lstat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        // Process directory contents recursively
        DIR *dir = opendir(path);
        if (dir == NULL) {
            if (!silent) {
                fprintf(stderr, "chown: cannot read directory '%s': %s\n", path, strerror(errno));
            }
            ret = -1;
        } else {
            struct dirent *entry;
            char fullpath[PATH_MAX];
            
            while ((entry = readdir(dir)) != NULL) {
                // Skip . and ..
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                    continue;
                }
                
                // Construct full path
                snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);
                
                // Process subdirectory/file recursively
                if (process_path(fullpath, uid, gid, flags, recursive, silent, verbose) != 0) {
                    ret = -1;
                }
            }
            closedir(dir);
        }
    }
    
    // Change ownership of current file/directory
    if (change_ownership(path, uid, gid, flags, silent, verbose) != 0) {
        ret = -1;
    }
    
    return ret;
}
