#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <errno.h>
#include <ctype.h>

#define MAX_FORMAT_LEN 1024
#define DATE_STR_LEN 256

/* Format specifiers for date command */
typedef struct {
    char spec;
    const char *description;
    int (*format_func)(char *buffer, size_t size, const struct tm *tm);
} FormatSpec;

/* Function prototypes */
void print_help(void);
void print_version(void);
int format_date(char *output, size_t size, const char *format, const struct tm *tm);
int set_system_date(const char *datestr);
int parse_date_string(const char *str, struct tm *tm);
void print_rfc_3339(const struct tm *tm, int precision);
void print_rfc_2822(const struct tm *tm);
void print_iso_8601(const struct tm *tm, const char *timespec);

/* Format functions for different specifiers */
int format_date_weekday(char *buf, size_t size, const struct tm *tm) {
    const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    return snprintf(buf, size, "%s", days[tm->tm_wday]);
}

int format_date_full_weekday(char *buf, size_t size, const struct tm *tm) {
    const char *days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", 
                          "Thursday", "Friday", "Saturday"};
    return snprintf(buf, size, "%s", days[tm->tm_wday]);
}

int format_month(char *buf, size_t size, const struct tm *tm) {
    const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    return snprintf(buf, size, "%s", months[tm->tm_mon]);
}

int format_full_month(char *buf, size_t size, const struct tm *tm) {
    const char *months[] = {"January", "February", "March", "April", "May", "June",
                            "July", "August", "September", "October", "November", "December"};
    return snprintf(buf, size, "%s", months[tm->tm_mon]);
}

int format_date_num(char *buf, size_t size, const struct tm *tm) {
    return snprintf(buf, size, "%02d", tm->tm_mday);
}

int format_hour(char *buf, size_t size, const struct tm *tm) {
    return snprintf(buf, size, "%02d", tm->tm_hour);
}

int format_hour_12(char *buf, size_t size, const struct tm *tm) {
    int hour = tm->tm_hour % 12;
    if (hour == 0) hour = 12;
    return snprintf(buf, size, "%02d", hour);
}

int format_minute(char *buf, size_t size, const struct tm *tm) {
    return snprintf(buf, size, "%02d", tm->tm_min);
}

int format_second(char *buf, size_t size, const struct tm *tm) {
    return snprintf(buf, size, "%02d", tm->tm_sec);
}

int format_year(char *buf, size_t size, const struct tm *tm) {
    return snprintf(buf, size, "%04d", tm->tm_year + 1900);
}

int format_short_year(char *buf, size_t size, const struct tm *tm) {
    return snprintf(buf, size, "%02d", (tm->tm_year + 1900) % 100);
}

int format_month_num(char *buf, size_t size, const struct tm *tm) {
    return snprintf(buf, size, "%02d", tm->tm_mon + 1);
}

int format_am_pm(char *buf, size_t size, const struct tm *tm) {
    return snprintf(buf, size, "%s", tm->tm_hour < 12 ? "AM" : "PM");
}

int format_timezone(char *buf, size_t size, const struct tm *tm) {
    char tz[32];
    strftime(tz, sizeof(tz), "%Z", tm);
    return snprintf(buf, size, "%s", tz);
}

int format_timezone_offset(char *buf, size_t size, const struct tm *tm) {
    char tz[32];
    strftime(tz, sizeof(tz), "%z", tm);
    return snprintf(buf, size, "%s", tz);
}

int format_day_of_year(char *buf, size_t size, const struct tm *tm) {
    return snprintf(buf, size, "%03d", tm->tm_yday + 1);
}

int format_week_of_year_sun(char *buf, size_t size, const struct tm *tm) {
    char week_str[4];
    strftime(week_str, sizeof(week_str), "%U", tm);
    return snprintf(buf, size, "%s", week_str);
}

int format_week_of_year_mon(char *buf, size_t size, const struct tm *tm) {
    char week_str[4];
    strftime(week_str, sizeof(week_str), "%W", tm);
    return snprintf(buf, size, "%s", week_str);
}

int format_weekday_num(char *buf, size_t size, const struct tm *tm) {
    return snprintf(buf, size, "%d", tm->tm_wday);
}

int format_weekday_num_iso(char *buf, size_t size, const struct tm *tm) {
    int wday = tm->tm_wday;
    if (wday == 0) wday = 7;
    return snprintf(buf, size, "%d", wday);
}

int format_nanosecond(char *buf, size_t size, const struct tm *tm) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return snprintf(buf, size, "%09ld", ts.tv_nsec);
}

int format_date_locale(char *buf, size_t size, const struct tm *tm) {
    strftime(buf, size, "%x", tm);
    return strlen(buf);
}

int format_time_locale(char *buf, size_t size, const struct tm *tm) {
    strftime(buf, size, "%X", tm);
    return strlen(buf);
}

