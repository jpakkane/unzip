/*
  Copyright (c) 1990-2001 Info-ZIP.  All rights reserved.

  See the accompanying file LICENSE, version 2000-Apr-09 or later
  (the contents of which are also included in unzip.h) for terms of use.
  If, for some reason, all these files are missing, the Info-ZIP license
  also may be found at:  ftp://ftp.info-zip.org/pub/infozip/license.html
*/
/*---------------------------------------------------------------------------

  consts.h

  This file contains global, initialized variables that never change.  It is
  included by unzip.c and windll/windll.c.

  ---------------------------------------------------------------------------*/

#pragma once

#include"unzvers.h"

/* And'ing with mask_bits[n] masks the lower n bits */
const unsigned int mask_bits[17] = {
    0x0000,
    0x0001, 0x0003, 0x0007, 0x000f, 0x001f, 0x003f, 0x007f, 0x00ff,
    0x01ff, 0x03ff, 0x07ff, 0x0fff, 0x1fff, 0x3fff, 0x7fff, 0xffff
};

const char VersionDate[] = UZ_VERSION_DATE;

   const char EndSigMsg[] =
   "\nnote:  didn't find end-of-central-dir signature at end of central dir.\n";

const char CentSigMsg[] =
  "error:  expected central file header signature not found (file #%lu).\n";
const char SeekMsg[] =
  "error [%s]:  attempt to seek before beginning of zipfile\n%s";
const char FilenameNotMatched[] = "caution: filename not matched:  %s\n";
const char ExclFilenameNotMatched[] =
  "caution: excluded filename not matched:  %s\n";

  const char ReportMsg[] = "\
  (please check that you have transferred or created the zipfile in the\n\
  appropriate BINARY mode and that you have compiled UnZip properly)\n";

  const char Zipnfo[] = "zipinfo";
  const char CompiledWith[] = "Compiled with %s%s for %s%s%s%s.\n\n";
