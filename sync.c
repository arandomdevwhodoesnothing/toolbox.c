/*
 * sync - Force writeback of kernel buffers to disk
 * Copyright (c) 2024
 *
 * Standard features implemented:
 * - Basic sync: sync filesystems
 * - Data sync: --data (-d) sync file data only
 * - File system sync: --file-system (-f) sync filesystems
 * - Help: --help (-h) display help
 * - Version: --version (-V) display version
 * - Error handling with proper exit codes
 * - Multiple file argument support
 */

#define _GNU_SOURCE
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <locale.h>
#include <libintl.h>

#define PACKAGE_NAME "sync"
#define PACKAGE_VERSION "1.0"

/* Exit codes */
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define EXIT_USAGE 2

/* Function prototypes */
static void usage(int status);
static void version(void);
static int sync_files(int argc, char **argv, int data_only, int fs_only);
static int sync_file(const char *path, int data_only, int fs_only);
static void sync_all(void);

/* Command line options */
static const struct option long_options[] = {
    {"data",     no_argument,       0, 'd'},
    {"file-system", no_argument,    0, 'f'},
    {"help",     no_argument,       0, 'h'},
    {"version",  no_argument,       0, 'V'},
    {0, 0, 0, 0}
};

int main(int argc, char **argv) {
    int c;
    int data_only = 0;
    int fs_only = 0;
    int ret = EXIT_SUCCESS;
    
    /* Set locale for translations (though we're not actually translating) */
    setlocale(LC_ALL, "");
    bindtextdomain(PACKAGE_NAME, "/usr/share/locale");
    textdomain(PACKAGE_NAME);
    
    /* Parse command line options */
    while ((c = getopt_long(argc, argv, "dfhV", long_options, NULL)) != -1) {
        switch (c) {
            case 'd':
                data_only = 1;
                break;
            case 'f':
                fs_only = 1;
                break;
            case 'h':
                usage(EXIT_SUCCESS);
                break;
            case 'V':
                version();
                break;
            case '?':
                /* getopt_long already printed an error message */
                usage(EXIT_USAGE);
                break;
            default:
                abort();
        }
    }
    
    /* Check for incompatible options */
    if (data_only && fs_only) {
        fprintf(stderr, "%s: options --data and --file-system are mutually exclusive\n", 
                PACKAGE_NAME);
        usage(EXIT_USAGE);
    }
    
    /* Process remaining arguments (files to sync) */
    if (optind < argc) {
        /* Sync specified files */
        ret = sync_files(argc - optind, argv + optind, data_only, fs_only);
    } else {
        /* No arguments - sync everything */
        if (data_only || fs_only) {
            /* With no arguments, --data and --file-system imply sync everything */
            sync_all();
        } else {
            /* Basic sync */
            sync_all();
        }
    }
    
    return ret;
}

/* Display usage information */
static void usage(int status) {
    if (status != EXIT_SUCCESS) {
        fprintf(stderr, "Try '%s --help' for more information.\n", PACKAGE_NAME);
    } else {
        printf("Usage: %s [OPTION] [FILE]...\n", PACKAGE_NAME);
        printf("Force writeback of dirty buffers to disk.\n\n");
        printf("With no FILE, sync correspondingly everything.\n\n");
        printf("  -d, --data             sync only file data, no unneeded metadata\n");
        printf("  -f, --file-system       sync the filesystems that contain the files\n");
        printf("  -h, --help              display this help and exit\n");
        printf("  -V, --version           output version information and exit\n");
    }
    exit(status);
}

/* Display version information */
static void version(void) {
    printf("%s %s\n", PACKAGE_NAME, PACKAGE_VERSION);
    printf("Copyright (c) 2024\n");
    printf("License: GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>\n");
    printf("This is free software: you are free to change and redistribute it.\n");
    printf("There is NO WARRANTY, to the extent permitted by law.\n");
    exit(EXIT_SUCCESS);
}

