/*
  Copyright (c) 1990-2009 Info-ZIP.  All rights reserved.

  See the accompanying file LICENSE, version 2009-Jan-02 or later
  (the contents of which are also included in unzip.h) for terms of use.
  If, for some reason, all these files are missing, the Info-ZIP license
  also may be found at:  ftp://ftp.info-zip.org/pub/infozip/license.html
*/
/*---------------------------------------------------------------------------

  unzip.c

  UnZip - a zipfile extraction utility.  See below for make instructions, or
  read the comments in Makefile and the various Contents files for more de-
  tailed explanations.  To report a bug, submit a *complete* description via
  //www.info-zip.org/zip-bug.html; include machine type, operating system and
  version, compiler and version, and reasonably detailed error messages or
  problem report.  To join Info-ZIP, see the instructions in README.

  UnZip 5.x is a greatly expanded and partially rewritten successor to 4.x,
  which in turn was almost a complete rewrite of version 3.x.  For a detailed
  revision history, see UnzpHist.zip at quest.jpl.nasa.gov.  For a list of
  the many (near infinite) contributors, see "CONTRIBS" in the UnZip source
  distribution.

  UnZip 6.0 adds support for archives larger than 4 GiB using the Zip64
  extensions as well as support for Unicode information embedded per the
  latest zip standard additions.

  ---------------------------------------------------------------------------

  [from original zipinfo.c]

  This program reads great gobs of totally nifty information, including the
  central directory stuff, from ZIP archives ("zipfiles" for short).  It
  started as just a testbed for fooling with zipfiles, but at this point it
  is actually a useful utility.  It also became the basis for the rewrite of
  UnZip (3.16 -> 4.0), using the central directory for processing rather than
  the individual (local) file headers.

  As of ZipInfo v2.0 and UnZip v5.1, the two programs are combined into one.
  If the executable is named "unzip" (or "unzip.exe", depending), it behaves
  like UnZip by default; if it is named "zipinfo" or "ii", it behaves like
  ZipInfo.  The ZipInfo behavior may also be triggered by use of unzip's -Z
  option; for example, "unzip -Z [zipinfo_options] archive.zip".

  Another dandy product from your buddies at Newtware!

  Author:  Greg Roelofs, newt@pobox.com, http://pobox.com/~newt/
           23 August 1990 -> April 1997

  ---------------------------------------------------------------------------

  Version:  unzip5??.{tar.Z | tar.gz | zip} for Unix, VMS, OS/2, MS-DOS, Amiga,
              Atari, Windows 3.x/95/NT/CE, Macintosh, Human68K, Acorn RISC OS,
              AtheOS, BeOS, SMS/QDOS, VM/CMS, MVS, AOS/VS, Tandem NSK, Theos
              and TOPS-20.

  Copyrights:  see accompanying file "LICENSE" in UnZip source distribution.
               (This software is free but NOT IN THE PUBLIC DOMAIN.)

  ---------------------------------------------------------------------------*/



#define __UNZIP_C       /* identifies this source module */
#define UNZIP_INTERNAL
#include "unzip.h"      /* includes, typedefs, macros, prototypes, etc. */
#include "crypt.h"
#include "unzvers.h"

#ifndef WINDLL          /* The WINDLL port uses windll/windll.c instead... */

/***************************/
/* Local type declarations */
/***************************/

#if (!defined(NO_EXCEPT_SIGNALS))
typedef struct _sign_info
    {
        struct _sign_info *previous;
        void (*sighandler)(int);
        int sigtype;
    } savsigs_info;
#endif

/*******************/
/* Local Functions */
/*******************/

#if (!defined(NO_EXCEPT_SIGNALS))
static int setsignalhandler (Uz_Globs *pG, savsigs_info **p_savedhandler_chain,
                                int signal_type, void (*newhandler)(int));
#endif
static void  help_extended      ();
static void  show_version_info  (Uz_Globs *pG);


/*************/
/* Constants */
/*************/

#include "consts.h"  /* all constant global variables are in here */
                     /* (non-constant globals were moved to globals.c) */

/* constant local variables: */

   static const char EnvUnZip[] = ENV_UNZIP;
   static const char EnvUnZip2[] = ENV_UNZIP2;
   static const char EnvZipInfo[] = ENV_ZIPINFO;
   static const char EnvZipInfo2[] = ENV_ZIPINFO2;
  static const char NoMemEnvArguments[] =
    "envargs:  cannot get memory for arguments";
  static const char CmdLineParamTooLong[] =
    "error:  command line parameter #%d exceeds internal size limit\n";

#if ( !defined(NO_EXCEPT_SIGNALS))
  static const char CantSaveSigHandler[] =
    "error:  cannot save signal handler settings\n";
#endif

   static const char NotExtracting[] =
     "caution:  not extracting; -d ignored\n";
   static const char MustGiveExdir[] =
     "error:  must specify directory to which to extract with -d option\n";
   static const char OnlyOneExdir[] =
     "error:  -d option used more than once (only one exdir allowed)\n";
#if (defined(UNICODE_SUPPORT) && !defined(UNICODE_WCHAR))
  static const char UTF8EscapeUnSupp[] =
    "warning:  -U \"escape all non-ASCII UTF-8 chars\" is not supported\n";
#endif

#if CRYPT
   static const char MustGivePasswd[] =
     "error:  must give decryption password with -P option\n";
#endif

   static const char Zfirst[] =
   "error:  -Z must be first option for ZipInfo mode (check UNZIP variable?)\n";
static const char InvalidOptionsMsg[] = "error:\
  -fn or any combination of -c, -l, -p, -t, -u and -v options invalid\n";
static const char IgnoreOOptionMsg[] =
  "caution:  both -n and -o specified; ignoring -o\n";

/* usage() strings */
   static const char Example3[] = "ReadMe";
   static const char Example2[] = " \
 unzip -p foo | more  => send contents of foo.zip via pipe into program more\n";

/* local1[]:  command options */
#if defined(TIMESTAMP)
   static const char local1[] =
     "  -T  timestamp archive to latest";
#else /* !TIMESTAMP */
   static const char local1[] = "";
#endif /* ?TIMESTAMP */

/* local2[] and local3[]:  modifier options */
#ifdef DOS_FLX_H68_OS2_W32
   static const char local2[] =
     " -$  label removables (-$$ => fixed disks)";
#ifdef OS2
#ifdef MORE
   static const char local3[] = "\
  -X  restore ACLs if supported              -s  spaces in filenames => '_'\n\
                                             -M  pipe through \"more\" pager\n";
#else
   static const char local3[] = " \
 -X  restore ACLs if supported              -s  spaces in filenames => '_'\n\n";
#endif /* ?MORE */
#else /* !OS2 */
#ifdef WIN32
#ifdef NTSD_EAS
#ifdef MORE
   static const char local3[] = "\
  -X  restore ACLs (-XX => use privileges)   -s  spaces in filenames => '_'\n\
                                             -M  pipe through \"more\" pager\n";
#else
   static const char local3[] = " \
 -X  restore ACLs (-XX => use privileges)   -s  spaces in filenames => '_'\n\n";
#endif /* ?MORE */
#else /* !NTSD_EAS */
#ifdef MORE
   static const char local3[] = "\
  -M  pipe through \"more\" pager            \
  -s  spaces in filenames => '_'\n\n";
#else
   static const char local3[] = " \
                                            -s  spaces in filenames => '_'\n\n";
#endif /* ?MORE */
#endif /* ?NTSD_EAS */
#else /* !WIN32 */
#ifdef MORE
   static const char local3[] = "  -\
M  pipe through \"more\" pager              -s  spaces in filenames => '_'\n\n";
#else
   static const char local3[] = "\
                                             -s  spaces in filenames => '_'\n";
#endif
#endif /* ?WIN32 */
#endif /* ?OS2 || ?WIN32 */
#else /* !DOS_FLX_OS2_W32 */
#ifdef VMS
   static const char local2[] = " -X  restore owner/ACL protection info";
#ifdef MORE
   static const char local3[] = "\
  -Y  treat \".nnn\" as \";nnn\" version         -2  force ODS2 names\n\
  --D restore dir (-D: no) timestamps        -M  pipe through \"more\" pager\n\
  (Must quote upper-case options, like \"-V\", unless SET PROC/PARSE=EXTEND.)\
\n\n";
#else
   static const char local3[] = "\n\
  -Y  treat \".nnn\" as \";nnn\" version         -2  force ODS2 names\n\
  --D restore dir (-D: no) timestamps\n\
  (Must quote upper-case options, like \"-V\", unless SET PROC/PARSE=EXTEND.)\
\n\n";
#endif
#else /* !VMS */
#ifdef ATH_BEO_UNX
   static const char local2[] = " -X  restore UID/GID info";
#ifdef MORE
   static const char local3[] = "\
  -K  keep setuid/setgid/tacky permissions   -M  pipe through \"more\" pager\n";
#else
   static const char local3[] = "\
  -K  keep setuid/setgid/tacky permissions\n";
#endif
#else /* !ATH_BEO_UNX */
#ifdef TANDEM
   static const char local2[] = "\
 -X  restore Tandem User ID                 -r  remove file extensions\n\
  -b  create 'C' (180) text files          ";
#ifdef MORE
   static const char local3[] = " \
                                            -M  pipe through \"more\" pager\n";
