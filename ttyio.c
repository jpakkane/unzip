/*
  Copyright (c) 1990-2008 Info-ZIP.  All rights reserved.

  See the accompanying file LICENSE, version 2000-Apr-09 or later
  (the contents of which are also included in zip.h) for terms of use.
  If, for some reason, all these files are missing, the Info-ZIP license
  also may be found at:  ftp://ftp.info-zip.org/pub/infozip/license.html
*/
/*---------------------------------------------------------------------------

  ttyio.c

  This file contains routines for doing console input/output, including code
  for non-echoing input.  It is used by the encryption/decryption code but
  does not contain any restricted code itself.  This file is shared between
  Info-ZIP's Zip and UnZip.

  Contains:  echo()         (VMS only)
             Echon()        (Unix only)
             Echoff()       (Unix only)
             screensize()   (Unix only)
             zgetch()       (Unix, VMS, and non-Unix/VMS versions)
             getp()         ("PC," Unix/Atari/Be, VMS/VMCMS/MVS)

  ---------------------------------------------------------------------------*/

#define __TTYIO_C       /* identifies this source module */

#include "zip.h"
#include "crypt.h"

#if (CRYPT || (defined(UNZIP)))
/* Non-echo console/keyboard input is needed for (en/de)cryption's password
 * entry, and for UnZip(SFX)'s MORE and Pause features.
 * (The corresponding #endif is found at the end of this module.)
 */

#include "ttyio.h"

#ifndef PUTC
#  define PUTC putc
#endif

#ifdef ZIP
#  ifdef GLOBAL          /* used in Amiga system headers, maybe others too */
#    undef GLOBAL
#  endif
#  define GLOBAL(g) g
#else
#  define GLOBAL(g) (*(Uz_Globs *)pG).g
#endif

#ifdef _POSIX_VERSION
#  ifndef USE_POSIX_TERMIOS
#    define USE_POSIX_TERMIOS  /* use POSIX style termio (termios) */
#  endif
#  ifndef HAVE_TERMIOS_H
#    define HAVE_TERMIOS_H     /* POSIX termios.h */
#  endif
#endif /* _POSIX_VERSION */

#ifdef UNZIP            /* Zip handles this with the unix/configure script */
#  ifndef _POSIX_VERSION
#    if (defined(SYSV) || defined(CRAY)) &&  !defined(__MINT__)
#      ifndef USE_SYSV_TERMIO
#        define USE_SYSV_TERMIO
#      endif
#      ifdef COHERENT
#        ifndef HAVE_TERMIO_H
#          define HAVE_TERMIO_H
#        endif
#        ifdef HAVE_SYS_TERMIO_H
#          undef HAVE_SYS_TERMIO_H
#        endif
#      else /* !COHERENT */
#        ifdef HAVE_TERMIO_H
#          undef HAVE_TERMIO_H
#        endif
#        ifndef HAVE_SYS_TERMIO_H
#           define HAVE_SYS_TERMIO_H
#        endif
#      endif /* ?COHERENT */
#    endif /* (SYSV || CRAY) && !__MINT__ */
#  endif /* !_POSIX_VERSION */
#  if !(defined(BSD4_4) || defined(SYSV) || defined(__convexc__))
#    ifndef NO_FCNTL_H
#      define NO_FCNTL_H
#    endif
#  endif /* !(BSD4_4 || SYSV || __convexc__) */
#endif /* UNZIP */

#ifdef HAVE_TERMIOS_H
#  ifndef USE_POSIX_TERMIOS
#    define USE_POSIX_TERMIOS
#  endif
#endif

#if (defined(HAVE_TERMIO_H) || defined(HAVE_SYS_TERMIO_H))
#  ifndef USE_SYSV_TERMIO
#    define USE_SYSV_TERMIO
#  endif
#endif

#ifndef HAVE_WORKING_GETCH
   /* include system support for switching of console echo */
#  ifdef VMS
#    include <descrip.h>
#    include <iodef.h>
#    include <ttdef.h>
     /* Workaround for broken header files of older DECC distributions
      * that are incompatible with the /NAMES=AS_IS qualifier. */
