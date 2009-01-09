/*
libdmtx - Data Matrix Encoding/Decoding Library

Copyright (c) 2008 Mike Laughton
Copyright (c) 2008 Ryan Raasch
Copyright (c) 2008 Olivier Guilyardi

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

Contact: mike@dragonflylogic.com
*/

/* $Id$ */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <assert.h>
#include <magick/api.h>
#include <dmtx.h>
#include "dmtxread.h"
#include "../common/dmtxutil.h"

#ifdef HAVE_SYSEXITS_H
#include <sysexits.h>
#else
#define EX_OK           0
#define EX_USAGE       64
#define EX_CANTCREAT   73
#endif

char *programName;

/**
 * @brief  Main function for the dmtxread Data Matrix scanning utility.
 * @param  argc count of arguments passed from command line
 * @param  argv list of argument passed strings from command line
 * @return Numeric exit code
 */
int
main(int argc, char *argv[])
{
   char *filePath;
   int i;
   int err;
   int fileIndex, imgPageIndex;
   int fileCount, imgPageCount;
   int imageScanCount, pageScanCount;
   int firstPage, finalPage;
   unsigned char *pxl;
   UserOptions opt;
   DmtxTime timeout;
   DmtxImage *img;
   DmtxDecode *dec;
   DmtxRegion reg;
   DmtxMessage *msg;
   GmImage *imgList, *imgPage;
   GmImageInfo *imgInfo;

   opt = GetDefaultOptions();

   err = HandleArgs(&opt, &fileIndex, &argc, &argv);
   if(err != DmtxPass)
      ShowUsage(EX_USAGE);

   fileCount = (argc == fileIndex) ? 1 : argc - fileIndex;

   gmInitializeMagick(*argv);

   /* Loop once for each image named on command line */
   imageScanCount = 0;
   for(i = 0; i < fileCount; i++) {

      /* Open image from file or stream (might contain multiple pages) */
      filePath = (argc == fileIndex) ? "-" : argv[fileIndex++];
      imgList = OpenImageList(&imgInfo, filePath, opt.resolution);
      if(imgList == NULL)
         continue;

      /* Loop once for each page within image */
      imgPageCount = gmGetImageListLength(imgList);

      /* Determine page range */
      if(opt.page == -1) {
         firstPage = 0;
         finalPage = imgPageCount - 1;
      }
      else {
         firstPage = opt.page - 1;
         finalPage = opt.page - 1;
      }

      for(imgPageIndex = firstPage; imgPageIndex <= finalPage; imgPageIndex++) {

         /* Reset timeout for each new page */
         if(opt.timeoutMS != -1)
            timeout = dmtxTimeAdd(dmtxTimeNow(), opt.timeoutMS);

         imgPage = gmGetImageFromList(imgList, imgPageIndex);
         assert(imgPage != NULL);

         /* Allocate memory for pixel data */
         pxl = (unsigned char *)malloc(3 * imgPage->columns * imgPage->rows *
               sizeof(unsigned char));
         if(pxl == NULL) {
            CleanupMagick(&imgList, &imgInfo);
            FatalError(80, "malloc() error");
         }

         /* Copy pixels to known format */
         WritePixelsToBuffer(pxl, imgPage);

         /* Initialize libdmtx image */
         img = dmtxImageCreate(pxl, imgPage->columns, imgPage->rows, 24, DmtxPackRGB, DmtxFlipY);
         if(img == NULL) {
            CleanupMagick(&imgList, &imgInfo);
            FatalError(80, "dmtxImageCreate() error");
         }

         /* Initialize scan */
         dec = dmtxDecodeStructCreate(img);
         if(dec == NULL) {
            CleanupMagick(&imgList, &imgInfo);
            FatalError(80, "decode create error");
         }

         err = SetDecodeOptions(dec, img, &opt);
         if(err != DmtxPass) {
            CleanupMagick(&imgList, &imgInfo);
            FatalError(80, "decode option error");
         }

         /* Find and decode every barcode on page */
         pageScanCount = 0;
         for(;;) {
            /* Find next barcode region within image, but do not decode yet */
            reg = dmtxDecodeFindNextRegion(dec, (opt.timeoutMS == -1) ?
                  NULL : &timeout);

            /* Finished file or ran out of time before finding another region */
            if(reg.found != DMTX_REGION_FOUND)
               break;

            /* Decode region based on requested barcode mode */
            if(opt.mosaic)
               msg = dmtxDecodeMosaicRegion(img, &reg, opt.correctionsMax);
            else
               msg = dmtxDecodeMatrixRegion(img, &reg, opt.correctionsMax);

            if(msg == NULL)
               continue;

            PrintDecodedOutput(&opt, img, &reg, msg, imgPageIndex);
            dmtxMessageDestroy(&msg);
            pageScanCount++;
            imageScanCount++;

            if(opt.stopAfter != -1 && imageScanCount >= opt.stopAfter)
               break;
         }

         if(opt.diagnose)
            WriteDiagnosticImage(dec, &reg, "debug.pnm");

         dmtxDecodeStructDestroy(&dec);
         dmtxImageDestroy(&img);
         free(pxl);
      }

      CleanupMagick(&imgList, &imgInfo);
   }

   gmDestroyMagick();

   exit((imageScanCount > 0) ? EX_OK : 1);
}