#else
   static const char local3[] = "\n";
#endif
#else /* !TANDEM */
#ifdef AMIGA
   static const char local2[] = " -N  restore comments as filenotes";
#ifdef MORE
   static const char local3[] = " \
                                            -M  pipe through \"more\" pager\n";
#else
   static const char local3[] = "\n";
#endif
#else /* !AMIGA */
#ifdef MACOS
   static const char local2[] = " -E  show Mac info during extraction";
   static const char local3[] = " \
 -i  ignore filenames in mac extra info     -J  junk (ignore) Mac extra info\n\
\n";
#else /* !MACOS */
   static const char local2[] = "";   /* Atari, Mac, CMS/MVS etc. */
   static const char local3[] = "";
#endif /* ?MACOS */
#endif /* ?AMIGA */
#endif /* ?TANDEM */
#endif /* ?ATH_BEO_UNX */
#endif /* ?VMS */
#endif /* ?DOS_FLX_OS2_W32 */

#ifndef NO_ZIPINFO
   static const char ZipInfoExample[] = "*, ?, [] (e.g., \"[a-j]*.zip\")";

static const char ZipInfoUsageLine1[] = "\
ZipInfo %d.%d%d%s of %s, by Greg Roelofs and the Info-ZIP group.\n\
\n\
List name, date/time, attribute, size, compression method, etc., about files\n\
in list (excluding those in xlist) contained in the specified .zip archive(s).\
\n\"file[.zip]\" may be a wildcard name containing %s.\n\n\
   usage:  zipinfo [-12smlvChMtTz] file[.zip] [list...] [-x xlist...]\n\
      or:  unzip %s-Z%s [-12smlvChMtTz] file[.zip] [list...] [-x xlist...]\n";

static const char ZipInfoUsageLine2[] = "\nmain\
 listing-format options:             -s  short Unix \"ls -l\" format (def.)\n\
  -1  filenames ONLY, one per line       -m  medium Unix \"ls -l\" format\n\
  -2  just filenames but allow -h/-t/-z  -l  long Unix \"ls -l\" format\n\
                                         -v  verbose, multi-page format\n";

static const char ZipInfoUsageLine3[] = "miscellaneous options:\n\
  -h  print header line       -t  print totals for listed files or for all\n\
  -z  print zipfile comment   -T  print file times in sortable decimal format\
\n  -C  be case-insensitive   %s\
  -x  exclude filenames that follow from listing\n";
   static const char ZipInfoUsageLine4[] = "";
#endif /* !NO_ZIPINFO */

   static const char CompileOptions[] =
     "UnZip special compilation options:\n";
   static const char CompileOptFormat[] = "        %s\n";
#ifndef _WIN32_WCE /* Win CE does not support environment variables */
   static const char EnvOptions[] =
     "\nUnZip and ZipInfo environment options:\n";
   static const char EnvOptFormat[] = "%16s:  %.1024s\n";
#endif
   static const char None[] = "[none]";
#  ifdef CHECK_VERSIONS
     static const char Check_Versions[] = "CHECK_VERSIONS";
#  endif
#  ifdef COPYRIGHT_CLEAN
     static const char Copyright_Clean[] =
     "COPYRIGHT_CLEAN (PKZIP 0.9x unreducing method not supported)";
#  endif
#  ifdef DEBUG
     static const char UDebug[] = "DEBUG";
#  endif
#  ifdef DEBUG_TIME
     static const char DebugTime[] = "DEBUG_TIME";
#  endif
#  ifdef DLL
     static const char Dll[] = "DLL";
#  endif
     static const char No_More[] = "NO_MORE";
#  ifdef NO_ZIPINFO
     static const char No_ZipInfo[] = "NO_ZIPINFO";
#  endif
#  ifdef NTSD_EAS
     static const char NTSDExtAttrib[] = "NTSD_EAS";
#  endif
#  if defined(WIN32) && defined(NO_W32TIMES_IZFIX)
     static const char W32NoIZTimeFix[] = "NO_W32TIMES_IZFIX";
#  endif
#  ifdef QLZIP
     static const char SMSExFldOnUnix[] = "QLZIP";
#  endif
     static const char Reentrant[] = "REENTRANT";
#  ifdef REGARGS
     static const char RegArgs[] = "REGARGS";
#  endif
#  ifdef RETURN_CODES
     static const char Return_Codes[] = "RETURN_CODES";
#  endif
#  ifdef SET_DIR_ATTRIB
     static const char SetDirAttrib[] = "SET_DIR_ATTRIB";
#  endif
#  ifdef SYMLINKS
     static const char SymLinkSupport[] =
     "SYMLINKS (symbolic links supported, if RTL and file system permit)";
#  endif
#  ifdef TIMESTAMP
     static const char TimeStamp[] = "TIMESTAMP";
#  endif
#  ifdef UNIXBACKUP
     static const char UnixBackup[] = "UNIXBACKUP";
#  endif
#  ifdef USE_EF_UT_TIME
     static const char Use_EF_UT_time[] = "USE_EF_UT_TIME";
#  endif
     static const char Use_Unshrink[] =
     "USE_UNSHRINK (PKZIP/Zip 1.x unshrinking method supported)";
#  ifndef COPYRIGHT_CLEAN
     static const char Use_Smith_Code[] =
     "USE_SMITH_CODE (PKZIP 0.9x unreducing method supported)";
#  endif
#  ifdef USE_DEFLATE64
     static const char Use_Deflate64[] =
     "USE_DEFLATE64 (PKZIP 4.x Deflate64(tm) supported)";
#  endif
#  ifdef UNICODE_SUPPORT
#   ifdef UTF8_MAYBE_NATIVE
#    ifdef UNICODE_WCHAR
       /* direct native UTF-8 check AND charset transform via wchar_t */
       static const char Use_Unicode[] =
       "UNICODE_SUPPORT [wide-chars, char coding: %s] (handle UTF-8 paths)";
#    else
       /* direct native UTF-8 check, only */
       static const char Use_Unicode[] =
       "UNICODE_SUPPORT [char coding: %s] (handle UTF-8 paths)";
#    endif
       static const char SysChUTF8[] = "UTF-8";
       static const char SysChOther[] = "other";
#   else /* !UTF8_MAYBE_NATIVE */
       /* charset transform via wchar_t, no native UTF-8 support */
       static const char Use_Unicode[] =
       "UNICODE_SUPPORT [wide-chars] (handle UTF-8 paths)";
#   endif /* ?UTF8_MAYBE_NATIVE */
#  endif /* UNICODE_SUPPORT */
#  ifdef _MBCS
     static const char Have_MBCS_Support[] =
     "MBCS-support (multibyte character support, MB_CUR_MAX = %u)";
#  endif
#  ifdef MULT_VOLUME
     static const char Use_MultiVol[] =
     "MULT_VOLUME (multi-volume archives supported)";
#  endif
#  ifdef LARGE_FILE_SUPPORT
     static const char Use_LFS[] =
     "LARGE_FILE_SUPPORT (large files over 2 GiB supported)";
#  endif
#  ifdef ZIP64_SUPPORT
     static const char Use_Zip64[] =
     "ZIP64_SUPPORT (archives using Zip64 for large files supported)";
#  endif
#  ifdef USE_VFAT
     static const char Use_VFAT_support[] = "USE_VFAT";
#  endif
#  ifdef USE_ZLIB
     static const char UseZlib[] =
     "USE_ZLIB (compiled with version %s; using version %s)";
#  endif
#  ifdef USE_BZIP2
     static const char UseBZip2[] =
     "USE_BZIP2 (PKZIP 4.6+, using bzip2 lib version %s)";
#  endif
#  ifdef VMS_TEXT_CONV
     static const char VmsTextConv[] = "VMS_TEXT_CONV";
#  endif
#  ifdef WILD_STOP_AT_DIR
     static const char WildStopAtDir[] = "WILD_STOP_AT_DIR";
#  endif
#  if CRYPT
#    ifdef PASSWD_FROM_STDIN
       static const char PasswdStdin[] = "PASSWD_FROM_STDIN";
#    endif
     static const char Decryption[] =
       "        [decryption, version %d.%d%s of %s]\n";
     static const char CryptDate[] = CR_VERSION_DATE;
#  endif
#  ifndef __RSXNT__
#    ifdef __EMX__
       static const char EnvEMX[] = "EMX";
       static const char EnvEMXOPT[] = "EMXOPT";
#    endif
#    if (defined(__GO32__) && (!defined(__DJGPP__) || (__DJGPP__ < 2)))
       static const char EnvGO32[] = "GO32";
       static const char EnvGO32TMP[] = "GO32TMP";
#    endif
#  endif /* !__RSXNT__ */

# ifdef COPYRIGHT_CLEAN
   static const char UnzipUsageLine1[] = "\
UnZip %d.%d%d%s of %s, by Info-ZIP.  Maintained by C. Spieler.  Send\n\
bug reports using http://www.info-zip.org/zip-bug.html; see README for details.\
\n\n";
# else
   static const char UnzipUsageLine1[] = "\
UnZip %d.%d%d%s of %s, by Info-ZIP.  UnReduce (c) 1989 by S. H. Smith.\n\
Send bug reports using //www.info-zip.org/zip-bug.html; see README for details.\
\n\n";
# endif /* ?COPYRIGHT_CLEAN */
# define UnzipUsageLine1v       UnzipUsageLine1

