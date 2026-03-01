#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdbool.h>
#include <sys/ioctl.h>  /* For TIOCGWINSZ and struct winsize */
#include <sys/sysmacros.h>  /* For minor/major macros if needed */

#define MAX_PATH 4096
#define MAX_NAME 256
#define BLOCK_SIZE 512

/* ANSI color codes for file types */
#define COLOR_RESET   "\033[0m"
#define COLOR_BLACK   "\033[30m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_WHITE   "\033[37m"
#define COLOR_BOLD    "\033[1m"

typedef struct {
    bool all;           /* -a: include dot files */
    bool almost_all;    /* -A: include all except . and .. */
    bool long_format;   /* -l: long format */
    bool one_per_line;  /* -1: one entry per line */
    bool recursive;     /* -R: recursive */
    bool reverse;       /* -r: reverse order */
    bool sort_time;     /* -t: sort by time */
    bool sort_size;     /* -S: sort by size */
    bool human_readable; /* -h: human readable sizes */
    bool inode;         /* -i: show inode numbers */
    bool numeric_uid_gid; /* -n: numeric uid/gid */
    bool color;         /* --color: colorize output */
    bool classify;      /* -F: classify files with symbols */
    bool directory;     /* -d: list directory entries */
    bool indicator;     /* -p: add / to directories */
    bool quote_name;    /* -Q: quote entry names */
    bool no_group;      /* -G: don't show group */
    bool show_control;  /* -b: show control chars */
    bool escape;        /* -E: escape non-printable chars */
    bool ignore_backups; /* -B: ignore backup files */
    bool dereference;   /* -L: follow symlinks */
    bool sort_none;     /* -f: no sort, enable -aU */
    bool blocksize;     /* -s: show allocated blocks */
    bool file_type;     /* --file-type */
    int mod_time;       /* modification time type: 0=mtime, 1=atime, 2=ctime */
} LS_Options;

/* Structure to hold file information */
typedef struct {
    char name[MAX_PATH];
    char fullpath[MAX_PATH];
    struct stat st;
    char link_target[MAX_PATH];
    bool is_link;
} FileInfo;

/* Function prototypes */
void parse_options(int argc, char *argv[], LS_Options *opts, char ***files, int *file_count);
int compare_files(const void *a, const void *b, LS_Options *opts);
void sort_files(FileInfo **files, int count, LS_Options *opts);
void list_directory(const char *path, LS_Options *opts, bool is_top_level);
void list_file(const char *path, LS_Options *opts);
void format_long_output(FileInfo *file, LS_Options *opts);
void format_short_output(FileInfo **files, int count, LS_Options *opts);
char *get_permissions(mode_t mode);
char *get_user(uid_t uid, bool numeric);
char *get_group(gid_t gid, bool numeric);
char *get_time(time_t mod_time);
char *get_size(off_t size, bool human_readable);
void get_file_info(const char *path, const char *name, FileInfo *info, LS_Options *opts);
void colorize_output(const char *name, mode_t mode, bool is_link);
void free_file_list(FileInfo **files, int count);
bool should_ignore(const char *name, LS_Options *opts);
void print_escaped_string(const char *str, bool quote, bool escape);

/* Default options */
LS_Options default_options = {
    .all = false,
    .almost_all = false,
    .long_format = false,
    .one_per_line = false,
    .recursive = false,
    .reverse = false,
    .sort_time = false,
    .sort_size = false,
    .human_readable = false,
    .inode = false,
    .numeric_uid_gid = false,
    .color = false,
    .classify = false,
    .directory = false,
    .indicator = false,
    .quote_name = false,
    .no_group = false,
    .show_control = false,
    .escape = false,
    .ignore_backups = false,
    .dereference = false,
    .sort_none = false,
    .blocksize = false,
    .file_type = false,
    .mod_time = 0
};