/**
 *
 *
 */
static UserOptions
GetDefaultOptions(void)
{
   UserOptions opt;

   memset(&opt, 0x00, sizeof(UserOptions));

   /* Default options */
   opt.codewords = 0;
   opt.edgeMin = -1;
   opt.edgeMax = -1;
   opt.squareDevn = -1;
   opt.scanGap = 2;
   opt.timeoutMS = -1;
   opt.newline = 0;
   opt.page = -1;
   opt.resolution = NULL;
   opt.sizeIdxExpected = DmtxSymbolShapeAuto;
   opt.edgeThresh = 5;
   opt.xMin = NULL;
   opt.xMax = NULL;
   opt.yMin = NULL;
   opt.yMax = NULL;
   opt.correctionsMax = -1;
   opt.diagnose = 0;
   opt.mosaic = 0;
   opt.stopAfter = -1;
   opt.pageNumbers = 0;
   opt.corners = 0;
   opt.shrinkMin = 1;
   opt.shrinkMax = 1;
   opt.verbose = 0;

   return opt;
}

/**
 * @brief  Set and validate user-requested options from command line arguments.
 * @param  opt runtime options from defaults or command line
 * @param  argcp pointer to argument count
 * @param  argvp pointer to argument list
 * @param  fileIndex pointer to index of first non-option arg (if successful)
 * @return DmtxPass | DmtxFail
 */
