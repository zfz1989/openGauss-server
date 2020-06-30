/* -------------------------------------------------------------------------
 *
 * sprompt.c
 *	  simple_prompt() routine
 *
 * Portions Copyright (c) 1996-2012, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/port/sprompt.c
 *
 * -------------------------------------------------------------------------
 */

/*
 * simple_prompt
 *
 * Generalized function especially intended for reading in usernames and
 * password interactively. Reads from /dev/tty or stdin/stderr.
 *
 * prompt:		The prompt to print
 * maxlen:		How many characters to accept
 * echo:		Set to false if you want to hide what is entered (for passwords)
 *
 * Returns a malloc()'ed string with the input (w/o trailing newline).
 */
#include "c.h"

#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif

extern char* simple_prompt(const char* prompt, int maxlen, bool echo);

char* simple_prompt(const char* prompt, int maxlen, bool echo)
{
    int length;
    char* destination = NULL;
    FILE* termin = NULL;
    FILE* termout = NULL;

#if defined(HAVE_TERMIOS_H)
    struct termios t_orig, t;
#elif defined(WIN32)
    HANDLE t = NULL;
    DWORD t_orig = 0;
#endif

    destination = (char*)malloc(maxlen + 1);
    if (destination == NULL) {
        return NULL;
    }

#ifdef WIN32

    /*
     * A Windows console has an "input code page" and an "output code page";
     * these usually match each other, but they rarely match the "Windows ANSI
     * code page" defined at system boot and expected of "char *" arguments to
     * Windows API functions.  The Microsoft CRT write() implementation
     * automatically converts text between these code pages when writing to a
     * console.  To identify such file descriptors, it calls GetConsoleMode()
     * on the underlying HANDLE, which in turn requires GENERIC_READ access on
     * the HANDLE.  Opening termout in mode "w+" allows that detection to
     * succeed.  Otherwise, write() would not recognize the descriptor as a
     * console, and non-ASCII characters would display incorrectly.
     *
     * XXX fgets() still receives text in the console's input code page.  This
     * makes non-ASCII credentials unportable.
     */
#ifndef WIN32_PG_DUMP
    termin = fopen("CONIN$", "w+");
    termout = fopen("CONOUT$", "w+");
#else
    termin = stdin;
    termout = stdout;
#endif
#else

    /*
     * Do not try to collapse these into one "w+" mode file. Doesn't work on
     * some platforms (eg, HPUX 10.20).
     */
    termin = fopen("/dev/tty", "r");
    termout = fopen("/dev/tty", "w");
#endif
    if ((termin == NULL) ||
        (termout == NULL)
#ifdef WIN32
        /*
         * Direct console I/O does not work from the MSYS 1.0.10 console.  Writes
         * reach nowhere user-visible; reads block indefinitely.  XXX This affects
         * most Windows terminal environments, including rxvt, mintty, Cygwin
         * xterm, Cygwin sshd, and PowerShell ISE.  Switch to a more-generic test.
         */
        || (gs_getenv_r("OSTYPE") && strcmp(gs_getenv_r("OSTYPE"), "msys") == 0)
#endif
    ) {
        if (termin != NULL) {
            fclose(termin);
        }
        if (termout != NULL) {
            fclose(termout);
        }
        termin = stdin;
        termout = stderr;
    }

    if (!echo) {
#if defined(HAVE_TERMIOS_H)
        /* disable echo via tcgetattr/tcsetattr */
        tcgetattr(fileno(termin), &t);
        t_orig = t;
        t.c_lflag &= ~ECHO;
        tcsetattr(fileno(termin), TCSAFLUSH, &t);
#elif defined(WIN32)
        /* need the file's HANDLE to turn echo off */
        t = (HANDLE)_get_osfhandle(_fileno(termin));

        /* save the old configuration first */
        GetConsoleMode(t, &t_orig);

        /* set to the new mode */
        SetConsoleMode(t, ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
#endif
    }

    if (prompt != NULL) {
        fputs(_(prompt), termout);
        fflush(termout);
    }

    if (fgets(destination, maxlen + 1, termin) == NULL) {
        destination[0] = '\0';
    }
    length = strlen(destination);
    if (length > 0 && destination[length - 1] != '\n') {
        /* eat rest of the line */
        char buf[128];
        int buflen;

        do {
            if (fgets(buf, sizeof(buf), termin) == NULL) {
                break;
            }
            buflen = strlen(buf);
        } while (buflen > 0 && buf[buflen - 1] != '\n');
    }

    if (length > 0 && destination[length - 1] == '\n') { /* remove trailing newline */
        destination[length - 1] = '\0';
    }
    if (!echo) {
        /* restore previous echo behavior, then echo \n */
#if defined(HAVE_TERMIOS_H)
        tcsetattr(fileno(termin), TCSAFLUSH, &t_orig);
        fputs("\n", termout);
        fflush(termout);
#elif defined(WIN32)
        SetConsoleMode(t, t_orig);
        fputs("\n", termout);
        fflush(termout);
#endif
    }

    if (termin != stdin) {
        fclose(termin);
        fclose(termout);
    }

    return destination;
}