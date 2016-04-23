/*
  Copyright (c) 1990-2009 Info-ZIP.  All rights reserved.

  See the accompanying file LICENSE, version 2009-Jan-02 or later
  (the contents of which are also included in unzip.h) for terms of use.
  If, for some reason, all these files are missing, the Info-ZIP license
  also may be found at:  ftp://ftp.info-zip.org/pub/infozip/license.html
*/
/*---------------------------------------------------------------------------

  fileio.c

  This file contains routines for doing direct but relatively generic input/
  output, file-related sorts of things, plus some miscellaneous stuff.  Most
  of the stuff has to do with opening, closing, reading and/or writing files.

  Contains:  open_input_file()
             open_outfile()           (not: VMS, AOS/VS, CMSMVS, MACOS, TANDEM)
             undefer_input()
             defer_leftover_input()
             readbuf()
             readbyte()
             fillinbuf()
             seek_zipf()
             flush()                  (non-VMS)
             is_vms_varlen_txt()      (non-VMS, VMS_TEXT_CONV only)
             disk_error()             (non-VMS)
             UzpMessagePrnt()
             UzpMessageNull()         (DLL only)
             UzpInput()
             UzpMorePause()
             UzpPassword()            (non-WINDLL)
             handler()
             dos_to_unix_time()       (non-VMS, non-VM/CMS, non-MVS)
             check_for_newer()        (non-VMS, non-OS/2, non-VM/CMS, non-MVS)
             do_string()
             makeword()
             makelong()
             makeint64()
             fzofft()
             str2iso()                (CRYPT && NEED_STR2ISO, only)
             str2oem()                (CRYPT && NEED_STR2OEM, only)
             memset()                 (ZMEM only)
             memcpy()                 (ZMEM only)
             zstrnicmp()              (NO_STRNICMP only)
             zstat()                  (REGULUS only)
             plastchar()              (_MBCS only)
             uzmbclen()               (_MBCS && NEED_UZMBCLEN, only)
             uzmbschr()               (_MBCS && NEED_UZMBSCHR, only)
             uzmbsrchr()              (_MBCS && NEED_UZMBSRCHR, only)
             fLoadFarString()         (SMALL_MEM only)
             fLoadFarStringSmall()    (SMALL_MEM only)
             fLoadFarStringSmall2()   (SMALL_MEM only)
             zfstrcpy()               (SMALL_MEM only)
             zfstrcmp()               (SMALL_MEM && !(SFX || FUNZIP) only)

  ---------------------------------------------------------------------------*/


#define __FILEIO_C      /* identifies this source module */
#define UNZIP_INTERNAL
#include "unzip.h"
#ifdef WINDLL
#  ifdef POCKET_UNZIP
#    include "wince/intrface.h"
#  else
#    include "windll/windll.h"
#  endif
#  include <setjmp.h>
#endif
#include "crc32.h"
#include "crypt.h"
#include "ttyio.h"

/* setup of codepage conversion for decryption passwords */
#if CRYPT
#  if (defined(CRYP_USES_ISO2OEM) && !defined(IZ_ISO2OEM_ARRAY))
#    define IZ_ISO2OEM_ARRAY            /* pull in iso2oem[] table */
#  endif
#  if (defined(CRYP_USES_OEM2ISO) && !defined(IZ_OEM2ISO_ARRAY))
#    define IZ_OEM2ISO_ARRAY            /* pull in oem2iso[] table */
#  endif
#endif
#include "ebcdic.h"   /* definition/initialization of ebcdic[] */


/*
   Note: Under Windows, the maximum size of the buffer that can be used
   with any of the *printf calls is 16,384, so win_fprintf was used to
   feed the fprintf clone no more than 16K chunks at a time. This should
   be valid for anything up to 64K (and probably beyond, assuming your
   buffers are that big).
*/
#ifdef WINDLL
#  define WriteError(buf,len,strm) \
   (win_fprintf(pG, strm, (extent)len, (char far *)buf) != (int)(len))
#else /* !WINDLL */
#  ifdef USE_FWRITE
#    define WriteError(buf,len,strm) \
     ((extent)fwrite((char *)(buf),1,(extent)(len),strm) != (extent)(len))
#  else
#    define WriteError(buf,len,strm) \
     ((extent)write(fileno(strm),(char *)(buf),(extent)(len)) != (extent)(len))
#  endif
#endif /* ?WINDLL */

/*
   2005-09-16 SMS.
   On VMS, when output is redirected to a file, as in a command like
   "PIPE UNZIP -v > X.OUT", the output file is created with VFC record
   format, and multiple calls to write() or fwrite() will produce multiple
   records, even when there's no newline terminator in the buffer.
   The result is unsightly output with spurious newlines.  Using fprintf()
   instead of write() here, and disabling a fflush(stdout) in UzpMessagePrnt()
   below, together seem to solve the problem.

   According to the C RTL manual, "The write and decc$record_write
   functions always generate at least one record."  Also, "[T]he fwrite
   function always generates at least <number_items> records."  So,
   "fwrite(buf, len, 1, strm)" is much better ("1" record) than
   "fwrite(buf, 1, len, strm)" ("len" (1-character) records, _really_
   ugly), but neither is better than write().  Similarly, "The fflush
   function always generates a record if there is unwritten data in the
   buffer."  Apparently fprintf() buffers the stuff somewhere, and puts
   out a record (only) when it sees a newline.
*/
#  define WriteTxtErr(buf,len,strm)  WriteError(buf,len,strm)

#if (defined(USE_DEFLATE64) && defined(__16BIT__))
static int partflush OF((Uz_Globs *pG, uch *rawbuf, ulg size, int unshrink));
#endif
static int disk_error OF((Uz_Globs *pG));


/****************************/
/* Strings used in fileio.c */
/****************************/

static const char CannotOpenZipfile[] =
  "error:  cannot open zipfile [ %s ]\n        %s\n";

#if (!defined(VMS) && !defined(AOS_VS) && !defined(CMS_MVS) && !defined(MACOS))
#if (!defined(TANDEM))
#if (defined(ATH_BEO_THS_UNX) || defined(DOS_FLX_NLM_OS2_W32))
   static const char CannotDeleteOldFile[] =
     "error:  cannot delete old %s\n        %s\n";
#ifdef UNIXBACKUP
   static const char CannotRenameOldFile[] =
     "error:  cannot rename old %s\n        %s\n";
   static const char BackupSuffix[] = "~";
#endif
#endif /* ATH_BEO_THS_UNX || DOS_FLX_NLM_OS2_W32 */
#ifdef NOVELL_BUG_FAILSAFE
   static const char NovellBug[] =
     "error:  %s: stat() says does not exist, but fopen() found anyway\n";
#endif
   static const char CannotCreateFile[] =
     "error:  cannot create %s\n        %s\n";
#endif /* !TANDEM */
#endif /* !VMS && !AOS_VS && !CMS_MVS && !MACOS */

static const char ReadError[] = "error:  zipfile read error\n";
static const char FilenameTooLongTrunc[] =
  "warning:  filename too long--truncating.\n";
#ifdef UNICODE_SUPPORT
   static const char UFilenameTooLongTrunc[] =
     "warning:  Converted unicode filename too long--truncating.\n";
#endif
static const char ExtraFieldTooLong[] =
  "warning:  extra field too long (%d).  Ignoring...\n";

#ifdef WINDLL
   static const char DiskFullQuery[] =
     "%s:  write error (disk full?).\n";
#else
   static const char DiskFullQuery[] =
     "%s:  write error (disk full?).  Continue? (y/n/^C) ";
   static const char ZipfileCorrupt[] =
     "error:  zipfile probably corrupt (%s)\n";
#  ifdef SYMLINKS
     static const char FileIsSymLink[] =
       "%s exists and is a symbolic link%s.\n";
#  endif
#  ifdef MORE
     static const char MorePrompt[] = "--More--(%lu)";
#  endif
   static const char QuitPrompt[] =
     "--- Press `Q' to quit, or any other key to continue ---";
   static const char HidePrompt[] = /* "\r                       \r"; */
     "\r                                                         \r";
#  if CRYPT
#    ifdef MACOS
       /* SPC: are names on MacOS REALLY so much longer than elsewhere ??? */
       static const char PasswPrompt[] = "[%s]\n %s password: ";
#    else
       static const char PasswPrompt[] = "[%s] %s password: ";
#    endif
     static const char PasswPrompt2[] = "Enter password: ";
     static const char PasswRetry[] = "password incorrect--reenter: ";
#  endif /* CRYPT */
#endif /* !WINDLL */





/******************************/
/* Function open_input_file() */
/******************************/

int 
open_input_file (    /* return 1 if open failed */
    Uz_Globs *pG
)
{
    /*
     *  open the zipfile for reading and in BINARY mode to prevent cr/lf
     *  translation, which would corrupt the bitstreams
     */

#ifdef VMS
    (*(Uz_Globs *)pG).zipfd = open((*(Uz_Globs *)pG).zipfn, O_RDONLY, 0, OPNZIP_RMS_ARGS);
#else /* !VMS */
#ifdef MACOS
    (*(Uz_Globs *)pG).zipfd = open((*(Uz_Globs *)pG).zipfn, 0);
#else /* !MACOS */
#ifdef CMS_MVS
    (*(Uz_Globs *)pG).zipfd = vmmvs_open_infile(pG);
#else /* !CMS_MVS */
#ifdef USE_STRM_INPUT
    (*(Uz_Globs *)pG).zipfd = fopen((*(Uz_Globs *)pG).zipfn, FOPR);
#else /* !USE_STRM_INPUT */
    (*(Uz_Globs *)pG).zipfd = open((*(Uz_Globs *)pG).zipfn, O_RDONLY | O_BINARY);
#endif /* ?USE_STRM_INPUT */
#endif /* ?CMS_MVS */
#endif /* ?MACOS */
#endif /* ?VMS */

#ifdef USE_STRM_INPUT
    if ((*(Uz_Globs *)pG).zipfd == NULL)
#else
    /* if ((*(Uz_Globs *)pG).zipfd < 0) */  /* no good for Windows CE port */
    if ((*(Uz_Globs *)pG).zipfd == -1)
#endif
    {
        Info(slide, 0x401, ((char *)slide, LoadFarString(CannotOpenZipfile),
          (*(Uz_Globs *)pG).zipfn, strerror(errno)));
        return 1;
    }
    return 0;

} /* end function open_input_file() */




#if (!defined(VMS) && !defined(AOS_VS) && !defined(CMS_MVS) && !defined(MACOS))
#if (!defined(TANDEM))

/***************************/
/* Function open_outfile() */
/***************************/

