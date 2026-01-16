// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avifutil.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "avifjpeg.h"
#include "avifpng.h"
#include "y4m.h"

char * avifFileFormatToString(avifAppFileFormat format)
{
    switch (format) {
        case AVIF_APP_FILE_FORMAT_UNKNOWN:
            return "unknown";
        case AVIF_APP_FILE_FORMAT_AVIF:
            return "AVIF";
        case AVIF_APP_FILE_FORMAT_JPEG:
            return "JPEG";
        case AVIF_APP_FILE_FORMAT_PNG:
            return "PNG";
        case AVIF_APP_FILE_FORMAT_Y4M:
            return "Y4M";
    }
    return "unknown";
}

// |a| and |b| hold int32_t values. The int64_t type is used so that we can negate INT32_MIN without
// overflowing int32_t.
static int64_t calcGCD(int64_t a, int64_t b)
{
    if (a < 0) {
        a *= -1;
    }
    if (b < 0) {
        b *= -1;
    }
    while (b != 0) {
        int64_t r = a % b;
        a = b;
        b = r;
    }
    return a;
}

static void printClapFraction(FILE * f, const char * name, int32_t n, int32_t d)
{
    fprintf(f, "%s: %d/%d", name, n, d);
    if (d != 0) {
        int64_t gcd = calcGCD(n, d);
        if (gcd > 1) {
            int32_t rn = (int32_t)(n / gcd);
            int32_t rd = (int32_t)(d / gcd);
            fprintf(f, " (%d/%d)", rn, rd);
        }
    }
}

