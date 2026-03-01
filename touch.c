#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utime.h>
#include <time.h>
#include <errno.h>
#include <getopt.h>

#define VERSION "1.0"
#define AUTHORS "Your Name"

/* Structure to hold command line options */
struct touch_options {
    int change_access;      /* -a: change only access time */
    int change_modification; /* -m: change only modification time */
    int no_create;          /* -c: do not create any files */
    int date_set;           /* -d: parse string and use as time */
    char *date_str;         /* date string for -d option */
    int ref_file_set;       /* -r: use time from reference file */
    char *ref_file;         /* reference file for -r option */
    int timestamp_set;      /* -t: use [[CC]YY]MMDDhhmm[.ss] time */
    char *timestamp;        /* timestamp for -t option */
    int time_set;           /* --time: change only specified time */
    char *time_type;        /* time type for --time option */
};

/* Function prototypes */
void print_usage(void);
void print_version(void);
int parse_arguments(int argc, char *argv[], struct touch_options *opts, char ***files, int *file_count);
int process_file(const char *file, struct touch_options *opts);
int update_times(const char *file, struct touch_options *opts, struct stat *st);
int parse_date_string(const char *date_str, time_t *result);
int parse_timestamp(const char *timestamp, time_t *result);
time_t get_reference_time(const char *ref_file, struct touch_options *opts);
void print_error(const char *msg, const char *file);

int main(int argc, char *argv[]) {
    struct touch_options opts;
    char **files = NULL;
    int file_count = 0;
    int exit_status = EXIT_SUCCESS;
    int i;
    
    /* Initialize options */
    memset(&opts, 0, sizeof(opts));
    
    /* Parse command line arguments */
    if (parse_arguments(argc, argv, &opts, &files, &file_count) != 0) {
        exit_status = EXIT_FAILURE;
        goto cleanup;
    }
    
    /* If no files specified, show usage */
    if (file_count == 0) {
        print_usage();
        exit_status = EXIT_FAILURE;
        goto cleanup;
    }
    
    /* Process each file */
    for (i = 0; i < file_count; i++) {
        if (process_file(files[i], &opts) != 0) {
            exit_status = EXIT_FAILURE;
        }
    }
    
cleanup:
    /* Free allocated memory */
    if (files) {
        for (i = 0; i < file_count; i++) {
            free(files[i]);
        }
        free(files);
    }
    
    return exit_status;
}

void print_usage(void) {
    fprintf(stderr, "Usage: touch [OPTION]... FILE...\n");
    fprintf(stderr, "Update the access and modification times of each FILE to the current time.\n\n");
    fprintf(stderr, "Mandatory arguments to long options are mandatory for short options too.\n");
    fprintf(stderr, "  -a                     change only the access time\n");
    fprintf(stderr, "  -c, --no-create        do not create any files\n");
    fprintf(stderr, "  -d, --date=STRING      parse STRING and use it instead of current time\n");
    fprintf(stderr, "  -f                     (ignored)\n");
    fprintf(stderr, "  -m                     change only the modification time\n");
    fprintf(stderr, "  -r, --reference=FILE   use this file's times instead of current time\n");
    fprintf(stderr, "  -t STAMP               use [[CC]YY]MMDDhhmm[.ss] instead of current time\n");
    fprintf(stderr, "      --time=WORD        change the specified time: WORD is access, atime, or use\n");
    fprintf(stderr, "                           (equivalent to -a) or modify, mtime (equivalent to -m)\n");
    fprintf(stderr, "      --help     display this help and exit\n");
    fprintf(stderr, "      --version  output version information and exit\n\n");
    fprintf(stderr, "Note that the -d and -t options accept different time-date formats.\n");
}