static DmtxPassFail
HandleArgs(UserOptions *opt, int *fileIndex, int *argcp, char **argvp[])
{
   int i;
   int err;
   int optchr;
   int longIndex;
   char *ptr;

   struct option longOptions[] = {
         {"codewords",        no_argument,       NULL, 'c'},
         {"minimum-edge",     required_argument, NULL, 'e'},
         {"maximum-edge",     required_argument, NULL, 'E'},
         {"gap",              required_argument, NULL, 'g'},
         {"list-formats",     no_argument,       NULL, 'l'},
         {"milliseconds",     required_argument, NULL, 'm'},
         {"newline",          no_argument,       NULL, 'n'},
         {"page",             required_argument, NULL, 'p'},
         {"square-deviation", required_argument, NULL, 'q'},
         {"resolution",       required_argument, NULL, 'r'},
         {"symbol-size",      required_argument, NULL, 's'},
         {"threshold",        required_argument, NULL, 't'},
         {"x-range-min",      required_argument, NULL, 'x'},
         {"x-range-max",      required_argument, NULL, 'X'},
         {"y-range-min",      required_argument, NULL, 'y'},
         {"y-range-max",      required_argument, NULL, 'Y'},
         {"max-corrections",  required_argument, NULL, 'C'},
         {"diagnose",         no_argument,       NULL, 'D'},
         {"mosaic",           no_argument,       NULL, 'M'},
         {"stop-after",       required_argument, NULL, 'N'},
         {"page-numbers",     no_argument,       NULL, 'P'},
         {"corners",          no_argument,       NULL, 'R'},
         {"shrink",           required_argument, NULL, 'S'},
         {"verbose",          no_argument,       NULL, 'v'},
         {"version",          no_argument,       NULL, 'V'},
         {"help",             no_argument,       NULL,  0 },
         {0, 0, 0, 0}
   };

   programName = Basename((*argvp)[0]);

   *fileIndex = 0;

   for(;;) {
      optchr = getopt_long(*argcp, *argvp, "ce:E:g:lm:np:q:r:s:t:x:X:y:Y:vC:DMN:PRS:V",
            longOptions, &longIndex);
      if(optchr == -1)
         break;

      switch(optchr) {
         case 0: /* --help */
            ShowUsage(EX_OK);
            break;
         case 'l': /* --help */
            ListImageFormats();
            exit(EX_OK);
            break;
         case 'c':
            opt->codewords = 1;
            break;
         case 'e':
            err = StringToInt(&(opt->edgeMin), optarg, &ptr);
            if(err != DmtxPass || opt->edgeMin <= 0 || *ptr != '\0')
               FatalError(EX_USAGE, _("Invalid edge length specified \"%s\""), optarg);
            break;
         case 'E':
            err = StringToInt(&(opt->edgeMax), optarg, &ptr);
            if(err != DmtxPass || opt->edgeMax <= 0 || *ptr != '\0')
               FatalError(EX_USAGE, _("Invalid edge length specified \"%s\""), optarg);
            break;
         case 'g':
            err = StringToInt(&(opt->scanGap), optarg, &ptr);
            if(err != DmtxPass || opt->scanGap <= 0 || *ptr != '\0')
               FatalError(EX_USAGE, _("Invalid gap specified \"%s\""), optarg);
            break;
         case 'm':
            err = StringToInt(&(opt->timeoutMS), optarg, &ptr);
            if(err != DmtxPass || opt->timeoutMS < 0 || *ptr != '\0')
               FatalError(EX_USAGE, _("Invalid timeout (in milliseconds) specified \"%s\""), optarg);
            break;
         case 'n':
            opt->newline = 1;
            break;
         case 'p':
            err = StringToInt(&(opt->page), optarg, &ptr);
            if(err != DmtxPass || opt->page < 1 || *ptr != '\0')
               FatalError(EX_USAGE, _("Invalid page specified \"%s\""), optarg);
         case 'q':
            err = StringToInt(&(opt->squareDevn), optarg, &ptr);
            if(err != DmtxPass || *ptr != '\0' ||
                  opt->squareDevn < 0 || opt->squareDevn > 90)
               FatalError(EX_USAGE, _("Invalid squareness deviation specified \"%s\""), optarg);
            break;
         case 'r':
            opt->resolution = optarg;
            break;
         case 's':
            /* Determine correct barcode size and/or shape */
            if(*optarg == 'a') {
               opt->sizeIdxExpected = DmtxSymbolShapeAuto;
            }
            else if(*optarg == 's') {
               opt->sizeIdxExpected = DmtxSymbolSquareAuto;
            }
            else if(*optarg == 'r') {
               opt->sizeIdxExpected = DmtxSymbolRectAuto;
            }
            else {
               for(i = 0; i < DMTX_SYMBOL_SQUARE_COUNT + DMTX_SYMBOL_RECT_COUNT; i++) {
                  if(strncmp(optarg, symbolSizes[i], 8) == 0) {
                     opt->sizeIdxExpected = i;
                     break;
                  }
               }
               if(i == DMTX_SYMBOL_SQUARE_COUNT + DMTX_SYMBOL_RECT_COUNT)
                  return DmtxFail;
            }
            break;
         case 't':
            err = StringToInt(&(opt->edgeThresh), optarg, &ptr);
            if(err != DmtxPass || *ptr != '\0' ||
                  opt->edgeThresh < 1 || opt->edgeThresh > 100)
               FatalError(EX_USAGE, _("Invalid edge threshold specified \"%s\""), optarg);
            break;
         case 'x':
            opt->xMin = optarg;
            break;
         case 'X':
            opt->xMax = optarg;
            break;
         case 'y':
            opt->yMin = optarg;
            break;
         case 'Y':
            opt->yMax = optarg;
            break;
         case 'v':
            opt->verbose = 1;
            break;
         case 'C':
            err = StringToInt(&(opt->correctionsMax), optarg, &ptr);
            if(err != DmtxPass || opt->correctionsMax < 0 || *ptr != '\0')
               FatalError(EX_USAGE, _("Invalid max corrections specified \"%s\""), optarg);
            break;
         case 'D':
            opt->diagnose = 1;
            break;
         case 'M':
            opt->mosaic = 1;
            break;
         case 'N':
            err = StringToInt(&(opt->stopAfter), optarg, &ptr);
            if(err != DmtxPass || opt->stopAfter < 1 || *ptr != '\0')
               FatalError(EX_USAGE, _("Invalid count specified \"%s\""), optarg);
            break;
         case 'P':
            opt->pageNumbers = 1;
            break;
         case 'R':
            opt->corners = 1;
            break;
         case 'S':
            err = StringToInt(&(opt->shrinkMax), optarg, &ptr);
            if(err != DmtxPass || opt->shrinkMax < 1 || *ptr != '\0')
               FatalError(EX_USAGE, _("Invalid shrink factor specified \"%s\""), optarg);
            /* XXX later also popular shrinkMin based on N-N range */
            break;
         case 'V':
            fprintf(stdout, "%s version %s\n", programName, DMTX_VERSION);
            fprintf(stdout, "libdmtx version %s\n", dmtxVersion());
            exit(EX_OK);
            break;
         default:
            return DmtxFail;
            break;
      }
   }
   *fileIndex = optind;

   return DmtxPass;
}