/* Sync multiple files */
static int sync_files(int argc, char **argv, int data_only, int fs_only) {
    int i;
    int ret = EXIT_SUCCESS;
    int has_error = 0;
    
    for (i = 0; i < argc; i++) {
        if (sync_file(argv[i], data_only, fs_only) != 0) {
            has_error = 1;
            ret = EXIT_FAILURE;
        }
    }
    
    /* If data_only or fs_only and no errors, sync everything as well */
    if (!has_error && (data_only || fs_only)) {
        sync_all();
    }
    
    return ret;
}

/* Sync a single file */
static int sync_file(const char *path, int data_only, int fs_only) {
    int fd;
    int ret = 0;
    struct stat st;
    
    /* Check if file exists and is accessible */
    if (stat(path, &st) == -1) {
        fprintf(stderr, "%s: cannot stat '%s': %s\n", 
                PACKAGE_NAME, path, strerror(errno));
        return -1;
    }
    
    /* Open the file */
    fd = open(path, O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (fd == -1) {
        fprintf(stderr, "%s: cannot open '%s': %s\n", 
                PACKAGE_NAME, path, strerror(errno));
        return -1;
    }
    
    /* Perform the appropriate sync operation */
    if (data_only) {
        /* Sync data only (not metadata) */
#ifdef HAVE_FDATASYNC
        ret = fdatasync(fd);
#else
        /* Fallback to fsync if fdatasync not available */
        ret = fsync(fd);
#endif
    } else if (fs_only) {
        /* Sync filesystem containing the file */
#ifdef HAVE_SYNCFS
        ret = syncfs(fd);
#else
        /* Fallback: sync everything */
        sync();
        ret = 0;
#endif
    } else {
        /* Basic file sync */
        ret = fsync(fd);
    }
    
    if (ret == -1) {
        fprintf(stderr, "%s: error syncing '%s': %s\n", 
                PACKAGE_NAME, path, strerror(errno));
    }
    
    close(fd);
    return ret;
}

/* Sync everything */
static void sync_all(void) {
    /* Standard sync - queues dirty buffers for writing */
    sync();
    
    /* On Linux, we can also sync block devices for extra safety */
#ifdef __linux__
    FILE *proc;
    char line[256];
    
    /* Try to sync block devices from /proc/partitions */
    proc = fopen("/proc/partitions", "r");
    if (proc) {
        while (fgets(line, sizeof(line), proc)) {
            char device[64];
            int major, minor, blocks;
            
            /* Parse partition info */
            if (sscanf(line, "%d %d %d %63s", &major, &minor, &blocks, device) == 4) {
                char dev_path[128];
                
                /* Try both /dev/ and /dev/block/ */
                snprintf(dev_path, sizeof(dev_path), "/dev/%s", device);
                sync_file(dev_path, 0, 1);  /* Sync filesystem containing device */
                
                snprintf(dev_path, sizeof(dev_path), "/dev/block/%d:%d", major, minor);
                sync_file(dev_path, 0, 1);  /* Sync filesystem containing device */
            }
        }
        fclose(proc);
    }
#endif
    
    /* On some systems, we can also sync specific filesystems */
#ifdef HAVE_SYNCFS
    FILE *mounts;
    char line[4096];
    
    /* Try to sync mounted filesystems from /proc/mounts */
    mounts = fopen("/proc/mounts", "r");
    if (mounts) {
        while (fgets(line, sizeof(line), mounts)) {
            char device[256], mountpoint[256], fstype[64], options[256];
            int fd;
            
            /* Parse mount info */
            if (sscanf(line, "%255s %255s %63s %255s %*d %*d", 
                       device, mountpoint, fstype, options) == 4) {
                /* Skip pseudo filesystems */
                if (strcmp(fstype, "proc") == 0 || 
                    strcmp(fstype, "sysfs") == 0 ||
                    strcmp(fstype, "devtmpfs") == 0 ||
                    strcmp(fstype, "tmpfs") == 0) {
                    continue;
                }
                
                /* Open mount point to sync its filesystem */
                fd = open(mountpoint, O_RDONLY | O_NOCTTY | O_NONBLOCK);
                if (fd != -1) {
                    syncfs(fd);
                    close(fd);
                }
            }
        }
        fclose(mounts);
    }
#endif
    
    /* Final sync to ensure everything is written */
    sync();
}