int 
open_outfile (           /* return 1 if fail */
    Uz_Globs *pG
)
{
#ifdef DLL
    if ((*(Uz_Globs *)pG).redirect_data)
        return (redirect_outfile(pG) == FALSE);
#endif
#ifdef QDOS
    QFilename(pG, (*(Uz_Globs *)pG).filename);
#endif
#if (defined(DOS_FLX_NLM_OS2_W32) || defined(ATH_BEO_THS_UNX))
#ifdef BORLAND_STAT_BUG
    /* Borland 5.0's stat() barfs if the filename has no extension and the
     * file doesn't exist. */
    if (access((*(Uz_Globs *)pG).filename, 0) == -1) {
        FILE *tmp = fopen((*(Uz_Globs *)pG).filename, "wb+");

        /* file doesn't exist, so create a dummy file to keep stat() from
         * failing (will be over-written anyway) */
        fputc('0', tmp);  /* just to have something in the file */
        fclose(tmp);
    }
#endif /* BORLAND_STAT_BUG */
#ifdef SYMLINKS
    if (SSTAT((*(Uz_Globs *)pG).filename, &(*(Uz_Globs *)pG).statbuf) == 0 ||
        lstat((*(Uz_Globs *)pG).filename, &(*(Uz_Globs *)pG).statbuf) == 0)
#else
    if (SSTAT((*(Uz_Globs *)pG).filename, &(*(Uz_Globs *)pG).statbuf) == 0)
#endif /* ?SYMLINKS */
    {
        Trace((stderr, "open_outfile:  stat(%s) returns 0:  file exists\n",
          FnFilter1((*(Uz_Globs *)pG).filename)));
#ifdef UNIXBACKUP
        if (uO.B_flag) {    /* do backup */
            char *tname;
            z_stat tmpstat;
            int blen, flen, tlen;

            blen = strlen(BackupSuffix);
            flen = strlen((*(Uz_Globs *)pG).filename);
            tlen = flen + blen + 6;    /* includes space for 5 digits */
            if (tlen >= FILNAMSIZ) {   /* in case name is too long, truncate */
                tname = (char *)malloc(FILNAMSIZ);
                if (tname == NULL)
                    return 1;                 /* in case we run out of space */
                tlen = FILNAMSIZ - 1 - blen;
                strcpy(tname, (*(Uz_Globs *)pG).filename);    /* make backup name */
                tname[tlen] = '\0';
                if (flen > tlen) flen = tlen;
                tlen = FILNAMSIZ;
            } else {
                tname = (char *)malloc(tlen);
                if (tname == NULL)
                    return 1;                 /* in case we run out of space */
                strcpy(tname, (*(Uz_Globs *)pG).filename);    /* make backup name */
            }
            strcpy(tname+flen, BackupSuffix);

            if (IS_OVERWRT_ALL) {
                /* If there is a previous backup file, delete it,
                 * otherwise the following rename operation may fail.
                 */
                if (SSTAT(tname, &tmpstat) == 0)
                    unlink(tname);
            } else {
                /* Check if backupname exists, and, if it's true, try
                 * appending numbers of up to 5 digits (or the maximum
                 * "unsigned int" number on 16-bit systems) to the
                 * BackupSuffix, until an unused name is found.
                 */
                unsigned maxtail, i;
                char *numtail = tname + flen + blen;

                /* take account of the "unsigned" limit on 16-bit systems: */
                maxtail = ( ((~0) >= 99999L) ? 99999 : (~0) );
                switch (tlen - flen - blen - 1) {
                    case 4: maxtail = 9999; break;
                    case 3: maxtail = 999; break;
                    case 2: maxtail = 99; break;
                    case 1: maxtail = 9; break;
                    case 0: maxtail = 0; break;
                }
                /* while filename exists */
                for (i = 0; (i < maxtail) && (SSTAT(tname, &tmpstat) == 0);)
                    sprintf(numtail,"%u", ++i);
            }

            if (rename((*(Uz_Globs *)pG).filename, tname) != 0) {   /* move file */
                Info(slide, 0x401, ((char *)slide,
                  LoadFarString(CannotRenameOldFile),
                  FnFilter1((*(Uz_Globs *)pG).filename), strerror(errno)));
                free(tname);
                return 1;
            }
            Trace((stderr, "open_outfile:  %s now renamed into %s\n",
              FnFilter1((*(Uz_Globs *)pG).filename), FnFilter2(tname)));
            free(tname);
        } else
#endif /* UNIXBACKUP */
        {
#ifdef DOS_FLX_OS2_W32
            if (!((*(Uz_Globs *)pG).statbuf.st_mode & S_IWRITE)) {
                Trace((stderr,
                  "open_outfile:  existing file %s is read-only\n",
                  FnFilter1((*(Uz_Globs *)pG).filename)));
                chmod((*(Uz_Globs *)pG).filename, S_IREAD | S_IWRITE);
                Trace((stderr, "open_outfile:  %s now writable\n",
                  FnFilter1((*(Uz_Globs *)pG).filename)));
            }
#endif /* DOS_FLX_OS2_W32 */
#ifdef NLM
            /* Give the file read/write permission (non-POSIX shortcut) */
            chmod((*(Uz_Globs *)pG).filename, 0);
#endif /* NLM */
            if (unlink((*(Uz_Globs *)pG).filename) != 0) {
                Info(slide, 0x401, ((char *)slide,
                  LoadFarString(CannotDeleteOldFile),
                  FnFilter1((*(Uz_Globs *)pG).filename), strerror(errno)));
                return 1;
            }
            Trace((stderr, "open_outfile:  %s now deleted\n",
              FnFilter1((*(Uz_Globs *)pG).filename)));
        }
    }
#endif /* DOS_FLX_NLM_OS2_W32 || ATH_BEO_THS_UNX */
#ifdef RISCOS
    if (SWI_OS_File_7((*(Uz_Globs *)pG).filename,0xDEADDEAD,0xDEADDEAD,(*(Uz_Globs *)pG).lrec.ucsize)!=NULL) {
        Info(slide, 1, ((char *)slide, LoadFarString(CannotCreateFile),
          FnFilter1((*(Uz_Globs *)pG).filename), strerror(errno)));
        return 1;
    }
#endif /* RISCOS */
#ifdef TOPS20
    char *tfilnam;

    if ((tfilnam = (char *)malloc(2*strlen((*(Uz_Globs *)pG).filename)+1)) == (char *)NULL)
        return 1;
    strcpy(tfilnam, (*(Uz_Globs *)pG).filename);
    upper(tfilnam);
    enquote(tfilnam);
    if (((*(Uz_Globs *)pG).outfile = fopen(tfilnam, FOPW)) == (FILE *)NULL) {
        Info(slide, 1, ((char *)slide, LoadFarString(CannotCreateFile),
          tfilnam, strerror(errno)));
        free(tfilnam);
        return 1;
    }
    free(tfilnam);
#else /* !TOPS20 */
#ifdef MTS
    if (uO.aflag)
        (*(Uz_Globs *)pG).outfile = zfopen((*(Uz_Globs *)pG).filename, FOPWT);
    else
        (*(Uz_Globs *)pG).outfile = zfopen((*(Uz_Globs *)pG).filename, FOPW);
    if ((*(Uz_Globs *)pG).outfile == (FILE *)NULL) {
        Info(slide, 1, ((char *)slide, LoadFarString(CannotCreateFile),
          FnFilter1((*(Uz_Globs *)pG).filename), strerror(errno)));
        return 1;
    }
#else /* !MTS */
#ifdef DEBUG
    Info(slide, 1, ((char *)slide,
      "open_outfile:  doing fopen(%s) for reading\n", FnFilter1((*(Uz_Globs *)pG).filename)));
    if (((*(Uz_Globs *)pG).outfile = zfopen((*(Uz_Globs *)pG).filename, FOPR)) == (FILE *)NULL)
        Info(slide, 1, ((char *)slide,
          "open_outfile:  fopen(%s) for reading failed:  does not exist\n",
          FnFilter1((*(Uz_Globs *)pG).filename)));
    else {
        Info(slide, 1, ((char *)slide,
          "open_outfile:  fopen(%s) for reading succeeded:  file exists\n",
          FnFilter1((*(Uz_Globs *)pG).filename)));
        fclose((*(Uz_Globs *)pG).outfile);
    }
#endif /* DEBUG */
#ifdef NOVELL_BUG_FAILSAFE
    if ((*(Uz_Globs *)pG).dne && (((*(Uz_Globs *)pG).outfile = zfopen((*(Uz_Globs *)pG).filename, FOPR)) != (FILE *)NULL)) {
        Info(slide, 0x401, ((char *)slide, LoadFarString(NovellBug),
          FnFilter1((*(Uz_Globs *)pG).filename)));
        fclose((*(Uz_Globs *)pG).outfile);
        return 1;   /* with "./" fix in checkdir(), should never reach here */
    }
#endif /* NOVELL_BUG_FAILSAFE */
    Trace((stderr, "open_outfile:  doing fopen(%s) for writing\n",
      FnFilter1((*(Uz_Globs *)pG).filename)));
    {
#if defined(ATH_BE_UNX) || defined(AOS_VS) || defined(QDOS) || defined(TANDEM)
        mode_t umask_sav = umask(0077);
#endif
#if defined(SYMLINKS) || defined(QLZIP)
        /* These features require the ability to re-read extracted data from
           the output files. Output files are created with Read&Write access.
         */
        (*(Uz_Globs *)pG).outfile = zfopen((*(Uz_Globs *)pG).filename, FOPWR);
#else
        (*(Uz_Globs *)pG).outfile = zfopen((*(Uz_Globs *)pG).filename, FOPW);
#endif
#if defined(ATH_BE_UNX) || defined(AOS_VS) || defined(QDOS) || defined(TANDEM)
        umask(umask_sav);
#endif
    }
    if ((*(Uz_Globs *)pG).outfile == (FILE *)NULL) {
        Info(slide, 0x401, ((char *)slide, LoadFarString(CannotCreateFile),
          FnFilter1((*(Uz_Globs *)pG).filename), strerror(errno)));
        return 1;
    }
    Trace((stderr, "open_outfile:  fopen(%s) for writing succeeded\n",
      FnFilter1((*(Uz_Globs *)pG).filename)));
#endif /* !MTS */
#endif /* !TOPS20 */

#ifdef USE_FWRITE
#ifdef DOS_NLM_OS2_W32
    /* 16-bit MSC: buffer size must be strictly LESS than 32K (WSIZE):  bogus */
    setbuf((*(Uz_Globs *)pG).outfile, (char *)NULL);   /* make output unbuffered */
#else /* !DOS_NLM_OS2_W32 */
#ifndef RISCOS
#ifdef _IOFBF  /* make output fully buffered (works just about like write()) */
    setvbuf((*(Uz_Globs *)pG).outfile, (char *)slide, _IOFBF, WSIZE);
#else
    setbuf((*(Uz_Globs *)pG).outfile, (char *)slide);
#endif
#endif /* !RISCOS */
#endif /* ?DOS_NLM_OS2_W32 */
#endif /* USE_FWRITE */
#ifdef OS2_W32
    /* preallocate the final file size to prevent file fragmentation */
    SetFileSize((*(Uz_Globs *)pG).outfile, (*(Uz_Globs *)pG).lrec.ucsize);
#endif
    return 0;

} /* end function open_outfile() */

#endif /* !TANDEM */
#endif /* !VMS && !AOS_VS && !CMS_MVS && !MACOS */





/*
 * These functions allow NEXTBYTE to function without needing two bounds
 * checks.  Call defer_leftover_input() if you ever have filled (*(Uz_Globs *)pG).inbuf
 * by some means other than readbyte(), and you then want to start using
 * NEXTBYTE.  When going back to processing bytes without NEXTBYTE, call
 * undefer_input().  For example, extract_or_test_member brackets its
 * central section that does the decompression with these two functions.
 * If you need to check the number of bytes remaining in the current
 * file while using NEXTBYTE, check ((*(Uz_Globs *)pG).csize + (*(Uz_Globs *)pG).incnt), not (*(Uz_Globs *)pG).csize.
 */

/****************************/
/* function undefer_input() */
/****************************/

void 
undefer_input (Uz_Globs *pG)
{
    if ((*(Uz_Globs *)pG).incnt > 0)
        (*(Uz_Globs *)pG).csize += (*(Uz_Globs *)pG).incnt;
    if ((*(Uz_Globs *)pG).incnt_leftover > 0) {
        /* We know that "((*(Uz_Globs *)pG).csize < MAXINT)" so we can cast (*(Uz_Globs *)pG).csize to int:
         * This condition was checked when (*(Uz_Globs *)pG).incnt_leftover was set > 0 in
         * defer_leftover_input(), and it is NOT allowed to touch (*(Uz_Globs *)pG).csize
         * before calling undefer_input() when ((*(Uz_Globs *)pG).incnt_leftover > 0)
         * (single exception: see read_byte()'s  "(*(Uz_Globs *)pG).csize <= 0" handling) !!
         */
        (*(Uz_Globs *)pG).incnt = (*(Uz_Globs *)pG).incnt_leftover + (int)(*(Uz_Globs *)pG).csize;
        (*(Uz_Globs *)pG).inptr = (*(Uz_Globs *)pG).inptr_leftover - (int)(*(Uz_Globs *)pG).csize;
        (*(Uz_Globs *)pG).incnt_leftover = 0;
    } else if ((*(Uz_Globs *)pG).incnt < 0)
        (*(Uz_Globs *)pG).incnt = 0;
} /* end function undefer_input() */





/***********************************/
/* function defer_leftover_input() */
/***********************************/

void 
defer_leftover_input (Uz_Globs *pG)
{
    if ((zoff_t)(*(Uz_Globs *)pG).incnt > (*(Uz_Globs *)pG).csize) {
        /* ((*(Uz_Globs *)pG).csize < MAXINT), we can safely cast it to int !! */
        if ((*(Uz_Globs *)pG).csize < 0L)
            (*(Uz_Globs *)pG).csize = 0L;
        (*(Uz_Globs *)pG).inptr_leftover = (*(Uz_Globs *)pG).inptr + (int)(*(Uz_Globs *)pG).csize;
        (*(Uz_Globs *)pG).incnt_leftover = (*(Uz_Globs *)pG).incnt - (int)(*(Uz_Globs *)pG).csize;
        (*(Uz_Globs *)pG).incnt = (int)(*(Uz_Globs *)pG).csize;
    } else
        (*(Uz_Globs *)pG).incnt_leftover = 0;
    (*(Uz_Globs *)pG).csize -= (*(Uz_Globs *)pG).incnt;
} /* end function defer_leftover_input() */





/**********************/
/* Function readbuf() */
/**********************/