void print_version(void) {
    fprintf(stderr, "touch (GNU coreutils) %s\n", VERSION);
    fprintf(stderr, "Copyright (C) 2024 Free Software Foundation, Inc.\n");
    fprintf(stderr, "License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>.\n");
    fprintf(stderr, "This is free software: you are free to change and redistribute it.\n");
    fprintf(stderr, "There is NO WARRANTY, to the extent permitted by law.\n\n");
    fprintf(stderr, "Written by %s.\n", AUTHORS);
}

int parse_arguments(int argc, char *argv[], struct touch_options *opts, char ***files, int *file_count) {
    int c;
    int option_index = 0;
    
    static struct option long_options[] = {
        {"no-create", no_argument, 0, 'c'},
        {"date", required_argument, 0, 'd'},
        {"reference", required_argument, 0, 'r'},
        {"time", required_argument, 0, 1},
        {"help", no_argument, 0, 2},
        {"version", no_argument, 0, 3},
        {0, 0, 0, 0}
    };
    
    while (1) {
        c = getopt_long(argc, argv, "acd:fmr:t:", long_options, &option_index);
        
        if (c == -1)
            break;
            
        switch (c) {
            case 'a':
                opts->change_access = 1;
                break;
                
            case 'c':
                opts->no_create = 1;
                break;
                
            case 'd':
                opts->date_set = 1;
                opts->date_str = optarg;
                break;
                
            case 'f':
                /* Ignored for compatibility */
                break;
                
            case 'm':
                opts->change_modification = 1;
                break;
                
            case 'r':
                opts->ref_file_set = 1;
                opts->ref_file = optarg;
                break;
                
            case 't':
                opts->timestamp_set = 1;
                opts->timestamp = optarg;
                break;
                
            case 1: /* --time */
                opts->time_set = 1;
                opts->time_type = optarg;
                break;
                
            case 2: /* --help */
                print_usage();
                exit(EXIT_SUCCESS);
                
            case 3: /* --version */
                print_version();
                exit(EXIT_SUCCESS);
                
            case '?':
                /* getopt_long already printed an error message */
                return -1;
                
            default:
                fprintf(stderr, "Unknown option\n");
                return -1;
        }
    }
    
    /* Check for mutually exclusive options */
    int time_sources = (opts->date_set + opts->ref_file_set + opts->timestamp_set);
    if (time_sources > 1) {
        fprintf(stderr, "touch: cannot specify times from more than one source\n");
        fprintf(stderr, "Try 'touch --help' for more information.\n");
        return -1;
    }
    
    /* Handle --time option */
    if (opts->time_set) {
        if (strcmp(opts->time_type, "access") == 0 || 
            strcmp(opts->time_type, "atime") == 0 ||
            strcmp(opts->time_type, "use") == 0) {
            opts->change_access = 1;
        } else if (strcmp(opts->time_type, "modify") == 0 ||
                   strcmp(opts->time_type, "mtime") == 0) {
            opts->change_modification = 1;
        } else {
            fprintf(stderr, "touch: invalid argument '%s' for '--time'\n", opts->time_type);
            fprintf(stderr, "Valid arguments are:\n");
            fprintf(stderr, "  - 'access', 'atime', 'use' - change only the access time\n");
            fprintf(stderr, "  - 'modify', 'mtime' - change only the modification time\n");
            return -1;
        }
    }
    
    /* If neither -a nor -m specified, change both times */
    if (!opts->change_access && !opts->change_modification) {
        opts->change_access = 1;
        opts->change_modification = 1;
    }
    
    /* Collect file names */
    if (optind < argc) {
        *file_count = argc - optind;
        *files = (char **)malloc(*file_count * sizeof(char *));
        if (!*files) {
            fprintf(stderr, "touch: memory allocation failed\n");
            return -1;
        }
        
        for (int i = 0; i < *file_count; i++) {
            (*files)[i] = strdup(argv[optind + i]);
            if (!(*files)[i]) {
                fprintf(stderr, "touch: memory allocation failed\n");
                return -1;
            }
        }
    }
    
    return 0;
}