/* Long options */
static struct option long_options[] = {
    {"all", no_argument, 0, 'a'},
    {"almost-all", no_argument, 0, 'A'},
    {"author", no_argument, 0, 128},
    {"block-size", required_argument, 0, 129},
    {"color", optional_argument, 0, 130},
    {"directory", no_argument, 0, 'd'},
    {"dereference", no_argument, 0, 'L'},
    {"file-type", no_argument, 0, 131},
    {"format", required_argument, 0, 132},
    {"full-time", no_argument, 0, 133},
    {"group-directories-first", no_argument, 0, 134},
    {"help", no_argument, 0, 135},  /* Changed from 'h' to 135 */
    {"human-readable", no_argument, 0, 'h'},
    {"inode", no_argument, 0, 'i'},
    {"ignore-backups", no_argument, 0, 'B'},
    {"literal", no_argument, 0, 136},
    {"numeric-uid-gid", no_argument, 0, 'n'},
    {"quote-name", no_argument, 0, 'Q'},
    {"recursive", no_argument, 0, 'R'},
    {"reverse", no_argument, 0, 'r'},
    {"size", no_argument, 0, 's'},
    {"sort", required_argument, 0, 137},
    {"time", required_argument, 0, 138},
    {"time-style", required_argument, 0, 139},
    {"version", no_argument, 0, 140},
    {0, 0, 0, 0}
};

/* Main function */
int main(int argc, char *argv[]) {
    LS_Options opts = default_options;
    char **files = NULL;
    int file_count = 0;
    int exit_status = 0;

    /* Parse command line options */
    parse_options(argc, argv, &opts, &files, &file_count);

    /* If no files specified, use current directory */
    if (file_count == 0) {
        files = malloc(sizeof(char *));
        files[0] = strdup(".");
        file_count = 1;
    }

    /* Process each file/directory */
    for (int i = 0; i < file_count; i++) {
        struct stat st;
        int stat_result;

        if (opts.dereference) {
            stat_result = stat(files[i], &st);
        } else {
            stat_result = lstat(files[i], &st);
        }

        if (stat_result < 0) {
            fprintf(stderr, "ls: cannot access '%s': %s\n", 
                    files[i], strerror(errno));
            exit_status = 1;
            continue;
        }

        /* If it's a directory and we're not in directory mode, list contents */
        if (S_ISDIR(st.st_mode) && !opts.directory) {
            if (file_count > 1) {
                printf("%s:\n", files[i]);
            }
            list_directory(files[i], &opts, true);
            if (i < file_count - 1) {
                printf("\n");
            }
        } else {
            /* It's a file or we're in directory mode */
            list_file(files[i], &opts);
        }
    }

    /* Cleanup */
    for (int i = 0; i < file_count; i++) {
        free(files[i]);
    }
    free(files);

    return exit_status;
}

