/*
 * whoami.c - print effective userid's name
 * 
 * Minimal implementation matching standard Linux whoami behavior
 * 
 * Compilation: gcc -Wall -Wextra -O2 -o whoami whoami.c
 * Usage: whoami
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <errno.h>

/* Error codes */
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

/*
 * Get user name from UID
 * Returns dynamically allocated string that must be freed by caller,
 * or NULL if name cannot be determined
 */
static char* get_user_name(uid_t uid)
{
    struct passwd *pwd;
    
    /* Get password entry - standard whoami uses non-reentrant version */
    errno = 0;
    pwd = getpwuid(uid);
    
    if (pwd == NULL) {
        return NULL;
    }
    
    return strdup(pwd->pw_name);
}

/*
 * Main function
 */
int main(int argc, char *argv[])
{
    uid_t uid;
    char *user_name;
    
    /* Standard whoami takes no arguments */
    if (argc != 1) {
        fprintf(stderr, "whoami: extra operand '%s'\n", argv[1]);
        fprintf(stderr, "Try 'whoami --help' for more information.\n");
        return EXIT_FAILURE;
    }
    
    /* Get effective user ID */
    uid = geteuid();
    
    /* Get user name from UID */
    user_name = get_user_name(uid);
    
    if (user_name == NULL) {
        /* If we can't get the name, print nothing and return error */
        fprintf(stderr, "whoami: cannot find name for user ID %u\n", uid);
        return EXIT_FAILURE;
    }
    
    /* Print the user name */
    printf("%s\n", user_name);
    
    /* Clean up */
    free(user_name);
    
    return EXIT_SUCCESS;
}