int process_file(const char *file, struct touch_options *opts) {
    struct stat st;
    int fd;
    
    /* Check if file exists */
    if (stat(file, &st) == 0) {
        /* File exists, update times */
        return update_times(file, opts, &st);
    } else {
        /* File doesn't exist */
        if (opts->no_create) {
            /* -c option: don't create file */
            return 0;
        }
        
        /* Create empty file */
        fd = open(file, O_WRONLY | O_CREAT, 0666);
        if (fd == -1) {
            print_error("cannot create file", file);
            return -1;
        }
        close(fd);
        
        /* Get new file stats */
        if (stat(file, &st) != 0) {
            print_error("cannot stat newly created file", file);
            return -1;
        }
        
        /* Update times if needed (for newly created files, times are already current) */
        if (opts->date_set || opts->ref_file_set || opts->timestamp_set) {
            return update_times(file, opts, &st);
        }
    }
    
    return 0;
}

int update_times(const char *file, struct touch_options *opts, struct stat *st) {
    struct utimbuf new_times;
    time_t current_time;
    time_t new_time;
    
    /* Get current time */
    current_time = time(NULL);
    
    /* Initialize new times with current file times */
    new_times.actime = st->st_atime;
    new_times.modtime = st->st_mtime;
    
    /* Determine new time value */
    if (opts->date_set) {
        /* Parse date string */
        if (parse_date_string(opts->date_str, &new_time) != 0) {
            print_error("invalid date format", file);
            return -1;
        }
    } else if (opts->ref_file_set) {
        /* Get time from reference file */
        new_time = get_reference_time(opts->ref_file, opts);
        if (new_time == (time_t)-1) {
            return -1;
        }
    } else if (opts->timestamp_set) {
        /* Parse timestamp */
        if (parse_timestamp(opts->timestamp, &new_time) != 0) {
            print_error("invalid timestamp format", file);
            return -1;
        }
    } else {
        /* Use current time */
        new_time = current_time;
    }
    
    /* Update appropriate times */
    if (opts->change_access) {
        new_times.actime = new_time;
    }
    if (opts->change_modification) {
        new_times.modtime = new_time;
    }
    
    /* Update file times */
    if (utime(file, &new_times) != 0) {
        print_error("cannot update times", file);
        return -1;
    }
    
    return 0;
}

time_t get_reference_time(const char *ref_file, struct touch_options *opts) {
    struct stat ref_st;
    
    if (stat(ref_file, &ref_st) != 0) {
        print_error("cannot stat reference file", ref_file);
        return (time_t)-1;
    }
    
    /* Return appropriate time based on options */
    if (opts->change_access && !opts->change_modification) {
        return ref_st.st_atime;
    } else if (!opts->change_access && opts->change_modification) {
        return ref_st.st_mtime;
    } else {
        /* If both or neither, use modification time (standard behavior) */
        return ref_st.st_mtime;
    }
}

int parse_date_string(const char *date_str, time_t *result) {
    /* Simple date parsing - this is a basic implementation */
    struct tm tm_time;
    time_t current_time;
    struct tm *current_tm;
    
    /* Get current time as base */
    current_time = time(NULL);
    current_tm = localtime(&current_time);
    
    /* Copy current time as default */
    memcpy(&tm_time, current_tm, sizeof(struct tm));
    
    /* Try to parse common formats */
    /* Format: YYYY-MM-DD HH:MM:SS */
    if (sscanf(date_str, "%d-%d-%d %d:%d:%d", 
               &tm_time.tm_year, &tm_time.tm_mon, &tm_time.tm_mday,
               &tm_time.tm_hour, &tm_time.tm_min, &tm_time.tm_sec) == 6) {
        tm_time.tm_year -= 1900;  /* Adjust year */
        tm_time.tm_mon -= 1;       /* Adjust month */
        *result = mktime(&tm_time);
        return 0;
    }
    
    /* Format: @seconds (seconds since epoch) */
    if (date_str[0] == '@') {
        *result = (time_t)atol(date_str + 1);
        return 0;
    }
    
    /* Could not parse */
    return -1;
}