/**
 * @brief  Display program usage and exit with received status.
 * @param  status error code returned to OS
 * @return void
 */
static void
ShowUsage(int status)
{
   if(status != 0) {
      fprintf(stderr, _("Usage: %s [OPTION]... [FILE]...\n"), programName);
      fprintf(stderr, _("Try `%s --help' for more information.\n"), programName);
   }
   else {
      fprintf(stdout, _("Usage: %s [OPTION]... [FILE]...\n"), programName);
      fprintf(stdout, _("\
Scan image FILE for Data Matrix barcodes and print decoded results to\n\
standard output.  Note that %s may find multiple barcodes in one image.\n\
\n\
Example: Scan top third of IMAGE001.png and stop after first barcode is found:\n\
\n\
   %s -n -Y33%% -N1 IMAGE001.png\n\
\n\
OPTIONS:\n"), programName, programName);
      fprintf(stdout, _("\
  -c, --codewords             print codewords extracted from barcode pattern\n\
  -e, --minimum-edge=N        pixel length of smallest expected edge in image\n\
  -E, --maximum-edge=N        pixel length of largest expected edge in image\n\
  -g, --gap=NUM               use scan grid with gap of NUM pixels between lines\n\
  -l, --list-formats          list supported image formats\n\
  -m, --milliseconds=N        stop scan after N milliseconds (per image)\n\
  -n, --newline               print newline character at the end of decoded data\n\
  -p, --page=N                only scan Nth page of images\n\
  -q, --square-deviation=N    allow non-squareness of corners in degrees (0-90)\n"));
      fprintf(stdout, _("\
  -r, --resolution=N          resolution for vector images (PDF, SVG, etc...)\n\
  -s, --symbol-size=[asr|RxC] only consider barcodes of specific size or shape\n\
        a = All sizes         [default]\n\
        s = Only squares\n\
        r = Only rectangles\n\
      RxC = Exactly this many rows and columns (10x10, 8x18, etc...)\n\
  -t, --threshold=N           ignore weak edges below threshold N (1-100)\n"));
      fprintf(stdout, _("\
  -x, --x-range-min=N[%%]      do not scan pixels to the left of N (or N%%)\n\
  -X, --x-range-max=N[%%]      do not scan pixels to the right of N (or N%%)\n\
  -y, --y-range-min=N[%%]      do not scan pixels above N (or N%%)\n\
  -Y, --y-range-max=N[%%]      do not scan pixels below N (or N%%)\n\
  -C, --corrections-max=N     correct at most N errors (0 = correction disabled)\n\
  -D, --diagnose              make copy of image with additional diagnostic data\n\
  -M, --mosaic                interpret detected regions as Data Mosaic barcodes\n"));
      fprintf(stdout, _("\
  -N, --stop-after=N          stop scanning after Nth barcode is returned\n\
  -P, --page-numbers          prefix decoded message with fax/tiff page number\n\
  -R, --corners               prefix decoded message with corner locations\n\
  -S, --shrink=N              internally shrink image by a factor of N\n\
  -v, --verbose               use verbose messages\n\
  -V, --version               print program version information\n\
      --help                  display this help and exit\n"));
      fprintf(stdout, _("\nReport bugs to <mike@dragonflylogic.com>.\n"));
   }

   exit(status);
}