unsigned 
readbuf (   /* return number of bytes read into buf */
    Uz_Globs *pG,
    char *buf,
    register unsigned size
)
{
    register unsigned count;
    unsigned n;

    n = size;
    while (size) {
        if ((*(Uz_Globs *)pG).incnt <= 0) {
            if (((*(Uz_Globs *)pG).incnt = read((*(Uz_Globs *)pG).zipfd, (char *)(*(Uz_Globs *)pG).inbuf, INBUFSIZ)) == 0)
                return (n-size);
            else if ((*(Uz_Globs *)pG).incnt < 0) {
                /* another hack, but no real harm copying same thing twice */
                (*(*(Uz_Globs *)pG).message)((void *)&(*(Uz_Globs *)pG),
                  (uch *)LoadFarString(ReadError),  /* CANNOT use slide */
                  (ulg)strlen(LoadFarString(ReadError)), 0x401);
                return 0;  /* discarding some data; better than lock-up */
            }
            /* buffer ALWAYS starts on a block boundary:  */
            (*(Uz_Globs *)pG).cur_zipfile_bufstart += INBUFSIZ;
            (*(Uz_Globs *)pG).inptr = (*(Uz_Globs *)pG).inbuf;
        }
        count = MIN(size, (unsigned)(*(Uz_Globs *)pG).incnt);
        memcpy(buf, (*(Uz_Globs *)pG).inptr, count);
        buf += count;
        (*(Uz_Globs *)pG).inptr += count;
        (*(Uz_Globs *)pG).incnt -= count;
        size -= count;
    }
    return n;

} /* end function readbuf() */





/***********************/
/* Function readbyte() */
/***********************/

int 
readbyte (   /* refill inbuf and return a byte if available, else EOF */
    Uz_Globs *pG
)
{
    if ((*(Uz_Globs *)pG).mem_mode)
        return EOF;
    if ((*(Uz_Globs *)pG).csize <= 0) {
        (*(Uz_Globs *)pG).csize--;             /* for tests done after exploding */
        (*(Uz_Globs *)pG).incnt = 0;
        return EOF;
    }
    if ((*(Uz_Globs *)pG).incnt <= 0) {
        if (((*(Uz_Globs *)pG).incnt = read((*(Uz_Globs *)pG).zipfd, (char *)(*(Uz_Globs *)pG).inbuf, INBUFSIZ)) == 0) {
            return EOF;
        } else if ((*(Uz_Globs *)pG).incnt < 0) {  /* "fail" (abort, retry, ...) returns this */
            /* another hack, but no real harm copying same thing twice */
            (*(*(Uz_Globs *)pG).message)((void *)&(*(Uz_Globs *)pG),
              (uch *)LoadFarString(ReadError),
              (ulg)strlen(LoadFarString(ReadError)), 0x401);
            echon();
#ifdef WINDLL
            longjmp(dll_error_return, 1);
#else
            DESTROYGLOBALS();
            EXIT(PK_BADERR);    /* totally bailing; better than lock-up */
#endif
        }
        (*(Uz_Globs *)pG).cur_zipfile_bufstart += INBUFSIZ; /* always starts on block bndry */
        (*(Uz_Globs *)pG).inptr = (*(Uz_Globs *)pG).inbuf;
        defer_leftover_input(pG);           /* decrements (*(Uz_Globs *)pG).csize */
    }

#if CRYPT
    if ((*(Uz_Globs *)pG).pInfo->encrypted) {
        uch *p;
        int n;

        /* This was previously set to decrypt one byte beyond (*(Uz_Globs *)pG).csize, when
         * incnt reached that far.  GRR said, "but it's required:  why?"  This
         * was a bug in fillinbuf() -- was it also a bug here?
         */
        for (n = (*(Uz_Globs *)pG).incnt, p = (*(Uz_Globs *)pG).inptr;  n--;  p++)
            zdecode(*p);
    }
#endif /* CRYPT */

    --(*(Uz_Globs *)pG).incnt;
    return *(*(Uz_Globs *)pG).inptr++;

} /* end function readbyte() */





#if defined(USE_ZLIB) || defined(USE_BZIP2)

/************************/
/* Function fillinbuf() */
/************************/

int 
fillinbuf ( /* like readbyte() except returns number of bytes in inbuf */
    Uz_Globs *pG
)
{
    if ((*(Uz_Globs *)pG).mem_mode ||
                  ((*(Uz_Globs *)pG).incnt = read((*(Uz_Globs *)pG).zipfd, (char *)(*(Uz_Globs *)pG).inbuf, INBUFSIZ)) <= 0)
        return 0;
    (*(Uz_Globs *)pG).cur_zipfile_bufstart += INBUFSIZ;  /* always starts on a block boundary */
    (*(Uz_Globs *)pG).inptr = (*(Uz_Globs *)pG).inbuf;
    defer_leftover_input(pG);           /* decrements (*(Uz_Globs *)pG).csize */

#if CRYPT
    if ((*(Uz_Globs *)pG).pInfo->encrypted) {
        uch *p;
        int n;

        for (n = (*(Uz_Globs *)pG).incnt, p = (*(Uz_Globs *)pG).inptr;  n--;  p++)
            zdecode(*p);
    }
#endif /* CRYPT */

    return (*(Uz_Globs *)pG).incnt;

} /* end function fillinbuf() */

#endif /* USE_ZLIB || USE_BZIP2 */





/************************/
/* Function seek_zipf() */
/************************/

int 
seek_zipf (Uz_Globs *pG, zoff_t abs_offset)
{
/*
 *  Seek to the block boundary of the block which includes abs_offset,
 *  then read block into input buffer and set pointers appropriately.
 *  If block is already in the buffer, just set the pointers.  This function
 *  is used by do_seekable (process.c), extract_or_test_entrylist (extract.c)
 *  and do_string (fileio.c).  Also, a slightly modified version is embedded
 *  within extract_or_test_entrylist (extract.c).  readbyte() and readbuf()
 *  (fileio.c) are compatible.  NOTE THAT abs_offset is intended to be the
 *  "proper offset" (i.e., if there were no extra bytes prepended);
 *  cur_zipfile_bufstart contains the corrected offset.
 *
 *  Since seek_zipf() is never used during decompression, it is safe to
 *  use the slide[] buffer for the error message.
 *
 * returns PK error codes:
 *  PK_BADERR if effective offset in zipfile is negative
 *  PK_EOF if seeking past end of zipfile
 *  PK_OK when seek was successful
 */
    zoff_t request = abs_offset + (*(Uz_Globs *)pG).extra_bytes;
    zoff_t inbuf_offset = request % INBUFSIZ;
    zoff_t bufstart = request - inbuf_offset;

    if (request < 0) {
        Info(slide, 1, ((char *)slide, LoadFarStringSmall(SeekMsg),
             (*(Uz_Globs *)pG).zipfn, LoadFarString(ReportMsg)));
        return(PK_BADERR);
    } else if (bufstart != (*(Uz_Globs *)pG).cur_zipfile_bufstart) {
        Trace((stderr,
          "fpos_zip: abs_offset = %s, (*(Uz_Globs *)pG).extra_bytes = %s\n",
          FmZofft(abs_offset, NULL, NULL),
          FmZofft((*(Uz_Globs *)pG).extra_bytes, NULL, NULL)));
#ifdef USE_STRM_INPUT
        zfseeko((*(Uz_Globs *)pG).zipfd, bufstart, SEEK_SET);
        (*(Uz_Globs *)pG).cur_zipfile_bufstart = zftello((*(Uz_Globs *)pG).zipfd);
#else /* !USE_STRM_INPUT */
        (*(Uz_Globs *)pG).cur_zipfile_bufstart = zlseek((*(Uz_Globs *)pG).zipfd, bufstart, SEEK_SET);
#endif /* ?USE_STRM_INPUT */
        Trace((stderr,
          "       request = %s, (abs+extra) = %s, inbuf_offset = %s\n",
          FmZofft(request, NULL, NULL),
          FmZofft((abs_offset+(*(Uz_Globs *)pG).extra_bytes), NULL, NULL),
          FmZofft(inbuf_offset, NULL, NULL)));
        Trace((stderr, "       bufstart = %s, cur_zipfile_bufstart = %s\n",
          FmZofft(bufstart, NULL, NULL),
          FmZofft((*(Uz_Globs *)pG).cur_zipfile_bufstart, NULL, NULL)));
        if (((*(Uz_Globs *)pG).incnt = read((*(Uz_Globs *)pG).zipfd, (char *)(*(Uz_Globs *)pG).inbuf, INBUFSIZ)) <= 0)
            return(PK_EOF);
        (*(Uz_Globs *)pG).incnt -= (int)inbuf_offset;
        (*(Uz_Globs *)pG).inptr = (*(Uz_Globs *)pG).inbuf + (int)inbuf_offset;
    } else {
        (*(Uz_Globs *)pG).incnt += ((*(Uz_Globs *)pG).inptr-(*(Uz_Globs *)pG).inbuf) - (int)inbuf_offset;
        (*(Uz_Globs *)pG).inptr = (*(Uz_Globs *)pG).inbuf + (int)inbuf_offset;
    }
    return(PK_OK);
} /* end function seek_zipf() */





#ifndef VMS  /* for VMS use code in vms.c */

/********************/
/* Function flush() */   /* returns PK error codes: */
/********************/   /* if tflag => always 0; PK_DISK if write error */

int flush(pG, rawbuf, size, unshrink)
    Uz_Globs *pG;
    uch *rawbuf;
    ulg size;
    int unshrink;
#if (defined(USE_DEFLATE64) && defined(__16BIT__))
{
    int ret;

    /* On 16-bit systems (MSDOS, OS/2 1.x), the standard C library functions
     * cannot handle writes of 64k blocks at once.  For these systems, the
     * blocks to flush are split into pieces of 32k or less.
     */
    while (size > 0x8000L) {
        ret = partflush(pG, rawbuf, 0x8000L, unshrink);
        if (ret != PK_OK)
            return ret;
        size -= 0x8000L;
        rawbuf += (extent)0x8000;
    }
    return partflush(pG, rawbuf, size, unshrink);
} /* end function flush() */


/************************/
/* Function partflush() */  /* returns PK error codes: */
/************************/  /* if tflag => always 0; PK_DISK if write error */

static int partflush(pG, rawbuf, size, unshrink)
    Uz_Globs *pG;
    uch *rawbuf;        /* cannot be const, gets passed to (*(*(Uz_Globs *)pG).message)() */
    ulg size;
    int unshrink;