/* Parse command line options */
void parse_options(int argc, char *argv[], LS_Options *opts, char ***files, int *file_count) {
    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "1AaBbcCdFfGgHhiIklLmnOoPpQqRrSstUuVvWwXxZ",
                              long_options, &option_index)) != -1) {
        switch (opt) {
            case 'a':
                opts->all = true;
                break;
            case 'A':
                opts->almost_all = true;
                break;
            case 'B':
                opts->ignore_backups = true;
                break;
            case 'b':
                opts->escape = true;
                break;
            case 'C':
                /* Column output (default) - no action needed */
                break;
            case 'c':
                /* Use ctime for sorting */
                opts->mod_time = 2;
                break;
            case 'd':
                opts->directory = true;
                break;
            case 'F':
                opts->classify = true;
                break;
            case 'f':
                opts->sort_none = true;
                opts->all = true;
                break;
            case 'G':
                opts->no_group = true;
                break;
            case 'g':
                /* Similar to -l but no owner */
                opts->long_format = true;
                opts->no_group = false; /* This is for -g which suppresses owner */
                break;
            case 'h':
                opts->human_readable = true;
                break;
            case 'i':
                opts->inode = true;
                break;
            case 'k':
                /* Use 1024-byte blocks (implied by -h sometimes) */
                break;
            case 'L':
                opts->dereference = true;
                break;
            case 'l':
                opts->long_format = true;
                break;
            case 'm':
                /* Comma-separated output - not implemented */
                break;
            case 'n':
                opts->numeric_uid_gid = true;
                opts->long_format = true;
                break;
            case 'o':
                /* Like -l but no group */
                opts->long_format = true;
                opts->no_group = true;
                break;
            case 'p':
                opts->indicator = true;
                break;
            case 'Q':
                opts->quote_name = true;
                break;
            case 'q':
                /* Print ? for non-printable - not implemented */
                break;
            case 'R':
                opts->recursive = true;
                break;
            case 'r':
                opts->reverse = true;
                break;
            case 'S':
                opts->sort_size = true;
                break;
            case 's':
                opts->blocksize = true;
                break;
            case 't':
                opts->sort_time = true;
                break;
            case 'u':
                /* Use access time for sorting */
                opts->mod_time = 1;
                break;
            case 'U':
                /* Use creation time - not implemented */
                break;
            case 'v':
                /* Natural sort of numbers - not implemented */
                break;
            case 'w':
                /* Width for formatting - not implemented */
                break;
            case 'x':
                /* Sort across, not down - not implemented */
                break;
            case 'X':
                /* Sort by extension - not implemented */
                break;
            case 'Z':
                /* Display SELinux context - not implemented */
                break;
            case '1':
                opts->one_per_line = true;
                break;
            case 128: /* --author */
                /* Print author - not implemented */
                break;
            case 130: /* --color */
                if (optarg) {
                    if (strcmp(optarg, "always") == 0 || strcmp(optarg, "yes") == 0) {
                        opts->color = true;
                    } else if (strcmp(optarg, "auto") == 0) {
                        opts->color = isatty(STDOUT_FILENO);
                    } else {
                        opts->color = false;
                    }
                } else {
                    opts->color = true;
                }
                break;
            case 131: /* --file-type */
                opts->file_type = true;
                break;
            case 135: /* --help */
                printf("Usage: %s [OPTION]... [FILE]...\n", argv[0]);
                printf("List information about the FILEs (the current directory by default).\n");
                printf("Common options:\n");
                printf("  -a, --all                  do not ignore entries starting with .\n");
                printf("  -A, --almost-all            do not list implied . and ..\n");
                printf("  -b, --escape                print C-style escapes for nongraphic characters\n");
                printf("  -B, --ignore-backups        do not list implied entries ending with ~\n");
                printf("  -c                         with -lt: sort by, and show, ctime\n");
                printf("  -d, --directory             list directories themselves, not their contents\n");
                printf("  -f                         do not sort, enable -aU, disable -ls --color\n");
                printf("  -F, --classify              append indicator (one of */=>@|) to entries\n");
                printf("  -g                         like -l, but do not list owner\n");
                printf("  -G, --no-group              in a long listing, don't print group names\n");
                printf("  -h, --human-readable        with -l and -s, print sizes like 1K 234M 2G etc.\n");
                printf("  -i, --inode                  print the index number of each file\n");
                printf("  -l                          use a long listing format\n");
                printf("  -L, --dereference            when showing file information for a symbolic\n");
                printf("                               link, show information for the file the link\n");
                printf("                               references rather than for the link itself\n");
                printf("  -n, --numeric-uid-gid        like -l, but list numeric user and group IDs\n");
                printf("  -o                         like -l, but do not list group information\n");
                printf("  -p, --indicator              append / indicator to directories\n");
                printf("  -q                         print ? for non-graphic characters\n");
                printf("  -Q, --quote-name             enclose entry names in double quotes\n");
                printf("  -r, --reverse                reverse order while sorting\n");
                printf("  -R, --recursive               list subdirectories recursively\n");
                printf("  -s, --size                    print the allocated size of each file, in blocks\n");
                printf("  -S                          sort by file size, largest first\n");
                printf("  -t                          sort by modification time, newest first\n");
                printf("  -u                         with -lt: sort by, and show, access time\n");
                printf("  -U                         do not sort; list entries in directory order\n");
                printf("  -v                         natural sort of (version) numbers within text\n");
                printf("  -x                         list entries by lines instead of by columns\n");
                printf("  -X                         sort alphabetically by entry extension\n");
                printf("  -1                         list one file per line\n");
                printf("      --color[=WHEN]          colorize the output; WHEN can be 'always' (default if omitted), 'auto', or 'never'\n");
                printf("      --help                   display this help and exit\n");
                printf("      --version                output version information and exit\n");
                exit(0);
                break;
            case 140: /* --version */
                printf("ls (custom implementation) 1.0\n");
                printf("Copyright (C) 2024 Custom LS\n");
                printf("This is free software: you are free to change and redistribute it.\n");
                exit(0);
                break;
            case '?':
                /* Invalid option */
                fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
                exit(1);
                break;
            default:
                /* Handle any other options */
                break;
        }
    }

    /* Collect remaining arguments as files/directories to list */
    *file_count = argc - optind;
    if (*file_count > 0) {
        *files = malloc(sizeof(char *) * (*file_count));
        for (int i = 0; i < *file_count; i++) {
            (*files)[i] = strdup(argv[optind + i]);
        }
    }
}