static const char UnzipUsageLine2v[] = "\
Latest sources and executables are at ftp://ftp.info-zip.org/pub/infozip/ ;\
\nsee ftp://ftp.info-zip.org/pub/infozip/UnZip.html for other sites.\
\n\n";

static const char UnzipUsageLine2[] = "\
Usage: unzip %s[-opts[modifiers]] file[.zip] [list] [-x xlist] [-d exdir]\n \
 Default action is to extract files in list, except those in xlist, to exdir;\n\
  file[.zip] may be a wildcard.  %s\n";

#ifdef NO_ZIPINFO
#  define ZIPINFO_MODE_OPTION  ""
   static const char ZipInfoMode[] =
     "(ZipInfo mode is disabled in this version.)";
#else
#  define ZIPINFO_MODE_OPTION  "[-Z] "
   static const char ZipInfoMode[] =
     "-Z => ZipInfo mode (\"unzip -Z\" for usage).";
#endif /* ?NO_ZIPINFO */

static const char UnzipUsageLine3[] = "\n\
  -p  extract files to pipe, no messages     -l  list files (short format)\n\
  -f  freshen existing files, create none    -t  test compressed archive data\n\
  -u  update files, create if necessary      -z  display archive comment only\n\
  -v  list verbosely/show version info     %s\n\
  -x  exclude files that follow (in xlist)   -d  extract files into exdir\n";

/* There is not enough space on a standard 80x25 Windows console screen for
 * the additional line advertising the UTF-8 debugging options. This may
 * eventually also be the case for other ports. Probably, the -U option need
 * not be shown on the introductory screen at all. [Chr. Spieler, 2008-02-09]
 *
 * Likely, other advanced options should be moved to an extended help page and
 * the option to list that page put here.  [E. Gordon, 2008-3-16]
 */
#if (defined(UNICODE_SUPPORT) && !defined(WIN32))
static const char UnzipUsageLine4[] = "\
modifiers:\n\
  -n  never overwrite existing files         -q  quiet mode (-qq => quieter)\n\
  -o  overwrite files WITHOUT prompting      -a  auto-convert any text files\n\
  -j  junk paths (do not make directories)   -aa treat ALL files as text\n\
  -U  use escapes for all non-ASCII Unicode  -UU ignore any Unicode fields\n\
  -C  match filenames case-insensitively     -L  make (some) names \
lowercase\n %-42s  -V  retain VMS version numbers\n%s";
#else /* !UNICODE_SUPPORT */
static const char UnzipUsageLine4[] = "\
modifiers:\n\
  -n  never overwrite existing files         -q  quiet mode (-qq => quieter)\n\
  -o  overwrite files WITHOUT prompting      -a  auto-convert any text files\n\
  -j  junk paths (do not make directories)   -aa treat ALL files as text\n\
  -C  match filenames case-insensitively     -L  make (some) names \
lowercase\n %-42s  -V  retain VMS version numbers\n%s";
#endif /* ?UNICODE_SUPPORT */

static const char UnzipUsageLine5[] = "\
See \"unzip -hh\" or unzip.txt for more help.  Examples:\n\
  unzip data1 -x joe   => extract all files except joe from zipfile data1.zip\n\
%s\
  unzip -fo foo %-6s => quietly replace existing %s if archive file newer\n";





/*****************************/
/*  main() / UzpMain() stub  */
/*****************************/

int main(int argc, char **argv)   /* return PK-type error code (except under VMS) */
{
    int r;

    CONSTRUCTGLOBALS();
    r = unzip(pG, argc, argv);
    DESTROYGLOBALS();
    RETURN(r);
}




/*******************************/
/*  Primary UnZip entry point  */
/*******************************/