/**
 * @brief  Open an image input file
 * @param  reader Pointer to ImageReader struct
 * @param  imagePath Image path or "-" for stdin
 * @return Image list
 */
static GmImage *
OpenImageList(GmImageInfo **imgInfo, char *imagePath, char *resolution)
{
   GmImageInfo *info;
   GmImage *imgList;
   GmExceptionInfo exception;

   info = *imgInfo = gmCloneImageInfo((GmImageInfo *) NULL);
   imgList = NULL;
   gmGetExceptionInfo(&exception);

   strncpy(info->filename, imagePath, MaxTextExtent);
   info->filename[MaxTextExtent-1] = '\0';

   if(resolution) {
      info->density = strdup(resolution);
      info->units = PixelsPerInchResolution;
   }
   imgList = gmReadImage(info, &exception);

   switch(exception.severity) {
/*    case UndefinedException:
         break; */
      case OptionError:
      case MissingDelegateError:
         fprintf(stderr,
               "%s: unsupported file format. Use -l for a list of available formats.\n",
               imagePath);
         break;
      default:
         gmCatchException(&exception);
         break;
   }

   gmDestroyExceptionInfo(&exception);

   return imgList;
}

/**
 *
 *
 */
static void
CleanupMagick(GmImage **imgList, GmImageInfo **imgInfo)
{
   if(*imgList != NULL) {
      gmDestroyImage(*imgList);
      *imgList = NULL;
   }

   if(*imgInfo != NULL) {
      gmDestroyImageInfo(*imgInfo);
      *imgInfo = NULL;
   }
}

/**
 * @brief  Read an image page
 * @param  reader pointer to ImageReader struct
 * @param  index page index
 * @return pointer to allocated DmtxImage or NULL
 */
static void
WritePixelsToBuffer(unsigned char *pxl, GmImage *imgPage)
{
   GmExceptionInfo exception;

   assert(pxl != NULL && imgPage != NULL);

   gmGetExceptionInfo(&exception);

   gmDispatchImage(imgPage, 0, 0, imgPage->columns, imgPage->rows, "RGB",
         CharPixel, pxl, &exception);

   gmCatchException(&exception);
   gmDestroyExceptionInfo(&exception);
}

/**
 *
 *
 */
static DmtxPassFail
SetDecodeOptions(DmtxDecode *dec, DmtxImage *img, UserOptions *opt)
{
   int err;

#define RETURN_IF_FAILED(e) if(e != DmtxPass) { return DmtxFail; }

   err = dmtxDecodeSetProp(dec, DmtxPropScanGap, opt->scanGap);
   RETURN_IF_FAILED(err)

   if(opt->edgeMin != -1) {
      err = dmtxDecodeSetProp(dec, DmtxPropEdgeMin, opt->edgeMin);
      RETURN_IF_FAILED(err)
   }

   if(opt->edgeMax != -1) {
      err = dmtxDecodeSetProp(dec, DmtxPropEdgeMax, opt->edgeMax);
      RETURN_IF_FAILED(err)
   }

   if(opt->squareDevn != -1) {
      err = dmtxDecodeSetProp(dec, DmtxPropSquareDevn, opt->squareDevn);
      RETURN_IF_FAILED(err)
   }

   err = dmtxDecodeSetProp(dec, DmtxPropSymbolSize, opt->sizeIdxExpected);
   RETURN_IF_FAILED(err)

   err = dmtxDecodeSetProp(dec, DmtxPropEdgeThresh, opt->edgeThresh);
   RETURN_IF_FAILED(err)

   if(opt->xMin) {
      err = dmtxDecodeSetProp(dec, DmtxPropXmin, ScaleNumberString(opt->xMin, img->width));
      RETURN_IF_FAILED(err)
   }

   if(opt->xMax) {
      err = dmtxDecodeSetProp(dec, DmtxPropXmax, ScaleNumberString(opt->xMax, img->width));
      RETURN_IF_FAILED(err)
   }

   if(opt->yMin) {
      err = dmtxDecodeSetProp(dec, DmtxPropYmin, ScaleNumberString(opt->yMin, img->height));
      RETURN_IF_FAILED(err)
   }

   if(opt->yMax) {
      err = dmtxDecodeSetProp(dec, DmtxPropYmax, ScaleNumberString(opt->yMax, img->height));
      RETURN_IF_FAILED(err)
   }

   err = dmtxDecodeSetProp(dec, DmtxPropShrinkMin, opt->shrinkMin);
   RETURN_IF_FAILED(err)

   err = dmtxDecodeSetProp(dec, DmtxPropShrinkMax, opt->shrinkMax);
   RETURN_IF_FAILED(err)

#undef RETURN_IF_FAILED

   return DmtxPass;
}