static void avifImageDumpInternal(const avifImage * avif, uint32_t gridCols, uint32_t gridRows, avifBool alphaPresent, avifProgressiveState progressiveState, FILE * f)
{
    uint32_t width = avif->width;
    uint32_t height = avif->height;
    if (gridCols && gridRows) {
        width *= gridCols;
        height *= gridRows;
    }
    fprintf(f, " * Resolution     : %ux%u\n", width, height);
    fprintf(f, " * Bit Depth      : %u\n", avif->depth);
    fprintf(f, " * Format         : %s\n", avifPixelFormatToString(avif->yuvFormat));
    if (avif->yuvFormat == AVIF_PIXEL_FORMAT_YUV420) {
        fprintf(f, " * Chroma Sam. Pos: %u\n", avif->yuvChromaSamplePosition);
    }
    fprintf(f, " * Alpha          : %s\n", alphaPresent ? (avif->alphaPremultiplied ? "Premultiplied" : "Not premultiplied") : "Absent");
    fprintf(f, " * Range          : %s\n", (avif->yuvRange == AVIF_RANGE_FULL) ? "Full" : "Limited");

    fprintf(f, " * Color Primaries: %u\n", avif->colorPrimaries);
    fprintf(f, " * Transfer Char. : %u\n", avif->transferCharacteristics);
    fprintf(f, " * Matrix Coeffs. : %u\n", avif->matrixCoefficients);

    if (avif->icc.size != 0) {
        fprintf(f, " * ICC Profile    : Present (%" AVIF_FMT_ZU " bytes)\n", avif->icc.size);
    } else {
        fprintf(f, " * ICC Profile    : Absent\n");
    }
    if (avif->xmp.size != 0) {
        fprintf(f, " * XMP Metadata   : Present (%" AVIF_FMT_ZU " bytes)\n", avif->xmp.size);
    } else {
        fprintf(f, " * XMP Metadata   : Absent\n");
    }
    if (avif->exif.size != 0) {
        fprintf(f, " * Exif Metadata  : Present (%" AVIF_FMT_ZU " bytes)\n", avif->exif.size);
    } else {
        fprintf(f, " * Exif Metadata  : Absent\n");
    }

    if (avif->transformFlags == AVIF_TRANSFORM_NONE) {
        printf(" * Transformations: None\n");
    } else {
        printf(" * Transformations:\n");

        if (avif->transformFlags & AVIF_TRANSFORM_PASP) {
            fprintf(f, "    * pasp (Aspect Ratio)  : %d/%d\n", (int)avif->pasp.hSpacing, (int)avif->pasp.vSpacing);
        }
        if (avif->transformFlags & AVIF_TRANSFORM_CLAP) {
                fprintf(f, "    * clap (Clean Aperture): ");
                printClapFraction(f, "W", (int32_t)avif->clap.widthN, (int32_t)avif->clap.widthD);
                fprintf(f, ", ");
                printClapFraction(f, "H", (int32_t)avif->clap.heightN, (int32_t)avif->clap.heightD);
                fprintf(f, ", ");
                printClapFraction(f, "hOff", (int32_t)avif->clap.horizOffN, (int32_t)avif->clap.horizOffD);
                fprintf(f, ", ");
                printClapFraction(f, "vOff", (int32_t)avif->clap.vertOffN, (int32_t)avif->clap.vertOffD);
                fprintf(f, "\n");

            avifCropRect cropRect;
            avifDiagnostics diag;
            avifDiagnosticsClearError(&diag);
            avifBool validClap = avifCropRectFromCleanApertureBox(&cropRect, &avif->clap, avif->width, avif->height, &diag);
            if (validClap) {
                fprintf(f, "      * Valid, derived crop rect: X: %d, Y: %d, W: %d, H: %d%s\n",
                       cropRect.x,
                       cropRect.y,
                       cropRect.width,
                       cropRect.height,
                       avifCropRectRequiresUpsampling(&cropRect, avif->yuvFormat) ? " (upsample before cropping)" : "");
            } else {
                fprintf(f, "      * Invalid: %s\n", diag.error);
            }
        }
        if (avif->transformFlags & AVIF_TRANSFORM_IROT) {
            fprintf(f, "    * irot (Rotation)      : %u\n", avif->irot.angle);
        }
        if (avif->transformFlags & AVIF_TRANSFORM_IMIR) {
            fprintf(f, "    * imir (Mirror)        : %u (%s)\n", avif->imir.axis, (avif->imir.axis == 0) ? "top-to-bottom" : "left-to-right");
        }
    }
    fprintf(f, " * Progressive    : %s\n", avifProgressiveStateToString(progressiveState));
    if (avif->clli.maxCLL > 0 || avif->clli.maxPALL > 0) {
        fprintf(f, " * CLLI           : %hu, %hu\n", avif->clli.maxCLL, avif->clli.maxPALL);
    }

    fprintf(f, " * Gain map       : ");
    avifImage * gainMapImage = avif->gainMap ? avif->gainMap->image : NULL;
    if (gainMapImage != NULL) {
        fprintf(f, "%ux%u pixels, %u bit, %s, %s Range, Matrix Coeffs. %u, Base Headroom %.2f (%s), Alternate Headroom %.2f (%s)\n",
               gainMapImage->width,
               gainMapImage->height,
               gainMapImage->depth,
               avifPixelFormatToString(gainMapImage->yuvFormat),
               (gainMapImage->yuvRange == AVIF_RANGE_FULL) ? "Full" : "Limited",
               gainMapImage->matrixCoefficients,
               avif->gainMap->baseHdrHeadroom.d == 0 ? 0
                                                     : (double)avif->gainMap->baseHdrHeadroom.n / avif->gainMap->baseHdrHeadroom.d,
               (avif->gainMap->baseHdrHeadroom.n == 0) ? "SDR" : "HDR",
               avif->gainMap->alternateHdrHeadroom.d == 0
                   ? 0
                   : (double)avif->gainMap->alternateHdrHeadroom.n / avif->gainMap->alternateHdrHeadroom.d,
               (avif->gainMap->alternateHdrHeadroom.n == 0) ? "SDR" : "HDR");
        fprintf(f, " * Alternate image:\n");
        fprintf(f, "    * Color Primaries: %u\n", avif->gainMap->altColorPrimaries);
        fprintf(f, "    * Transfer Char. : %u\n", avif->gainMap->altTransferCharacteristics);
        fprintf(f, "    * Matrix Coeffs. : %u\n", avif->gainMap->altMatrixCoefficients);
        if (avif->gainMap->altICC.size != 0) {
            fprintf(f, "    * ICC Profile    : Present (%" AVIF_FMT_ZU " bytes)\n", avif->gainMap->altICC.size);
        } else {
            fprintf(f, "    * ICC Profile    : Absent\n");
        }
        if (avif->gainMap->altDepth) {
            fprintf(f, "    * Bit Depth      : %u\n", avif->gainMap->altDepth);
        }
        if (avif->gainMap->altPlaneCount) {
            fprintf(f, "    * Planes         : %u\n", avif->gainMap->altPlaneCount);
        }
        if (avif->gainMap->altCLLI.maxCLL > 0 || avif->gainMap->altCLLI.maxPALL > 0) {
            fprintf(f, "    * CLLI           : %hu, %hu\n", avif->gainMap->altCLLI.maxCLL, avif->gainMap->altCLLI.maxPALL);
        }
        fprintf(f, "\n");
    } else if (avif->gainMap != NULL) {
        fprintf(f, "Present (but ignored)\n");
    } else {
        fprintf(f, "Absent\n");
    }
}