/* Check if file should be ignored based on options */
bool should_ignore(const char *name, LS_Options *opts) {
    if (opts->all) {
        return false;
    }
    
    if (opts->almost_all) {
        return (strcmp(name, ".") == 0 || strcmp(name, "..") == 0);
    }
    
    if (name[0] == '.') {
        return true;
    }
    
    if (opts->ignore_backups && name[strlen(name) - 1] == '~') {
        return true;
    }
    
    return false;
}

/* Get file information */
void get_file_info(const char *path, const char *name, FileInfo *info, LS_Options *opts) {
    strncpy(info->name, name, MAX_PATH - 1);
    info->name[MAX_PATH - 1] = '\0';
    
    /* Construct full path */
    if (path && strcmp(path, ".") != 0) {
        snprintf(info->fullpath, MAX_PATH - 1, "%s/%s", path, name);
    } else {
        strncpy(info->fullpath, name, MAX_PATH - 1);
        info->fullpath[MAX_PATH - 1] = '\0';
    }
    
    /* Get file status */
    if (opts->dereference) {
        stat(info->fullpath, &info->st);
    } else {
        lstat(info->fullpath, &info->st);
    }
    
    /* Check if it's a symlink */
    info->is_link = false;
    if (!opts->dereference) {
        struct stat link_st;
        if (lstat(info->fullpath, &link_st) == 0 && S_ISLNK(link_st.st_mode)) {
            info->is_link = true;
            ssize_t len = readlink(info->fullpath, info->link_target, MAX_PATH - 1);
            if (len != -1) {
                info->link_target[len] = '\0';
            } else {
                info->link_target[0] = '\0';
            }
        }
    }
}

/* Compare two files for sorting */
int compare_files(const void *a, const void *b, LS_Options *opts) {
    const FileInfo *fa = *(const FileInfo **)a;
    const FileInfo *fb = *(const FileInfo **)b;
    int result = 0;
    
    /* Handle no sorting */
    if (opts->sort_none) {
        return 0;
    }
    
    /* Sort by size */
    if (opts->sort_size) {
        if (fa->st.st_size < fb->st.st_size) result = 1;
        else if (fa->st.st_size > fb->st.st_size) result = -1;
        else result = 0;
    }
    /* Sort by time */
    else if (opts->sort_time) {
        time_t ta, tb;
        
        switch (opts->mod_time) {
            case 1: /* access time */
                ta = fa->st.st_atime;
                tb = fb->st.st_atime;
                break;
            case 2: /* status change time */
                ta = fa->st.st_ctime;
                tb = fb->st.st_ctime;
                break;
            default: /* modification time */
                ta = fa->st.st_mtime;
                tb = fb->st.st_mtime;
                break;
        }
        
        if (ta < tb) result = 1;
        else if (ta > tb) result = -1;
        else result = 0;
    }
    /* Default: sort by name */
    else {
        result = strcmp(fa->name, fb->name);
    }
    
    return opts->reverse ? -result : result;
}

/* Sort files array */
void sort_files(FileInfo **files, int count, LS_Options *opts) {
    if (opts->sort_none) {
        return;
    }
    
    /* Simple bubble sort - for production, you'd want qsort with a proper comparator */
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (compare_files((const void **)&files[i], (const void **)&files[j], opts) > 0) {
                FileInfo *temp = files[i];
                files[i] = files[j];
                files[j] = temp;
            }
        }
    }
}