/* Format specifier table */
FormatSpec format_specs[] = {
    {'a', "abbreviated weekday name", format_date_weekday},
    {'A', "full weekday name", format_date_full_weekday},
    {'b', "abbreviated month name", format_month},
    {'B', "full month name", format_full_month},
    {'d', "day of month (01-31)", format_date_num},
    {'H', "hour (00-23)", format_hour},
    {'I', "hour (01-12)", format_hour_12},
    {'M', "minute (00-59)", format_minute},
    {'m', "month (01-12)", format_month_num},
    {'S', "second (00-60)", format_second},
    {'Y', "year (4 digits)", format_year},
    {'y', "year (2 digits)", format_short_year},
    {'p', "AM or PM", format_am_pm},
    {'Z', "timezone", format_timezone},
    {'z', "timezone offset", format_timezone_offset},
    {'j', "day of year (001-366)", format_day_of_year},
    {'U', "week of year (Sunday first)", format_week_of_year_sun},
    {'W', "week of year (Monday first)", format_week_of_year_mon},
    {'w', "day of week (0-6, Sunday=0)", format_weekday_num},
    {'u', "day of week (1-7, Monday=1)", format_weekday_num_iso},
    {'N', "nanoseconds", format_nanosecond},
    {'x', "locale date", format_date_locale},
    {'X', "locale time", format_time_locale},
    {'\0', NULL, NULL}
};

/* Format date according to format string */
int format_date(char *output, size_t size, const char *format, const struct tm *tm) {
    char *outptr = output;
    size_t remaining = size;
    const char *fmtptr = format;
    
    if (!format || !*format) {
        /* Default format if no format specified */
        strftime(output, size, "%a %b %d %H:%M:%S %Z %Y", tm);
        return strlen(output);
    }

    while (*fmtptr && remaining > 1) {
        if (*fmtptr == '%') {
            fmtptr++;
            if (*fmtptr == '\0') break;
            
            if (*fmtptr == '%') {
                *outptr++ = '%';
                remaining--;
                fmtptr++;
            } else {
                /* Find format specifier */
                FormatSpec *spec = format_specs;
                int found = 0;
                while (spec->spec) {
                    if (spec->spec == *fmtptr) {
                        char buf[64];
                        int len = spec->format_func(buf, sizeof(buf), tm);
                        if (len > 0 && len < (int)remaining) {
                            strcpy(outptr, buf);
                            outptr += len;
                            remaining -= len;
                        }
                        found = 1;
                        break;
                    }
                    spec++;
                }
                if (!found) {
                    /* Unknown specifier, skip */
                    *outptr++ = *fmtptr;
                    remaining--;
                }
                fmtptr++;
            }
        } else {
            *outptr++ = *fmtptr++;
            remaining--;
        }
    }
    *outptr = '\0';
    return outptr - output;
}

/* Print in RFC 3339 format */
void print_rfc_3339(const struct tm *tm, int precision) {
    char buf[64];
    if (precision == 0) {
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S%z", tm);
        /* Insert colon in timezone */
        char *tz = strchr(buf, '+');
        if (!tz) tz = strchr(buf, '-');
        if (tz && strlen(tz) > 3) {
            char tz_copy[8];
            strcpy(tz_copy, tz);
            tz[3] = ':';
            strcpy(tz + 4, tz_copy + 3);
        }
    } else {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
        printf("%s.%09ld", buf, ts.tv_nsec);
        return;
    }
    printf("%s", buf);
}

/* Print in RFC 2822 format */
void print_rfc_2822(const struct tm *tm) {
    char buf[64];
    strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S %z", tm);
    printf("%s", buf);
}

/* Print in ISO 8601 format */
void print_iso_8601(const struct tm *tm, const char *timespec) {
    char buf[64];
    if (!timespec || strcmp(timespec, "date") == 0) {
        strftime(buf, sizeof(buf), "%Y-%m-%d", tm);
    } else if (strcmp(timespec, "hours") == 0) {
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H", tm);
    } else if (strcmp(timespec, "minutes") == 0) {
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M", tm);
    } else if (strcmp(timespec, "seconds") == 0) {
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", tm);
    } else {
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", tm);
    }
    printf("%s", buf);
}