int 
unzip (Uz_Globs *pG, int argc, char *argv[])
{
#ifndef NO_ZIPINFO
    char *p;
#endif
    int i;
    int retcode, error=FALSE;
#ifndef NO_EXCEPT_SIGNALS
    savsigs_info *oldsighandlers = NULL;
#   define SET_SIGHANDLER(sigtype, newsighandler) \
      if ((retcode = setsignalhandler(pG, &oldsighandlers, (sigtype), \
                                      (newsighandler))) > PK_WARN) \
          goto cleanup_and_exit
#endif /* NO_EXCEPT_SIGNALS */

    /* initialize international char support to the current environment */
    SETLOCALE(LC_CTYPE, "");

#ifdef UNICODE_SUPPORT
    /* see if can use UTF-8 Unicode locale */
# ifdef UTF8_MAYBE_NATIVE
    {
        char *codeset;
#  if !(defined(NO_NL_LANGINFO) || defined(NO_LANGINFO_H))
        /* get the codeset (character set encoding) currently used */
#       include <langinfo.h>

        codeset = nl_langinfo(CODESET);
#  else /* NO_NL_LANGINFO || NO_LANGINFO_H */
        /* query the current locale setting for character classification */
        codeset = setlocale(LC_CTYPE, NULL);
        if (codeset != NULL) {
            /* extract the codeset portion of the locale name */
            codeset = strchr(codeset, '.');
            if (codeset != NULL) ++codeset;
        }
#  endif /* ?(NO_NL_LANGINFO || NO_LANGINFO_H) */
        /* is the current codeset UTF-8 ? */
        if ((codeset != NULL) && (strcmp(codeset, "UTF-8") == 0)) {
            /* successfully found UTF-8 char coding */
            (*(Uz_Globs *)pG).native_is_utf8 = TRUE;
        } else {
            /* Current codeset is not UTF-8 or cannot be determined. */
            (*(Uz_Globs *)pG).native_is_utf8 = FALSE;
        }
        /* Note: At least for UnZip, trying to change the process codeset to
         *       UTF-8 does not work.  For the example Linux setup of the
         *       UnZip maintainer, a successful switch to "en-US.UTF-8"
         *       resulted in garbage display of all non-basic ASCII characters.
         */
    }
# endif /* UTF8_MAYBE_NATIVE */

    /* initialize Unicode */
    (*(Uz_Globs *)pG).unicode_escape_all = 0;
    (*(Uz_Globs *)pG).unicode_mismatch = 0;

    (*(Uz_Globs *)pG).unipath_version = 0;
    (*(Uz_Globs *)pG).unipath_checksum = 0;
    (*(Uz_Globs *)pG).unipath_filename = NULL;
#endif /* UNICODE_SUPPORT */


#ifdef MALLOC_WORK
    /* The following (rather complex) expression determines the allocation
       size of the decompression work area.  It simulates what the
       combined "union" and "struct" declaration of the "static" work
       area reservation achieves automatically at compile time.
       Any decent compiler should evaluate this expression completely at
       compile time and provide constants to the zcalloc() call.
       (For better readability, some subexpressions are encapsulated
       in temporarly defined macros.)
     */
#   define UZ_SLIDE_CHUNK (sizeof(shrint)+sizeof(uch)+sizeof(uch))
#   define UZ_NUMOF_CHUNKS \
      (unsigned)(((WSIZE+UZ_SLIDE_CHUNK-1)/UZ_SLIDE_CHUNK > HSIZE) ? \
                 (WSIZE+UZ_SLIDE_CHUNK-1)/UZ_SLIDE_CHUNK : HSIZE)
    (*(Uz_Globs *)pG).area.Slide = (uch *)zcalloc(UZ_NUMOF_CHUNKS, UZ_SLIDE_CHUNK);
#   undef UZ_SLIDE_CHUNK
#   undef UZ_NUMOF_CHUNKS
    (*(Uz_Globs *)pG).area.shrink.Parent = (shrint *)(*(Uz_Globs *)pG).area.Slide;
    (*(Uz_Globs *)pG).area.shrink.value = (*(Uz_Globs *)pG).area.Slide + (sizeof(shrint)*(HSIZE));
    (*(Uz_Globs *)pG).area.shrink.Stack = (*(Uz_Globs *)pG).area.Slide +
                           (sizeof(shrint) + sizeof(uch))*(HSIZE);
#endif

/*---------------------------------------------------------------------------
    Set signal handler for restoring echo, warn of zipfile corruption, etc.
  ---------------------------------------------------------------------------*/
#ifndef NO_EXCEPT_SIGNALS
#ifdef SIGINT
    SET_SIGHANDLER(SIGINT, handler);
#endif
#ifdef SIGTERM                 /* some systems really have no SIGTERM */
    SET_SIGHANDLER(SIGTERM, handler);
#endif
#if defined(SIGABRT) && !(defined(AMIGA) && defined(__SASC))
    SET_SIGHANDLER(SIGABRT, handler);
#endif
#ifdef SIGBREAK
    SET_SIGHANDLER(SIGBREAK, handler);
#endif
#ifdef SIGBUS
    SET_SIGHANDLER(SIGBUS, handler);
#endif
#ifdef SIGILL
    SET_SIGHANDLER(SIGILL, handler);
#endif
#ifdef SIGSEGV
    SET_SIGHANDLER(SIGSEGV, handler);
#endif
#endif /* NO_EXCEPT_SIGNALS */

#if (defined(WIN32) && defined(__RSXNT__))
    for (i = 0 ; i < argc; i++) {
        _ISO_INTERN(argv[i]);
    }
#endif

/*---------------------------------------------------------------------------
    Sanity checks.  Commentary by Otis B. Driftwood and Fiorello:

    D:  It's all right.  That's in every contract.  That's what they
        call a sanity clause.

    F:  Ha-ha-ha-ha-ha.  You can't fool me.  There ain't no Sanity
        Claus.
  ---------------------------------------------------------------------------*/

#ifdef DEBUG
# ifdef LARGE_FILE_SUPPORT
  /* test if we can support large files - 10/6/04 EG */
    if (sizeof(zoff_t) < 8) {
        Info(slide, 0x401, ((char *)slide, "LARGE_FILE_SUPPORT set but not supported\n"));
        retcode = PK_BADERR;
        goto cleanup_and_exit;
    }
    /* test if we can show 64-bit values */
    {
        zoff_t z = ~(zoff_t)0;  /* z should be all 1s now */
        char *sz;

        sz = FmZofft(z, FZOFFT_HEX_DOT_WID, "X");
        if ((sz[0] != 'F') || (strlen(sz) != 16))
        {
            z = 0;
        }

        /* shift z so only MSB is set */
        z <<= 63;
        sz = FmZofft(z, FZOFFT_HEX_DOT_WID, "X");
        if ((sz[0] != '8') || (strlen(sz) != 16))
        {
            Info(slide, 0x401, ((char *)slide,
              "Can't show 64-bit values correctly\n"));
            retcode = PK_BADERR;
            goto cleanup_and_exit;
        }
    }
# endif /* LARGE_FILE_SUPPORT */

    /* 2004-11-30 SMS.
       Test the NEXTBYTE macro for proper operation.
    */
    {
        int test_char;
        static uch test_buf[2] = { 'a', 'b' };

        (*(Uz_Globs *)pG).inptr = test_buf;
        (*(Uz_Globs *)pG).incnt = 1;

        test_char = NEXTBYTE;           /* Should get 'a'. */
        if (test_char == 'a')
        {
            test_char = NEXTBYTE;       /* Should get EOF, not 'b'. */
        }
        if (test_char != EOF)
        {
            Info(slide, 0x401, ((char *)slide,
 "NEXTBYTE macro failed.  Try compiling with ALT_NEXTBYTE defined?"));

            retcode = PK_BADERR;
            goto cleanup_and_exit;
        }
    }
#endif /* DEBUG */

/*---------------------------------------------------------------------------
    First figure out if we're running in UnZip mode or ZipInfo mode, and put
    the appropriate environment-variable options into the queue.  Then rip
    through any command-line options lurking about...
  ---------------------------------------------------------------------------*/


    (*(Uz_Globs *)pG).noargs = (argc == 1);   /* no options, no zipfile, no anything */

#ifndef NO_ZIPINFO
    for (p = argv[0] + strlen(argv[0]); p >= argv[0]; --p) {
        if (*p == DIR_END
#ifdef DIR_END2
            || *p == DIR_END2
#endif
           )
            break;
    }
    ++p;

    if (STRNICMP(p, LoadFarStringSmall(Zipnfo), 7) == 0 ||
        STRNICMP(p, "ii", 2) == 0 ||
        (argc > 1 && strncmp(argv[1], "-Z", 2) == 0))
    {
        uO.zipinfo_mode = TRUE;
        if ((error = envargs(&argc, &argv, LoadFarStringSmall(EnvZipInfo),
                             LoadFarStringSmall2(EnvZipInfo2))) != PK_OK)
            perror(LoadFarString(NoMemEnvArguments));
    } else
#endif /* !NO_ZIPINFO */
    {
        uO.zipinfo_mode = FALSE;
    }

    if (!error) {
        /* Check the length of all passed command line parameters.
         * Command arguments might get sent through the Info() message
         * system, which uses the sliding window area as string buffer.
         * As arguments may additionally get fed through one of the FnFilter
         * macros, we require all command line arguments to be shorter than
         * WSIZE/4 (and ca. 2 standard line widths for fixed message text).
         */
        for (i = 1 ; i < argc; i++) {
           if (strlen(argv[i]) > ((WSIZE>>2) - 160)) {
               Info(slide, 0x401, ((char *)slide,
                 LoadFarString(CmdLineParamTooLong), i));
               retcode = PK_PARAM;
               goto cleanup_and_exit;
           }
        }
#ifndef NO_ZIPINFO
        if (uO.zipinfo_mode)
            error = zi_opts(pG, &argc, &argv);
        else
#endif /* !NO_ZIPINFO */
            error = uz_opts(pG, &argc, &argv);
    }

    if ((argc < 0) || error) {
        retcode = error;
        goto cleanup_and_exit;
    }

/*---------------------------------------------------------------------------
    Now get the zipfile name from the command line and then process any re-
    maining options and file specifications.
  ---------------------------------------------------------------------------*/

#ifdef DOS_FLX_H68_NLM_OS2_W32
    /* convert MSDOS-style 'backward slash' directory separators to Unix-style
     * 'forward slashes' for user's convenience (include zipfile name itself)
     */
#ifdef SFX
    for ((*(Uz_Globs *)pG).pfnames = argv, i = argc;  i > 0;  --i) {
#else
    /* argc does not include the zipfile specification */
    for ((*(Uz_Globs *)pG).pfnames = argv, i = argc+1;  i > 0;  --i) {
#endif
        char *q = *(*(Uz_Globs *)pG).pfnames;

        while (*q != '\0') {
            if (*q == '\\')
                *q = '/';
            INCSTR(q);
        }
        ++(*(Uz_Globs *)pG).pfnames;
    }
#endif /* DOS_FLX_H68_NLM_OS2_W32 */

    (*(Uz_Globs *)pG).wildzipfn = *argv++;


    (*(Uz_Globs *)pG).filespecs = argc;
    (*(Uz_Globs *)pG).xfilespecs = 0;

    if (argc > 0) {
        int in_files=FALSE, in_xfiles=FALSE;
        char **pp = argv-1;

        (*(Uz_Globs *)pG).process_all_files = FALSE;
        (*(Uz_Globs *)pG).pfnames = argv;
        while (*++pp) {
            Trace((stderr, "pp - argv = %d\n", pp-argv));
            if (!uO.exdir && strncmp(*pp, "-d", 2) == 0) {
                int firstarg = (pp == argv);

                uO.exdir = (*pp) + 2;
                if (in_files) {      /* ... zipfile ... -d exdir ... */
                    *pp = (char *)NULL;         /* terminate (*(Uz_Globs *)pG).pfnames */
                    (*(Uz_Globs *)pG).filespecs = pp - (*(Uz_Globs *)pG).pfnames;
                    in_files = FALSE;
                } else if (in_xfiles) {
                    *pp = (char *)NULL;         /* terminate (*(Uz_Globs *)pG).pxnames */
                    (*(Uz_Globs *)pG).xfilespecs = pp - (*(Uz_Globs *)pG).pxnames;
                    /* "... -x xlist -d exdir":  nothing left */
                }
                /* first check for "-dexdir", then for "-d exdir" */
                if (*uO.exdir == '\0') {
                    if (*++pp)
                        uO.exdir = *pp;
                    else {
                        Info(slide, 0x401, ((char *)slide,
                          LoadFarString(MustGiveExdir)));
                        /* don't extract here by accident */
                        retcode = PK_PARAM;
                        goto cleanup_and_exit;
                    }
                }
                if (firstarg) { /* ... zipfile -d exdir ... */
                    if (pp[1]) {
                        (*(Uz_Globs *)pG).pfnames = pp + 1;  /* argv+2 */
                        (*(Uz_Globs *)pG).filespecs = argc - ((*(Uz_Globs *)pG).pfnames-argv);  /* for now... */
                    } else {
                        (*(Uz_Globs *)pG).process_all_files = TRUE;
                        (*(Uz_Globs *)pG).pfnames = (char **)fnames;  /* GRR: necessary? */
                        (*(Uz_Globs *)pG).filespecs = 0;     /* GRR: necessary? */
                        break;
                    }
                }
            } else if (!in_xfiles) {
                if (strcmp(*pp, "-x") == 0) {
                    in_xfiles = TRUE;
                    if (pp == (*(Uz_Globs *)pG).pfnames) {
                        (*(Uz_Globs *)pG).pfnames = (char **)fnames;  /* defaults */
                        (*(Uz_Globs *)pG).filespecs = 0;
                    } else if (in_files) {
                        *pp = 0;                   /* terminate (*(Uz_Globs *)pG).pfnames */
                        (*(Uz_Globs *)pG).filespecs = pp - (*(Uz_Globs *)pG).pfnames;  /* adjust count */
                        in_files = FALSE;
                    }
                    (*(Uz_Globs *)pG).pxnames = pp + 1; /* excluded-names ptr starts after -x */
                    (*(Uz_Globs *)pG).xfilespecs = argc - ((*(Uz_Globs *)pG).pxnames-argv);  /* anything left */
                } else
                    in_files = TRUE;
            }
        }
    } else
        (*(Uz_Globs *)pG).process_all_files = TRUE;      /* for speed */

    if (uO.exdir != (char *)NULL && !(*(Uz_Globs *)pG).extract_flag)    /* -d ignored */
        Info(slide, 0x401, ((char *)slide, LoadFarString(NotExtracting)));

#ifdef UNICODE_SUPPORT
    /* set Unicode-escape-all if option -U used */
    if (uO.U_flag == 1)
# ifdef UNICODE_WCHAR
        (*(Uz_Globs *)pG).unicode_escape_all = TRUE;
# else
        Info(slide, 0x401, ((char *)slide, LoadFarString(UTF8EscapeUnSupp)));
# endif
#endif


/*---------------------------------------------------------------------------
    Okey dokey, we have everything we need to get started.  Let's roll.
  ---------------------------------------------------------------------------*/

    retcode = process_zipfiles(pG);

cleanup_and_exit:
#if (!defined(NO_EXCEPT_SIGNALS))
    /* restore all signal handlers back to their state at function entry */
    while (oldsighandlers != NULL) {
        savsigs_info *thissigsav = oldsighandlers;

        signal(thissigsav->sigtype, thissigsav->sighandler);
        oldsighandlers = thissigsav->previous;
        free(thissigsav);
    }
#endif
    return(retcode);

} /* end main()/unzip() */





#if (!defined(NO_EXCEPT_SIGNALS))
/*******************************/
/* Function setsignalhandler() */
/*******************************/

static int setsignalhandler(pG, p_savedhandler_chain, signal_type,
                            newhandler)
    Uz_Globs *pG;
    savsigs_info **p_savedhandler_chain;
    int signal_type;
    void (*newhandler)(int);
{
    savsigs_info *savsig;

    savsig = malloc(sizeof(savsigs_info));
    if (savsig == NULL) {
        /* error message and break */
        Info(slide, 0x401, ((char *)slide, LoadFarString(CantSaveSigHandler)));
        return PK_MEM;
    }
    savsig->sigtype = signal_type;
    savsig->sighandler = signal(SIGINT, newhandler);
    if (savsig->sighandler == SIG_ERR) {
        free(savsig);
    } else {
        savsig->previous = *p_savedhandler_chain;
        *p_savedhandler_chain = savsig;
    }
    return PK_OK;

} /* end function setsignalhandler() */

#endif /* REENTRANT && !NO_EXCEPT_SIGNALS */





/**********************/
/* Function uz_opts() */
/**********************/

int uz_opts(pG, pargc, pargv)
    Uz_Globs *pG;
    int *pargc;
    char ***pargv;
{
    char **argv, *s;
    int argc, c, error=FALSE, negative=0, showhelp=0;


    argc = *pargc;
    argv = *pargv;

    while (++argv, (--argc > 0 && *argv != NULL && **argv == '-')) {
        s = *argv + 1;
        while ((c = *s++) != 0) {    /* "!= 0":  prevent Turbo C warning */
            switch (c)
            {
                case ('-'):
                    ++negative;
                    break;
                case ('a'):
                    if (negative) {
                        uO.aflag = MAX(uO.aflag-negative,0);
                        negative = 0;
                    } else
                        ++uO.aflag;
                    break;
#if (defined(DLL) && defined(API_DOC))
                case ('A'):    /* extended help for API */
                    APIhelp(pG, argc, argv);
                    *pargc = -1;  /* signal to exit successfully */
                    return 0;
#endif
                case ('b'):
                    if (negative) {
                        negative = 0;   /* do nothing:  "-b" is default */
                    } else {
                        uO.aflag = 0;
                    }
                    break;
#ifdef UNIXBACKUP
                case ('B'): /* -B: back up existing files */
                    if (negative)
                        uO.B_flag = FALSE, negative = 0;
                    else
                        uO.B_flag = TRUE;
                    break;
#endif
                case ('c'):
                    if (negative) {
                        uO.cflag = FALSE, negative = 0;
#ifdef NATIVE
                        uO.aflag = 0;
#endif
                    } else {
                        uO.cflag = TRUE;
#ifdef NATIVE
                        uO.aflag = 2;   /* so you can read it on the screen */
#endif
#ifdef DLL
                        if ((*(Uz_Globs *)pG).redirect_text)
                            (*(Uz_Globs *)pG).redirect_data = 2;
#endif
                    }
                    break;
                case ('C'):    /* -C:  match filenames case-insensitively */
                    if (negative)
                        uO.C_flag = FALSE, negative = 0;
                    else
                        uO.C_flag = TRUE;
                    break;
                case ('d'):
                    if (negative) {   /* negative not allowed with -d exdir */
                        Info(slide, 0x401, ((char *)slide,
                          LoadFarString(MustGiveExdir)));
                        return(PK_PARAM);  /* don't extract here by accident */
                    }
                    if (uO.exdir != (char *)NULL) {
                        Info(slide, 0x401, ((char *)slide,
                          LoadFarString(OnlyOneExdir)));
                        return(PK_PARAM);    /* GRR:  stupid restriction? */
                    } else {
                        /* first check for "-dexdir", then for "-d exdir" */
                        uO.exdir = s;
                        if (*uO.exdir == '\0') {
                            if (argc > 1) {
                                --argc;
                                uO.exdir = *++argv;
                                if (*uO.exdir == '-') {
                                    Info(slide, 0x401, ((char *)slide,
                                      LoadFarString(MustGiveExdir)));
                                    return(PK_PARAM);
                                }
                                /* else uO.exdir points at extraction dir */
                            } else {
                                Info(slide, 0x401, ((char *)slide,
                                  LoadFarString(MustGiveExdir)));
                                return(PK_PARAM);
                            }
                        }
                        /* uO.exdir now points at extraction dir (-dexdir or
                         *  -d exdir); point s at end of exdir to avoid mis-
                         *  interpretation of exdir characters as more options
                         */
                        if (*s != 0)
                            while (*++s != 0)
                                ;
                    }
                    break;
#if (!defined(NO_TIMESTAMPS))
                case ('D'):    /* -D: Skip restoring dir (or any) timestamp. */
                    if (negative) {
                        uO.D_flag = MAX(uO.D_flag-negative,0);
                        negative = 0;
                    } else
                        uO.D_flag++;
                    break;
#endif /* (!NO_TIMESTAMPS) */
                case ('e'):    /* just ignore -e, -x options (extract) */
                    break;
                case ('f'):    /* "freshen" (extract only newer files) */
                    if (negative)
                        uO.fflag = uO.uflag = FALSE, negative = 0;
                    else
                        uO.fflag = uO.uflag = TRUE;
                    break;
                case ('h'):    /* just print help message and quit */
                    if (showhelp == 0) {
                        if (*s == 'h')
                            showhelp = 2;
                        else
                        {
                            showhelp = 1;
                        }
                    }
                    break;
                case ('j'):    /* junk pathnames/directory structure */
                    if (negative)
                        uO.jflag = FALSE, negative = 0;
                    else
                        uO.jflag = TRUE;
                    break;
                case ('l'):
                    if (negative) {
                        uO.vflag = MAX(uO.vflag-negative,0);
                        negative = 0;
                    } else
                        ++uO.vflag;
                    break;
                case ('n'):    /* don't overwrite any files */
                    if (negative)
                        uO.overwrite_none = FALSE, negative = 0;
                    else
                        uO.overwrite_none = TRUE;
                    break;
                case ('o'):    /* OK to overwrite files without prompting */
                    if (negative) {
                        uO.overwrite_all = MAX(uO.overwrite_all-negative,0);
                        negative = 0;
                    } else
                        ++uO.overwrite_all;
                    break;
                case ('p'):    /* pipes:  extract to stdout, no messages */
                    if (negative) {
                        uO.cflag = FALSE;
                        uO.qflag = MAX(uO.qflag-999,0);
                        negative = 0;
                    } else {
                        uO.cflag = TRUE;
                        uO.qflag += 999;
                    }
                    break;
#if CRYPT
                /* GRR:  yes, this is highly insecure, but dozens of people
                 * have pestered us for this, so here we go... */
                case ('P'):
                    if (negative) {   /* negative not allowed with -P passwd */
                        Info(slide, 0x401, ((char *)slide,
                          LoadFarString(MustGivePasswd)));
                        return(PK_PARAM);  /* don't extract here by accident */
                    }
                    if (uO.pwdarg != (char *)NULL) {
/*
                        GRR:  eventually support multiple passwords?
                        Info(slide, 0x401, ((char *)slide,
                          LoadFarString(OnlyOnePasswd)));
                        return(PK_PARAM);
 */
                    } else {
                        /* first check for "-Ppasswd", then for "-P passwd" */
                        uO.pwdarg = s;
                        if (*uO.pwdarg == '\0') {
                            if (argc > 1) {
                                --argc;
                                uO.pwdarg = *++argv;
                                if (*uO.pwdarg == '-') {
                                    Info(slide, 0x401, ((char *)slide,
                                      LoadFarString(MustGivePasswd)));
                                    return(PK_PARAM);
                                }
                                /* else pwdarg points at decryption password */
                            } else {
                                Info(slide, 0x401, ((char *)slide,
                                  LoadFarString(MustGivePasswd)));
                                return(PK_PARAM);
                            }
                        }
                        /* pwdarg now points at decryption password (-Ppasswd or
                         *  -P passwd); point s at end of passwd to avoid mis-
                         *  interpretation of passwd characters as more options
                         */
                        if (*s != 0)
                            while (*++s != 0)
                                ;
                    }
                    break;
#endif /* CRYPT */
                case ('q'):    /* quiet:  fewer comments/messages */
                    if (negative) {
                        uO.qflag = MAX(uO.qflag-negative,0);
                        negative = 0;
                    } else
                        ++uO.qflag;
                    break;
#ifdef DOS_FLX_NLM_OS2_W32
                case ('s'):    /* spaces in filenames:  allow by default */
                    if (negative)
                        uO.sflag = FALSE, negative = 0;
                    else
                        uO.sflag = TRUE;
                    break;
#endif /* DOS_FLX_NLM_OS2_W32 */
                case ('t'):
                    if (negative)
                        uO.tflag = FALSE, negative = 0;
                    else
                        uO.tflag = TRUE;
                    break;
#ifdef TIMESTAMP
                case ('T'):
                    if (negative)
                        uO.T_flag = FALSE, negative = 0;
                    else
                        uO.T_flag = TRUE;
                    break;
#endif
                case ('u'):    /* update (extract only new and newer files) */
                    if (negative)
                        uO.uflag = FALSE, negative = 0;
                    else
                        uO.uflag = TRUE;
                    break;
#ifdef UNICODE_SUPPORT
                case ('U'):    /* escape UTF-8, or disable UTF-8 support */
                    if (negative) {
                        uO.U_flag = MAX(uO.U_flag-negative,0);
                        negative = 0;
                    } else
                        uO.U_flag++;
                    break;
#else /* !UNICODE_SUPPORT */
#endif /* ?UNICODE_SUPPORT */
                case ('v'):    /* verbose */
                    if (negative) {
                        uO.vflag = MAX(uO.vflag-negative,0);
                        negative = 0;
                    } else if (uO.vflag)
                        ++uO.vflag;
                    else
                        uO.vflag = 2;
                    break;
#ifdef WILD_STOP_AT_DIR
                case ('W'):    /* Wildcard interpretation (stop at '/'?) */
                    if (negative)
                        uO.W_flag = FALSE, negative = 0;
                    else
                        uO.W_flag = TRUE;
                    break;
#endif /* WILD_STOP_AT_DIR */
                case ('x'):    /* extract:  default */
                    break;
#if (defined(RESTORE_UIDGID) || defined(RESTORE_ACL))
                case ('X'):   /* restore owner/protection info (need privs?) */
                    if (negative) {
                        uO.X_flag = MAX(uO.X_flag-negative,0);
                        negative = 0;
                    } else
                        ++uO.X_flag;
                    break;
#endif /* RESTORE_UIDGID || RESTORE_ACL */
                case ('z'):    /* display only the archive comment */
                    if (negative) {
                        uO.zflag = MAX(uO.zflag-negative,0);
                        negative = 0;
                    } else
                        ++uO.zflag;
                    break;
                case ('Z'):    /* should have been first option (ZipInfo) */
                    Info(slide, 0x401, ((char *)slide, LoadFarString(Zfirst)));
                    error = TRUE;
                    break;
                case (':'):    /* allow "parent dir" path components */
                    if (negative) {
                        uO.ddotflag = MAX(uO.ddotflag-negative,0);
                        negative = 0;
                    } else
                        ++uO.ddotflag;
                    break;
#ifdef UNIX
                case ('^'):    /* allow control chars in filenames */
                    if (negative) {
                        uO.cflxflag = MAX(uO.cflxflag-negative,0);
                        negative = 0;
                    } else
                        ++uO.cflxflag;
                    break;
#endif /* UNIX */
                default:
                    error = TRUE;
                    break;

            } /* end switch */
        } /* end while (not end of argument string) */
    } /* end while (not done with switches) */

/*---------------------------------------------------------------------------
    Check for nonsensical combinations of options.
  ---------------------------------------------------------------------------*/


    if (showhelp > 0) {         /* just print help message and quit */
        *pargc = -1;
        if (showhelp == 2) {
            help_extended(pG);
            return PK_OK;
        } else
        {
            return USAGE(PK_OK);
        }
    }

    if ((uO.cflag && (uO.tflag || uO.uflag)) ||
        (uO.tflag && uO.uflag) || (uO.fflag && uO.overwrite_none))
    {
        Info(slide, 0x401, ((char *)slide, LoadFarString(InvalidOptionsMsg)));
        error = TRUE;
    }
    if (uO.aflag > 2)
        uO.aflag = 2;
    if (uO.overwrite_all && uO.overwrite_none) {
        Info(slide, 0x401, ((char *)slide, LoadFarString(IgnoreOOptionMsg)));
        uO.overwrite_all = FALSE;
    }

    if ((argc-- == 0) || error)
    {
        *pargc = argc;
        *pargv = argv;
        if (uO.vflag >= 2 && argc == -1) {              /* "unzip -v" */
            show_version_info(pG);
            return PK_OK;
        }
        if (!(*(Uz_Globs *)pG).noargs && !error)
            error = TRUE;       /* had options (not -h or -v) but no zipfile */
        return USAGE(error);
    }

    if (uO.cflag || uO.tflag || uO.vflag || uO.zflag
#ifdef TIMESTAMP
                                                     || uO.T_flag
#endif
                                                                 )
        (*(Uz_Globs *)pG).extract_flag = FALSE;
    else
        (*(Uz_Globs *)pG).extract_flag = TRUE;

    *pargc = argc;
    *pargv = argv;
    return PK_OK;

} /* end function uz_opts() */




/********************/
/* Function usage() */
/********************/

#    define QUOT ' '
#    define QUOTS ""

int usage(pG, error)   /* return PK-type error code */
    Uz_Globs *pG;
    int error;
{
    int flag = (error? 1 : 0);


/*---------------------------------------------------------------------------
    Print either ZipInfo usage or UnZip usage, depending on incantation.
    (Strings must be no longer than 512 bytes for Turbo C, apparently.)
  ---------------------------------------------------------------------------*/

    if (uO.zipinfo_mode) {

#ifndef NO_ZIPINFO

        Info(slide, flag, ((char *)slide, LoadFarString(ZipInfoUsageLine1),
          ZI_MAJORVER, ZI_MINORVER, UZ_PATCHLEVEL, UZ_BETALEVEL,
          LoadFarStringSmall(VersionDate),
          LoadFarStringSmall2(ZipInfoExample), QUOTS,QUOTS));
        Info(slide, flag, ((char *)slide, LoadFarString(ZipInfoUsageLine2)));
        Info(slide, flag, ((char *)slide, LoadFarString(ZipInfoUsageLine3),
          LoadFarStringSmall(ZipInfoUsageLine4)));

#endif /* !NO_ZIPINFO */

    } else {   /* UnZip mode */

        Info(slide, flag, ((char *)slide, LoadFarString(UnzipUsageLine1),
          UZ_MAJORVER, UZ_MINORVER, UZ_PATCHLEVEL, UZ_BETALEVEL,
          LoadFarStringSmall(VersionDate)));

        Info(slide, flag, ((char *)slide, LoadFarString(UnzipUsageLine2),
          ZIPINFO_MODE_OPTION, LoadFarStringSmall(ZipInfoMode)));

        Info(slide, flag, ((char *)slide, LoadFarString(UnzipUsageLine3),
          LoadFarStringSmall(local1)));

        Info(slide, flag, ((char *)slide, LoadFarString(UnzipUsageLine4),
          LoadFarStringSmall(local2), LoadFarStringSmall2(local3)));

        /* This is extra work for SMALL_MEM, but it will work since
         * LoadFarStringSmall2 uses the same buffer.  Remember, this
         * is a hack. */
        Info(slide, flag, ((char *)slide, LoadFarString(UnzipUsageLine5),
          LoadFarStringSmall(Example2), LoadFarStringSmall2(Example3),
          LoadFarStringSmall2(Example3)));

    } /* end if (uO.zipinfo_mode) */

    if (error)
        return PK_PARAM;
    else
        return PK_COOL;     /* just wanted usage screen: no error */

} /* end function usage() */





/* Print extended help to stdout. */
static void help_extended(pG)
    Uz_Globs *pG;
{
    extent i;             /* counter for help array */

    /* help array */
    static const char *text[] = {
  "",
  "Extended Help for UnZip",
  "",
  "See the UnZip Manual for more detailed help",
  "",
  "",
  "UnZip lists and extracts files in zip archives.  The default action is to",
  "extract zipfile entries to the current directory, creating directories as",
  "needed.  With appropriate options, UnZip lists the contents of archives",
  "instead.",
  "",
  "Basic unzip command line:",
  "  unzip [-Z] options archive[.zip] [file ...] [-x xfile ...] [-d exdir]",
  "",
  "Some examples:",
  "  unzip -l foo.zip        - list files in short format in archive foo.zip",
  "",
  "  unzip -t foo            - test the files in archive foo",
  "",
  "  unzip -Z foo            - list files using more detailed zipinfo format",
  "",
  "  unzip foo               - unzip the contents of foo in current dir",
  "",
  "  unzip -a foo            - unzip foo and convert text files to local OS",
  "",
  "If unzip is run in zipinfo mode, a more detailed list of archive contents",
  "is provided.  The -Z option sets zipinfo mode and changes the available",
  "options.",
  "",
  "Basic zipinfo command line:",
  "  zipinfo options archive[.zip] [file ...] [-x xfile ...]",
  "  unzip -Z options archive[.zip] [file ...] [-x xfile ...]",
  "",
  "Below, Mac OS refers to Mac OS before Mac OS X.  Mac OS X is a Unix based",
  "port and is referred to as Unix Apple.",
  "",
  "",
  "unzip options:",
  "  -Z   Switch to zipinfo mode.  Must be first option.",
  "  -hh  Display extended help.",
  "  -A   [OS/2, Unix DLL] Print extended help for DLL.",
  "  -c   Extract files to stdout/screen.  As -p but include names.  Also,",
  "         -a allowed and EBCDIC conversions done if needed.",
  "  -f   Freshen by extracting only if older file on disk.",
  "  -l   List files using short form.",
  "  -p   Extract files to pipe (stdout).  Only file data is output and all",
  "         files extracted in binary mode (as stored).",
  "  -t   Test archive files.",
  "  -T   Set timestamp on archive(s) to that of newest file.  Similar to",
  "       zip -o but faster.",
  "  -u   Update existing older files on disk as -f and extract new files.",
  "  -v   Use verbose list format.  If given alone as unzip -v show version",
  "         information.  Also can be added to other list commands for more",
  "         verbose output.",
  "  -z   Display only archive comment.",
  "",
  "unzip modifiers:",
  "  -a   Convert text files to local OS format.  Convert line ends, EOF",
  "         marker, and from or to EBCDIC character set as needed.",
  "  -b   Treat all files as binary.  [Tandem] Force filecode 180 ('C').",
  "         [VMS] Autoconvert binary files.  -bb forces convert of all files.",
  "  -B   [UNIXBACKUP compile option enabled] Save a backup copy of each",
  "         overwritten file in foo~ or foo~99999 format.",
  "  -C   Use case-insensitive matching.",
  "  -D   Skip restoration of timestamps for extracted directories.  On VMS this",
  "         is on by default and -D essentially becames -DD.",
  "  -DD  Skip restoration of timestamps for all entries.",
  "  -E   [MacOS (not Unix Apple)]  Display contents of MacOS extra field during",
  "         restore.",
  "  -F   [Acorn] Suppress removal of NFS filetype extension.  [Non-Acorn if",
  "         ACORN_FTYPE_NFS] Translate filetype and append to name.",
  "  -i   [MacOS] Ignore filenames in MacOS extra field.  Instead, use name in",
  "         standard header.",
  "  -j   Junk paths and deposit all files in extraction directory.",
  "  -J   [BeOS] Junk file attributes.  [MacOS] Ignore MacOS specific info.",
  "  -K   [AtheOS, BeOS, Unix] Restore SUID/SGID/Tacky file attributes.",
  "  -L   Convert to lowercase any names from uppercase only file system.",
  "  -LL  Convert all files to lowercase.",
  "  -M   Pipe all output through internal pager similar to Unix more(1).",
  "  -n   Never overwrite existing files.  Skip extracting that file, no prompt.",
  "  -N   [Amiga] Extract file comments as Amiga filenotes.",
  "  -o   Overwrite existing files without prompting.  Useful with -f.  Use with",
  "         care.",
  "  -P p Use password p to decrypt files.  THIS IS INSECURE!  Some OS show",
  "         command line to other users.",
  "  -q   Perform operations quietly.  The more q (as in -qq) the quieter.",
  "  -s   [OS/2, NT, MS-DOS] Convert spaces in filenames to underscores.",
  "  -S   [VMS] Convert text files (-a, -aa) into Stream_LF format.",
  "  -U   [UNICODE enabled] Show non-local characters as #Uxxxx or #Lxxxxxx ASCII",
  "         text escapes where x is hex digit.  [Old] -U used to leave names",
  "         uppercase if created on MS-DOS, VMS, etc.  See -L.",
  "  -UU  [UNICODE enabled] Disable use of stored UTF-8 paths.  Note that UTF-8",
  "         paths stored as native local paths are still processed as Unicode.",
  "  -V   Retain VMS file version numbers.",
  "  -W   [Only if WILD_STOP_AT_DIR] Modify pattern matching so ? and * do not",
  "         match directory separator /, but ** does.  Allows matching at specific",
  "         directory levels.",
  "  -X   [VMS, Unix, OS/2, NT, Tandem] Restore UICs and ACL entries under VMS,",
  "         or UIDs/GIDs under Unix, or ACLs under certain network-enabled",
  "         versions of OS/2, or security ACLs under Windows NT.  Can require",
  "         user privileges.",
  "  -XX  [NT] Extract NT security ACLs after trying to enable additional",
  "         system privileges.",
  "  -Y   [VMS] Treat archived name endings of .nnn as VMS version numbers.",
  "  -$   [MS-DOS, OS/2, NT] Restore volume label if extraction medium is",
  "         removable.  -$$ allows fixed media (hard drives) to be labeled.",
  "  -/ e [Acorn] Use e as extension list.",
  "  -:   [All but Acorn, VM/CMS, MVS, Tandem] Allow extract archive members into",
  "         locations outside of current extraction root folder.  This allows",
  "         paths such as ../foo to be extracted above the current extraction",
  "         directory, which can be a security problem.",
  "  -^   [Unix] Allow control characters in names of extracted entries.  Usually",
  "         this is not a good thing and should be avoided.",
  "  -2   [VMS] Force unconditional conversion of names to ODS-compatible names.",
  "         Default is to exploit destination file system, preserving cases and",
  "         extended name characters on ODS5 and applying ODS2 filtering on ODS2.",
  "",
  "",
  "Wildcards:",
  "  Internally unzip supports the following wildcards:",
  "    ?       (or %% or #, depending on OS) matches any single character",
  "    *       matches any number of characters, including zero",
  "    [list]  matches char in list (regex), can do range [ac-f], all but [!bf]",
  "  If port supports [], must escape [ as [[]",
  "  For shells that expand wildcards, escape (\\* or \"*\") so unzip can recurse.",
  "",
  "Include and Exclude:",
  "  -i pattern pattern ...   include files that match a pattern",
  "  -x pattern pattern ...   exclude files that match a pattern",
  "  Patterns are paths with optional wildcards and match paths as stored in",
  "  archive.  Exclude and include lists end at next option or end of line.",
  "    unzip archive -x pattern pattern ...",
  "",
  "Multi-part (split) archives (archives created as a set of split files):",
  "  Currently split archives are not readable by unzip.  A workaround is",
  "  to use zip to convert the split archive to a single-file archive and",
  "  use unzip on that.  See the manual page for Zip 3.0 or later.",
  "",
  "Streaming (piping into unzip):",
  "  Currently unzip does not support streaming.  The funzip utility can be",
  "  used to process the first entry in a stream.",
  "    cat archive | funzip",
  "",
  "Testing archives:",
  "  -t        test contents of archive",
  "  This can be modified using -q for quieter operation, and -qq for even",
  "  quieter operation.",
  "",
  "Unicode:",
  "  If compiled with Unicode support, unzip automatically handles archives",
  "  with Unicode entries.  Currently Unicode on Win32 systems is limited.",
  "  Characters not in the current character set are shown as ASCII escapes",
  "  in the form #Uxxxx where the Unicode character number fits in 16 bits,",
  "  or #Lxxxxxx where it doesn't, where x is the ASCII character for a hex",
  "  digit.",
  "",
  "",
  "zipinfo options (these are used in zipinfo mode (unzip -Z ...)):",
  "  -1  List names only, one per line.  No headers/trailers.  Good for scripts.",
  "  -2  List names only as -1, but include headers, trailers, and comments.",
  "  -s  List archive entries in short Unix ls -l format.  Default list format.",
  "  -m  List in long Unix ls -l format.  As -s, but includes compression %.",
  "  -l  List in long Unix ls -l format.  As -m, but compression in bytes.",
  "  -v  List zipfile information in verbose, multi-page format.",
  "  -h  List header line.  Includes archive name, actual size, total files.",
  "  -M  Pipe all output through internal pager similar to Unix more(1) command.",
  "  -t  List totals for files listed or for all files.  Includes uncompressed",
  "        and compressed sizes, and compression factors.",
  "  -T  Print file dates and times in a sortable decimal format (yymmdd.hhmmss)",
  "        Default date and time format is a more human-readable version.",
  "  -U  [UNICODE] If entry has a UTF-8 Unicode path, display any characters",
  "        not in current character set as text #Uxxxx and #Lxxxxxx escapes",
  "        representing the Unicode character number of the character in hex.",
  "  -UU [UNICODE]  Disable use of any UTF-8 path information.",
  "  -z  Include archive comment if any in listing.",
  "",
  "",
  "funzip stream extractor:",
  "  funzip extracts the first member in an archive to stdout.  Typically",
  "  used to unzip the first member of a stream or pipe.  If a file argument",
  "  is given, read from that file instead of stdin.",
  "",
  "funzip command line:",
  "  funzip [-password] [input[.zip|.gz]]",
  "",
  "",
  "unzipsfx self extractor:",
  "  Self-extracting archives made with unzipsfx are no more (or less)",
  "  portable across different operating systems than unzip executables.",
  "  In general, a self-extracting archive made on a particular Unix system,",
  "  for example, will only self-extract under the same flavor of Unix.",
  "  Regular unzip may still be used to extract embedded archive however.",
  "",
  "unzipsfx command line:",
  "  <unzipsfx+archive_filename>  [-options] [file(s) ... [-x xfile(s) ...]]",
  "",
  "unzipsfx options:",
  "  -c, -p - Output to pipe.  (See above for unzip.)",
  "  -f, -u - Freshen and Update, as for unzip.",
  "  -t     - Test embedded archive.  (Can be used to list contents.)",
  "  -z     - Print archive comment.  (See unzip above.)",
  "",
  "unzipsfx modifiers:",
  "  Most unzip modifiers are supported.  These include",
  "  -a     - Convert text files.",
  "  -n     - Never overwrite.",
  "  -o     - Overwrite without prompting.",
  "  -q     - Quiet operation.",
  "  -C     - Match names case-insensitively.",
  "  -j     - Junk paths.",
  "  -V     - Keep version numbers.",
  "  -s     - Convert spaces to underscores.",
  "  -$     - Restore volume label.",
  "",
  "If unzipsfx compiled with SFX_EXDIR defined, -d option also available:",
  "  -d exd - Extract to directory exd.",
  "By default, all files extracted to current directory.  This option",
  "forces extraction to specified directory.",
  "",
  "See unzipsfx manual page for more information.",
  ""
    };

    for (i = 0; i < sizeof(text)/sizeof(char *); i++)
    {
        Info(slide, 0, ((char *)slide, "%s\n", text[i]));
    }
} /* end function help_extended() */




/********************************/
/* Function show_version_info() */
/********************************/

static void show_version_info(Uz_Globs *pG)
{
    if (uO.qflag > 3)                           /* "unzip -vqqqq" */
        Info(slide, 0, ((char *)slide, "%d\n",
          (UZ_MAJORVER*100 + UZ_MINORVER*10 + UZ_PATCHLEVEL)));
    else {
        char *envptr;
        int numopts = 0;

        Info(slide, 0, ((char *)slide, LoadFarString(UnzipUsageLine1v),
          UZ_MAJORVER, UZ_MINORVER, UZ_PATCHLEVEL, UZ_BETALEVEL,
          LoadFarStringSmall(VersionDate)));
        Info(slide, 0, ((char *)slide,
          LoadFarString(UnzipUsageLine2v)));
        version(pG);
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptions)));
#ifdef CHECK_VERSIONS
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptFormat),
          LoadFarStringSmall(Check_Versions)));
        ++numopts;