#    define sys$assign SYS$ASSIGN
#    define sys$dassgn SYS$DASSGN
#    define sys$qiow SYS$QIOW
#    include <starlet.h>
#    include <ssdef.h>
#  else /* !VMS */
#    ifdef HAVE_TERMIOS_H
#      include <termios.h>
#      define sgttyb termios
#      define sg_flags c_lflag
#      define GTTY(f, s) tcgetattr(f, (void *) s)
#      define STTY(f, s) tcsetattr(f, TCSAFLUSH, (void *) s)
#    else /* !HAVE_TERMIOS_H */
#      ifdef USE_SYSV_TERMIO           /* Amdahl, Cray, all SysV? */
#        ifdef HAVE_TERMIO_H
#          include <termio.h>
#        endif
#        ifdef HAVE_SYS_TERMIO_H
#          include <sys/termio.h>
#        endif
#        ifdef NEED_PTEM
#          include <sys/stream.h>
#          include <sys/ptem.h>
#        endif
#        define sgttyb termio
#        define sg_flags c_lflag
#        define GTTY(f,s) ioctl(f,TCGETA,(void *)s)
#        define STTY(f,s) ioctl(f,TCSETAW,(void *)s)
#      else /* !USE_SYSV_TERMIO */
#        ifndef CMS_MVS
#          if (!defined(MINIX) && !defined(GOT_IOCTL_H))
#            include <sys/ioctl.h>
#          endif
#          include <sgtty.h>
#          define GTTY gtty
#          define STTY stty
#          ifdef UNZIP
             /*
              * XXX : Are these declarations needed at all ????
              */
             /*
              * GRR: let's find out...   Hmmm, appears not...
             int gtty OF((int, struct sgttyb *));
             int stty OF((int, struct sgttyb *));
              */
#          endif
#        endif /* !CMS_MVS */
#      endif /* ?USE_SYSV_TERMIO */
#    endif /* ?HAVE_TERMIOS_H */
#    ifndef NO_FCNTL_H
#      ifndef UNZIP
#        include <fcntl.h>
#      endif
#    else
       char *ttyname OF((int));
#    endif
#  endif /* ?VMS */
#endif /* !HAVE_WORKING_GETCH */



#ifndef HAVE_WORKING_GETCH
#ifdef VMS
#else /* !VMS:  basically Unix */


/* For VM/CMS and MVS, non-echo terminal input is not (yet?) supported. */
#ifndef CMS_MVS

#ifdef ZIP                      /* moved to globals.h for UnZip */
   static int echofd=(-1);      /* file descriptor whose echo is off */
#endif

/*
 * Turn echo off for file descriptor f.  Assumes that f is a tty device.
 */
void 
Echoff (
    Uz_Globs *pG,
    int f                    /* file descriptor for which to turn echo off */
)
{
    struct sgttyb sg;         /* tty device structure */

    GLOBAL(echofd) = f;
    GTTY(f, &sg);             /* get settings */
    sg.sg_flags &= ~ECHO;     /* turn echo off */
    STTY(f, &sg);
}

/*
 * Turn echo back on for file descriptor echofd.
 */
void 
Echon (Uz_Globs *pG)
{
    struct sgttyb sg;         /* tty device structure */

    if (GLOBAL(echofd) != -1) {
        GTTY(GLOBAL(echofd), &sg);    /* get settings */
        sg.sg_flags |= ECHO;  /* turn echo on */
        STTY(GLOBAL(echofd), &sg);
        GLOBAL(echofd) = -1;
    }
}

#endif /* !CMS_MVS */
#endif /* ?VMS */


#if (defined(UNZIP))

#ifdef ATH_BEO_UNX

/*
 * Get a character from the given file descriptor without echo or newline.
 */