/**
 * @brief  Print decoded message to standard output
 * @param  opt runtime options from defaults or command line
 * @param  dec pointer to DmtxDecode struct
 * @return DmtxPass | DmtxFail
 */
static DmtxPassFail
PrintDecodedOutput(UserOptions *opt, DmtxImage *image,
      DmtxRegion *reg, DmtxMessage *msg, int imgPageIndex)
{
   int i;
   int height;
   int dataWordLength;
   int rotateInt;
   double rotate;
   DmtxVector2 p00, p10, p11, p01;

   height = dmtxImageGetProp(image, DmtxPropScaledHeight);

   p00.X = p00.Y = p10.Y = p01.X = 0.0;
   p10.X = p01.Y = p11.X = p11.Y = 1.0;
   dmtxMatrix3VMultiplyBy(&p00, reg->fit2raw);
   dmtxMatrix3VMultiplyBy(&p10, reg->fit2raw);
   dmtxMatrix3VMultiplyBy(&p11, reg->fit2raw);
   dmtxMatrix3VMultiplyBy(&p01, reg->fit2raw);

   dataWordLength = dmtxGetSymbolAttribute(DmtxSymAttribSymbolDataWords, reg->sizeIdx);
   if(opt->verbose) {

      rotate = (2 * M_PI) + (atan2(reg->fit2raw[0][1], reg->fit2raw[1][1]) -
            atan2(reg->fit2raw[1][0], reg->fit2raw[0][0])) / 2.0;

      rotateInt = (int)(rotate * 180/M_PI + 0.5);
      if(rotateInt >= 360)
         rotateInt -= 360;

      fprintf(stdout, "--------------------------------------------------\n");
      fprintf(stdout, "       Matrix Size: %d x %d\n",
            dmtxGetSymbolAttribute(DmtxSymAttribSymbolRows, reg->sizeIdx),
            dmtxGetSymbolAttribute(DmtxSymAttribSymbolCols, reg->sizeIdx));
      fprintf(stdout, "    Data Codewords: %d (capacity %d)\n",
            msg->outputIdx, dataWordLength);
      fprintf(stdout, "   Error Codewords: %d\n",
            dmtxGetSymbolAttribute(DmtxSymAttribSymbolErrorWords, reg->sizeIdx));
      fprintf(stdout, "      Data Regions: %d x %d\n",
            dmtxGetSymbolAttribute(DmtxSymAttribHorizDataRegions, reg->sizeIdx),
            dmtxGetSymbolAttribute(DmtxSymAttribVertDataRegions, reg->sizeIdx));
      fprintf(stdout, "Interleaved Blocks: %d\n",
            dmtxGetSymbolAttribute(DmtxSymAttribInterleavedBlocks, reg->sizeIdx));
      fprintf(stdout, "    Rotation Angle: %d\n", rotateInt);
      fprintf(stdout, "          Corner 0: (%0.1f, %0.1f)\n", p00.X, height - 1 - p00.Y);
      fprintf(stdout, "          Corner 1: (%0.1f, %0.1f)\n", p10.X, height - 1 - p10.Y);
      fprintf(stdout, "          Corner 2: (%0.1f, %0.1f)\n", p11.X, height - 1 - p11.Y);
      fprintf(stdout, "          Corner 3: (%0.1f, %0.1f)\n", p01.X, height - 1 - p01.Y);
      fprintf(stdout, "--------------------------------------------------\n");
   }

   if(opt->pageNumbers)
      fprintf(stdout, "%d:", imgPageIndex + 1);

   if(opt->corners) {
      fprintf(stdout, "%d,%d:", (int)(p00.X + 0.5), height - 1 - (int)(p00.Y + 0.5));
      fprintf(stdout, "%d,%d:", (int)(p10.X + 0.5), height - 1 - (int)(p10.Y + 0.5));
      fprintf(stdout, "%d,%d:", (int)(p11.X + 0.5), height - 1 - (int)(p11.Y + 0.5));
      fprintf(stdout, "%d,%d:", (int)(p01.X + 0.5), height - 1 - (int)(p01.Y + 0.5));
   }

   if(opt->codewords) {
      for(i = 0; i < msg->codeSize; i++) {
         fprintf(stdout, "%c:%03d\n", (i < dataWordLength) ?
               'd' : 'e', msg->code[i]);
      }
   }
   else {
      fwrite(msg->output, sizeof(char), msg->outputIdx, stdout);
      if(opt->newline)
         fputc('\n', stdout);
   }
   fflush(stdout);

   return DmtxPass;
}