#endif
#ifdef COPYRIGHT_CLEAN
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptFormat),
          LoadFarStringSmall(Copyright_Clean)));
        ++numopts;
#endif
#ifdef DEBUG
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptFormat),
          LoadFarStringSmall(UDebug)));
        ++numopts;
#endif
#ifdef DEBUG_TIME
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptFormat),
          LoadFarStringSmall(DebugTime)));
        ++numopts;
#endif
#ifdef DLL
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptFormat),
          LoadFarStringSmall(Dll)));
        ++numopts;
#endif
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptFormat),
          LoadFarStringSmall(No_More)));
        ++numopts;
#ifdef NO_ZIPINFO
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptFormat),
          LoadFarStringSmall(No_ZipInfo)));
        ++numopts;
#endif
#ifdef NTSD_EAS
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptFormat),
          LoadFarStringSmall(NTSDExtAttrib)));
        ++numopts;
#endif
#if defined(WIN32) && defined(NO_W32TIMES_IZFIX)
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptFormat),
          LoadFarStringSmall(W32NoIZTimeFix)));
        ++numopts;
#endif
#ifdef QLZIP
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptFormat),
          LoadFarStringSmall(SMSExFldOnUnix)));
        ++numopts;
#endif
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptFormat),
          LoadFarStringSmall(Reentrant)));
        ++numopts;