/* List directory contents */
void list_directory(const char *path, LS_Options *opts, bool is_top_level) {
    DIR *dir;
    struct dirent *entry;
    FileInfo **files = NULL;
    int file_count = 0;
    int file_capacity = 0;
    
    dir = opendir(path);
    if (!dir) {
        fprintf(stderr, "ls: cannot open directory '%s': %s\n", 
                path, strerror(errno));
        return;
    }
    
    /* Read directory entries */
    while ((entry = readdir(dir)) != NULL) {
        /* Check if we should ignore this entry */
        if (should_ignore(entry->d_name, opts)) {
            continue;
        }
        
        /* Allocate space for file info */
        if (file_count >= file_capacity) {
            file_capacity = file_capacity ? file_capacity * 2 : 64;
            files = realloc(files, sizeof(FileInfo *) * file_capacity);
        }
        
        files[file_count] = malloc(sizeof(FileInfo));
        get_file_info(path, entry->d_name, files[file_count], opts);
        file_count++;
    }
    
    closedir(dir);
    
    /* Sort files */
    sort_files(files, file_count, opts);
    
    /* Display files */
    if (opts->long_format) {
        /* Calculate total blocks for -s option if needed */
        if (opts->blocksize) {
            long total_blocks = 0;
            for (int i = 0; i < file_count; i++) {
                total_blocks += files[i]->st.st_blocks;
            }
            printf("total %ld\n", total_blocks / 2); /* Convert 512-byte blocks to 1K blocks */
        }
        
        for (int i = 0; i < file_count; i++) {
            format_long_output(files[i], opts);
        }
    } else {
        format_short_output(files, file_count, opts);
    }
    
    /* Handle recursive listing */
    if (opts->recursive) {
        for (int i = 0; i < file_count; i++) {
            if (S_ISDIR(files[i]->st.st_mode) && 
                strcmp(files[i]->name, ".") != 0 && 
                strcmp(files[i]->name, "..") != 0) {
                printf("\n%s:\n", files[i]->fullpath);
                list_directory(files[i]->fullpath, opts, false);
            }
        }
    }
    
    /* Cleanup */
    free_file_list(files, file_count);
}

/* List a single file */
void list_file(const char *path, LS_Options *opts) {
    FileInfo file;
    get_file_info(NULL, path, &file, opts);
    
    if (opts->long_format) {
        format_long_output(&file, opts);
    } else {
        FileInfo *files[] = {&file};
        format_short_output(files, 1, opts);
    }
}

/* Format long output for a file */
void format_long_output(FileInfo *file, LS_Options *opts) {
    char *perms;
    char *user;
    char *group;
    char *time_str;
    char *size_str;
    
    /* Inode number */
    if (opts->inode) {
        printf("%8lu ", (unsigned long)file->st.st_ino);
    }
    
    /* Blocks */
    if (opts->blocksize) {
        printf("%4ld ", (long)(file->st.st_blocks / 2)); /* Convert to 1K blocks */
    }
    
    /* Permissions */
    perms = get_permissions(file->st.st_mode);
    printf("%s ", perms);
    free(perms);
    
    /* Number of links */
    printf("%2ld ", (long)file->st.st_nlink);
    
    /* Owner */
    user = get_user(file->st.st_uid, opts->numeric_uid_gid);
    printf("%-8s ", user);
    free(user);
    
    /* Group */
    if (!opts->no_group) {
        group = get_group(file->st.st_gid, opts->numeric_uid_gid);
        printf("%-8s ", group);
        free(group);
    }
    
    /* Size */
    size_str = get_size(file->st.st_size, opts->human_readable);
    printf("%8s ", size_str);
    free(size_str);
    
    /* Time */
    time_str = get_time(file->st.st_mtime);
    printf("%s ", time_str);
    free(time_str);
    
    /* Filename */
    if (opts->color) {
        colorize_output(file->name, file->st.st_mode, file->is_link);
    } else {
        print_escaped_string(file->name, opts->quote_name, opts->escape);
    }
    
    /* Classify symbols */
    if (opts->classify || opts->file_type || opts->indicator) {
        if (S_ISDIR(file->st.st_mode)) {
            printf("/");
        } else if (S_ISLNK(file->st.st_mode)) {
            if (opts->classify) printf("@");
        } else if (S_ISFIFO(file->st.st_mode)) {
            if (opts->classify) printf("|");
        } else if (S_ISSOCK(file->st.st_mode)) {
            if (opts->classify) printf("=");
        } else if (file->st.st_mode & S_IXUSR) {
            if (opts->classify || opts->file_type) printf("*");
        }
    }
    
    /* Show link target */
    if (file->is_link && file->link_target[0] != '\0') {
        printf(" -> ");
        print_escaped_string(file->link_target, opts->quote_name, opts->escape);
    }
    
    printf("\n");
}

