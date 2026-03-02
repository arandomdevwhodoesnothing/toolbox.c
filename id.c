/*
 * id.c - Implementation of the standard id command
 * Usage: id [OPTION]... [USER]
 * Options:
 *   -g, --group     print only the effective group ID
 *   -G, --groups    print all group IDs
 *   -n, --name      print a name instead of a number (with -ugG)
 *   -r, --real      print the real ID instead of effective (with -ugG)
 *   -u, --user      print only the effective user ID
 *   -z, --zero      delimit entries with NUL, not whitespace (with -G)
 *   --help          display help
 *   --version       output version information
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <getopt.h>
#include <errno.h>
#include <limits.h>

#define VERSION "1.0"
#define PROGRAM_NAME "id"

/* Structure to hold command line options */
struct id_options {
    int group;      /* -g: print only effective group ID */
    int groups;     /* -G: print all group IDs */
    int name;       /* -n: print name instead of number (with -ugG) */
    int real;       /* -r: print real ID instead of effective */
    int user;       /* -u: print only effective user ID */
    int zero;       /* -z: delimit with NUL (with -G) */
    int help;       /* --help */
    int version;    /* --version */
    char *username; /* Target username (NULL for current user) */
};

/* Function prototypes */
static void print_help(void);
static void print_version(void);
static void parse_options(int argc, char *argv[], struct id_options *opts);
static void print_user_info(struct id_options *opts);
static uid_t get_user_uid(const char *username, int real);
static gid_t get_user_gid(const char *username, int real);
static char *get_group_name(gid_t gid);
static char *get_user_name(uid_t uid);
static int get_group_list(const char *username, gid_t *groups, int max_groups);
static void print_all_info(uid_t uid, gid_t gid, const char *username, 
                          gid_t *groups, int ngroups);
static void print_groups_only(gid_t *groups, int ngroups, int zero);
static void die(const char *msg);

/* Print help information */
static void print_help(void) {
    printf("Usage: %s [OPTION]... [USER]\n", PROGRAM_NAME);
    printf("Print user and group information for the specified USER,\n");
    printf("or (when USER omitted) for the current user.\n\n");
    printf("  -g, --group     print only the effective group ID\n");
    printf("  -G, --groups    print all group IDs\n");
    printf("  -n, --name      print a name instead of a number (with -ugG)\n");
    printf("  -r, --real      print the real ID instead of the effective ID (with -ugG)\n");
    printf("  -u, --user      print only the effective user ID\n");
    printf("  -z, --zero      delimit entries with NUL, not whitespace (with -G)\n");
    printf("      --help      display this help and exit\n");
    printf("      --version   output version information and exit\n\n");
    printf("Without any OPTION, print full set of user and group information.\n");
}

/* Print version information */
static void print_version(void) {
    printf("%s %s\n", PROGRAM_NAME, VERSION);
    printf("Copyright (C) 2026\n");
    printf("This is free software. You may redistribute copies of it\n");
    printf("under the terms of the GNU General Public License.\n");
}

/* Error handling */
static void die(const char *msg) {
    fprintf(stderr, "%s: %s\n", PROGRAM_NAME, msg);
    exit(EXIT_FAILURE);
}