/**
 *
 *
 */
static void
WriteDiagnosticImage(DmtxDecode *dec, DmtxRegion *reg, char *imagePath)
{
   int row, col;
   int width, height;
   int offset;
   double shade;
   FILE *fp;
/* DmtxRgb rgb; */
   int rgb[3];

   fp = fopen(imagePath, "wb");
   if(fp == NULL) {
      perror(programName);
      FatalError(EX_CANTCREAT, _("Unable to write image \"%s\""), imagePath);
   }

   width = dmtxImageGetProp(dec->image, DmtxPropScaledWidth);
   height = dmtxImageGetProp(dec->image, DmtxPropScaledHeight);

   /* Test each pixel of input image to see if it lies in region */
   fprintf(fp, "P6\n%d %d\n255\n", width, height);
   for(row = height - 1; row >= 0; row--) {
      for(col = 0; col < width; col++) {

         offset = dmtxImageGetPixelOffset(dec->image, col, row);
         if(offset == DMTX_BAD_OFFSET) {
            rgb[0] = 0;
            rgb[1] = 0;
            rgb[2] = 128;
         }
         else {
/*          dmtxImageGetRgb(dec->image, col, row, rgb); */
            dmtxImageGetPixelValue(dec->image, col, row, 0, &rgb[0]);
            dmtxImageGetPixelValue(dec->image, col, row, 1, &rgb[1]);
            dmtxImageGetPixelValue(dec->image, col, row, 2, &rgb[2]);

            if(dec->image->cache[offset] & 0x40) {
               rgb[0] = 255;
               rgb[1] = 0;
               rgb[2] = 0;
            }
            else {
               shade = (dec->image->cache[offset] & 0x80) ? 0.0 : 0.7;
               rgb[0] += (shade * (255 - rgb[0]));
               rgb[1] += (shade * (255 - rgb[1]));
               rgb[2] += (shade * (255 - rgb[2]));
            }
         }
         fwrite(rgb, sizeof(char), 3, fp);
      }
   }

   fclose(fp);
}

/**
 * @brief  List supported input image formats on stdout
 * @return void
 */
static void
ListImageFormats(void)
{
   int i;
   MagickInfo **formats;
   GmExceptionInfo exception;

   gmGetExceptionInfo(&exception);
   formats = gmGetMagickInfoArray(&exception);
   gmCatchException(&exception);

   if(formats) {
      fprintf(stdout, "   Format  Description\n");
      fprintf(stdout, "---------  ------------------------------------------------------\n");
      for(i=0; formats[i] != 0; i++) {
         if(formats[i]->stealth || !formats[i]->decoder)
            continue;

         fprintf(stdout, "%9s", formats[i]->name ? formats[i]->name : "");
         if(formats[i]->description != (char *)NULL)
            fprintf(stdout, "  %s\n",formats[i]->description);
      }
      free(formats);
   }

   gmDestroyExceptionInfo(&exception);
}

/**
 *
 *
 */
static int
ScaleNumberString(char *s, int extent)
{
   int err;
   int numValue;
   int scaledValue;
   char *terminate;

   assert(s != NULL);

   err = StringToInt(&numValue, s, &terminate);
   if(err != DmtxPass)
      FatalError(EX_USAGE, _("Integer value required"));

   scaledValue = (*terminate == '%') ? (int)(0.01 * numValue * extent + 0.5) : numValue;

   if(scaledValue < 0)
      scaledValue = 0;

   if(scaledValue >= extent)
      scaledValue = extent - 1;

   return scaledValue;
}