#endif /* USE_DEFLATE64 && __16BIT__ */
{
    uch *p;
    uch *q;
    uch *transbuf;
    /* static int didCRlast = FALSE;    moved to globals.h */


/*---------------------------------------------------------------------------
    Compute the CRC first; if testing or if disk is full, that's it.
  ---------------------------------------------------------------------------*/

    (*(Uz_Globs *)pG).crc32val = crc32((*(Uz_Globs *)pG).crc32val, rawbuf, (extent)size);

#ifdef DLL
    if (((*(Uz_Globs *)pG).statreportcb != NULL) &&
        (*(*(Uz_Globs *)pG).statreportcb)(pG, UZ_ST_IN_PROGRESS, (*(Uz_Globs *)pG).zipfn, (*(Uz_Globs *)pG).filename, NULL))
        return IZ_CTRLC;        /* cancel operation by user request */
#endif

    if (uO.tflag || size == 0L)  /* testing or nothing to write:  all done */
        return PK_OK;

    if ((*(Uz_Globs *)pG).disk_full)
        return PK_DISK;         /* disk already full:  ignore rest of file */

/*---------------------------------------------------------------------------
    Write the bytes rawbuf[0..size-1] to the output device, first converting
    end-of-lines and ASCII/EBCDIC as needed.  If SMALL_MEM or MED_MEM are NOT
    defined, outbuf is assumed to be at least as large as rawbuf and is not
    necessarily checked for overflow.
  ---------------------------------------------------------------------------*/

    if (!(*(Uz_Globs *)pG).pInfo->textmode) {   /* write raw binary data */
        /* GRR:  note that for standard MS-DOS compilers, size argument to
         * fwrite() can never be more than 65534, so WriteError macro will
         * have to be rewritten if size can ever be that large.  For now,
         * never more than 32K.  Also note that write() returns an int, which
         * doesn't necessarily limit size to 32767 bytes if write() is used
         * on 16-bit systems but does make it more of a pain; however, because
         * at least MSC 5.1 has a lousy implementation of fwrite() (as does
         * DEC Ultrix cc), write() is used anyway.
         */
#ifdef DLL
        if ((*(Uz_Globs *)pG).redirect_data) {
#ifdef NO_SLIDE_REDIR
            if (writeToMemory(pG, rawbuf, (extent)size)) return PK_ERR;
#else
            writeToMemory(pG, rawbuf, (extent)size);
#endif
        } else
#endif
        if (!uO.cflag && WriteError(rawbuf, size, (*(Uz_Globs *)pG).outfile))
            return disk_error(pG);
        else if (uO.cflag && (*(*(Uz_Globs *)pG).message)((void *)&(*(Uz_Globs *)pG), rawbuf, size, 0))
            return PK_OK;
    } else {   /* textmode:  aflag is true */
        if (unshrink) {
            /* rawbuf = outbuf */
            transbuf = (*(Uz_Globs *)pG).outbuf2;
        } else {
            /* rawbuf = slide */
            transbuf = (*(Uz_Globs *)pG).outbuf;
        }
        if ((*(Uz_Globs *)pG).newfile) {
            (*(Uz_Globs *)pG).didCRlast = FALSE;         /* no previous buffers written */
            (*(Uz_Globs *)pG).newfile = FALSE;
        }


    /*-----------------------------------------------------------------------
        Algorithm:  CR/LF => native; lone CR => native; lone LF => native.
        This routine is only for non-raw-VMS, non-raw-VM/CMS files (i.e.,
        stream-oriented files, not record-oriented).
      -----------------------------------------------------------------------*/

        /* else not VMS text */ {
            p = rawbuf;
            if (*p == LF && (*(Uz_Globs *)pG).didCRlast)
                ++p;
            (*(Uz_Globs *)pG).didCRlast = FALSE;
            for (q = transbuf;  (extent)(p-rawbuf) < (extent)size;  ++p) {
                if (*p == CR) {           /* lone CR or CR/LF: treat as EOL  */
                    PutNativeEOL
                    if ((extent)(p-rawbuf) == (extent)size-1)
                        /* last char in buffer */
                        (*(Uz_Globs *)pG).didCRlast = TRUE;
                    else if (p[1] == LF)  /* get rid of accompanying LF */
                        ++p;
                } else if (*p == LF)      /* lone LF */
                    PutNativeEOL
                else
#ifndef DOS_FLX_OS2_W32
                if (*p != CTRLZ)          /* lose all ^Z's */
#endif
                    *q++ = native(*p);

#if (defined(SMALL_MEM) || defined(MED_MEM))
# if (lenEOL == 1)   /* don't check unshrink:  both buffers small but equal */
                if (!unshrink)
# endif
                    /* check for danger of buffer overflow and flush */
                    if (q > transbuf+(extent)transbufsiz-lenEOL) {
                        Trace((stderr,
                          "p - rawbuf = %u   q-transbuf = %u   size = %lu\n",
                          (unsigned)(p-rawbuf), (unsigned)(q-transbuf), size));
                        if (!uO.cflag && WriteError(transbuf,
                            (extent)(q-transbuf), (*(Uz_Globs *)pG).outfile))
                            return disk_error(pG);
                        else if (uO.cflag && (*(*(Uz_Globs *)pG).message)((void *)&(*(Uz_Globs *)pG),
                                 transbuf, (ulg)(q-transbuf), 0))
                            return PK_OK;
                        q = transbuf;
                        continue;
                    }
#endif /* SMALL_MEM || MED_MEM */
            }
        }

    /*-----------------------------------------------------------------------
        Done translating:  write whatever we've got to file (or screen).
      -----------------------------------------------------------------------*/

        Trace((stderr, "p - rawbuf = %u   q-transbuf = %u   size = %lu\n",
          (unsigned)(p-rawbuf), (unsigned)(q-transbuf), size));
        if (q > transbuf) {
#ifdef DLL
            if ((*(Uz_Globs *)pG).redirect_data) {
                if (writeToMemory(pG, transbuf, (extent)(q-transbuf)))
                    return PK_ERR;
            } else
#endif
            if (!uO.cflag && WriteError(transbuf, (extent)(q-transbuf),
                (*(Uz_Globs *)pG).outfile))
                return disk_error(pG);
            else if (uO.cflag && (*(*(Uz_Globs *)pG).message)((void *)&(*(Uz_Globs *)pG), transbuf,
                (ulg)(q-transbuf), 0))
                return PK_OK;
        }
    }

    return PK_OK;

} /* end function flush() [resp. partflush() for 16-bit Deflate64 support] */





/*************************/
/* Function disk_error() */
/*************************/

static int 
disk_error (Uz_Globs *pG)
{
    /* OK to use slide[] here because this file is finished regardless */
    Info(slide, 0x4a1, ((char *)slide, LoadFarString(DiskFullQuery),
      FnFilter1((*(Uz_Globs *)pG).filename)));

#ifndef WINDLL
    fgets((*(Uz_Globs *)pG).answerbuf, sizeof((*(Uz_Globs *)pG).answerbuf), stdin);
    if (*(*(Uz_Globs *)pG).answerbuf == 'y')   /* stop writing to this file */
        (*(Uz_Globs *)pG).disk_full = 1;       /*  (outfile bad?), but new OK */
    else
#endif
        (*(Uz_Globs *)pG).disk_full = 2;       /* no:  exit program */

    return PK_DISK;

} /* end function disk_error() */

#endif /* !VMS */





/*****************************/
/* Function UzpMessagePrnt() */
/*****************************/

int UZ_EXP UzpMessagePrnt(pG, buf, size, flag)
    void *pG;   /* globals struct:  always passed */
    uch *buf;    /* preformatted string to be printed */
    ulg size;    /* length of string (may include nulls) */
    int flag;    /* flag bits */
{
    /* IMPORTANT NOTE:
     *    The name of the first parameter of UzpMessagePrnt(), which passes
     *    the "Uz_Globs" address, >>> MUST <<< be identical to the string
     *    expansion of the pG, macro in the REENTRANT case (see globals.h).
     *    This name identity is mandatory for the LoadFarString() macro
     *    (in the SMALL_MEM case) !!!
     */
    int error;
    uch *q=buf, *endbuf=buf+(unsigned)size;
#ifdef MORE
    uch *p=buf;
#if (defined(SCREENWIDTH) && defined(SCREENLWRAP))
    int islinefeed = FALSE;
#endif
#endif
    FILE *outfp;


/*---------------------------------------------------------------------------
    These tests are here to allow fine-tuning of UnZip's output messages,
    but none of them will do anything without setting the appropriate bit
    in the flag argument of every Info() statement which is to be turned
    *off*.  That is, all messages are currently turned on for all ports.
    To turn off *all* messages, use the UzpMessageNull() function instead
    of this one.
  ---------------------------------------------------------------------------*/

#if (defined(OS2) && defined(DLL))
    if (MSG_NO_DLL2(flag))  /* if OS/2 DLL bit is set, do NOT print this msg */
        return 0;
#endif
#ifdef WINDLL
    if (MSG_NO_WDLL(flag))
        return 0;
#endif
#ifdef WINDLL
    if (MSG_NO_WGUI(flag))
        return 0;
#endif
/*
#ifdef ACORN_GUI
    if (MSG_NO_AGUI(flag))
        return 0;
#endif
 */
#ifdef DLL                 /* don't display message if data is redirected */
    if (((Uz_Globs *)pG)->redirect_data &&
        !((Uz_Globs *)pG)->redirect_text)
        return 0;
#endif

    if (MSG_STDERR(flag) && !((Uz_Globs *)pG)->UzO.tflag)
        outfp = (FILE *)stderr;
    else
        outfp = (FILE *)stdout;

#ifdef QUERY_TRNEWLN
    /* some systems require termination of query prompts with '\n' to force
     * immediate display */
    if (MSG_MNEWLN(flag)) {   /* assumes writable buffer (e.g., slide[]) */
        *endbuf++ = '\n';     /*  with room for one more char at end of buf */
        ++size;               /*  (safe assumption:  only used for four */
    }                         /*  short queries in extract.c and fileio.c) */
#endif

    if (MSG_TNEWLN(flag)) {   /* again assumes writable buffer:  fragile... */
        if ((!size && !((Uz_Globs *)pG)->sol) ||
            (size && (endbuf[-1] != '\n')))
        {
            *endbuf++ = '\n';
            ++size;
        }
    }

#ifdef MORE
# ifdef SCREENSIZE
    /* room for --More-- and one line of overlap: */
#  if (defined(SCREENWIDTH) && defined(SCREENLWRAP))
    SCREENSIZE(&((Uz_Globs *)pG)->height, &((Uz_Globs *)pG)->width);
#  else
    SCREENSIZE(&((Uz_Globs *)pG)->height, (int *)NULL);
#  endif
    ((Uz_Globs *)pG)->height -= 2;
# else
    /* room for --More-- and one line of overlap: */
    ((Uz_Globs *)pG)->height = SCREENLINES - 2;
#  if (defined(SCREENWIDTH) && defined(SCREENLWRAP))
    ((Uz_Globs *)pG)->width = SCREENWIDTH;
#  endif
# endif
#endif /* MORE */

    if (MSG_LNEWLN(flag) && !((Uz_Globs *)pG)->sol) {
        /* not at start of line:  want newline */
#ifdef OS2DLL
        if (!((Uz_Globs *)pG)->redirect_text) {
#endif
            putc('\n', outfp);
            fflush(outfp);
#ifdef MORE
            if (((Uz_Globs *)pG)->M_flag)
            {
#if (defined(SCREENWIDTH) && defined(SCREENLWRAP))
                ((Uz_Globs *)pG)->chars = 0;
#endif
                ++((Uz_Globs *)pG)->numlines;
                ++((Uz_Globs *)pG)->lines;
                if (((Uz_Globs *)pG)->lines >= ((Uz_Globs *)pG)->height)
                    (*((Uz_Globs *)pG)->mpause)((void *)pG,
                      LoadFarString(MorePrompt), 1);
            }
#endif /* MORE */
            if (MSG_STDERR(flag) && ((Uz_Globs *)pG)->UzO.tflag &&
                !isatty(1) && isatty(2))
            {
                /* error output from testing redirected:  also send to stderr */
                putc('\n', stderr);
                fflush(stderr);
            }
#ifdef OS2DLL
        } else
           REDIRECTC('\n');
#endif
        ((Uz_Globs *)pG)->sol = TRUE;
    }

    /* put zipfile name, filename and/or error/warning keywords here */

#ifdef MORE
    if (((Uz_Globs *)pG)->M_flag
#ifdef OS2DLL
         && !((Uz_Globs *)pG)->redirect_text
#endif
                                                 )
    {
        while (p < endbuf) {
            if (*p == '\n') {
#if (defined(SCREENWIDTH) && defined(SCREENLWRAP))
                islinefeed = TRUE;
            } else if (SCREENLWRAP) {
                if (*p == '\r') {
                    ((Uz_Globs *)pG)->chars = 0;
                } else {
#  ifdef TABSIZE
                    if (*p == '\t')
                        ((Uz_Globs *)pG)->chars +=
                            (TABSIZE - (((Uz_Globs *)pG)->chars % TABSIZE));
                    else
#  endif
                        ++((Uz_Globs *)pG)->chars;

                    if (((Uz_Globs *)pG)->chars >= ((Uz_Globs *)pG)->width)
                        islinefeed = TRUE;
                }
            }
            if (islinefeed) {
                islinefeed = FALSE;
                ((Uz_Globs *)pG)->chars = 0;
#endif /* (SCREENWIDTH && SCREEN_LWRAP) */
                ++((Uz_Globs *)pG)->numlines;
                ++((Uz_Globs *)pG)->lines;
                if (((Uz_Globs *)pG)->lines >= ((Uz_Globs *)pG)->height)
                {
                    if ((error = WriteTxtErr(q, p-q+1, outfp)) != 0)
                        return error;
                    fflush(outfp);
                    ((Uz_Globs *)pG)->sol = TRUE;
                    q = p + 1;
                    (*((Uz_Globs *)pG)->mpause)((void *)pG,
                      LoadFarString(MorePrompt), 1);
                }
            }
            INCSTR(p);
        } /* end while */
        size = (ulg)(p - q);   /* remaining text */
    }
#endif /* MORE */

    if (size) {
#ifdef OS2DLL
        if (!((Uz_Globs *)pG)->redirect_text) {
#endif
            if ((error = WriteTxtErr(q, size, outfp)) != 0)
                return error;
#ifndef VMS     /* 2005-09-16 SMS.  See note at "WriteTxtErr()", above. */
            fflush(outfp);
#endif
            if (MSG_STDERR(flag) && ((Uz_Globs *)pG)->UzO.tflag &&
                !isatty(1) && isatty(2))
            {
                /* error output from testing redirected:  also send to stderr */
                if ((error = WriteTxtErr(q, size, stderr)) != 0)
                    return error;
                fflush(stderr);
            }
#ifdef OS2DLL
        } else {                /* GRR:  this is ugly:  hide with macro */
            if ((error = REDIRECTPRINT(q, size)) != 0)
                return error;
        }
#endif /* OS2DLL */
        ((Uz_Globs *)pG)->sol = (endbuf[-1] == '\n');
    }
    return 0;

} /* end function UzpMessagePrnt() */





#ifdef DLL

/*****************************/
/* Function UzpMessageNull() */  /* convenience routine for no output at all */
/*****************************/

int UZ_EXP UzpMessageNull(pG, buf, size, flag)
    void *pG;    /* globals struct:  always passed */
    uch *buf;     /* preformatted string to be printed */
    ulg size;     /* length of string (may include nulls) */
    int flag;     /* flag bits */
{
    return 0;

} /* end function UzpMessageNull() */

#endif /* DLL */





/***********************/
/* Function UzpInput() */   /* GRR:  this is a placeholder for now */
/***********************/