/* Parse command line options */
static void parse_options(int argc, char *argv[], struct id_options *opts) {
    static struct option long_options[] = {
        {"group",   no_argument, 0, 'g'},
        {"groups",  no_argument, 0, 'G'},
        {"name",    no_argument, 0, 'n'},
        {"real",    no_argument, 0, 'r'},
        {"user",    no_argument, 0, 'u'},
        {"zero",    no_argument, 0, 'z'},
        {"help",    no_argument, 0, 256},
        {"version", no_argument, 0, 257},
        {0, 0, 0, 0}
    };
    
    int c;
    
    /* Initialize options */
    memset(opts, 0, sizeof(struct id_options));
    
    while ((c = getopt_long(argc, argv, "gGnruz", long_options, NULL)) != -1) {
        switch (c) {
            case 'g':
                opts->group = 1;
                break;
            case 'G':
                opts->groups = 1;
                break;
            case 'n':
                opts->name = 1;
                break;
            case 'r':
                opts->real = 1;
                break;
            case 'u':
                opts->user = 1;
                break;
            case 'z':
                opts->zero = 1;
                break;
            case 256:
                opts->help = 1;
                break;
            case 257:
                opts->version = 1;
                break;
            case '?':
                /* getopt_long already printed an error */
                exit(EXIT_FAILURE);
            default:
                fprintf(stderr, "Try '%s --help' for more information.\n", PROGRAM_NAME);
                exit(EXIT_FAILURE);
        }
    }
    
    /* Handle username argument */
    if (optind < argc) {
        opts->username = argv[optind];
        if (optind + 1 < argc) {
            fprintf(stderr, "%s: extra operand '%s'\n", PROGRAM_NAME, argv[optind + 1]);
            fprintf(stderr, "Try '%s --help' for more information.\n", PROGRAM_NAME);
            exit(EXIT_FAILURE);
        }
    }
    
    /* Check for option conflicts */
    if (opts->group + opts->groups + opts->user > 1) {
        die("cannot print only user and only group and only groups");
    }
    
    if (opts->name && !(opts->group || opts->groups || opts->user)) {
        die("option --name can only be used with -u, -g, or -G");
    }
    
    if (opts->real && !(opts->group || opts->user)) {
        die("option --real can only be used with -u or -g");
    }
    
    if (opts->zero && !opts->groups) {
        die("option --zero can only be used with -G");
    }
}

/* Get user name from UID */
static char *get_user_name(uid_t uid) {
    struct passwd *pwd = getpwuid(uid);
    return pwd ? pwd->pw_name : NULL;
}

/* Get group name from GID */
static char *get_group_name(gid_t gid) {
    struct group *grp = getgrgid(gid);
    return grp ? grp->gr_name : NULL;
}

/* Get UID for username (real or effective) */
static uid_t get_user_uid(const char *username, int real) {
    if (!username) {
        return real ? getuid() : geteuid();
    }
    
    struct passwd *pwd = getpwnam(username);
    if (!pwd) {
        die("invalid user");
    }
    return pwd->pw_uid;
}

/* Get GID for username (real or effective) */
static gid_t get_user_gid(const char *username, int real) {
    if (!username) {
        return real ? getgid() : getegid();
    }
    
    struct passwd *pwd = getpwnam(username);
    if (!pwd) {
        die("invalid user");
    }
    return pwd->pw_gid;
}

/* Get group list for user */
static int get_group_list(const char *username, gid_t *groups, int max_groups) {
    int ngroups = max_groups;
    
    if (!username) {
        /* Current user - get supplementary groups */
        if (getgrouplist(get_user_name(getuid()), getgid(), groups, &ngroups) == -1) {
            return -1;
        }
    } else {
        /* Specified user - get from password entry */
        struct passwd *pwd = getpwnam(username);
        if (!pwd) {
            return -1;
        }
        
        if (getgrouplist(username, pwd->pw_gid, groups, &ngroups) == -1) {
            return -1;
        }
    }
    
    return ngroups;
}

/* Print only group IDs */
static void print_groups_only(gid_t *groups, int ngroups, int zero) {
    char separator = zero ? '\0' : ' ';
    int i;
    
    for (i = 0; i < ngroups; i++) {
        if (i > 0) {
            putchar(separator);
        }
        printf("%d", groups[i]);
    }
    putchar(zero ? '\0' : '\n');
}