/* Parse date string (enhanced implementation) */
int parse_date_string(const char *str, struct tm *tm) {
    /* Get current time as base */
    time_t now = time(NULL);
    struct tm *now_tm = localtime(&now);
    *tm = *now_tm;

    /* Try various formats */
    int month, day, year, hour, minute, second;
    
    /* Format: MMDDhhmm[[CC]YY][.ss] */
    if (sscanf(str, "%2d%2d%2d%2d", &month, &day, &hour, &minute) == 4) {
        tm->tm_mon = month - 1;
        tm->tm_mday = day;
        tm->tm_hour = hour;
        tm->tm_min = minute;
        tm->tm_sec = 0;
        
        /* Check for optional year and seconds */
        const char *p = str + 8;
        if (*p == '.') {
            if (sscanf(p + 1, "%2d", &second) == 1) {
                tm->tm_sec = second;
                p += 3;
            }
        }
        if (*p) {
            int year_val;
            if (sscanf(p, "%4d", &year_val) == 1) {
                tm->tm_year = year_val - 1900;
            } else if (sscanf(p, "%2d", &year_val) == 1) {
                /* Two-digit year - add century */
                int current_year = now_tm->tm_year + 1900;
                int century = (current_year / 100) * 100;
                year_val += century;
                if (year_val < current_year - 50) year_val += 100;
                tm->tm_year = year_val - 1900;
            }
        }
        return 1;
    }
    
    /* Try YYYY-MM-DD */
    if (sscanf(str, "%d-%d-%d", &year, &month, &day) == 3) {
        tm->tm_mon = month - 1;
        tm->tm_mday = day;
        tm->tm_year = year - 1900;
        return 1;
    }
    
    /* Try MM/DD/YYYY */
    if (sscanf(str, "%d/%d/%d", &month, &day, &year) == 3) {
        tm->tm_mon = month - 1;
        tm->tm_mday = day;
        tm->tm_year = year - 1900;
        return 1;
    }
    
    /* Try HH:MM:SS */
    if (sscanf(str, "%d:%d:%d", &hour, &minute, &second) == 3) {
        tm->tm_hour = hour;
        tm->tm_min = minute;
        tm->tm_sec = second;
        return 1;
    }
    
    /* Try HH:MM */
    if (sscanf(str, "%d:%d", &hour, &minute) == 2) {
        tm->tm_hour = hour;
        tm->tm_min = minute;
        tm->tm_sec = 0;
        return 1;
    }
    
    return 0;
}

/* Set system date (requires appropriate privileges) */
int set_system_date(const char *datestr) {
    struct tm tm;
    struct timeval tv;
    
    if (!parse_date_string(datestr, &tm)) {
        fprintf(stderr, "date: invalid date format '%s'\n", datestr);
        return -1;
    }
    
    time_t t = mktime(&tm);
    if (t == (time_t)-1) {
        fprintf(stderr, "date: invalid date\n");
        return -1;
    }
    
    tv.tv_sec = t;
    tv.tv_usec = 0;
    
    if (settimeofday(&tv, NULL) < 0) {
        perror("date: cannot set date");
        return -1;
    }
    
    return 0;
}

/* Print help information */
void print_help(void) {
    printf("Usage: date [OPTION]... [+FORMAT]\n");
    printf("  or:  date [OPTION] [MMDDhhmm[[CC]YY][.ss]]\n");
    printf("Display the current time in the given FORMAT, or set the system date.\n\n");
    printf("Options:\n");
    printf("  -d, --date=STRING      display time described by STRING, not 'now'\n");
    printf("  -f, --file=DATEFILE     like --date once for each line of DATEFILE\n");
    printf("  -I[TIMESPEC]           output date/time in ISO 8601 format\n");
    printf("                            TIMESPEC='date' (default), 'hours', 'minutes', 'seconds'\n");
    printf("  -R, --rfc-2822         output date and time in RFC 2822 format\n");
    printf("  --rfc-3339=TIMESPEC    output date and time in RFC 3339 format\n");
    printf("                            TIMESPEC='date', 'seconds', 'ns'\n");
    printf("  -r, --reference=FILE   display the last modification time of FILE\n");
    printf("  -s, --set=STRING       set time described by STRING\n");
    printf("  -u, --utc, --universal print or set Coordinated Universal Time (UTC)\n");
    printf("  --help                  display this help and exit\n");
    printf("  --version               output version information and exit\n\n");
    printf("FORMAT controls the output. Interpreted sequences are:\n");
    printf("  %%a     abbreviated weekday name (e.g., Sun)\n");
    printf("  %%A     full weekday name (e.g., Sunday)\n");
    printf("  %%b     abbreviated month name (e.g., Jan)\n");
    printf("  %%B     full month name (e.g., January)\n");
    printf("  %%d     day of month (e.g., 01)\n");
    printf("  %%H     hour (00..23)\n");
    printf("  %%I     hour (01..12)\n");
    printf("  %%j     day of year (001..366)\n");
    printf("  %%m     month (01..12)\n");
    printf("  %%M     minute (00..59)\n");
    printf("  %%N     nanoseconds (000000000..999999999)\n");
    printf("  %%p     locale's equivalent of either AM or PM\n");
    printf("  %%S     second (00..60)\n");
    printf("  %%u     day of week (1..7); 1 is Monday\n");
    printf("  %%U     week number of year, Sunday as first day (00..53)\n");
    printf("  %%w     day of week (0..6); 0 is Sunday\n");
    printf("  %%W     week number of year, Monday as first day (00..53)\n");
    printf("  %%x     locale's date representation (e.g., 12/31/99)\n");
    printf("  %%X     locale's time representation (e.g., 23:13:48)\n");
    printf("  %%y     last two digits of year (00..99)\n");
    printf("  %%Y     year\n");
    printf("  %%z     +hhmm numeric time zone (e.g., -0400)\n");
    printf("  %%Z     alphabetic time zone abbreviation (e.g., EDT)\n");
    printf("  %%      a literal %%\n");
}

