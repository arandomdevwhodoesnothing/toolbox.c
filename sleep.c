/**
 * COMPLETE STANDARD SLEEP IMPLEMENTATION
 * Exactly what POSIX sleep() does - NOTHING MORE
 */

#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

static volatile sig_atomic_t interrupted = 0;

static void sleep_handler(int sig) {
    (void)sig;
    interrupted = 1;
}

unsigned int my_sleep(unsigned int seconds) {
    struct sigaction sa, old_alrm, old_int;
    unsigned int remaining;
    
    /* sleep(0) returns immediately */
    if (seconds == 0) {
        return 0;
    }
    
    /* Save original handlers */
    sigaction(SIGALRM, NULL, &old_alrm);
    sigaction(SIGINT, NULL, &old_int);
    
    /* Set up our handler */
    sa.sa_handler = sleep_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    
    /* Set alarm */
    interrupted = 0;
    alarm(seconds);
    
    /* Wait for signal */
    pause();
    
    /* Get remaining time */
    remaining = alarm(0);
    
    /* Restore original handlers */
    sigaction(SIGALRM, &old_alrm, NULL);
    sigaction(SIGINT, &old_int, NULL);
    
    /* Return remaining seconds if interrupted */
    return (interrupted) ? remaining : 0;
}

/* Alternative using nanosleep (better) */
unsigned int my_sleep_nano(unsigned int seconds) {
    struct timespec req, rem;
    
    if (seconds == 0) return 0;
    
    req.tv_sec = seconds;
    req.tv_nsec = 0;
    
    if (nanosleep(&req, &rem) == -1 && errno == EINTR) {
        return rem.tv_sec + (rem.tv_nsec ? 1 : 0);
    }
    
    return 0;
}

int main(int argc, char *argv[]) {
    unsigned int seconds = 5;  /* Default */
    unsigned int remaining;
    
    /* Get seconds from command line if provided */
    if (argc > 1) {
        seconds = (unsigned int)atoi(argv[1]);
    }
    
    printf("Sleeping for %u seconds... (Ctrl+C to interrupt)\n", seconds);
    fflush(stdout);
    
    remaining = my_sleep_nano(seconds);
    
    if (remaining == 0) {
        printf("\nSlept full %u seconds\n", seconds);
    } else {
        printf("\nInterrupted! %u seconds remaining\n", remaining);
    }
    
    return 0;
}