void avifImageDump(const avifImage * avif, uint32_t gridCols, uint32_t gridRows, avifProgressiveState progressiveState, FILE * f)
{
    const avifBool alphaPresent = avif->alphaPlane && (avif->alphaRowBytes > 0);
    avifImageDumpInternal(avif, gridCols, gridRows, alphaPresent, progressiveState, f);
}

void avifContainerDump(const avifDecoder * decoder, FILE * f)
{
    avifImageDumpInternal(decoder->image, 0, 0, decoder->alphaPresent, decoder->progressiveState, f);
    if (decoder->imageSequenceTrackPresent) {
        if (decoder->repetitionCount == AVIF_REPETITION_COUNT_INFINITE) {
            fprintf(f, " * Repeat Count   : Infinite\n");
        } else if (decoder->repetitionCount == AVIF_REPETITION_COUNT_UNKNOWN) {
            fprintf(f, " * Repeat Count   : Unknown\n");
        } else {
            fprintf(f, " * Repeat Count   : %d\n", decoder->repetitionCount);
        }
    }
}

void avifPrintVersions()
{
    char codecVersions[256];
    avifCodecVersions(codecVersions);
    printf("Version: %s (%s)\n", avifVersion(), codecVersions);

    unsigned int libyuvVersion = avifLibYUVVersion();
    if (libyuvVersion == 0) {
        printf("libyuv : unavailable\n");
    } else {
        printf("libyuv : available (%u)\n", libyuvVersion);
    }

    printf("\n");
}

avifAppFileFormat avifGuessFileFormat(const char * filename)
{
    // Guess from the file header
    FILE * f = fopen(filename, "rb");
    if (f) {
        uint8_t headerBuffer[144];
        size_t bytesRead = fread(headerBuffer, 1, sizeof(headerBuffer), f);
        fclose(f);

        if (bytesRead > 0) {
            // If the file could be read, use the first bytes to guess the file format.
            return avifGuessBufferFileFormat(headerBuffer, bytesRead);
        }
    }

    // If we get here, the file header couldn't be read for some reason. Guess from the extension.

    const char * fileExt = strrchr(filename, '.');
    if (!fileExt) {
        return AVIF_APP_FILE_FORMAT_UNKNOWN;
    }
    ++fileExt; // skip past the dot

    char lowercaseExt[8]; // This only needs to fit up to "jpeg", so this is plenty
    const size_t fileExtLen = strlen(fileExt);
    if (fileExtLen >= sizeof(lowercaseExt)) { // >= accounts for NULL terminator
        return AVIF_APP_FILE_FORMAT_UNKNOWN;
    }

    for (size_t i = 0; i < fileExtLen; ++i) {
        lowercaseExt[i] = (char)tolower((unsigned char)fileExt[i]);
    }
    lowercaseExt[fileExtLen] = 0;

    if (!strcmp(lowercaseExt, "avif")) {
        return AVIF_APP_FILE_FORMAT_AVIF;
    } else if (!strcmp(lowercaseExt, "y4m")) {
        return AVIF_APP_FILE_FORMAT_Y4M;
    } else if (!strcmp(lowercaseExt, "jpg") || !strcmp(lowercaseExt, "jpeg")) {
        return AVIF_APP_FILE_FORMAT_JPEG;
    } else if (!strcmp(lowercaseExt, "png")) {
        return AVIF_APP_FILE_FORMAT_PNG;
    }
    return AVIF_APP_FILE_FORMAT_UNKNOWN;
}