int UZ_EXP UzpInput(pG, buf, size, flag)
    void *pG;    /* globals struct:  always passed */
    uch *buf;     /* preformatted string to be printed */
    int *size;    /* (address of) size of buf and of returned string */
    int flag;     /* flag bits (bit 0: no echo) */
{
    /* tell picky compilers to shut up about "unused variable" warnings */
    pG = pG; buf = buf; flag = flag;

    *size = 0;
    return 0;

} /* end function UzpInput() */





#if (!defined(WINDLL) && !defined(MACOS))

/***************************/
/* Function UzpMorePause() */
/***************************/

void UZ_EXP 
UzpMorePause (
    void *pG,            /* globals struct:  always passed */
    const char *prompt,  /* "--More--" prompt */
    int flag             /* 0 = any char OK; 1 = accept only '\n', ' ', q */
)
{
    uch c;

/*---------------------------------------------------------------------------
    Print a prompt and wait for the user to press a key, then erase prompt
    if possible.
  ---------------------------------------------------------------------------*/

    if (!((Uz_Globs *)pG)->sol)
        fprintf(stderr, "\n");
    /* numlines may or may not be used: */
    fprintf(stderr, prompt, ((Uz_Globs *)pG)->numlines);
    fflush(stderr);
    if (flag & 1) {
        do {
            c = (uch)FGETCH(0);
        } while (
#ifdef THEOS
                 c != 17 &&     /* standard QUIT key */
#endif
                 c != '\r' && c != '\n' && c != ' ' && c != 'q' && c != 'Q');
    } else
        c = (uch)FGETCH(0);

    /* newline was not echoed, so cover up prompt line */
    fprintf(stderr, LoadFarString(HidePrompt));
    fflush(stderr);

    if (
#ifdef THEOS
        (c == 17) ||            /* standard QUIT key */
#endif
        (ToLower(c) == 'q')) {
        DESTROYGLOBALS();
        EXIT(PK_COOL);
    }

    ((Uz_Globs *)pG)->sol = TRUE;

#ifdef MORE
    /* space for another screen, enter for another line. */
    if ((flag & 1) && c == ' ')
        ((Uz_Globs *)pG)->lines = 0;
#endif /* MORE */

} /* end function UzpMorePause() */

#endif /* !WINDLL && !MACOS */




#ifndef WINDLL

/**************************/
/* Function UzpPassword() */
/**************************/

int UZ_EXP 
UzpPassword (
    void *pG,         /* pointer to UnZip's internal global vars */
    int *rcnt,         /* retry counter */
    char *pwbuf,       /* buffer for password */
    int size,          /* size of password buffer */
    const char *zfn,  /* name of zip archive */
    const char *efn  /* name of archive entry being processed */
)
{
#if CRYPT
    int r = IZ_PW_ENTERED;
    char *m;
    char *prompt;


    if (*rcnt == 0) {           /* First call for current entry */
        *rcnt = 2;
        if ((prompt = (char *)malloc(2*FILNAMSIZ + 15)) != (char *)NULL) {
            sprintf(prompt, LoadFarString(PasswPrompt),
                    FnFilter1(zfn), FnFilter2(efn));
            m = prompt;
        } else
            m = (char *)LoadFarString(PasswPrompt2);
    } else {                    /* Retry call, previous password was wrong */
        (*rcnt)--;
        prompt = NULL;
        m = (char *)LoadFarString(PasswRetry);
    }

    m = getp(pG, m, pwbuf, size);
    if (prompt != (char *)NULL) {
        free(prompt);
    }
    if (m == (char *)NULL) {
        r = IZ_PW_ERROR;
    }
    else if (*pwbuf == '\0') {
        r = IZ_PW_CANCELALL;
    }
    return r;

#else /* !CRYPT */
    /* tell picky compilers to shut up about "unused variable" warnings */
    pG = pG; rcnt = rcnt; pwbuf = pwbuf; size = size; zfn = zfn; efn = efn;

    return IZ_PW_ERROR;  /* internal error; function should never get called */
#endif /* ?CRYPT */

} /* end function UzpPassword() */





/**********************/
/* Function handler() */
/**********************/

void handler(signal)   /* upon interrupt, turn on echo and exit cleanly */
    int signal;
{
    GETGLOBALS();

#if !(defined(SIGBUS) || defined(SIGSEGV))      /* add a newline if not at */
    (*(*(Uz_Globs *)pG).message)((void *)&(*(Uz_Globs *)pG), slide, 0L, 0x41); /*  start of line (to stderr; */
#endif                                          /*  slide[] should be safe) */

    echon();

#ifdef SIGBUS
    if (signal == SIGBUS) {
        Info(slide, 0x421, ((char *)slide, LoadFarString(ZipfileCorrupt),
          "bus error"));
        DESTROYGLOBALS();
        EXIT(PK_BADERR);
    }
#endif /* SIGBUS */

#ifdef SIGILL
    if (signal == SIGILL) {
        Info(slide, 0x421, ((char *)slide, LoadFarString(ZipfileCorrupt),
          "illegal instruction"));
        DESTROYGLOBALS();
        EXIT(PK_BADERR);
    }
#endif /* SIGILL */

#ifdef SIGSEGV
    if (signal == SIGSEGV) {
        Info(slide, 0x421, ((char *)slide, LoadFarString(ZipfileCorrupt),
          "segmentation violation"));
        DESTROYGLOBALS();
        EXIT(PK_BADERR);
    }
#endif /* SIGSEGV */

    /* probably ctrl-C */
    DESTROYGLOBALS();
#if defined(AMIGA) && defined(__SASC)
    _abort();
#endif
    EXIT(IZ_CTRLC);       /* was EXIT(0), then EXIT(PK_ERR) */
}

#endif /* !WINDLL */




#if (!defined(VMS) && !defined(CMS_MVS))
#if (!defined(OS2) || defined(TIMESTAMP))

#if (!defined(HAVE_MKTIME) || defined(WIN32))
/* also used in amiga/filedate.c and win32/win32.c */
const ush ydays[] =
    { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 };
#endif

/*******************************/
/* Function dos_to_unix_time() */ /* used for freshening/updating/timestamps */
/*******************************/

time_t dos_to_unix_time(dosdatetime)
    ulg dosdatetime;
{
    time_t m_time;

#ifdef HAVE_MKTIME

    const time_t now = time(NULL);
    struct tm *tm;
#   define YRBASE  1900

    tm = localtime(&now);
    tm->tm_isdst = -1;          /* let mktime determine if DST is in effect */

    /* dissect date */
    tm->tm_year = ((int)(dosdatetime >> 25) & 0x7f) + (1980 - YRBASE);
    tm->tm_mon  = ((int)(dosdatetime >> 21) & 0x0f) - 1;
    tm->tm_mday = ((int)(dosdatetime >> 16) & 0x1f);

    /* dissect time */
    tm->tm_hour = (int)((unsigned)dosdatetime >> 11) & 0x1f;
    tm->tm_min  = (int)((unsigned)dosdatetime >> 5) & 0x3f;
    tm->tm_sec  = (int)((unsigned)dosdatetime << 1) & 0x3e;

    m_time = mktime(tm);
    NATIVE_TO_TIMET(m_time)     /* NOP unless MSC 7.0 or Macintosh */
    TTrace((stderr, "  final m_time  =       %lu\n", (ulg)m_time));

#else /* !HAVE_MKTIME */

    int yr, mo, dy, hh, mm, ss;
#ifdef TOPS20
#   define YRBASE  1900
    struct tmx *tmx;
    char temp[20];
#else /* !TOPS20 */
#   define YRBASE  1970
    int leap;
    unsigned days;
    struct tm *tm;
#if (!defined(MACOS) && !defined(RISCOS) && !defined(QDOS) && !defined(TANDEM))
#ifdef WIN32
    TIME_ZONE_INFORMATION tzinfo;
    DWORD res;
#else /* ! WIN32 */
#ifndef BSD4_4   /* GRR:  change to !defined(MODERN) ? */
#if (defined(BSD) || defined(MTS) || defined(__GO32__))
    struct timeb tbp;
#else /* !(BSD || MTS || __GO32__) */
#ifdef DECLARE_TIMEZONE
    extern time_t timezone;
#endif
#endif /* ?(BSD || MTS || __GO32__) */
#endif /* !BSD4_4 */
#endif /* ?WIN32 */
#endif /* !MACOS && !RISCOS && !QDOS && !TANDEM */
#endif /* ?TOPS20 */


    /* dissect date */
    yr = ((int)(dosdatetime >> 25) & 0x7f) + (1980 - YRBASE);
    mo = ((int)(dosdatetime >> 21) & 0x0f) - 1;
    dy = ((int)(dosdatetime >> 16) & 0x1f) - 1;

    /* dissect time */
    hh = (int)((unsigned)dosdatetime >> 11) & 0x1f;
    mm = (int)((unsigned)dosdatetime >> 5) & 0x3f;
    ss = (int)((unsigned)dosdatetime & 0x1f) * 2;

#ifdef TOPS20
    tmx = (struct tmx *)malloc(sizeof(struct tmx));
    sprintf (temp, "%02d/%02d/%02d %02d:%02d:%02d", mo+1, dy+1, yr, hh, mm, ss);
    time_parse(temp, tmx, (char *)0);
    m_time = time_make(tmx);
    free(tmx);

#else /* !TOPS20 */

/*---------------------------------------------------------------------------
    Calculate the number of seconds since the epoch, usually 1 January 1970.
  ---------------------------------------------------------------------------*/

    /* leap = # of leap yrs from YRBASE up to but not including current year */
    leap = ((yr + YRBASE - 1) / 4);   /* leap year base factor */

    /* calculate days from BASE to this year and add expired days this year */
    days = (yr * 365) + (leap - 492) + ydays[mo];

    /* if year is a leap year and month is after February, add another day */
    if ((mo > 1) && ((yr+YRBASE)%4 == 0) && ((yr+YRBASE) != 2100))
        ++days;                 /* OK through 2199 */

    /* convert date & time to seconds relative to 00:00:00, 01/01/YRBASE */
    m_time = (time_t)((unsigned long)(days + dy) * 86400L +
                      (unsigned long)hh * 3600L +
                      (unsigned long)(mm * 60 + ss));
      /* - 1;   MS-DOS times always rounded up to nearest even second */
    TTrace((stderr, "dos_to_unix_time:\n"));
    TTrace((stderr, "  m_time before timezone = %lu\n", (ulg)m_time));

/*---------------------------------------------------------------------------
    Adjust for local standard timezone offset.
  ---------------------------------------------------------------------------*/

#if (!defined(MACOS) && !defined(RISCOS) && !defined(QDOS) && !defined(TANDEM))
#ifdef WIN32
    /* account for timezone differences */
    res = GetTimeZoneInformation(&tzinfo);
    if (res != TIME_ZONE_ID_INVALID)
    {
    m_time += 60*(tzinfo.Bias);
#else /* !WIN32 */
#if (defined(BSD) || defined(MTS) || defined(__GO32__))
#ifdef BSD4_4
    if ( (dosdatetime >= DOSTIME_2038_01_18) &&
         (m_time < (time_t)0x70000000L) )
        m_time = U_TIME_T_MAX;  /* saturate in case of (unsigned) overflow */
    if (m_time < (time_t)0L)    /* a converted DOS time cannot be negative */
        m_time = S_TIME_T_MAX;  /*  -> saturate at max signed time_t value */
    if ((tm = localtime(&m_time)) != (struct tm *)NULL)
        m_time -= tm->tm_gmtoff;                /* sec. EAST of GMT: subtr. */
#else /* !(BSD4_4 */
    ftime(&tbp);                                /* get `timezone' */
    m_time += tbp.timezone * 60L;               /* seconds WEST of GMT:  add */
#endif /* ?(BSD4_4 || __EMX__) */
#else /* !(BSD || MTS || __GO32__) */
    /* tzset was already called at start of process_zipfiles() */
    /* tzset(); */              /* set `timezone' variable */
#ifndef __BEOS__                /* BeOS DR8 has no timezones... */
    m_time += timezone;         /* seconds WEST of GMT:  add */
#endif
#endif /* ?(BSD || MTS || __GO32__) */
#endif /* ?WIN32 */
    TTrace((stderr, "  m_time after timezone =  %lu\n", (ulg)m_time));

/*---------------------------------------------------------------------------
    Adjust for local daylight savings (summer) time.
  ---------------------------------------------------------------------------*/

#ifndef BSD4_4  /* (DST already added to tm_gmtoff, so skip tm_isdst) */
    if ( (dosdatetime >= DOSTIME_2038_01_18) &&
         (m_time < (time_t)0x70000000L) )
        m_time = U_TIME_T_MAX;  /* saturate in case of (unsigned) overflow */
    if (m_time < (time_t)0L)    /* a converted DOS time cannot be negative */
        m_time = S_TIME_T_MAX;  /*  -> saturate at max signed time_t value */
    TIMET_TO_NATIVE(m_time)     /* NOP unless MSC 7.0 or Macintosh */
    if (((tm = localtime((time_t *)&m_time)) != NULL) && tm->tm_isdst)
#ifdef WIN32
        m_time += 60L * tzinfo.DaylightBias;    /* adjust with DST bias */
    else
        m_time += 60L * tzinfo.StandardBias;    /* add StdBias (normally 0) */
#else
        m_time -= 60L * 60L;    /* adjust for daylight savings time */
#endif
    NATIVE_TO_TIMET(m_time)     /* NOP unless MSC 7.0 or Macintosh */
    TTrace((stderr, "  m_time after DST =       %lu\n", (ulg)m_time));
#endif /* !BSD4_4 */
#ifdef WIN32
    }
#endif
#endif /* !MACOS && !RISCOS && !QDOS && !TANDEM */
#endif /* ?TOPS20 */

#endif /* ?HAVE_MKTIME */

    if ( (dosdatetime >= DOSTIME_2038_01_18) &&
         (m_time < (time_t)0x70000000L) )
        m_time = U_TIME_T_MAX;  /* saturate in case of (unsigned) overflow */
    if (m_time < (time_t)0L)    /* a converted DOS time cannot be negative */
        m_time = S_TIME_T_MAX;  /*  -> saturate at max signed time_t value */

    return m_time;

} /* end function dos_to_unix_time() */