int 
zgetch (
    Uz_Globs *pG,
    int f                      /* file descriptor from which to read */
)
{
#if (defined(USE_SYSV_TERMIO) || defined(USE_POSIX_TERMIOS))
    char oldmin, oldtim;
#endif
    char c;
    struct sgttyb sg;           /* tty device structure */

    GTTY(f, &sg);               /* get settings */
#if (defined(USE_SYSV_TERMIO) || defined(USE_POSIX_TERMIOS))
    oldmin = sg.c_cc[VMIN];     /* save old values */
    oldtim = sg.c_cc[VTIME];
    sg.c_cc[VMIN] = 1;          /* need only one char to return read() */
    sg.c_cc[VTIME] = 0;         /* no timeout */
    sg.sg_flags &= ~ICANON;     /* canonical mode off */
#else
    sg.sg_flags |= CBREAK;      /* cbreak mode on */
#endif
    sg.sg_flags &= ~ECHO;       /* turn echo off, too */
    STTY(f, &sg);               /* set cbreak mode */
    GLOBAL(echofd) = f;         /* in case ^C hit (not perfect: still CBREAK) */

    read(f, &c, 1);             /* read our character */

#if (defined(USE_SYSV_TERMIO) || defined(USE_POSIX_TERMIOS))
    sg.c_cc[VMIN] = oldmin;     /* restore old values */
    sg.c_cc[VTIME] = oldtim;
    sg.sg_flags |= ICANON;      /* canonical mode on */
#else
    sg.sg_flags &= ~CBREAK;     /* cbreak mode off */
#endif
    sg.sg_flags |= ECHO;        /* turn echo on */
    STTY(f, &sg);               /* restore canonical mode */
    GLOBAL(echofd) = -1;

    return (int)(uch)c;
}


#else /* !ATH_BEO_UNX */
#ifndef VMS     /* VMS supplies its own variant of getch() */


int 
zgetch (
    Uz_Globs *pG,
    int f    /* file descriptor from which to read (must be open already) */
)
{
    char c, c2;

/*---------------------------------------------------------------------------
    Get a character from the given file descriptor without echo; can't fake
    CBREAK mode (i.e., newline required), but can get rid of all chars up to
    and including newline.
  ---------------------------------------------------------------------------*/

    echoff(f);
    read(f, &c, 1);
    if (c != '\n')
        do {
            read(f, &c2, 1);   /* throw away all other chars up thru newline */
        } while (c2 != '\n');
    echon();
    return (int)c;
}

#endif /* !VMS */
#endif /* ?ATH_BEO_UNX */

#endif /* UNZIP && !FUNZIP */
#endif /* !HAVE_WORKING_GETCH */


#if CRYPT                       /* getp() is only used with full encryption */

/*
 * Simple compile-time check for source compatibility between
 * zcrypt and ttyio:
 */
#if (!defined(CR_MAJORVER) || (CR_MAJORVER < 2) || (CR_MINORVER < 7))
   error:  This Info-ZIP tool requires zcrypt 2.7 or later.
#endif

/*
 * Get a password of length n-1 or less into *p using the prompt *m.
 * The entered password is not echoed.
 */

#ifdef HAVE_WORKING_GETCH
/*
 * For the AMIGA, getch() is defined as Agetch(), which is in
 * amiga/filedate.c; SAS/C 6.x provides a getch(), but since Agetch()
 * uses the infrastructure that is already in place in filedate.c, it is
 * smaller.  With this function, echoff() and echon() are not needed.
 *
 * For the MAC, a non-echo macgetch() function is defined in the MacOS
 * specific sources which uses the event handling mechanism of the
 * desktop window manager to get a character from the keyboard.
 *
 * For the other systems in this section, a non-echo getch() function
 * is either contained the C runtime library (conio package), or getch()
 * is defined as an alias for a similar system specific RTL function.
 */

#ifndef WINDLL   /* WINDLL does not support a console interface */
#ifndef QDOS     /* QDOS supplies a variant of this function */

/* This is the getp() function for all systems (with TTY type user interface)
 * that supply a working `non-echo' getch() function for "raw" console input.
 */
