/*
  Copyright (c) 1990-2002 Info-ZIP.  All rights reserved.

  See the accompanying file LICENSE, version 2000-Apr-09 or later
  (the contents of which are also included in unzip.h) for terms of use.
  If, for some reason, all these files are missing, the Info-ZIP license
  also may be found at:  ftp://ftp.info-zip.org/pub/infozip/license.html
*/
/* Write out a fragment of assembly or C preprocessor source giving offsets
 * in "Uz_Globs" and "struct huft".  Used by Amiga and Human68k ports.
 */

#define UNZIP_INTERNAL
#include "unzip.h"
#include "crypt.h"

static int asm_setflag(const char *flagname);
static int ccp_setflag(const char *flagname);

static int asm_setflag(const char *flagname)
{
    static const char asm_flagdef[] = "   IFND %s\n%-15s EQU     1\n   ENDC\n";
    return printf(asm_flagdef, flagname, flagname);
}
static int ccp_setflag(const char *flagname)
{
    static const char ccp_flagdef[] = "#ifndef %s\n#  define %s\n#endif\n";
    return printf(ccp_flagdef, flagname, flagname);
}

int 
main (int argc, char **argv)
{
    Uz_Globs *pG = NULL;
    struct huft *t = NULL;
    static const char asm_offsdef[] = "%-15s EQU     %lu\n";
    static const char ccp_offsdef[] = "#define %-15s %lu\n";

    const char *out_format;
    int (*set_flag)(const char *flagname);
    int ccp_select = 0;

    if (argc > 1 && argv[1] != NULL && !strcmp(argv[1], "-ccp"))
      ccp_select = 1;

    if (ccp_select) {
        out_format = ccp_offsdef;
        set_flag = ccp_setflag;
    } else {
        out_format = asm_offsdef;
        set_flag = asm_setflag;
    }

    printf(out_format, "h_e", (ulg)&t->e - (ulg)t);
    printf(out_format, "h_b", (ulg)&t->b - (ulg)t);
    printf(out_format, "h_v_n", (ulg)&t->v.n - (ulg)t);
    printf(out_format, "h_v_t", (ulg)&t->v.t - (ulg)t);
    printf(out_format, "SIZEOF_huft", (ulg)sizeof(struct huft));

    printf(out_format, "bb", (ulg)&(*(Uz_Globs *)pG).bb - (ulg)&(*(Uz_Globs *)pG));
    printf(out_format, "bk", (ulg)&(*(Uz_Globs *)pG).bk - (ulg)&(*(Uz_Globs *)pG));
    printf(out_format, "wp", (ulg)&(*(Uz_Globs *)pG).wp - (ulg)&(*(Uz_Globs *)pG));
    printf(out_format, "incnt", (ulg)&(*(Uz_Globs *)pG).incnt - (ulg)&(*(Uz_Globs *)pG));
    printf(out_format, "inptr", (ulg)&(*(Uz_Globs *)pG).inptr - (ulg)&(*(Uz_Globs *)pG));
    printf(out_format, "csize", (ulg)&(*(Uz_Globs *)pG).csize - (ulg)&(*(Uz_Globs *)pG));
    printf(out_format, "mem_mode", (ulg)&(*(Uz_Globs *)pG).mem_mode - (ulg)&(*(Uz_Globs *)pG));
#endif
    printf(out_format, "redirslide", (ulg)&redirSlide - (ulg)&(*(Uz_Globs *)pG));
    printf(out_format, "SIZEOF_slide", (ulg)sizeof(redirSlide));
#if (defined(DLL) && !defined(NO_SLIDE_REDIR))
    printf(out_format, "_wsize", (ulg)&(*(Uz_Globs *)pG)._wsize - (ulg)&(*(Uz_Globs *)pG));
#endif /* DLL && !NO_SLIDE_REDIR */
    printf(out_format, "CRYPT", (ulg)CRYPT);
    (*set_flag)("REENTRANT");
#ifdef DLL
    (*set_flag)("DLL");
# ifdef NO_SLIDE_REDIR
    (*set_flag)("NO_SLIDE_REDIR");
# endif
#endif
    (*set_flag)("USE_DEFLATE64");

    return 0;
}