#endif /* !OS2 || TIMESTAMP */
#endif /* !VMS && !CMS_MVS */



#if (!defined(VMS) && !defined(OS2) && !defined(CMS_MVS))

/******************************/
/* Function check_for_newer() */  /* used for overwriting/freshening/updating */
/******************************/

int 
check_for_newer (  /* return 1 if existing file is newer */
    Uz_Globs *pG,                           /*  or equal; 0 if older; -1 if doesn't */
    char *filename                  /*  exist yet */
)
{
    time_t existing, archive;
#ifdef USE_EF_UT_TIME
    iztimes z_utime;
#endif
#ifdef AOS_VS
    long    dyy, dmm, ddd, dhh, dmin, dss;


    dyy = (lrec.last_mod_dos_datetime >> 25) + 1980;
    dmm = (lrec.last_mod_dos_datetime >> 21) & 0x0f;
    ddd = (lrec.last_mod_dos_datetime >> 16) & 0x1f;
    dhh = (lrec.last_mod_dos_datetime >> 11) & 0x1f;
    dmin = (lrec.last_mod_dos_datetime >> 5) & 0x3f;
    dss = (lrec.last_mod_dos_datetime & 0x1f) * 2;

    /* under AOS/VS, file times can only be set at creation time,
     * with the info in a special DG format.  Make sure we can create
     * it here - we delete it later & re-create it, whether or not
     * it exists now.
     */
    if (!zvs_create(filename, (((ulg)dgdate(dmm, ddd, dyy)) << 16) |
        (dhh*1800L + dmin*30L + dss/2L), -1L, -1L, (char *) -1, -1, -1, -1))
        return DOES_NOT_EXIST;
#endif /* AOS_VS */

    Trace((stderr, "check_for_newer:  doing stat(%s)\n", FnFilter1(filename)));
    if (SSTAT(filename, &(*(Uz_Globs *)pG).statbuf)) {
        Trace((stderr,
          "check_for_newer:  stat(%s) returns %d:  file does not exist\n",
          FnFilter1(filename), SSTAT(filename, &(*(Uz_Globs *)pG).statbuf)));
#ifdef SYMLINKS
        Trace((stderr, "check_for_newer:  doing lstat(%s)\n",
          FnFilter1(filename)));
        /* GRR OPTION:  could instead do this test ONLY if (*(Uz_Globs *)pG).symlnk is true */
        if (lstat(filename, &(*(Uz_Globs *)pG).statbuf) == 0) {
            Trace((stderr,
              "check_for_newer:  lstat(%s) returns 0:  symlink does exist\n",
              FnFilter1(filename)));
            if (QCOND2 && !IS_OVERWRT_ALL)
                Info(slide, 0, ((char *)slide, LoadFarString(FileIsSymLink),
                  FnFilter1(filename), " with no real file"));
            return EXISTS_AND_OLDER;   /* symlink dates are meaningless */
        }
#endif /* SYMLINKS */
        return DOES_NOT_EXIST;
    }
    Trace((stderr, "check_for_newer:  stat(%s) returns 0:  file exists\n",
      FnFilter1(filename)));

#ifdef SYMLINKS
    /* GRR OPTION:  could instead do this test ONLY if (*(Uz_Globs *)pG).symlnk is true */
    if (lstat(filename, &(*(Uz_Globs *)pG).statbuf) == 0 && S_ISLNK((*(Uz_Globs *)pG).statbuf.st_mode)) {
        Trace((stderr, "check_for_newer:  %s is a symbolic link\n",
          FnFilter1(filename)));
        if (QCOND2 && !IS_OVERWRT_ALL)
            Info(slide, 0, ((char *)slide, LoadFarString(FileIsSymLink),
              FnFilter1(filename), ""));
        return EXISTS_AND_OLDER;   /* symlink dates are meaningless */
    }
#endif /* SYMLINKS */

    NATIVE_TO_TIMET((*(Uz_Globs *)pG).statbuf.st_mtime)   /* NOP unless MSC 7.0 or Macintosh */

#ifdef USE_EF_UT_TIME
    /* The `Unix extra field mtime' should be used for comparison with the
     * time stamp of the existing file >>>ONLY<<< when the EF info is also
     * used to set the modification time of the extracted file.
     */
    if ((*(Uz_Globs *)pG).extra_field &&
#ifdef IZ_CHECK_TZ
        (*(Uz_Globs *)pG).tz_is_valid &&
#endif
        (ef_scan_for_izux((*(Uz_Globs *)pG).extra_field, (*(Uz_Globs *)pG).lrec.extra_field_length, 0,
                          (*(Uz_Globs *)pG).lrec.last_mod_dos_datetime, &z_utime, NULL)
         & EB_UT_FL_MTIME))
    {
        TTrace((stderr, "check_for_newer:  using Unix extra field mtime\n"));
        existing = (*(Uz_Globs *)pG).statbuf.st_mtime;
        archive  = z_utime.mtime;
    } else {
        /* round up existing filetime to nearest 2 seconds for comparison,
         * but saturate in case of arithmetic overflow
         */
        existing = (((*(Uz_Globs *)pG).statbuf.st_mtime & 1) &&
                    ((*(Uz_Globs *)pG).statbuf.st_mtime + 1 > (*(Uz_Globs *)pG).statbuf.st_mtime)) ?
                   (*(Uz_Globs *)pG).statbuf.st_mtime + 1 : (*(Uz_Globs *)pG).statbuf.st_mtime;
        archive  = dos_to_unix_time((*(Uz_Globs *)pG).lrec.last_mod_dos_datetime);
    }
#else /* !USE_EF_UT_TIME */
    /* round up existing filetime to nearest 2 seconds for comparison,
     * but saturate in case of arithmetic overflow
     */
    existing = (((*(Uz_Globs *)pG).statbuf.st_mtime & 1) &&
                ((*(Uz_Globs *)pG).statbuf.st_mtime + 1 > (*(Uz_Globs *)pG).statbuf.st_mtime)) ?
               (*(Uz_Globs *)pG).statbuf.st_mtime + 1 : (*(Uz_Globs *)pG).statbuf.st_mtime;
    archive  = dos_to_unix_time((*(Uz_Globs *)pG).lrec.last_mod_dos_datetime);
#endif /* ?USE_EF_UT_TIME */

    TTrace((stderr, "check_for_newer:  existing %lu, archive %lu, e-a %ld\n",
      (ulg)existing, (ulg)archive, (long)(existing-archive)));

    return (existing >= archive);

} /* end function check_for_newer() */

#endif /* !VMS && !OS2 && !CMS_MVS */





/************************/
/* Function do_string() */
/************************/