#ifdef REGARGS
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptFormat),
          LoadFarStringSmall(RegArgs)));
        ++numopts;
#endif
#ifdef RETURN_CODES
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptFormat),
          LoadFarStringSmall(Return_Codes)));
        ++numopts;
#endif
#ifdef SET_DIR_ATTRIB
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptFormat),
          LoadFarStringSmall(SetDirAttrib)));
        ++numopts;
#endif
#ifdef SYMLINKS
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptFormat),
          LoadFarStringSmall(SymLinkSupport)));
        ++numopts;
#endif
#ifdef TIMESTAMP
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptFormat),
          LoadFarStringSmall(TimeStamp)));
        ++numopts;
#endif
#ifdef UNIXBACKUP
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptFormat),
          LoadFarStringSmall(UnixBackup)));
        ++numopts;
#endif
#ifdef USE_EF_UT_TIME
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptFormat),
          LoadFarStringSmall(Use_EF_UT_time)));
        ++numopts;
#endif
#ifndef COPYRIGHT_CLEAN
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptFormat),
          LoadFarStringSmall(Use_Smith_Code)));
        ++numopts;
#endif
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptFormat),
          LoadFarStringSmall(Use_Unshrink)));
        ++numopts;
#ifdef USE_DEFLATE64
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptFormat),
          LoadFarStringSmall(Use_Deflate64)));
        ++numopts;