char *getp(pG, m, p, n)
    Uz_Globs *pG;
    const char *m;             /* prompt for password */
    char *p;                    /* return value: line input */
    int n;                      /* bytes available in p[] */
{
    char c;                     /* one-byte buffer for read() to use */
    int i;                      /* number of characters input */
    char *w;                    /* warning on retry */

    /* get password */
    w = "";
    do {
        fputs(w, stderr);       /* warning if back again */
        fputs(m, stderr);       /* display prompt and flush */
        fflush(stderr);
        i = 0;
        do {                    /* read line, keeping first n characters */
            if ((c = (char)getch()) == '\r')
                c = '\n';       /* until user hits CR */
            if (c == 8 || c == 127) {
                if (i > 0) i--; /* the `backspace' and `del' keys works */
            }
            else if (i < n)
                p[i++] = c;     /* truncate past n */
        } while (c != '\n');
        PUTC('\n', stderr);  fflush(stderr);
        w = "(line too long--try again)\n";
    } while (p[i-1] != '\n');
    p[i-1] = 0;                 /* terminate at newline */

    return p;                   /* return pointer to password */

} /* end function getp() */

#endif /* !QDOS */
#endif /* !WINDLL */


#else /* !HAVE_WORKING_GETCH */


#if (defined(ATH_BEO_UNX) || defined(__MINT__))

#ifndef _PATH_TTY
#  ifdef __MINT__
#    define _PATH_TTY ttyname(2)
#  else
#    define _PATH_TTY "/dev/tty"
#  endif
#endif

char *
getp (
    Uz_Globs *pG,
    const char *m,             /* prompt for password */
    char *p,                    /* return value: line input */
    int n                      /* bytes available in p[] */
)
{
    char c;                     /* one-byte buffer for read() to use */
    int i;                      /* number of characters input */
    char *w;                    /* warning on retry */
    int f;                      /* file descriptor for tty device */

#ifdef PASSWD_FROM_STDIN
    /* Read from stdin. This is unsafe if the password is stored on disk. */
    f = 0;
#else
    /* turn off echo on tty */

    if ((f = open(_PATH_TTY, 0)) == -1)
        return NULL;
#endif
    /* get password */
    w = "";
    do {
        fputs(w, stderr);       /* warning if back again */
        fputs(m, stderr);       /* prompt */
        fflush(stderr);
        i = 0;
        echoff(f);
        do {                    /* read line, keeping n */
            read(f, &c, 1);
            if (i < n)
                p[i++] = c;
        } while (c != '\n');
        echon();
        PUTC('\n', stderr);  fflush(stderr);
        w = "(line too long--try again)\n";
    } while (p[i-1] != '\n');
    p[i-1] = 0;                 /* terminate at newline */

#ifndef PASSWD_FROM_STDIN
    close(f);
#endif

    return p;                   /* return pointer to password */

} /* end function getp() */

#endif /* ATH_BEO_UNX || __MINT__ */



#if (defined(VMS) || defined(CMS_MVS))

char *
getp (
    Uz_Globs *pG,
    const char *m,             /* prompt for password */
    char *p,                    /* return value: line input */
    int n                      /* bytes available in p[] */
)
{
    char c;                     /* one-byte buffer for read() to use */
    int i;                      /* number of characters input */
    char *w;                    /* warning on retry */
    FILE *f;                    /* file structure for SYS$COMMAND device */

#ifdef PASSWD_FROM_STDIN
    f = stdin;
#else
    if ((f = fopen(ctermid(NULL), "r")) == NULL)
        return NULL;
#endif

    /* get password */
    fflush(stdout);
    w = "";
    do {
        if (*w)                 /* bug: VMS apparently adds \n to NULL fputs */
            fputs(w, stderr);   /* warning if back again */
        fputs(m, stderr);       /* prompt */
        fflush(stderr);
        i = 0;
        echoff(f);
        do {                    /* read line, keeping n */
            if ((c = (char)getc(f)) == '\r')
                c = '\n';
            if (i < n)
                p[i++] = c;
        } while (c != '\n');
        echon();
        PUTC('\n', stderr);  fflush(stderr);
        w = "(line too long--try again)\n";
    } while (p[i-1] != '\n');
    p[i-1] = 0;                 /* terminate at newline */
#ifndef PASSWD_FROM_STDIN
    fclose(f);
#endif

    return p;                   /* return pointer to password */

} /* end function getp() */

#endif /* VMS || CMS_MVS */
#endif /* ?HAVE_WORKING_GETCH */
#endif /* CRYPT */
#endif /* CRYPT || (UNZIP && !FUNZIP) */