int 
do_string (   /* return PK-type error code */
    Uz_Globs *pG,
    unsigned int length,        /* without prototype, ush converted to this */
    int option
)
{
    unsigned comment_bytes_left;
    unsigned int block_len;
    int error=PK_OK;
#ifdef AMIGA
    char tmp_fnote[2 * AMIGA_FILENOTELEN];   /* extra room for squozen chars */
#endif


/*---------------------------------------------------------------------------
    This function processes arbitrary-length (well, usually) strings.  Four
    major options are allowed:  SKIP, wherein the string is skipped (pretty
    logical, eh?); DISPLAY, wherein the string is printed to standard output
    after undergoing any necessary or unnecessary character conversions;
    DS_FN, wherein the string is put into the filename[] array after under-
    going appropriate conversions (including case-conversion, if that is
    indicated: see the global variable pInfo->lcflag); and EXTRA_FIELD,
    wherein the `string' is assumed to be an extra field and is copied to
    the (freshly malloced) buffer (*(Uz_Globs *)pG).extra_field.  The third option should
    be OK since filename is dimensioned at 1025, but we check anyway.

    The string, by the way, is assumed to start at the current file-pointer
    position; its length is given by 'length'.  So start off by checking the
    length of the string:  if zero, we're already done.
  ---------------------------------------------------------------------------*/

    if (!length)
        return PK_COOL;

    switch (option) {

#if (defined(SFX) && defined(CHEAP_SFX_AUTORUN))
    /*
     * Special case: See if the comment begins with an autorun command line.
     * Save that and display (or skip) the remainder.
     */

    case CHECK_AUTORUN:
    case CHECK_AUTORUN_Q:
        comment_bytes_left = length;
        if (length >= 10)
        {
            block_len = readbuf(pG, (char *)(*(Uz_Globs *)pG).outbuf, 10);
            if (block_len == 0)
                return PK_EOF;
            comment_bytes_left -= block_len;
            (*(Uz_Globs *)pG).outbuf[block_len] = '\0';
            if (!strcmp((char *)(*(Uz_Globs *)pG).outbuf, "$AUTORUN$>")) {
                char *eol;
                length -= 10;
                block_len = readbuf(pG, (*(Uz_Globs *)pG).autorun_command,
                                    MIN(length, sizeof((*(Uz_Globs *)pG).autorun_command)-1));
                if (block_len == 0)
                    return PK_EOF;
                comment_bytes_left -= block_len;
                (*(Uz_Globs *)pG).autorun_command[block_len] = '\0';
                A_TO_N((*(Uz_Globs *)pG).autorun_command);
                eol = strchr((*(Uz_Globs *)pG).autorun_command, '\n');
                if (!eol)
                    eol = (*(Uz_Globs *)pG).autorun_command + strlen((*(Uz_Globs *)pG).autorun_command) - 1;
                length -= eol + 1 - (*(Uz_Globs *)pG).autorun_command;
                while (eol >= (*(Uz_Globs *)pG).autorun_command && isspace(*eol))
                    *eol-- = '\0';
#if (defined(WIN32) && !defined(_WIN32_WCE))
                /* Win9x console always uses OEM character coding, and
                   WinNT console is set to OEM charset by default, too */
                INTERN_TO_OEM((*(Uz_Globs *)pG).autorun_command, (*(Uz_Globs *)pG).autorun_command);
#endif /* (WIN32 && !_WIN32_WCE) */
            }
        }
        if (option == CHECK_AUTORUN_Q)  /* don't display the remainder */
            length = 0;
        /* seek to beginning of remaining part of comment -- rewind if */
        /* displaying entire comment, or skip to end if discarding it  */
        seek_zipf(pG, (*(Uz_Globs *)pG).cur_zipfile_bufstart - (*(Uz_Globs *)pG).extra_bytes +
                  ((*(Uz_Globs *)pG).inptr - (*(Uz_Globs *)pG).inbuf) + comment_bytes_left - length);
        if (!length)
            break;
        /*  FALL THROUGH...  */
#endif /* SFX && CHEAP_SFX_AUTORUN */

    /*
     * First normal case:  print string on standard output.  First set loop
     * variables, then loop through the comment in chunks of OUTBUFSIZ bytes,
     * converting formats and printing as we go.  The second half of the
     * loop conditional was added because the file might be truncated, in
     * which case comment_bytes_left will remain at some non-zero value for
     * all time.  outbuf and slide are used as scratch buffers because they
     * are available (we should be either before or in between any file pro-
     * cessing).
     */

    case DISPLAY:
    case DISPL_8:
        comment_bytes_left = length;
        block_len = OUTBUFSIZ;       /* for the while statement, first time */
        while (comment_bytes_left > 0 && block_len > 0) {
            register uch *p = (*(Uz_Globs *)pG).outbuf;
            register uch *q = (*(Uz_Globs *)pG).outbuf;

            if ((block_len = readbuf(pG, (char *)(*(Uz_Globs *)pG).outbuf,
                   MIN((unsigned)OUTBUFSIZ, comment_bytes_left))) == 0)
                return PK_EOF;
            comment_bytes_left -= block_len;

            /* this is why we allocated an extra byte for outbuf:  terminate
             *  with zero (ASCIIZ) */
            (*(Uz_Globs *)pG).outbuf[block_len] = '\0';

            /* remove all ASCII carriage returns from comment before printing
             * (since used before A_TO_N(), check for CR instead of '\r')
             */
            while (*p) {
                while (*p == CR)
                    ++p;
                *q++ = *p++;
            }
            /* could check whether (p - outbuf) == block_len here */
            *q = '\0';

            if (option == DISPL_8) {
                /* translate the text coded in the entry's host-dependent
                   "extended ASCII" charset into the compiler's (system's)
                   internal text code page */
                Ext_ASCII_TO_Native((char *)(*(Uz_Globs *)pG).outbuf, (*(Uz_Globs *)pG).pInfo->hostnum,
                                    (*(Uz_Globs *)pG).pInfo->hostver, (*(Uz_Globs *)pG).pInfo->HasUxAtt,
                                    FALSE);
#ifdef WINDLL
                /* translate to ANSI (RTL internal codepage may be OEM) */
                INTERN_TO_ISO((char *)(*(Uz_Globs *)pG).outbuf, (char *)(*(Uz_Globs *)pG).outbuf);
#else /* !WINDLL */
#if (defined(WIN32) && !defined(_WIN32_WCE))
                /* Win9x console always uses OEM character coding, and
                   WinNT console is set to OEM charset by default, too */
                INTERN_TO_OEM((char *)(*(Uz_Globs *)pG).outbuf, (char *)(*(Uz_Globs *)pG).outbuf);
#endif /* (WIN32 && !_WIN32_WCE) */
#endif /* ?WINDLL */
            } else {
                A_TO_N((*(Uz_Globs *)pG).outbuf);   /* translate string to native */
            }

#ifdef WINDLL
            /* ran out of local mem -- had to cheat */
            win_fprintf((void *)&(*(Uz_Globs *)pG), stdout, (extent)(q-(*(Uz_Globs *)pG).outbuf),
                        (char *)(*(Uz_Globs *)pG).outbuf);
            win_fprintf((void *)&(*(Uz_Globs *)pG), stdout, 2, (char *)"\n\n");
#else /* !WINDLL */
#ifdef NOANSIFILT       /* GRR:  can ANSI be used with EBCDIC? */
            (*(*(Uz_Globs *)pG).message)((void *)&(*(Uz_Globs *)pG), (*(Uz_Globs *)pG).outbuf, (ulg)(q-(*(Uz_Globs *)pG).outbuf), 0);
#else /* ASCII, filter out ANSI escape sequences and handle ^S (pause) */
            p = (*(Uz_Globs *)pG).outbuf - 1;
            q = slide;
            while (*++p) {
                int pause = FALSE;

                if (*p == 0x1B) {          /* ASCII escape char */
                    *q++ = '^';
                    *q++ = '[';
                } else if (*p == 0x13) {   /* ASCII ^S (pause) */
                    pause = TRUE;
                    if (p[1] == LF)        /* ASCII LF */
                        *q++ = *++p;
                    else if (p[1] == CR && p[2] == LF) {  /* ASCII CR LF */
                        *q++ = *++p;
                        *q++ = *++p;
                    }
                } else
                    *q++ = *p;
                if ((unsigned)(q-slide) > WSIZE-3 || pause) {   /* flush */
                    (*(*(Uz_Globs *)pG).message)((void *)&(*(Uz_Globs *)pG), slide, (ulg)(q-slide), 0);
                    q = slide;
                    if (pause && (*(Uz_Globs *)pG).extract_flag) /* don't pause for list/test */
                        (*(*(Uz_Globs *)pG).mpause)((void *)&(*(Uz_Globs *)pG), LoadFarString(QuitPrompt), 0);
                }
            }
            (*(*(Uz_Globs *)pG).message)((void *)&(*(Uz_Globs *)pG), slide, (ulg)(q-slide), 0);
#endif /* ?NOANSIFILT */
#endif /* ?WINDLL */
        }
        /* add '\n' if not at start of line */
        (*(*(Uz_Globs *)pG).message)((void *)&(*(Uz_Globs *)pG), slide, 0L, 0x40);
        break;

    /*
     * Second case:  read string into filename[] array.  The filename should
     * never ever be longer than FILNAMSIZ-1 (1024), but for now we'll check,
     * just to be sure.
     */

    case DS_FN:
    case DS_FN_L:
#ifdef UNICODE_SUPPORT
        /* get the whole filename as need it for Unicode checksum */
        if ((*(Uz_Globs *)pG).fnfull_bufsize <= length) {
            extent fnbufsiz = FILNAMSIZ;

            if (fnbufsiz <= length)
                fnbufsiz = length + 1;
            if ((*(Uz_Globs *)pG).filename_full)
                free((*(Uz_Globs *)pG).filename_full);
            (*(Uz_Globs *)pG).filename_full = malloc(fnbufsiz);
            if ((*(Uz_Globs *)pG).filename_full == NULL)
                return PK_MEM;
            (*(Uz_Globs *)pG).fnfull_bufsize = fnbufsiz;
        }
        if (readbuf(pG, (*(Uz_Globs *)pG).filename_full, length) == 0)
            return PK_EOF;
        (*(Uz_Globs *)pG).filename_full[length] = '\0';      /* terminate w/zero:  ASCIIZ */

        /* if needed, chop off end so standard filename is a valid length */
        if (length >= FILNAMSIZ) {
            Info(slide, 0x401, ((char *)slide,
              LoadFarString(FilenameTooLongTrunc)));
            error = PK_WARN;
            length = FILNAMSIZ - 1;
        }
        /* no excess size */
        block_len = 0;
        strncpy((*(Uz_Globs *)pG).filename, (*(Uz_Globs *)pG).filename_full, length);
        (*(Uz_Globs *)pG).filename[length] = '\0';      /* terminate w/zero:  ASCIIZ */
#else /* !UNICODE_SUPPORT */
        if (length >= FILNAMSIZ) {
            Info(slide, 0x401, ((char *)slide,
              LoadFarString(FilenameTooLongTrunc)));
            error = PK_WARN;
            /* remember excess length in block_len */
            block_len = length - (FILNAMSIZ - 1);
            length = FILNAMSIZ - 1;
        } else
            /* no excess size */
            block_len = 0;
        if (readbuf(pG, (*(Uz_Globs *)pG).filename, length) == 0)
            return PK_EOF;
        (*(Uz_Globs *)pG).filename[length] = '\0';      /* terminate w/zero:  ASCIIZ */
#endif /* ?UNICODE_SUPPORT */

        /* translate the Zip entry filename coded in host-dependent "extended
           ASCII" into the compiler's (system's) internal text code page */
        Ext_ASCII_TO_Native((*(Uz_Globs *)pG).filename, (*(Uz_Globs *)pG).pInfo->hostnum, (*(Uz_Globs *)pG).pInfo->hostver,
                            (*(Uz_Globs *)pG).pInfo->HasUxAtt, (option == DS_FN_L));

        if ((*(Uz_Globs *)pG).pInfo->lcflag)      /* replace with lowercase filename */
            STRLOWER((*(Uz_Globs *)pG).filename, (*(Uz_Globs *)pG).filename);

        if ((*(Uz_Globs *)pG).pInfo->vollabel && length > 8 && (*(Uz_Globs *)pG).filename[8] == '.') {
            char *p = (*(Uz_Globs *)pG).filename+8;
            while (*p++)
                p[-1] = *p;  /* disk label, and 8th char is dot:  remove dot */
        }

        if (!block_len)         /* no overflow, we're done here */
            break;

        /*
         * We truncated the filename, so print what's left and then fall
         * through to the SKIP routine.
         */
        Info(slide, 0x401, ((char *)slide, "[ %s ]\n", FnFilter1((*(Uz_Globs *)pG).filename)));
        length = block_len;     /* SKIP the excess bytes... */
        /*  FALL THROUGH...  */

    /*
     * Third case:  skip string, adjusting readbuf's internal variables
     * as necessary (and possibly skipping to and reading a new block of
     * data).
     */

    case SKIP:
        /* cur_zipfile_bufstart already takes account of extra_bytes, so don't
         * correct for it twice: */
        seek_zipf(pG, (*(Uz_Globs *)pG).cur_zipfile_bufstart - (*(Uz_Globs *)pG).extra_bytes +
                  ((*(Uz_Globs *)pG).inptr-(*(Uz_Globs *)pG).inbuf) + length);
        break;

    /*
     * Fourth case:  assume we're at the start of an "extra field"; malloc
     * storage for it and read data into the allocated space.
     */

    case EXTRA_FIELD:
        if ((*(Uz_Globs *)pG).extra_field != (uch *)NULL)
            free((*(Uz_Globs *)pG).extra_field);
        if (((*(Uz_Globs *)pG).extra_field = (uch *)malloc(length)) == (uch *)NULL) {
            Info(slide, 0x401, ((char *)slide, LoadFarString(ExtraFieldTooLong),
              length));
            /* cur_zipfile_bufstart already takes account of extra_bytes,
             * so don't correct for it twice: */
            seek_zipf(pG, (*(Uz_Globs *)pG).cur_zipfile_bufstart - (*(Uz_Globs *)pG).extra_bytes +
                      ((*(Uz_Globs *)pG).inptr-(*(Uz_Globs *)pG).inbuf) + length);
        } else {
            if (readbuf(pG, (char *)(*(Uz_Globs *)pG).extra_field, length) == 0)
                return PK_EOF;
            /* Looks like here is where extra fields are read */
            getZip64Data(pG, (*(Uz_Globs *)pG).extra_field, length);
#ifdef UNICODE_SUPPORT
            (*(Uz_Globs *)pG).unipath_filename = NULL;
            if ((*(Uz_Globs *)pG).UzO.U_flag < 2) {
              /* check if GPB11 (General Purpuse Bit 11) is set indicating
                 the standard path and comment are UTF-8 */
              if ((*(Uz_Globs *)pG).pInfo->GPFIsUTF8) {
                /* if GPB11 set then filename_full is untruncated UTF-8 */
                (*(Uz_Globs *)pG).unipath_filename = (*(Uz_Globs *)pG).filename_full;
              } else {
                /* Get the Unicode fields if exist */
                getUnicodeData(pG, (*(Uz_Globs *)pG).extra_field, length);
                if ((*(Uz_Globs *)pG).unipath_filename && strlen((*(Uz_Globs *)pG).unipath_filename) == 0) {
                  /* the standard filename field is UTF-8 */
                  free((*(Uz_Globs *)pG).unipath_filename);
                  (*(Uz_Globs *)pG).unipath_filename = (*(Uz_Globs *)pG).filename_full;
                }
              }
              if ((*(Uz_Globs *)pG).unipath_filename) {
# ifdef UTF8_MAYBE_NATIVE
                if ((*(Uz_Globs *)pG).native_is_utf8
#  ifdef UNICODE_WCHAR
                    && (!(*(Uz_Globs *)pG).unicode_escape_all)
#  endif
                   ) {
                  strncpy((*(Uz_Globs *)pG).filename, (*(Uz_Globs *)pG).unipath_filename, FILNAMSIZ - 1);
                  /* make sure filename is short enough */
                  if (strlen((*(Uz_Globs *)pG).unipath_filename) >= FILNAMSIZ) {
                    (*(Uz_Globs *)pG).filename[FILNAMSIZ - 1] = '\0';
                    Info(slide, 0x401, ((char *)slide,
                      LoadFarString(UFilenameTooLongTrunc)));
                    error = PK_WARN;
                  }
                }
#  ifdef UNICODE_WCHAR
                else
#  endif
# endif /* UTF8_MAYBE_NATIVE */
# ifdef UNICODE_WCHAR
                {
                  char *fn;

                  /* convert UTF-8 to local character set */
                  fn = utf8_to_local_string((*(Uz_Globs *)pG).unipath_filename,
                                            (*(Uz_Globs *)pG).unicode_escape_all);
                  /* make sure filename is short enough */
                  if (strlen(fn) >= FILNAMSIZ) {
                    fn[FILNAMSIZ - 1] = '\0';
                    Info(slide, 0x401, ((char *)slide,
                      LoadFarString(UFilenameTooLongTrunc)));
                    error = PK_WARN;
                  }
                  /* replace filename with converted UTF-8 */
                  strcpy((*(Uz_Globs *)pG).filename, fn);
                  free(fn);
                }
# endif /* UNICODE_WCHAR */
                if ((*(Uz_Globs *)pG).unipath_filename != (*(Uz_Globs *)pG).filename_full)
                  free((*(Uz_Globs *)pG).unipath_filename);
                (*(Uz_Globs *)pG).unipath_filename = NULL;
              }
            }
#endif /* UNICODE_SUPPORT */
        }
        break;

#ifdef AMIGA
    /*
     * Fifth case, for the Amiga only:  take the comment that would ordinarily
     * be skipped over, and turn it into a 79 character string that will be
     * attached to the file as a "filenote" after it is extracted.
     */

    case FILENOTE:
        if ((block_len = readbuf(pG, tmp_fnote, (unsigned)
                                 MIN(length, 2 * AMIGA_FILENOTELEN - 1))) == 0)
            return PK_EOF;
        if ((length -= block_len) > 0)  /* treat remainder as in case SKIP: */
            seek_zipf(pG, (*(Uz_Globs *)pG).cur_zipfile_bufstart - (*(Uz_Globs *)pG).extra_bytes
                      + ((*(Uz_Globs *)pG).inptr - (*(Uz_Globs *)pG).inbuf) + length);
        /* convert multi-line text into single line with no ctl-chars: */
        tmp_fnote[block_len] = '\0';
        while ((short int) --block_len >= 0)
            if ((unsigned) tmp_fnote[block_len] < ' ')
                if (tmp_fnote[block_len+1] == ' ')     /* no excess */
                    strcpy(tmp_fnote+block_len, tmp_fnote+block_len+1);
                else
                    tmp_fnote[block_len] = ' ';
        tmp_fnote[AMIGA_FILENOTELEN - 1] = '\0';
        if ((*(Uz_Globs *)pG).filenotes[(*(Uz_Globs *)pG).filenote_slot])
            free((*(Uz_Globs *)pG).filenotes[(*(Uz_Globs *)pG).filenote_slot]);     /* should not happen */
        (*(Uz_Globs *)pG).filenotes[(*(Uz_Globs *)pG).filenote_slot] = NULL;
        if (tmp_fnote[0]) {
            if (!((*(Uz_Globs *)pG).filenotes[(*(Uz_Globs *)pG).filenote_slot] = malloc(strlen(tmp_fnote)+1)))
                return PK_MEM;
            strcpy((*(Uz_Globs *)pG).filenotes[(*(Uz_Globs *)pG).filenote_slot], tmp_fnote);
        }
        break;
#endif /* AMIGA */

    } /* end switch (option) */

    return error;

} /* end function do_string() */