int parse_timestamp(const char *timestamp, time_t *result) {
    /* Parse timestamp in format: [[CC]YY]MMDDhhmm[.ss] */
    struct tm tm_time;
    time_t current_time;
    struct tm *current_tm;
    int century = 0, year = 0, month, day, hour, min, sec;
    int len;
    int items_scanned;
    
    /* Get current time as base */
    current_time = time(NULL);
    current_tm = localtime(&current_time);
    
    /* Initialize with current time */
    memcpy(&tm_time, current_tm, sizeof(struct tm));
    
    len = strlen(timestamp);
    sec = 0;
    
    /* Check if we have seconds (format includes .ss) */
    if (strchr(timestamp, '.') != NULL) {
        /* With seconds */
        if (len > 11) {
            /* Format: CCYYMMDDhhmm.ss (13+ chars) */
            items_scanned = sscanf(timestamp, "%2d%2d%2d%2d%2d%2d.%2d", 
                                   &century, &year, &month, &day, &hour, &min, &sec);
            if (items_scanned == 7) {
                tm_time.tm_year = (century * 100 + year) - 1900;
            } else {
                return -1;
            }
        } else if (len > 9) {
            /* Format: YYMMDDhhmm.ss (11+ chars) */
            items_scanned = sscanf(timestamp, "%2d%2d%2d%2d%2d.%2d", 
                                   &year, &month, &day, &hour, &min, &sec);
            if (items_scanned == 6) {
                /* Use current century */
                century = (current_tm->tm_year + 1900) / 100;
                tm_time.tm_year = (century * 100 + year) - 1900;
            } else {
                return -1;
            }
        } else {
            /* Format: MMDDhhmm.ss (9+ chars) - use current year */
            items_scanned = sscanf(timestamp, "%2d%2d%2d%2d.%2d", 
                                   &month, &day, &hour, &min, &sec);
            if (items_scanned == 5) {
                tm_time.tm_year = current_tm->tm_year;
            } else {
                return -1;
            }
        }
    } else {
        /* Without seconds */
        if (len > 10) {
            /* Format: CCYYMMDDhhmm (12 chars) */
            items_scanned = sscanf(timestamp, "%2d%2d%2d%2d%2d%2d", 
                                   &century, &year, &month, &day, &hour, &min);
            if (items_scanned == 6) {
                tm_time.tm_year = (century * 100 + year) - 1900;
            } else {
                return -1;
            }
        } else if (len > 8) {
            /* Format: YYMMDDhhmm (10 chars) */
            items_scanned = sscanf(timestamp, "%2d%2d%2d%2d%2d", 
                                   &year, &month, &day, &hour, &min);
            if (items_scanned == 5) {
                /* Use current century */
                century = (current_tm->tm_year + 1900) / 100;
                tm_time.tm_year = (century * 100 + year) - 1900;
            } else {
                return -1;
            }
        } else {
            /* Format: MMDDhhmm (8 chars) - use current year */
            items_scanned = sscanf(timestamp, "%2d%2d%2d%2d", 
                                   &month, &day, &hour, &min);
            if (items_scanned == 4) {
                tm_time.tm_year = current_tm->tm_year;
            } else {
                return -1;
            }
        }
    }
    
    /* Validate ranges */
    if (month < 1 || month > 12 || day < 1 || day > 31 || 
        hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 59) {
        return -1;
    }
    
    /* Set time values */
    tm_time.tm_mon = month - 1;
    tm_time.tm_mday = day;
    tm_time.tm_hour = hour;
    tm_time.tm_min = min;
    tm_time.tm_sec = sec;
    tm_time.tm_isdst = -1;  /* Let mktime determine DST */
    
    *result = mktime(&tm_time);
    return 0;
}

void print_error(const char *msg, const char *file) {
    fprintf(stderr, "touch: %s '%s': %s\n", msg, file, strerror(errno));
}