/* Print full user information */
static void print_all_info(uid_t uid, gid_t gid, const char *username,
                          gid_t *groups, int ngroups) {
    char *user_name = get_user_name(uid);
    char *group_name = get_group_name(gid);
    int i;
    
    /* Print user information */
    printf("uid=%d(%s) gid=%d(%s)", 
           uid, user_name ? user_name : "?", 
           gid, group_name ? group_name : "?");
    
    /* Print supplementary groups */
    if (ngroups > 0) {
        printf(" groups=");
        for (i = 0; i < ngroups; i++) {
            if (i > 0) {
                putchar(',');
            }
            char *grp_name = get_group_name(groups[i]);
            printf("%d(%s)", groups[i], grp_name ? grp_name : "?");
        }
    }
    
    /* Print context if available (simplified - SELinux context not implemented) */
    /* Standard id doesn't always print context */
    
    putchar('\n');
}

/* Main function to print user info based on options */
static void print_user_info(struct id_options *opts) {
    uid_t uid;
    gid_t gid;
    gid_t *groups = NULL;
    int ngroups = 0;
    int max_groups;
    char *user_name = NULL;
    
    /* Handle --help and --version */
    if (opts->help) {
        print_help();
        exit(EXIT_SUCCESS);
    }
    
    if (opts->version) {
        print_version();
        exit(EXIT_SUCCESS);
    }
    
    /* Get group list size */
    max_groups = sysconf(_SC_NGROUPS_MAX);
    if (max_groups < 0) {
        max_groups = 64; /* Fallback value */
    }
    
    groups = malloc(max_groups * sizeof(gid_t));
    if (!groups) {
        die("memory allocation failed");
    }
    
    /* Get supplementary groups */
    ngroups = get_group_list(opts->username, groups, max_groups);
    if (ngroups < 0) {
        free(groups);
        die("failed to get group list");
    }
    
    /* Get user name for specified user if needed */
    if (opts->username) {
        struct passwd *pwd = getpwnam(opts->username);
        if (!pwd) {
            free(groups);
            die("invalid user");
        }
        uid = pwd->pw_uid;
        gid = pwd->pw_gid;
        user_name = pwd->pw_name;
    }
    
    /* Handle specific output modes */
    if (opts->user) {
        uid = get_user_uid(opts->username, opts->real);
        if (opts->name) {
            char *name = get_user_name(uid);
            printf("%s\n", name ? name : "?");
        } else {
            printf("%d\n", uid);
        }
    } else if (opts->group) {
        gid = get_user_gid(opts->username, opts->real);
        if (opts->name) {
            char *name = get_group_name(gid);
            printf("%s\n", name ? name : "?");
        } else {
            printf("%d\n", gid);
        }
    } else if (opts->groups) {
        /* For -G, include the primary group as well */
        gid_t *all_groups = malloc((ngroups + 1) * sizeof(gid_t));
        if (!all_groups) {
            free(groups);
            die("memory allocation failed");
        }
        
        /* Add primary group first (except when showing real IDs? */
        gid_t primary_gid = get_user_gid(opts->username, 0);
        all_groups[0] = primary_gid;
        memcpy(all_groups + 1, groups, ngroups * sizeof(gid_t));
        
        if (opts->name) {
            int i;
            char separator = opts->zero ? '\0' : ' ';
            for (i = 0; i <= ngroups; i++) {
                if (i > 0) {
                    putchar(separator);
                }
                char *name = get_group_name(all_groups[i]);
                printf("%s", name ? name : "?");
            }
            putchar(opts->zero ? '\0' : '\n');
        } else {
            print_groups_only(all_groups, ngroups + 1, opts->zero);
        }
        
        free(all_groups);
    } else {
        /* Default: print all information */
        if (opts->username) {
            uid = get_user_uid(opts->username, 0);
            gid = get_user_gid(opts->username, 0);
        } else {
            uid = geteuid();
            gid = getegid();
        }
        print_all_info(uid, gid, user_name, groups, ngroups);
    }
    
    free(groups);
}

int main(int argc, char *argv[]) {
    struct id_options opts;
    
    parse_options(argc, argv, &opts);
    print_user_info(&opts);
    
    return EXIT_SUCCESS;
}