#endif
#ifdef UNICODE_SUPPORT
# ifdef UTF8_MAYBE_NATIVE
        sprintf((char *)(slide+256), LoadFarStringSmall(Use_Unicode),
          LoadFarStringSmall2((*(Uz_Globs *)pG).native_is_utf8 ? SysChUTF8 : SysChOther));
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptFormat),
          (char *)(slide+256)));
# else
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptFormat),
          LoadFarStringSmall(Use_Unicode)));
# endif
        ++numopts;
#endif
#ifdef _MBCS
        sprintf((char *)(slide+256), LoadFarStringSmall(Have_MBCS_Support),
          (unsigned int)MB_CUR_MAX);
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptFormat),
          (char *)(slide+256)));
        ++numopts;
#endif
#ifdef MULT_VOLUME
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptFormat),
          LoadFarStringSmall(Use_MultiVol)));
        ++numopts;
#endif
#ifdef LARGE_FILE_SUPPORT
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptFormat),
          LoadFarStringSmall(Use_LFS)));
        ++numopts;
#endif
#ifdef ZIP64_SUPPORT
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptFormat),
          LoadFarStringSmall(Use_Zip64)));
        ++numopts;
#endif
#ifdef USE_VFAT
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptFormat),
          LoadFarStringSmall(Use_VFAT_support)));
        ++numopts;
