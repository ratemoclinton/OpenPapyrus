/*====================================================================*
 -  Copyright (C) 2001 Leptonica.  All rights reserved.
 -
 -  Redistribution and use in source and binary forms, with or without
 -  modification, are permitted provided that the following conditions
 -  are met:
 -  1. Redistributions of source code must retain the above copyright
 -     notice, this list of conditions and the following disclaimer.
 -  2. Redistributions in binary form must reproduce the above
 -     copyright notice, this list of conditions and the following
 -     disclaimer in the documentation and/or other materials
 -     provided with the distribution.
 -
 -  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 -  ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 -  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 -  A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL ANY
 -  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 -  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 -  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 -  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 -  OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 -  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 -  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *====================================================================*/

/*!
 * \file jpegiostub.c
 * <pre>
 *
 *     Stubs for jpegio.c functions
 * </pre>
 */

//#ifdef HAVE_CONFIG_H
//#include "config_auto.h"
//#endif  /* HAVE_CONFIG_H */

#include "allheaders.h"
#pragma hdrstop

/* --------------------------------------------*/
#if  !HAVE_LIBJPEG   /* defined in environ.h */
/* --------------------------------------------*/

/* ----------------------------------------------------------------------*/

PIX * pixReadJpeg(const char *filename, int32 cmflag, int32 reduction,
                  int32 *pnwarn, int32 hint)
{
    return (PIX * )ERROR_PTR("function not present", "pixReadJpeg", NULL);
}

/* ----------------------------------------------------------------------*/

PIX * pixReadStreamJpeg(FILE *fp, int32 cmflag, int32 reduction,
                        int32 *pnwarn, int32 hint)
{
    return (PIX * )ERROR_PTR("function not present", "pixReadStreamJpeg", NULL);
}

/* ----------------------------------------------------------------------*/

int32 readHeaderJpeg(const char *filename, int32 *pw, int32 *ph,
                       int32 *pspp, int32 *pycck, int32 *pcmyk)
{
    return ERROR_INT("function not present", "readHeaderJpeg", 1);
}

/* ----------------------------------------------------------------------*/

int32 freadHeaderJpeg(FILE *fp, int32 *pw, int32 *ph,
                       int32 *pspp, int32 *pycck, int32 *pcmyk)
{
    return ERROR_INT("function not present", "freadHeaderJpeg", 1);
}

/* ----------------------------------------------------------------------*/

int32 fgetJpegResolution(FILE *fp, int32 *pxres, int32 *pyres)
{
    return ERROR_INT("function not present", "fgetJpegResolution", 1);
}

/* ----------------------------------------------------------------------*/

int32 fgetJpegComment(FILE *fp, uint8 **pcomment)
{
    return ERROR_INT("function not present", "fgetJpegComment", 1);
}

/* ----------------------------------------------------------------------*/

int32 pixWriteJpeg(const char *filename, PIX *pix, int32 quality,
                     int32 progressive)
{
    return ERROR_INT("function not present", "pixWriteJpeg", 1);
}

/* ----------------------------------------------------------------------*/

int32 pixWriteStreamJpeg(FILE *fp, PIX *pix, int32 quality,
                           int32 progressive)
{
    return ERROR_INT("function not present", "pixWriteStreamJpeg", 1);
}

/* ----------------------------------------------------------------------*/

PIX * pixReadMemJpeg(const uint8 *cdata, size_t size, int32 cmflag,
                     int32 reduction, int32 *pnwarn, int32 hint)
{
    return (PIX * )ERROR_PTR("function not present", "pixReadMemJpeg", NULL);
}

/* ----------------------------------------------------------------------*/

int32 readHeaderMemJpeg(const uint8 *cdata, size_t size,
                          int32 *pw, int32 *ph, int32 *pspp,
                          int32 *pycck, int32 *pcmyk)
{
    return ERROR_INT("function not present", "readHeaderMemJpeg", 1);
}

/* ----------------------------------------------------------------------*/

int32 pixWriteMemJpeg(uint8 **pdata, size_t *psize, PIX *pix,
                        int32 quality, int32 progressive)
{
    return ERROR_INT("function not present", "pixWriteMemJpeg", 1);
}

/* ----------------------------------------------------------------------*/

int32 pixSetChromaSampling(PIX *pix, int32 sampling)
{
    return ERROR_INT("function not present", "pixSetChromaSampling", 1);
}

/* ----------------------------------------------------------------------*/

/* --------------------------------------------*/
#endif  /* !HAVE_LIBJPEG */
/* --------------------------------------------*/