/* Print version information */
void print_version(void) {
    printf("date (custom implementation) 1.0\n");
    printf("Copyright (C) 2024 Custom Date Command\n");
}

int main(int argc, char *argv[]) {
    int opt;
    int option_index = 0;
    int use_utc = 0;
    int rfc_2822 = 0;
    int rfc_3339 = 0;
    int iso_8601 = 0;
    const char *rfc_3339_timespec = NULL;
    const char *iso_8601_timespec = NULL;
    const char *date_string = NULL;
    const char *set_string = NULL;
    const char *format = NULL;
    const char *reference_file = NULL;
    
    static struct option long_options[] = {
        {"date", required_argument, 0, 'd'},
        {"set", required_argument, 0, 's'},
        {"utc", no_argument, 0, 'u'},
        {"universal", no_argument, 0, 'u'},
        {"rfc-2822", no_argument, 0, 'R'},
        {"rfc-3339", required_argument, 0, 1},
        {"iso-8601", optional_argument, 0, 'I'},
        {"help", no_argument, 0, 2},
        {"version", no_argument, 0, 3},
        {"reference", required_argument, 0, 'r'},
        {0, 0, 0, 0}
    };
    
    while ((opt = getopt_long(argc, argv, "d:f:I::Rr:s:u", 
                              long_options, &option_index)) != -1) {
        switch (opt) {
            case 'd':
                date_string = optarg;
                break;
            case 's':
                set_string = optarg;
                break;
            case 'u':
                use_utc = 1;
                break;
            case 'R':
                rfc_2822 = 1;
                break;
            case 1: /* --rfc-3339 */
                rfc_3339 = 1;
                rfc_3339_timespec = optarg;
                break;
            case 'r':
                reference_file = optarg;
                break;
            case 'I':
                iso_8601 = 1;
                iso_8601_timespec = optarg;
                break;
            case 2: /* --help */
                print_help();
                return 0;
            case 3: /* --version */
                print_version();
                return 0;
            case 'f':
                fprintf(stderr, "date: -f option not fully implemented\n");
                break;
            default:
                fprintf(stderr, "Try 'date --help' for more information.\n");
                return 1;
        }
    }
    
    /* Check for format string */
    if (optind < argc && argv[optind][0] == '+') {
        format = argv[optind] + 1;
        optind++;
    } else if (optind < argc && !set_string && !date_string) {
        /* Handle legacy date set format: MMDDhhmm[[CC]YY][.ss] */
        set_string = argv[optind];
    }
    
    /* Handle setting date */
    if (set_string) {
        if (set_system_date(set_string) < 0) {
            return 1;
        }
        return 0;
    }
    
    /* Get current time */
    time_t now;
    struct tm tm;
    struct stat file_stat;
    
    if (reference_file) {
        if (stat(reference_file, &file_stat) < 0) {
            perror("date");
            return 1;
        }
        now = file_stat.st_mtime;
    } else {
        now = time(NULL);
    }
    
    if (use_utc) {
        gmtime_r(&now, &tm);
    } else {
        localtime_r(&now, &tm);
    }
    
    /* Handle date string parsing */
    if (date_string) {
        if (!parse_date_string(date_string, &tm)) {
            fprintf(stderr, "date: invalid date '%s'\n", date_string);
            return 1;
        }
    }
    
    /* Output in requested format */
    char output[MAX_FORMAT_LEN];
    
    if (rfc_2822) {
        print_rfc_2822(&tm);
        printf("\n");
    } else if (rfc_3339) {
        int precision = 0;
        if (rfc_3339_timespec) {
            if (strcmp(rfc_3339_timespec, "ns") == 0) precision = 1;
            else if (strcmp(rfc_3339_timespec, "seconds") == 0) precision = 0;
        }
        print_rfc_3339(&tm, precision);
        printf("\n");
    } else if (iso_8601) {
        print_iso_8601(&tm, iso_8601_timespec);
        printf("\n");
    } else if (format) {
        format_date(output, sizeof(output), format, &tm);
        printf("%s\n", output);
    } else {
        /* Default format */
        strftime(output, sizeof(output), "%a %b %d %H:%M:%S %Z %Y", &tm);
        printf("%s\n", output);
    }
    
    return 0;
}
