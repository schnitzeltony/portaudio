/** @file paex_accel_performance.c
	@ingroup examples_src
	@brief Run preformance tests to check SIMD acceleration.
	@author Andreas Müller <schnitzeltony@googlemail.com>
*/
/*
 * $Id$
 *
 * This program uses the PortAudio Portable Audio Library.
 * For more information see: http://www.portaudio.com/
 * Copyright (c) 1999-2000 Ross Bencina and Phil Burk
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * The text above constitutes the entire PortAudio license; however,
 * the PortAudio community also makes the following non-binding requests:
 *
 * Any person wishing to distribute modifications to the Software is
 * requested to send the modifications to the original developer so that
 * they can be incorporated into the canonical version. It is also
 * requested that these non-binding requests be included along with the
 * license above.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "pa_converters.h"
#include "pa_dither.h"
#include "pa_endianness.h"
#include "pa_types.h"

const int iRetryPerCase = 1000;
#define MAX_BUFFLEN 1024

extern PaUtilConverterTable paConverters;
extern volatile int withAcceleration;

typedef enum
{
    int8,
    uint8,
    int16,
    int24,
    int32,
    float32
} PaDataTypes;

typedef struct {
    PaUtilConverter *pConverter;
    PaDataTypes inDataType;
    PaDataTypes outDataType;
    int Dither, Clipping;
    char name[256];
} PaUtilConverterTablePerf;

/***********************************************************************/
/* some helpers */
#define ADD_TAB_ENTRY(_num, _name, _inDataType, _outDataType, _Dither, _Clipping) \
    table[_num].pConverter = paConverters._name; \
    table[_num].inDataType = _inDataType; \
    table[_num].outDataType = _outDataType; \
    table[_num].Dither = _Dither; \
    table[_num].Clipping = _Clipping; \
    strcpy(table[_num].name, #_name);


static PaInt32 _Int24_ToIn32(unsigned char* pBuf)
{
    PaInt32 iVal24;
    #if defined(PA_LITTLE_ENDIAN)
    iVal24 =
        ((PaInt32)pBuf[0]) |
        ((PaInt32)pBuf[1] << 8) |
        ((PaInt32)pBuf[2] << 16);
    #elif defined(PA_BIG_ENDIAN)
    iVal24 =
        ((PaInt32)pBuf[0]) |
        ((PaInt32)pBuf[1] << 8) |
        ((PaInt32)pBuf[2] << 16);
    #endif
    return iVal24;
}

#define CHECK_BUFFER_ACCEL(_DATATYPE, _FORMAT) \
    _DATATYPE *pBuffNoAccelNoStride   = (_DATATYPE *)destBuffer; \
    _DATATYPE *pBuffNoAccelStride = (_DATATYPE *)destBufferStride; \
    _DATATYPE *pBuffAccelNoStride = (_DATATYPE *)destBufferAccel; \
    _DATATYPE *pBuffAccelStride = (_DATATYPE *)destBufferAccelStride; \
    for(int iDestElem=0; iDestElem<iBufferSize; iDestElem++) \
    { \
        if(pBuffNoAccelNoStride[iDestElem] != pBuffAccelNoStride[iDestElem]) \
        { \
            if(errorCount < MaxValueErrorMsg) \
                printf ("AccelError " #_DATATYPE " at element %i: " #_FORMAT "/0x%08X expected " #_FORMAT "/0x%08X\n", \
                    iDestElem, \
                    pBuffAccelNoStride[iDestElem], \
                    pBuffAccelNoStride[iDestElem], \
                    pBuffNoAccelNoStride[iDestElem], \
                    pBuffNoAccelNoStride[iDestElem]); \
            errorCount++; \
        } \
        if(pBuffNoAccelStride[iDestElem*iStride] != pBuffAccelStride[iDestElem*iStride]) \
        { \
            if(errorCount < MaxValueErrorMsg) \
                printf ("AccelError " #_DATATYPE " stride %i at element %i: " #_FORMAT "/0x%08X expected " #_FORMAT "/0x%08X\n", \
                    iStride, \
                    iDestElem, \
                    pBuffAccelStride[iDestElem*iStride], \
                    pBuffAccelStride[iDestElem*iStride], \
                    pBuffNoAccelStride[iDestElem*iStride], \
                    pBuffNoAccelStride[iDestElem*iStride]); \
            errorCount++; \
        } \
    }

/*******************************************************************/
int main(void);
int main(void)
{
    /* create dither instance */
    PaUtilTriangularDitherGenerator dither;

    /* test proper working float dither generator */
    withAcceleration = 0;
    PaUtil_InitializeTriangularDitherState(&dither);
    float dithersOrig[MAX_BUFFLEN];
    int iDither;
    for(iDither=0; iDither<MAX_BUFFLEN; iDither++)
        dithersOrig[iDither] = PaUtil_GenerateFloatTriangularDither(&dither);

#ifdef __ARM_NEON__
    withAcceleration = 1;
    PaUtil_InitializeTriangularDitherState(&dither);
    float dithersAccel[MAX_BUFFLEN];
    float32x4_t neonDither;
    for(iDither=0; iDither<MAX_BUFFLEN/ARM_NEON_BEST_VECTOR_SIZE; iDither++)
    {
        neonDither = PaUtil_GenerateFloatTriangularDitherVector(&dither);
        vst1q_f32(dithersAccel+(iDither*ARM_NEON_BEST_VECTOR_SIZE), neonDither);
    }
    /* check dither for equality */
    int errorCountDither = 0;
    for(iDither=0; iDither<MAX_BUFFLEN; iDither++)
    {
        if(fabs(dithersOrig[iDither] - dithersAccel[iDither]) > 1e-15)
        {
            if(errorCountDither < 16)
            {
                printf("Accel dither test error at %i: %.15f expected %.15f\n",
                    iDither,
                    dithersAccel[iDither],
                    dithersOrig[iDither]);
            }
            errorCountDither++;
        }
    }
#endif

    /* test all converters for performance and correct data */
    int iCountConverters = 58;  /* set max entry+1 below */

    /* Create our converter table */
    PaUtilConverterTablePerf table[iCountConverters];
    /* Zero mem -> We can simply reduce number of test by commenting out
     * ADD_TAB_ENTRY below
     */
    memset(table, 0, sizeof(table));

    ADD_TAB_ENTRY(0,  Float32_To_Int32, float32, int32, 0, 0);
    ADD_TAB_ENTRY(1,  Float32_To_Int32_Dither, float32, int32, 1, 0);
    ADD_TAB_ENTRY(2,  Float32_To_Int32_Clip, float32, int32, 0, 1);
    ADD_TAB_ENTRY(3,  Float32_To_Int32_DitherClip, float32, int32, 1, 1);

    ADD_TAB_ENTRY(4,  Float32_To_Int24, float32, int24, 0, 0);
    ADD_TAB_ENTRY(5,  Float32_To_Int24_Dither, float32, int24, 1, 0);
    ADD_TAB_ENTRY(6,  Float32_To_Int24_Clip, float32, int24, 0, 1);
    ADD_TAB_ENTRY(7,  Float32_To_Int24_DitherClip, float32, int24, 1, 1);

    ADD_TAB_ENTRY(8,  Float32_To_Int16, float32, int16, 0, 0);
    /*ADD_TAB_ENTRY(9,  Float32_To_Int16_Dither, float32, int16, 1, 0);*/
    ADD_TAB_ENTRY(10, Float32_To_Int16_Clip, float32, int16, 0, 1);
    /*ADD_TAB_ENTRY(11, Float32_To_Int16_DitherClip, float32, int16, 1, 1);

    ADD_TAB_ENTRY(12, Float32_To_Int8, float32, int8, 0, 0);
    ADD_TAB_ENTRY(13, Float32_To_Int8_Dither, float32, int8, 1, 0);
    ADD_TAB_ENTRY(14, Float32_To_Int8_Clip, float32, int8, 0, 1);
    ADD_TAB_ENTRY(15, Float32_To_Int8_DitherClip, float32, int8, 1, 1);

    ADD_TAB_ENTRY(16, Float32_To_UInt8, float32, uint8, 0, 0);
    ADD_TAB_ENTRY(17, Float32_To_UInt8_Dither, float32, uint8, 1, 0);
    ADD_TAB_ENTRY(18, Float32_To_UInt8_Clip, float32, uint8, 0, 1);
    ADD_TAB_ENTRY(19, Float32_To_UInt8_DitherClip, float32, uint8, 1, 1);

    ADD_TAB_ENTRY(20, Int32_To_Float32, int32, float32, 0, 0);
    ADD_TAB_ENTRY(21, Int32_To_Int24, int32, int24, 0, 0);
    ADD_TAB_ENTRY(22, Int32_To_Int24_Dither, int32, int24, 1, 0);
    ADD_TAB_ENTRY(23, Int32_To_Int16, int32, int16, 1, 0);
    ADD_TAB_ENTRY(24, Int32_To_Int16_Dither, int32, int16, 1, 0);
    ADD_TAB_ENTRY(25, Int32_To_Int8, int32, int8, 0, 0);
    ADD_TAB_ENTRY(26, Int32_To_Int8_Dither, int32, int8, 1, 0);
    ADD_TAB_ENTRY(27, Int32_To_UInt8, int32, uint8, 0, 0);
    ADD_TAB_ENTRY(28, Int32_To_UInt8_Dither, int32, uint8, 1, 0);

    ADD_TAB_ENTRY(29, Int24_To_Float32, int24, float32, 0, 0);
    ADD_TAB_ENTRY(30, Int24_To_Int32, int24, int32, 0, 0);
    ADD_TAB_ENTRY(31, Int24_To_Int16, int24, int16, 0, 0);
    ADD_TAB_ENTRY(32, Int24_To_Int16_Dither, int24, int16, 1, 0);
    ADD_TAB_ENTRY(33, Int24_To_Int8, int24, int8, 0, 0);
    ADD_TAB_ENTRY(34, Int24_To_Int8_Dither, int24, int8, 1, 0);
    ADD_TAB_ENTRY(35, Int24_To_UInt8, int24, uint8, 0, 0);
    ADD_TAB_ENTRY(36, Int24_To_UInt8_Dither, int24, uint8, 1, 0);

    ADD_TAB_ENTRY(37, Int16_To_Float32, int16, float32, 0, 0);
    ADD_TAB_ENTRY(38, Int16_To_Int32, int16, int32, 0, 0);
    ADD_TAB_ENTRY(39, Int16_To_Int24, int16, int24, 0, 0);
    ADD_TAB_ENTRY(40, Int16_To_Int8, int16, int8, 0, 0);
    ADD_TAB_ENTRY(41, Int16_To_Int8_Dither, int16, int8, 1, 0);
    ADD_TAB_ENTRY(42, Int16_To_UInt8, int16, uint8, 0, 0);
    ADD_TAB_ENTRY(43, Int16_To_UInt8_Dither, int16, int8, 1, 0);

    ADD_TAB_ENTRY(44, Int8_To_Float32, int8, float32, 0, 0);
    ADD_TAB_ENTRY(45, Int8_To_Int32, int8, int32, 0, 0);
    ADD_TAB_ENTRY(46, Int8_To_Int24, int8, int24, 0, 0);
    ADD_TAB_ENTRY(47, Int8_To_Int16, int8, int16, 0, 0);
    ADD_TAB_ENTRY(48, Int8_To_UInt8, int8, uint8, 0, 0);

    ADD_TAB_ENTRY(49, UInt8_To_Float32, uint8, float32, 0, 0);
    ADD_TAB_ENTRY(50, UInt8_To_Int32, uint8, int32, 0, 0);
    ADD_TAB_ENTRY(51, UInt8_To_Int24, uint8, int24, 0, 0);
    ADD_TAB_ENTRY(52, UInt8_To_Int16, uint8, int16, 0, 0);
    ADD_TAB_ENTRY(53, UInt8_To_Int8, uint8, int8, 0, 0);

    ADD_TAB_ENTRY(54, Copy_8_To_8, int8, int8, 0, 0);
    ADD_TAB_ENTRY(55, Copy_16_To_16, int16, int16, 0, 0);
    ADD_TAB_ENTRY(56, Copy_24_To_24, int24, int24, 0, 0);
    ADD_TAB_ENTRY(57, Copy_32_To_32, int32, int32, 0, 0); */

    /* define test tupels */
    int buffer_sizes[] = {64, 256, MAX_BUFFLEN};
    #define MAX_STRIDE 4
    int strides[] = {1, 2, MAX_STRIDE};
    int countStrides = sizeof(strides)/sizeof(int);

    /* create our buffers */
    float sourceBuffer[MAX_BUFFLEN];
    float sourceBufferStride[MAX_BUFFLEN * MAX_STRIDE];
    float destBuffer[MAX_BUFFLEN];
    float destBufferStride[MAX_BUFFLEN * MAX_STRIDE];
    float destBufferAccel[MAX_BUFFLEN];
    float destBufferAccelStride[MAX_BUFFLEN * MAX_STRIDE];

    float timeNoAccelStrideSource[countStrides];
    float timeAccelStrideSource[countStrides];
    float timeNoAccelStrideDest[countStrides];
    float timeAccelStrideDest[countStrides];

    int iRepetition;

    for(int iConverter=0; iConverter<iCountConverters; iConverter++)
    {
        if(table[iConverter].pConverter == NULL)
            continue;

        for(int iBuffersizeNum=0; iBuffersizeNum<sizeof(buffer_sizes)/sizeof(int); iBuffersizeNum++)
        {
            int iBufferSize = buffer_sizes[iBuffersizeNum];
            for(int iStrideNum=0; iStrideNum<countStrides; iStrideNum++)
            {
                int iStride = strides[iStrideNum];
                /* prepare input test data depending on input data type */
                int iEntry;
                switch(table[iConverter].inDataType)
                {
                    case int8:
                    {
                        unsigned char i8Value, *pBuff;
                        for(iEntry=0; iEntry<iBufferSize; iEntry++)
                        {
                            i8Value = (iEntry % 256) - 128;
                            /* no stride */
                            pBuff = (signed char *)sourceBuffer;
                            pBuff[iEntry] = i8Value;
                            /* stride */
                            pBuff = (signed char *)sourceBufferStride;
                            pBuff[iEntry*iStride] = i8Value;
                        }
                        break;
                    }
                    case uint8:
                    {
                        unsigned char ui8Value, *pBuff;
                        for(iEntry=0; iEntry<iBufferSize; iEntry++)
                        {
                            ui8Value = (iEntry % 256);
                            /* no stride */
                            pBuff = (unsigned char *)sourceBuffer;
                            pBuff[iEntry] = ui8Value;
                            /* stride */
                            pBuff = (unsigned char *)sourceBufferStride;
                            pBuff[iEntry*iStride] = ui8Value;
                        }
                        break;
                    }
                    case int16:
                    {
                        PaInt16 i16Value, *pBuff;
                        for(iEntry=0; iEntry<iBufferSize; iEntry++)
                        {
                            i16Value = ((iEntry % 256) - 128) * 256;
                            /* no stride */
                            pBuff = (PaInt16 *)sourceBuffer;
                            pBuff[iEntry] = i16Value;
                            /* stride */
                            pBuff = (PaInt16 *)sourceBufferStride;
                            pBuff[iEntry*iStride] = i16Value;
                        }
                        break;
                    }
                    case int24:
                    {
                        unsigned char *pBuff = (unsigned char*)sourceBuffer;
                        PaInt32 value;
                        for(iEntry=0; iEntry<iBufferSize; iEntry++)
                        {
                            value = ((iEntry % 256) - 128) * 256 * 256;
                            /* no stride */
                            pBuff = (unsigned char*)sourceBuffer + 3*iEntry;
                            #if defined(PA_LITTLE_ENDIAN)
                                    pBuff[0] = (unsigned char)(value >> 0);
                                    pBuff[1] = (unsigned char)(value >> 8);
                                    pBuff[2] = (unsigned char)(value >> 16);
                            #elif defined(PA_BIG_ENDIAN)
                                    pBuff[0] = (unsigned char)(value >> 16);
                                    pBuff[1] = (unsigned char)(value >> 8);
                                    pBuff[2] = (unsigned char)(value >> 0);
                            #endif
                            /* stride */
                            pBuff = (unsigned char*)sourceBufferStride + 3*iEntry*iStride;
                            #if defined(PA_LITTLE_ENDIAN)
                                    pBuff[0] = (unsigned char)(value >> 0);
                                    pBuff[1] = (unsigned char)(value >> 8);
                                    pBuff[2] = (unsigned char)(value >> 16);
                            #elif defined(PA_BIG_ENDIAN)
                                    pBuff[0] = (unsigned char)(value >> 16);
                                    pBuff[1] = (unsigned char)(value >> 8);
                                    pBuff[2] = (unsigned char)(value >> 0);
                            #endif
                        }
                        break;
                    }
                    case int32:
                    {
                        /* no stride */
                        PaInt32 i32Value, *pBuff;
                        for(iEntry=0; iEntry<iBufferSize; iEntry++)
                        {
                            i32Value = ((iEntry % 256) - 128) * 256 * 256 * 256;
                            /* no stride */
                            pBuff = (PaInt32 *)sourceBuffer;
                            pBuff[iEntry] = i32Value;
                            /* stride */
                            pBuff = (PaInt32 *)sourceBufferStride;
                            pBuff[iEntry*iStride] = i32Value;
                        }
                        break;
                    }
                    case float32:
                    {
                        float fValue, *pBuff;
                        for(iEntry=0; iEntry<iBufferSize; iEntry++)
                        {
                            if(table[iConverter].Clipping)
                                /* Divider is 120 instead of 128 to test clipping */
                                fValue = ((float)((iEntry % 256) - 128)) / 120.0;
                            else
                                fValue = ((float)((iEntry % 256) - 128)) / 128.0;
                            /* no stride */
                            pBuff = (float *)sourceBuffer;
                            pBuff[iEntry] = fValue;
                            /* stride */
                            pBuff = (float *)sourceBufferStride;
                            pBuff[iEntry*iStride] = fValue;
                        }
                        break;
                    }
                }

                /* Run without acceleration / stride for source buffer */
                withAcceleration = 0;
                PaUtil_InitializeTriangularDitherState(&dither);
                clock_t tNoAccelSource = clock();
                for(iRepetition=0; iRepetition<iRetryPerCase; iRepetition++)
                {
                    table[iConverter].pConverter(
                        /* dest */
                        (void *)destBuffer, 1,
                        /* source */
                        (void *)sourceBufferStride, iStride,
                        /* other */
                        iBufferSize, &dither );
                }
                timeNoAccelStrideSource[iStrideNum] = ((float)(clock() - tNoAccelSource)) / CLOCKS_PER_SEC;

                /* Run without acceleration / stride for destination buffer */
                withAcceleration = 0;
                PaUtil_InitializeTriangularDitherState(&dither);
                clock_t tNoAccelDest = clock();
                for(iRepetition=0; iRepetition<iRetryPerCase; iRepetition++)
                {
                    table[iConverter].pConverter(
                        /* dest */
                        (void *)destBufferStride, iStride,
                        /* source */
                        (void *)sourceBuffer, 1,
                        /* other */
                        iBufferSize, &dither );
                }
                timeNoAccelStrideDest[iStrideNum] = ((float)(clock() - tNoAccelDest)) / CLOCKS_PER_SEC;

                /* Run with acceleration / stride for source buffer */
                withAcceleration = 1;
                PaUtil_InitializeTriangularDitherState(&dither);
                clock_t tAccelSource = clock();
                for(iRepetition=0; iRepetition<iRetryPerCase; iRepetition++)
                {
                    table[iConverter].pConverter(
                        /* dest */
                        (void *)destBufferAccel, 1,
                        /* source */
                        (void *)sourceBufferStride, iStride,
                        /* other */
                        iBufferSize, &dither );
                }
                timeAccelStrideSource[iStrideNum] = ((float)(clock() - tAccelSource)) / CLOCKS_PER_SEC;

                /* Run with acceleration / stride for destination buffer */
                withAcceleration = 1;
                PaUtil_InitializeTriangularDitherState(&dither);
                clock_t tAccelDest = clock();
                for(iRepetition=0; iRepetition<iRetryPerCase; iRepetition++)
                {
                    table[iConverter].pConverter(
                        /* dest */
                        (void *)destBufferAccelStride, iStride,
                        /* source */
                        (void *)sourceBuffer, 1,
                        /* other */
                        iBufferSize, &dither );
                }
                timeAccelStrideDest[iStrideNum] = ((float)(clock() - tAccelDest)) / CLOCKS_PER_SEC;

                printf ("%s Accel=0 / size %i / stride(S%i,D1) %8.6f sec stride(S1,D%i) %8.6f sec\n",
                    table[iConverter].name,
                    iBufferSize,
                    iStride,
                    timeNoAccelStrideSource[iStrideNum],
                    iStride,
                    timeNoAccelStrideDest[iStrideNum]);
                printf ("%s Accel=1 / size %i / stride(S%i,D1) %8.6f sec stride(S1,D%i) %8.6f sec\n",
                    table[iConverter].name,
                    iBufferSize,
                    iStride,
                    timeAccelStrideSource[iStrideNum],
                    iStride,
                    timeAccelStrideDest[iStrideNum]);
                printf ("%s Eval    / size %i / stride(S%i,D1) %8.2f %%   stride(S1,D%i) %8.2f %%\n",
                    table[iConverter].name,
                    iBufferSize,
                    iStride,
                    ((timeNoAccelStrideSource[iStrideNum] / timeAccelStrideSource[iStrideNum]) - 1.0) * 100.0,
                    iStride,
                    ((timeNoAccelStrideDest[iStrideNum] / timeAccelStrideDest[iStrideNum]) - 1.0) * 100.0);

                /* Check for valid contents in destination buffers */
                int errorCount = 0;
                const int MaxValueErrorMsg = 32;
                /* contents depend on destination data type */
                switch(table[iConverter].outDataType)
                {
                    case int8:
                    {
                        CHECK_BUFFER_ACCEL(signed char, %u)
                        break;
                    }
                    case uint8:
                    {
                        CHECK_BUFFER_ACCEL(unsigned char, %u)
                        break;
                    }
                    case int16:
                    {
                        CHECK_BUFFER_ACCEL(PaInt16, %i)
                        break;
                    }
                    case int24:
                    {
                        unsigned char *pBuffNoAccelNoStride = (unsigned char *)destBuffer;
                        unsigned char *pBuffNoAccelStride   = (unsigned char *)destBufferStride;
                        unsigned char *pBuffAccelNoStride   = (unsigned char *)destBufferAccel;
                        unsigned char *pBuffAccelStride     = (unsigned char *)destBufferAccelStride;
                        for(int iDestElem=0; iDestElem<iBufferSize; iDestElem++)
                        {
                            if( pBuffNoAccelNoStride[iDestElem*3 + 0] != pBuffAccelNoStride[iDestElem*3 + 0] ||
                                pBuffNoAccelNoStride[iDestElem*3 + 1] != pBuffAccelNoStride[iDestElem*3 + 1] ||
                                pBuffNoAccelNoStride[iDestElem*3 + 2] != pBuffAccelNoStride[iDestElem*3 + 2])
                            {
                                /* Yeah this seems being a bug but let's be a bit more tolerant... */
                                PaInt32 absNoStrideDiff =
                                    _Int24_ToIn32(pBuffNoAccelNoStride+iDestElem*3) -
                                    _Int24_ToIn32(pBuffAccelNoStride+iDestElem*3);
                                if(absNoStrideDiff < 0)
                                    absNoStrideDiff = -absNoStrideDiff;
                                if(absNoStrideDiff > 1)
                                {
                                    if(errorCount < MaxValueErrorMsg)
                                        printf ("AccelError int24 at element %i: %i/0x%06X expected %i/0x%08X\n",
                                            iDestElem,
                                            _Int24_ToIn32(pBuffAccelNoStride + iDestElem*3),
                                            _Int24_ToIn32(pBuffAccelNoStride + iDestElem*3),
                                            _Int24_ToIn32(pBuffNoAccelNoStride + iDestElem*3),
                                            _Int24_ToIn32(pBuffNoAccelNoStride + iDestElem*3));
                                    errorCount++;
                                }
                            }
                            if( pBuffNoAccelStride[iDestElem*iStride*3 + 0] != pBuffAccelStride[iDestElem*iStride*3 + 0] ||
                                pBuffNoAccelStride[iDestElem*iStride*3 + 1] != pBuffAccelStride[iDestElem*iStride*3 + 1] ||
                                pBuffNoAccelStride[iDestElem*iStride*3 + 2] != pBuffAccelStride[iDestElem*iStride*3 + 2])
                            {
                                /* Yeah this seems being a bug but let's be a bit more tolerant... */
                                PaInt32 absStrideDiff =
                                    _Int24_ToIn32(pBuffNoAccelStride+iDestElem*3) -
                                    _Int24_ToIn32(pBuffAccelStride+iDestElem*3);
                                if(absStrideDiff < 0)
                                    absStrideDiff = -absStrideDiff;
                                if(absStrideDiff > 1)
                                {
                                    if(errorCount < MaxValueErrorMsg)
                                        printf ("AccelError int24 stride %i at element %i/0x%06X: %i expected %i/0x%08X\n",
                                            iStride,
                                            iDestElem,
                                            _Int24_ToIn32(pBuffAccelStride + iDestElem*iStride*3),
                                            _Int24_ToIn32(pBuffAccelStride + iDestElem*iStride*3),
                                            _Int24_ToIn32(pBuffNoAccelStride + iDestElem*iStride*3),
                                            _Int24_ToIn32(pBuffNoAccelStride + iDestElem*iStride*3));
                                    errorCount++;
                                }
                            }
                        }
                        break;
                    }
                    case int32:
                    {
                        PaInt32 *pBuffNoAccelNoStride = (PaInt32 *)destBuffer;
                        PaInt32 *pBuffNoAccelStride = (PaInt32 *)destBufferStride;
                        PaInt32 *pBuffAccelNoStride = (PaInt32 *)destBufferAccel;
                        PaInt32 *pBuffAccelStride = (PaInt32 *)destBufferAccelStride;
                        int errorNoStride = 0;
                        int errorStride = 0;
                        for(int iDestElem=0; iDestElem<iBufferSize; iDestElem++)
                        {
                            #ifdef __ARM_NEON__
                            /* ARM NEON does not dither -> special treatment for dither cases */
                            int errorNoStride = 0;
                            if(table[iConverter].Dither)
                            {
                                /* we allow max deviation +-3 (2 + 1 rounding) - we don't dither for 32 bit accelerated
                                 * abs is not always available so:
                                 */
                                PaInt32 absNoStrideDiff = pBuffNoAccelNoStride[iDestElem]-pBuffAccelNoStride[iDestElem];
                                if(absNoStrideDiff < 0)
                                    absNoStrideDiff = -absNoStrideDiff;
                                if(absNoStrideDiff > 3)
                                    errorNoStride = 1;
                            }
                            else
                                errorNoStride = pBuffNoAccelNoStride[iDestElem] != pBuffAccelNoStride[iDestElem];
                            if(errorNoStride)
                            #else
                            /* Others check for equality as ususal */
                            if(pBuffNoAccelNoStride[iDestElem] != pBuffAccelNoStride[iDestElem])
                            #endif
                            {
                                if(errorCount < MaxValueErrorMsg)
                                    printf ("AccelError PaInt32 at element %i: %i expected %i\n",
                                        iDestElem,
                                        pBuffAccelNoStride[iDestElem],
                                        pBuffNoAccelNoStride[iDestElem]);
                                errorCount++;
                            }

                            #ifdef __ARM_NEON__
                            /* ARM NEON does not dither -> special treatment for dither cases */
                            int errorStride = 0;
                            if(table[iConverter].Dither)
                            {
                                /* we allow max deviation +-3 (2 + 1 rounding) - we don't dither for 32 bit accelerated
                                 * abs is not always available so:
                                 */
                                PaInt32 absDiffStride = pBuffNoAccelNoStride[iDestElem]-pBuffAccelNoStride[iDestElem];
                                if(absDiffStride < 0)
                                    absDiffStride = -absDiffStride;
                                if(absDiffStride > 3)
                                    errorStride = 1;
                            }
                            else
                                errorStride = pBuffNoAccelStride[iDestElem*iStride] != pBuffAccelStride[iDestElem*iStride];
                            if(errorStride)
                            #else
                            /* Others check for equality as ususal */
                            if(pBuffNoAccelStride[iDestElem*iStride] != pBuffAccelStride[iDestElem*iStride])
                            #endif
                            {
                                if(errorCount < MaxValueErrorMsg)
                                    printf ("AccelError PaInt32 stride %i at element %i: %i expected %i\n",
                                        iStride,
                                        iDestElem,
                                        pBuffAccelStride[iDestElem*iStride],
                                        pBuffNoAccelStride[iDestElem*iStride]);
                                errorCount++;
                            }
                        }
                        break;
                    }
                    case float32:
                    {
                        float *pBuffNoAccelNoStride = (float *)destBuffer;
                        float *pBuffNoAccelStride = (float *)destBufferStride;
                        float *pBuffAccelNoStride = (float *)destBufferAccel;
                        float *pBuffAccelStride = (float *)destBufferAccelStride;
                        for(int iDestElem=0; iDestElem<iBufferSize; iDestElem++)
                        {
                            if(fabs(pBuffNoAccelNoStride[iDestElem]-pBuffAccelNoStride[iDestElem]) > 1.0 / 2147483648.0)
                            {
                                if(errorCount < MaxValueErrorMsg)
                                    printf ("AccelError float at element %i: %.12f expected %.12f\n",
                                        iDestElem,
                                        pBuffAccelNoStride[iDestElem],
                                        pBuffNoAccelNoStride[iDestElem]);
                                errorCount++;
                            }
                            if(fabs(pBuffNoAccelStride[iDestElem*iStride]-pBuffAccelStride[iDestElem*iStride]) > 1.0 / 2147483648.0)
                            {
                                if(errorCount < MaxValueErrorMsg)
                                    printf ("AccelError float stride %i at element %i: %.12f expected %.12f\n",
                                        iStride,
                                        iDestElem,
                                        pBuffAccelStride[iDestElem*iStride],
                                        pBuffNoAccelStride[iDestElem*iStride]);
                                errorCount++;
                            }
                        }
                        break;
                    }
                }
                printf ("\n");
            }
        }
    }
    return 0;
}