avifAppFileFormat avifGuessBufferFileFormat(const uint8_t * data, size_t size)
{
    if (size == 0) {
        return AVIF_APP_FILE_FORMAT_UNKNOWN;
    }

    avifROData header;
    header.data = data;
    header.size = size;

    if (avifPeekCompatibleFileType(&header)) {
        return AVIF_APP_FILE_FORMAT_AVIF;
    }

    static const uint8_t signatureJPEG[2] = { 0xFF, 0xD8 };
    static const uint8_t signaturePNG[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
    static const uint8_t signatureY4M[9] = { 0x59, 0x55, 0x56, 0x34, 0x4D, 0x50, 0x45, 0x47, 0x32 }; // "YUV4MPEG2"
    struct avifHeaderSignature
    {
        avifAppFileFormat format;
        const uint8_t * magic;
        size_t magicSize;
    } signatures[] = { { AVIF_APP_FILE_FORMAT_JPEG, signatureJPEG, sizeof(signatureJPEG) },
                       { AVIF_APP_FILE_FORMAT_PNG, signaturePNG, sizeof(signaturePNG) },
                       { AVIF_APP_FILE_FORMAT_Y4M, signatureY4M, sizeof(signatureY4M) } };
    const size_t signaturesCount = sizeof(signatures) / sizeof(signatures[0]);

    for (size_t signatureIndex = 0; signatureIndex < signaturesCount; ++signatureIndex) {
        const struct avifHeaderSignature * const signature = &signatures[signatureIndex];
        if (header.size < signature->magicSize) {
            continue;
        }
        if (!memcmp(header.data, signature->magic, signature->magicSize)) {
            return signature->format;
        }
    }

    return AVIF_APP_FILE_FORMAT_UNKNOWN;
}

avifAppFileFormat avifReadImage(const char * filename,
                                avifAppFileFormat inputFormat,
                                avifPixelFormat requestedFormat,
                                int requestedDepth,
                                avifChromaDownsampling chromaDownsampling,
                                avifBool ignoreColorProfile,
                                avifBool ignoreExif,
                                avifBool ignoreXMP,
                                avifBool ignoreGainMap,
                                uint32_t imageSizeLimit,
                                avifImage * image,
                                uint32_t * outDepth,
                                avifAppSourceTiming * sourceTiming,
                                struct y4mFrameIterator ** frameIter)
{
    if (inputFormat == AVIF_APP_FILE_FORMAT_UNKNOWN) {
        inputFormat = avifGuessFileFormat(filename);
    }

    if (inputFormat == AVIF_APP_FILE_FORMAT_Y4M) {
        if (!y4mRead(filename, imageSizeLimit, image, sourceTiming, frameIter)) {
            return AVIF_APP_FILE_FORMAT_UNKNOWN;
        }
        if (outDepth) {
            *outDepth = image->depth;
        }
    } else if (inputFormat == AVIF_APP_FILE_FORMAT_JPEG) {
        // imageSizeLimit is also used to limit Exif and XMP metadata here.
        if (!avifJPEGRead(filename, image, requestedFormat, requestedDepth, chromaDownsampling, ignoreColorProfile, ignoreExif, ignoreXMP, ignoreGainMap, imageSizeLimit)) {
            return AVIF_APP_FILE_FORMAT_UNKNOWN;
        }
        if (outDepth) {
            *outDepth = 8;
        }
    } else if (inputFormat == AVIF_APP_FILE_FORMAT_PNG) {
        if (!avifPNGRead(filename, image, requestedFormat, requestedDepth, chromaDownsampling, ignoreColorProfile, ignoreExif, ignoreXMP, imageSizeLimit, outDepth)) {
            return AVIF_APP_FILE_FORMAT_UNKNOWN;
        }
    } else if (inputFormat == AVIF_APP_FILE_FORMAT_UNKNOWN) {
        fprintf(stderr, "Unrecognized file format for input file: %s\n", filename);
        return AVIF_APP_FILE_FORMAT_UNKNOWN;
    } else {
        fprintf(stderr, "Unsupported file format %s for input file: %s\n", avifFileFormatToString(inputFormat), filename);
        return AVIF_APP_FILE_FORMAT_UNKNOWN;
    }
    return inputFormat;
}

avifBool avifReadEntireFile(const char * filename, avifRWData * raw)
{
    FILE * f = fopen(filename, "rb");
    if (!f) {
        return AVIF_FALSE;
    }

    fseek(f, 0, SEEK_END);
    long pos = ftell(f);
    if (pos <= 0) {
        fclose(f);
        return AVIF_FALSE;
    }
    size_t fileSize = (size_t)pos;
    fseek(f, 0, SEEK_SET);

    if (avifRWDataRealloc(raw, fileSize) != AVIF_RESULT_OK) {
        fclose(f);
        return AVIF_FALSE;
    }
    size_t bytesRead = fread(raw->data, 1, fileSize, f);
    fclose(f);

    if (bytesRead != fileSize) {
        avifRWDataFree(raw);
        return AVIF_FALSE;
    }
    return AVIF_TRUE;
}

void avifImageFixXMP(avifImage * image)
{
    // Zero bytes are forbidden in UTF-8 XML: https://en.wikipedia.org/wiki/Valid_characters_in_XML
    // Keeping zero bytes in XMP may lead to issues at encoding or decoding.
    // For example, the PNG specification forbids null characters in XMP. See avifPNGWrite().
    // The XMP Specification Part 3 says "When XMP is encoded as UTF-8,
    // there are no zero bytes in the XMP packet" for GIF.

    // Consider a single trailing null character following a non-null character
    // as a programming error. Leave other null characters as is.
    // See the discussion at https://github.com/AOMediaCodec/libavif/issues/1333.
    if (image->xmp.size >= 2 && image->xmp.data[image->xmp.size - 1] == '\0' && image->xmp.data[image->xmp.size - 2] != '\0') {
        --image->xmp.size;
    }
}

void avifDumpDiagnostics(const avifDiagnostics * diag, FILE * f)
{
    if (!*diag->error) {
        return;
    }

    fprintf(f, "Diagnostics:\n");
    fprintf(f, " * %s\n", diag->error);
}

// ---------------------------------------------------------------------------
// avifQueryCPUCount (separated into OS implementations)

#if defined(_WIN32)

// Windows

#include <windows.h>

int avifQueryCPUCount(void)
{
    int numCPU;
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    numCPU = sysinfo.dwNumberOfProcessors;
    return numCPU;
}

#elif defined(__APPLE__)

// Apple

#include <sys/sysctl.h>

int avifQueryCPUCount(void)
{
    int mib[4];
    int numCPU;
    size_t len = sizeof(numCPU);

    /* set the mib for hw.ncpu */
    mib[0] = CTL_HW;
    mib[1] = HW_AVAILCPU; // alternatively, try HW_NCPU;

    /* get the number of CPUs from the system */
    sysctl(mib, 2, &numCPU, &len, NULL, 0);

    if (numCPU < 1) {
        mib[1] = HW_NCPU;
        sysctl(mib, 2, &numCPU, &len, NULL, 0);
        if (numCPU < 1)
            numCPU = 1;
    }
    return numCPU;
}

#elif defined(__EMSCRIPTEN__)

// Emscripten

int avifQueryCPUCount(void)
{
    return 1;
}

#else

// POSIX

#include <unistd.h>

int avifQueryCPUCount(void)
{
    int numCPU = (int)sysconf(_SC_NPROCESSORS_ONLN);
    return (numCPU > 0) ? numCPU : 1;
}

#endif