/***********************/
/* Function makeword() */
/***********************/

ush makeword(b)
    const uch *b;
{
    /*
     * Convert Intel style 'short' integer to non-Intel non-16-bit
     * host format.  This routine also takes care of byte-ordering.
     */
    return (ush)((b[1] << 8) | b[0]);
}





/***********************/
/* Function makelong() */
/***********************/

ulg makelong(sig)
    const uch *sig;
{
    /*
     * Convert intel style 'long' variable to non-Intel non-16-bit
     * host format.  This routine also takes care of byte-ordering.
     */
    return (((ulg)sig[3]) << 24)
         + (((ulg)sig[2]) << 16)
         + (ulg)((((unsigned)sig[1]) << 8)
               + ((unsigned)sig[0]));
}





/************************/
/* Function makeint64() */
/************************/

zusz_t makeint64(sig)
    const uch *sig;
{
#ifdef LARGE_FILE_SUPPORT
    /*
     * Convert intel style 'int64' variable to non-Intel non-16-bit
     * host format.  This routine also takes care of byte-ordering.
     */
    return (((zusz_t)sig[7]) << 56)
        + (((zusz_t)sig[6]) << 48)
        + (((zusz_t)sig[4]) << 32)
        + (zusz_t)((((ulg)sig[3]) << 24)
                 + (((ulg)sig[2]) << 16)
                 + (((unsigned)sig[1]) << 8)
                 + (sig[0]));

#else /* !LARGE_FILE_SUPPORT */

    if ((sig[7] | sig[6] | sig[5] | sig[4]) != 0)
        return (zusz_t)0xffffffffL;
    else
        return (zusz_t)((((ulg)sig[3]) << 24)
                      + (((ulg)sig[2]) << 16)
                      + (((unsigned)sig[1]) << 8)
                      + (sig[0]));

#endif /* ?LARGE_FILE_SUPPORT */
}





/*********************/
/* Function fzofft() */
/*********************/

/* Format a zoff_t value in a cylindrical buffer set. */
char *
fzofft (Uz_Globs *pG, zoff_t val, const char *pre, const char *post)
{
    /* Storage cylinder. (now in globals.h) */
    /*static char fzofft_buf[FZOFFT_NUM][FZOFFT_LEN];*/
    /*static int fzofft_index = 0;*/

    /* Temporary format string storage. */
    char fmt[16];

    /* Assemble the format string. */
    fmt[0] = '%';
    fmt[1] = '\0';             /* Start after initial "%". */
    if (pre == FZOFFT_HEX_WID)  /* Special hex width. */
    {
        strcat(fmt, FZOFFT_HEX_WID_VALUE);
    }
    else if (pre == FZOFFT_HEX_DOT_WID) /* Special hex ".width". */
    {
        strcat(fmt, ".");
        strcat(fmt, FZOFFT_HEX_WID_VALUE);
    }
    else if (pre != NULL)       /* Caller's prefix (width). */
    {
        strcat(fmt, pre);
    }

    strcat(fmt, FZOFFT_FMT);   /* Long or long-long or whatever. */

    if (post == NULL)
        strcat(fmt, "d");      /* Default radix = decimal. */
    else
        strcat(fmt, post);     /* Caller's radix. */

    /* Advance the cylinder. */
    (*(Uz_Globs *)pG).fzofft_index = ((*(Uz_Globs *)pG).fzofft_index + 1) % FZOFFT_NUM;

    /* Write into the current chamber. */
    sprintf((*(Uz_Globs *)pG).fzofft_buf[(*(Uz_Globs *)pG).fzofft_index], fmt, val);

    /* Return a pointer to this chamber. */
    return (*(Uz_Globs *)pG).fzofft_buf[(*(Uz_Globs *)pG).fzofft_index];
}




#if CRYPT

#ifdef NEED_STR2ISO
/**********************/
/* Function str2iso() */
/**********************/

char *
str2iso (
    char *dst,                          /* destination buffer */
    register const char *src          /* source string */
)
{
#ifdef INTERN_TO_ISO
    INTERN_TO_ISO(src, dst);
#else
    register uch c;
    register char *dstp = dst;

    do {
        c = (uch)foreign(*src++);
        *dstp++ = (char)ASCII2ISO(c);
    } while (c != '\0');
#endif

    return dst;
}
#endif /* NEED_STR2ISO */


#ifdef NEED_STR2OEM
/**********************/
/* Function str2oem() */
/**********************/

char *
str2oem (
    char *dst,                          /* destination buffer */
    register const char *src          /* source string */
)
{
#ifdef INTERN_TO_OEM
    INTERN_TO_OEM(src, dst);
#else
    register uch c;
    register char *dstp = dst;

    do {
        c = (uch)foreign(*src++);
        *dstp++ = (char)ASCII2OEM(c);
    } while (c != '\0');
#endif

    return dst;
}
#endif /* NEED_STR2OEM */

#endif /* CRYPT */


#ifdef ZMEM  /* memset/memcmp/memcpy for systems without either them or */
             /* bzero/bcmp/bcopy */
             /* (no known systems as of 960211) */

/*********************/
/* Function memset() */
/*********************/

void *memset(buf, init, len)
    register void *buf;        /* buffer location */
    register int init;          /* initializer character */
    register unsigned int len;  /* length of the buffer */
{
    void *start;

    start = buf;
    while (len--)
        *((char *)buf++) = (char)init;
    return start;
}



/*********************/
/* Function memcmp() */
/*********************/

int memcmp(b1, b2, len)
    register const void *b1;
    register const void *b2;
    register unsigned int len;
{
    register int c;

    if (len > 0) do {
        if ((c = (int)(*((const unsigned char *)b1)++) -
                 (int)(*((const unsigned char *)b2)++)) != 0)
           return c;
    } while (--len > 0)
    return 0;
}



/*********************/
/* Function memcpy() */
/*********************/

void *memcpy(dst, src, len)
    register void *dst;
    register const void *src;
    register unsigned int len;
{
    void *start;

    start = dst;
    while (len-- > 0)
        *((char *)dst)++ = *((const char *)src)++;
    return start;
}

#endif /* ZMEM */




#ifdef NO_STRNICMP

/************************/
/* Function zstrnicmp() */
/************************/

int 
zstrnicmp (register const char *s1, register const char *s2, register unsigned n)
{
    for (; n > 0;  --n, ++s1, ++s2) {

        if (ToLower(*s1) != ToLower(*s2))
            /* test includes early termination of one string */
            return ((uch)ToLower(*s1) < (uch)ToLower(*s2))? -1 : 1;

        if (*s1 == '\0')   /* both strings terminate early */
            return 0;
    }
    return 0;
}

#endif /* NO_STRNICMP */




#ifdef REGULUS  /* returns the inode number on success(!)...argh argh argh */
#  undef stat

/********************/
/* Function zstat() */
/********************/

int zstat(p, s)
    const char *p;
    struct stat *s;
{
    return (stat((char *)p,s) >= 0? 0 : (-1));
}

#endif /* REGULUS */




#ifdef _MBCS

/* DBCS support for Info-ZIP's zip  (mainly for japanese (-: )
 * by Yoshioka Tsuneo (QWF00133@nifty.ne.jp,tsuneo-y@is.aist-nara.ac.jp)
 * This code is public domain!   Date: 1998/12/20
 */

/************************/
/* Function plastchar() */
/************************/

char *plastchar(ptr, len)
    const char *ptr;
    extent len;
{
    unsigned clen;
    const char *oldptr = ptr;
    while(*ptr != '\0' && len > 0){
        oldptr = ptr;
        clen = CLEN(ptr);
        ptr += clen;
        len -= clen;
    }
    return (char *)oldptr;
}


#ifdef NEED_UZMBCLEN
/***********************/
/* Function uzmbclen() */
/***********************/

extent 
uzmbclen (const unsigned char *ptr)
{
    int mbl;

    mbl = mblen((const char *)ptr, MB_CUR_MAX);
    /* For use in code scanning through MBCS strings, we need a strictly
       positive "MB char bytes count".  For our scanning purpose, it is not
       not relevant whether the MB character is valid or not. And, the NUL
       char '\0' has a byte count of 1, but mblen() returns 0. So, we make
       sure that the uzmbclen() return value is not less than 1.
     */
    return (extent)(mbl > 0 ? mbl : 1);
}
#endif /* NEED_UZMBCLEN */


#ifdef NEED_UZMBSCHR
/***********************/
/* Function uzmbschr() */
/***********************/

unsigned char *
uzmbschr (const unsigned char *str, unsigned int c)
{
    while(*str != '\0'){
        if (*str == c) {return (unsigned char *)str;}
        INCSTR(str);
    }
    return NULL;
}
#endif /* NEED_UZMBSCHR */


#ifdef NEED_UZMBSRCHR
/************************/
/* Function uzmbsrchr() */
/************************/

unsigned char *
uzmbsrchr (const unsigned char *str, unsigned int c)
{
    unsigned char *match = NULL;
    while(*str != '\0'){
        if (*str == c) {match = (unsigned char *)str;}
        INCSTR(str);
    }
    return match;
}
#endif /* NEED_UZMBSRCHR */
#endif /* _MBCS */





#ifdef SMALL_MEM

/*******************************/
/*  Function fLoadFarString()  */   /* (and friends...) */
/*******************************/

char *fLoadFarString(Uz_Globs *pG, const char *sz)
{
    (void)zfstrcpy((*(Uz_Globs *)pG).rgchBigBuffer, sz);
    return (*(Uz_Globs *)pG).rgchBigBuffer;
}

char *fLoadFarStringSmall(Uz_Globs *pG, const char *sz)
{
    (void)zfstrcpy((*(Uz_Globs *)pG).rgchSmallBuffer, sz);
    return (*(Uz_Globs *)pG).rgchSmallBuffer;
}

char *fLoadFarStringSmall2(Uz_Globs *pG, const char *sz)
{
    (void)zfstrcpy((*(Uz_Globs *)pG).rgchSmallBuffer2, sz);
    return (*(Uz_Globs *)pG).rgchSmallBuffer2;
}




#if (!defined(_MSC_VER) || (_MSC_VER < 600))
/*************************/
/*  Function zfstrcpy()  */   /* portable clone of _fstrcpy() */
/*************************/

char * zfstrcpy(char *s1, const char *s2)
{
    char *p = s1;

    while ((*s1++ = *s2++) != '\0');
    return p;
}

#if (!(defined(SFX) || defined(FUNZIP)))
/*************************/
/*  Function zfstrcmp()  */   /* portable clone of _fstrcmp() */
/*************************/

int zfstrcmp(const char *s1, const char *s2)
{
    int ret;

    while ((ret = (int)(uch)*s1 - (int)(uch)*s2) == 0
           && *s2 != '\0') {
        ++s2; ++s1;
    }
    return ret;
}
#endif /* !(SFX || FUNZIP) */
#endif /* !_MSC_VER || (_MSC_VER < 600) */

#endif /* SMALL_MEM */