/* Format short output (columnar or one per line) */
void format_short_output(FileInfo **files, int count, LS_Options *opts) {
    int max_width = 0;
    int cols, rows;
    struct winsize w;
    
    if (opts->one_per_line || count == 0) {
        /* One entry per line */
        for (int i = 0; i < count; i++) {
            if (opts->inode) {
                printf("%8lu ", (unsigned long)files[i]->st.st_ino);
            }
            
            if (opts->blocksize) {
                printf("%4ld ", (long)(files[i]->st.st_blocks / 2));
            }
            
            if (opts->color) {
                colorize_output(files[i]->name, files[i]->st.st_mode, files[i]->is_link);
            } else {
                print_escaped_string(files[i]->name, opts->quote_name, opts->escape);
            }
            
            /* Classify symbols */
            if (opts->classify || opts->file_type || opts->indicator) {
                if (S_ISDIR(files[i]->st.st_mode)) {
                    printf("/");
                } else if (S_ISLNK(files[i]->st.st_mode)) {
                    if (opts->classify) printf("@");
                } else if (S_ISFIFO(files[i]->st.st_mode)) {
                    if (opts->classify) printf("|");
                } else if (S_ISSOCK(files[i]->st.st_mode)) {
                    if (opts->classify) printf("=");
                } else if (files[i]->st.st_mode & S_IXUSR) {
                    if (opts->classify || opts->file_type) printf("*");
                }
            }
            
            printf("\n");
        }
        return;
    }
    
    /* Columnar output */
    /* Find maximum filename width */
    for (int i = 0; i < count; i++) {
        int len = strlen(files[i]->name);
        if (opts->inode) len += 9; /* Space for inode */
        if (opts->blocksize) len += 5; /* Space for blocks */
        if (len > max_width) {
            max_width = len;
        }
    }
    
    /* Add some padding */
    max_width += 2;
    
    /* Get terminal width */
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1 || w.ws_col == 0) {
        w.ws_col = 80; /* Default to 80 columns */
    }
    
    cols = w.ws_col / max_width;
    if (cols < 1) cols = 1;
    rows = (count + cols - 1) / cols;
    
    /* Print in columns */
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int idx = c * rows + r;
            if (idx < count) {
                if (opts->inode) {
                    printf("%8lu ", (unsigned long)files[idx]->st.st_ino);
                }
                
                if (opts->blocksize) {
                    printf("%4ld ", (long)(files[idx]->st.st_blocks / 2));
                }
                
                if (opts->color) {
                    colorize_output(files[idx]->name, files[idx]->st.st_mode, files[idx]->is_link);
                } else {
                    print_escaped_string(files[idx]->name, opts->quote_name, opts->escape);
                }
                
                /* Classify symbols */
                if (opts->classify || opts->file_type || opts->indicator) {
                    if (S_ISDIR(files[idx]->st.st_mode)) {
                        printf("/");
                    } else if (S_ISLNK(files[idx]->st.st_mode)) {
                        if (opts->classify) printf("@");
                    } else if (S_ISFIFO(files[idx]->st.st_mode)) {
                        if (opts->classify) printf("|");
                    } else if (S_ISSOCK(files[idx]->st.st_mode)) {
                        if (opts->classify) printf("=");
                    } else if (files[idx]->st.st_mode & S_IXUSR) {
                        if (opts->classify || opts->file_type) printf("*");
                    }
                }
                
                /* Pad to column width */
                int used = strlen(files[idx]->name);
                if (opts->inode) used += 9;
                if (opts->blocksize) used += 5;
                if (opts->classify || opts->file_type || opts->indicator) used++;
                
                for (int p = used; p < max_width; p++) {
                    printf(" ");
                }
            }
        }
        printf("\n");
    }
}