#endif
#ifdef USE_ZLIB
        sprintf((char *)(slide+256), LoadFarStringSmall(UseZlib),
          ZLIB_VERSION, zlibVersion());
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptFormat),
          (char *)(slide+256)));
        ++numopts;
#endif
#ifdef USE_BZIP2
        sprintf((char *)(slide+256), LoadFarStringSmall(UseBZip2),
          BZ2_bzlibVersion());
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptFormat),
          (char *)(slide+256)));
        ++numopts;
#endif
#ifdef VMS_TEXT_CONV
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptFormat),
          LoadFarStringSmall(VmsTextConv)));
        ++numopts;
#endif
#ifdef WILD_STOP_AT_DIR
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptFormat),
          LoadFarStringSmall(WildStopAtDir)));
        ++numopts;
#endif
#if CRYPT
# ifdef PASSWD_FROM_STDIN
        Info(slide, 0, ((char *)slide, LoadFarString(CompileOptFormat),
          LoadFarStringSmall(PasswdStdin)));
# endif
        Info(slide, 0, ((char *)slide, LoadFarString(Decryption),
          CR_MAJORVER, CR_MINORVER, CR_BETA_VER,
          LoadFarStringSmall(CryptDate)));
        ++numopts;
#endif /* CRYPT */
        if (numopts == 0)
            Info(slide, 0, ((char *)slide,
              LoadFarString(CompileOptFormat),
              LoadFarStringSmall(None)));

        Info(slide, 0, ((char *)slide, LoadFarString(EnvOptions)));
        envptr = getenv(LoadFarStringSmall(EnvUnZip));
        Info(slide, 0, ((char *)slide, LoadFarString(EnvOptFormat),
          LoadFarStringSmall(EnvUnZip),
          (envptr == (char *)NULL || *envptr == 0)?
          LoadFarStringSmall2(None) : envptr));
        envptr = getenv(LoadFarStringSmall(EnvUnZip2));
        Info(slide, 0, ((char *)slide, LoadFarString(EnvOptFormat),
          LoadFarStringSmall(EnvUnZip2),
          (envptr == (char *)NULL || *envptr == 0)?
          LoadFarStringSmall2(None) : envptr));
        envptr = getenv(LoadFarStringSmall(EnvZipInfo));
        Info(slide, 0, ((char *)slide, LoadFarString(EnvOptFormat),
          LoadFarStringSmall(EnvZipInfo),
          (envptr == (char *)NULL || *envptr == 0)?
          LoadFarStringSmall2(None) : envptr));
        envptr = getenv(LoadFarStringSmall(EnvZipInfo2));
        Info(slide, 0, ((char *)slide, LoadFarString(EnvOptFormat),
          LoadFarStringSmall(EnvZipInfo2),
          (envptr == (char *)NULL || *envptr == 0)?
          LoadFarStringSmall2(None) : envptr));
    }
} /* end function show_version() */

#endif /* !WINDLL */
