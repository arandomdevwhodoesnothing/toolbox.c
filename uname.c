#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <getopt.h>
#include <errno.h>

/* Version information */
#define PROGRAM_NAME "uname"
#define PROGRAM_VERSION "1.0"

/* Print all system information */
static void print_all(const struct utsname *buf) {
    printf("%s %s %s %s %s\n", 
           buf->sysname, buf->nodename, buf->release, buf->version, buf->machine);
}

/* Print system information based on options */
static void print_info(const struct utsname *buf, 
                       int kernel_name, int nodename, int kernel_release,
                       int kernel_version, int machine, int processor,
                       int hardware_platform, int operating_system) {
    
    /* If no options specified, print kernel name only (compatible with standard uname) */
    if (!kernel_name && !nodename && !kernel_release && 
        !kernel_version && !machine && !processor && 
        !hardware_platform && !operating_system) {
        printf("%s\n", buf->sysname);
        return;
    }
    
    int printed = 0;
    
    if (kernel_name) {
        printf("%s", buf->sysname);
        printed = 1;
    }
    
    if (nodename) {
        if (printed) printf(" ");
        printf("%s", buf->nodename);
        printed = 1;
    }
    
    if (kernel_release) {
        if (printed) printf(" ");
        printf("%s", buf->release);
        printed = 1;
    }
    
    if (kernel_version) {
        if (printed) printf(" ");
        printf("%s", buf->version);
        printed = 1;
    }
    
    if (machine) {
        if (printed) printf(" ");
        printf("%s", buf->machine);
        printed = 1;
    }
    
    if (processor) {
        if (printed) printf(" ");
        /* Try to get processor info from /proc/cpuinfo if possible */
        FILE *fp = fopen("/proc/cpuinfo", "r");
        if (fp) {
            char line[256];
            while (fgets(line, sizeof(line), fp)) {
                if (strncmp(line, "model name", 10) == 0) {
                    char *p = strchr(line, ':');
                    if (p) {
                        p++;
                        while (*p == ' ') p++;
                        printf("%s", p);
                        break;
                    }
                }
            }
            fclose(fp);
        } else {
            printf("unknown");
        }
        printed = 1;
    }
    
    if (hardware_platform) {
        if (printed) printf(" ");
        /* Hardware platform often same as machine */
        printf("%s", buf->machine);
        printed = 1;
    }
    
    if (operating_system) {
        if (printed) printf(" ");
        printf("%s", buf->sysname);
        printed = 1;
    }
    
    printf("\n");
}

/* Print version information */
static void print_version(void) {
    printf("%s %s\n", PROGRAM_NAME, PROGRAM_VERSION);
    printf("Copyright (C) 2024\n");
    printf("This is free software; see the source for copying conditions.\n");
}

/* Print help information */
static void print_help(void) {
    printf("Usage: %s [OPTION]...\n", PROGRAM_NAME);
    printf("Print certain system information.  With no OPTION, same as -s.\n\n");
    printf("  -a, --all                print all information, in the following order,\n");
    printf("                             except omit -p and -i if unknown:\n");
    printf("  -s, --kernel-name        print the kernel name\n");
    printf("  -n, --nodename           print the network node hostname\n");
    printf("  -r, --kernel-release     print the kernel release\n");
    printf("  -v, --kernel-version     print the kernel version\n");
    printf("  -m, --machine            print the machine hardware name\n");
    printf("  -p, --processor          print the processor type (non-portable)\n");
    printf("  -i, --hardware-platform  print the hardware platform (non-portable)\n");
    printf("  -o, --operating-system   print the operating system\n");
    printf("      --help               display this help and exit\n");
    printf("      --version            output version information and exit\n");
}

int main(int argc, char **argv) {
    struct utsname buf;
    int opt;
    
    /* Option flags */
    int all = 0;
    int kernel_name = 0;
    int nodename = 0;
    int kernel_release = 0;
    int kernel_version = 0;
    int machine = 0;
    int processor = 0;
    int hardware_platform = 0;
    int operating_system = 0;
    
    /* Long options */
    static struct option long_options[] = {
        {"all", no_argument, 0, 'a'},
        {"kernel-name", no_argument, 0, 's'},
        {"nodename", no_argument, 0, 'n'},
        {"kernel-release", no_argument, 0, 'r'},
        {"kernel-version", no_argument, 0, 'v'},
        {"machine", no_argument, 0, 'm'},
        {"processor", no_argument, 0, 'p'},
        {"hardware-platform", no_argument, 0, 'i'},
        {"operating-system", no_argument, 0, 'o'},
        {"help", no_argument, 0, 256},
        {"version", no_argument, 0, 257},
        {0, 0, 0, 0}
    };
    
    /* Parse command line options */
    while ((opt = getopt_long(argc, argv, "asnrvmpio", long_options, NULL)) != -1) {
        switch (opt) {
            case 'a':
                all = 1;
                break;
            case 's':
                kernel_name = 1;
                break;
            case 'n':
                nodename = 1;
                break;
            case 'r':
                kernel_release = 1;
                break;
            case 'v':
                kernel_version = 1;
                break;
            case 'm':
                machine = 1;
                break;
            case 'p':
                processor = 1;
                break;
            case 'i':
                hardware_platform = 1;
                break;
            case 'o':
                operating_system = 1;
                break;
            case 256: /* --help */
                print_help();
                return 0;
            case 257: /* --version */
                print_version();
                return 0;
            default:
                fprintf(stderr, "Try '%s --help' for more information.\n", PROGRAM_NAME);
                return 1;
        }
    }
    
    /* Handle invalid arguments */
    if (optind < argc) {
        fprintf(stderr, "%s: extra operand '%s'\n", PROGRAM_NAME, argv[optind]);
        fprintf(stderr, "Try '%s --help' for more information.\n", PROGRAM_NAME);
        return 1;
    }
    
    /* Get system information */
    if (uname(&buf) == -1) {
        fprintf(stderr, "%s: cannot get system name: %s\n", 
                PROGRAM_NAME, strerror(errno));
        return 1;
    }
    
    /* If -a is specified, turn on all options except processor and hardware platform
       (they're excluded because they're non-portable and may be unknown) */
    if (all) {
        kernel_name = nodename = kernel_release = kernel_version = machine = 1;
        /* Don't set processor or hardware platform for -a */
    }
    
    /* Print requested information */
    if (all) {
        print_all(&buf);
    } else {
        print_info(&buf, kernel_name, nodename, kernel_release, kernel_version, 
                   machine, processor, hardware_platform, operating_system);
    }
    
    return 0;
}