/* Get permission string */
char *get_permissions(mode_t mode) {
    static const char *rwx[] = {"---", "--x", "-w-", "-wx", "r--", "r-x", "rw-", "rwx"};
    char *perms = malloc(12);
    
    /* File type */
    if (S_ISREG(mode)) perms[0] = '-';
    else if (S_ISDIR(mode)) perms[0] = 'd';
    else if (S_ISCHR(mode)) perms[0] = 'c';
    else if (S_ISBLK(mode)) perms[0] = 'b';
    else if (S_ISFIFO(mode)) perms[0] = 'p';
    else if (S_ISLNK(mode)) perms[0] = 'l';
    else if (S_ISSOCK(mode)) perms[0] = 's';
    else perms[0] = '?';
    
    /* Owner permissions */
    strcpy(&perms[1], rwx[(mode >> 6) & 7]);
    
    /* Group permissions */
    strcpy(&perms[4], rwx[(mode >> 3) & 7]);
    
    /* Other permissions */
    strcpy(&perms[7], rwx[mode & 7]);
    
    /* Special bits */
    perms[10] = ' ';
    if (mode & S_ISUID) {
        perms[3] = (perms[3] == 'x') ? 's' : 'S';
    }
    if (mode & S_ISGID) {
        perms[6] = (perms[6] == 'x') ? 's' : 'S';
    }
    if (mode & S_ISVTX) {
        perms[9] = (perms[9] == 'x') ? 't' : 'T';
    }
    
    perms[11] = '\0';
    return perms;
}

/* Get user name or uid */
char *get_user(uid_t uid, bool numeric) {
    struct passwd *pw;
    char *user = malloc(32);
    
    if (!numeric && (pw = getpwuid(uid)) != NULL) {
        strncpy(user, pw->pw_name, 31);
        user[31] = '\0';
    } else {
        snprintf(user, 31, "%u", uid);
    }
    return user;
}

/* Get group name or gid */
char *get_group(gid_t gid, bool numeric) {
    struct group *gr;
    char *group = malloc(32);
    
    if (!numeric && (gr = getgrgid(gid)) != NULL) {
        strncpy(group, gr->gr_name, 31);
        group[31] = '\0';
    } else {
        snprintf(group, 31, "%u", gid);
    }
    return group;
}

/* Get formatted time string */
char *get_time(time_t mod_time) {
    char *time_str = malloc(32);
    struct tm *tm_info;
    time_t now;
    
    time(&now);
    tm_info = localtime(&mod_time);
    
    /* If file is older than 6 months, show year instead of time */
    if (now - mod_time > 180 * 24 * 60 * 60) {
        strftime(time_str, 31, "%b %d  %Y", tm_info);
    } else {
        strftime(time_str, 31, "%b %d %H:%M", tm_info);
    }
    
    return time_str;
}

/* Get human-readable size */
char *get_size(off_t size, bool human_readable) {
    char *size_str = malloc(32);
    
    if (!human_readable) {
        snprintf(size_str, 31, "%lld", (long long)size);
        return size_str;
    }
    
    /* Human readable sizes */
    const char *units[] = {"B", "K", "M", "G", "T", "P", "E"};
    int unit = 0;
    double human_size = size;
    
    while (human_size >= 1024 && unit < 6) {
        human_size /= 1024;
        unit++;
    }
    
    if (unit == 0) {
        snprintf(size_str, 31, "%lld%s", (long long)size, units[unit]);
    } else {
        snprintf(size_str, 31, "%.1f%s", human_size, units[unit]);
    }
    
    return size_str;
}

/* Colorize output based on file type */
void colorize_output(const char *name, mode_t mode, bool is_link) {
    const char *color = COLOR_RESET;
    
    if (is_link) {
        color = COLOR_CYAN;
    } else if (S_ISDIR(mode)) {
        color = COLOR_BLUE COLOR_BOLD;
    } else if (S_ISFIFO(mode)) {
        color = COLOR_YELLOW;
    } else if (S_ISSOCK(mode)) {
        color = COLOR_MAGENTA;
    } else if (mode & S_IXUSR) {
        color = COLOR_GREEN COLOR_BOLD;
    } else if (S_ISCHR(mode) || S_ISBLK(mode)) {
        color = COLOR_YELLOW COLOR_BOLD;
    }
    
    printf("%s%s%s", color, name, COLOR_RESET);
}

/* Print escaped string */
void print_escaped_string(const char *str, bool quote, bool escape) {
    if (quote) {
        printf("\"");
    }
    
    if (escape) {
        for (const char *c = str; *c; c++) {
            if (*c < 32 || *c >= 127) {
                printf("\\%03o", *c);
            } else {
                putchar(*c);
            }
        }
    } else {
        printf("%s", str);
    }
    
    if (quote) {
        printf("\"");
    }
}

/* Free file list */
void free_file_list(FileInfo **files, int count) {
    for (int i = 0; i < count; i++) {
        free(files[i]);
    }
    free(files);
}
