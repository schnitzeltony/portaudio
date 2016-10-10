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
#include "pa_converters.h"
#include "pa_dither.h"
#include "pa_endianness.h"
#include "pa_types.h"

const int iRetryPerCase = 1000;
#define MAX_BUFFLEN 4096

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
    char name[256];
} PaUtilConverterTablePerf;

#define ADD_TAB_ENTRY(_num, _name, _inDataType, _outDataType) \
    table[_num].pConverter = paConverters._name; \
    table[_num].inDataType = _inDataType; \
    table[_num].outDataType = _outDataType; \
    strcpy(table[_num].name, #_name);


/*******************************************************************/
int main(void);
int main(void)
{
    int iCountConverters = 58;  /* set max entry+1 below */

    /* Create our converter table */
    PaUtilConverterTablePerf table[iCountConverters];
    ADD_TAB_ENTRY(0,  Float32_To_Int32, float32, int32);
    ADD_TAB_ENTRY(1,  Float32_To_Int32_Dither, float32, int32);
    ADD_TAB_ENTRY(2,  Float32_To_Int32_Clip, float32, int32);
    ADD_TAB_ENTRY(3,  Float32_To_Int32_DitherClip, float32, int32);

    ADD_TAB_ENTRY(4,  Float32_To_Int24, float32, int24);
    ADD_TAB_ENTRY(5,  Float32_To_Int24_Dither, float32, int24);
    ADD_TAB_ENTRY(6,  Float32_To_Int24_Clip, float32, int24);
    ADD_TAB_ENTRY(7,  Float32_To_Int24_DitherClip, float32, int24);

    ADD_TAB_ENTRY(8,  Float32_To_Int16, float32, int16);
    ADD_TAB_ENTRY(9,  Float32_To_Int16_Dither, float32, int16);
    ADD_TAB_ENTRY(10, Float32_To_Int16_Clip, float32, int16);
    ADD_TAB_ENTRY(11, Float32_To_Int16_DitherClip, float32, int16);

    ADD_TAB_ENTRY(12, Float32_To_Int8, float32, int8);
    ADD_TAB_ENTRY(13, Float32_To_Int8_Dither, float32, int8);
    ADD_TAB_ENTRY(14, Float32_To_Int8_Clip, float32, int8);
    ADD_TAB_ENTRY(15, Float32_To_Int8_DitherClip, float32, int8);

    ADD_TAB_ENTRY(16, Float32_To_UInt8, float32, uint8);
    ADD_TAB_ENTRY(17, Float32_To_UInt8_Dither, float32, uint8);
    ADD_TAB_ENTRY(18, Float32_To_UInt8_Clip, float32, uint8);
    ADD_TAB_ENTRY(19, Float32_To_UInt8_DitherClip, float32, uint8);

    ADD_TAB_ENTRY(20, Int32_To_Float32, int32, float32);
    ADD_TAB_ENTRY(21, Int32_To_Int24, int32, int24);
    ADD_TAB_ENTRY(22, Int32_To_Int24_Dither, int32, int24);
    ADD_TAB_ENTRY(23, Int32_To_Int16, int32, int16);
    ADD_TAB_ENTRY(24, Int32_To_Int16_Dither, int32, int16);
    ADD_TAB_ENTRY(25, Int32_To_Int8, int32, int8);
    ADD_TAB_ENTRY(26, Int32_To_Int8_Dither, int32, int8);
    ADD_TAB_ENTRY(27, Int32_To_UInt8, int32, uint8);
    ADD_TAB_ENTRY(28, Int32_To_UInt8_Dither, int32, uint8);

    ADD_TAB_ENTRY(29, Int24_To_Float32, int24, float32);
    ADD_TAB_ENTRY(30, Int24_To_Int32, int24, int32);
    ADD_TAB_ENTRY(31, Int24_To_Int16, int24, int16);
    ADD_TAB_ENTRY(32, Int24_To_Int16_Dither, int24, int16);
    ADD_TAB_ENTRY(33, Int24_To_Int8, int24, int8);
    ADD_TAB_ENTRY(34, Int24_To_Int8_Dither, int24, int8);
    ADD_TAB_ENTRY(35, Int24_To_UInt8, int24, uint8);
    ADD_TAB_ENTRY(36, Int24_To_UInt8_Dither, int24, uint8);

    ADD_TAB_ENTRY(37, Int16_To_Float32, int16, float32);
    ADD_TAB_ENTRY(38, Int16_To_Int32, int16, int32);
    ADD_TAB_ENTRY(39, Int16_To_Int24, int16, int24);
    ADD_TAB_ENTRY(40, Int16_To_Int8, int16, int8);
    ADD_TAB_ENTRY(41, Int16_To_Int8_Dither, int16, int8);
    ADD_TAB_ENTRY(42, Int16_To_UInt8, int16, uint8);
    ADD_TAB_ENTRY(43, Int16_To_UInt8_Dither, int16, int8);

    ADD_TAB_ENTRY(44, Int8_To_Float32, int8, float32);
    ADD_TAB_ENTRY(45, Int8_To_Int32, int8, int32);
    ADD_TAB_ENTRY(46, Int8_To_Int24, int8, int24);
    ADD_TAB_ENTRY(47, Int8_To_Int16, int8, int16);
    ADD_TAB_ENTRY(48, Int8_To_UInt8, int8, uint8);

    ADD_TAB_ENTRY(49, UInt8_To_Float32, uint8, float32);
    ADD_TAB_ENTRY(50, UInt8_To_Int32, uint8, int32);
    ADD_TAB_ENTRY(51, UInt8_To_Int24, uint8, int24);
    ADD_TAB_ENTRY(52, UInt8_To_Int16, uint8, int16);
    ADD_TAB_ENTRY(53, UInt8_To_Int8, uint8, int8);

    ADD_TAB_ENTRY(54, Copy_8_To_8, int8, int8);
    ADD_TAB_ENTRY(55, Copy_16_To_16, int16, int16);
    ADD_TAB_ENTRY(56, Copy_24_To_24, int24, int24);
    ADD_TAB_ENTRY(57, Copy_32_To_32, int32, int32);

    /* create dither instance */
    PaUtilTriangularDitherGenerator dither;

    /* define test tupels */
    int buffer_sizes[] = {64, 256, 1024, MAX_BUFFLEN};
    #define MAX_STRIDE 4
    int strides[] = {1, 2, MAX_STRIDE};

    /* create our buffers */
    float outputBuffer[MAX_BUFFLEN * MAX_STRIDE];
    float inputBuffer[MAX_BUFFLEN * MAX_STRIDE];

    int iRepetition;

    for(int iConverter=0; iConverter<4/*iCountConverters*/; iConverter++)
    {
        for(int iBuffersizeNum=0; iBuffersizeNum<sizeof(buffer_sizes)/sizeof(int); iBuffersizeNum++)
        {
            int iBufferSize = buffer_sizes[iBuffersizeNum];
            for(int iStrideNum=0; iStrideNum<sizeof(strides)/sizeof(int); iStrideNum++)
            {
                int iStride = strides[iStrideNum];
                /* prepare interesting input test data depending on input data type */
                int iEntry;
                switch(table[iConverter].inDataType)
                {
                    case int8:
                    {
                        signed char *pBuff = (signed char *)inputBuffer;
                        for(iEntry=0; iEntry<iBufferSize; iEntry++)
                            pBuff[iEntry*iStride] = (iEntry % 256) - 128;
                        break;
                    }
                    case uint8:
                    {
                        unsigned char *pBuff = (unsigned char *)inputBuffer;
                        for(iEntry=0; iEntry<iBufferSize; iEntry++)
                            pBuff[iEntry*iStride] = (iEntry % 256);
                        break;
                    }
                    case int16:
                    {
                        PaInt16 *pBuff = (PaInt16 *)inputBuffer;
                        for(iEntry=0; iEntry<iBufferSize; iEntry++)
                            pBuff[iEntry*iStride] = ((iEntry % 256) - 128) * 256;
                        break;
                    }
                    case int24:
                    {
                        unsigned char *pBuff = (unsigned char*) inputBuffer;
                        for(iEntry=0; iEntry<iBufferSize; iEntry++)
                        {
                            PaInt32 value = ((iEntry % 256) - 128) * 256 * 256;
                            #if defined(PA_LITTLE_ENDIAN)
                                    pBuff[0] = (unsigned char)(value >> 0);
                                    pBuff[1] = (unsigned char)(value >> 8);
                                    pBuff[2] = (unsigned char)(value >> 16);
                            #elif defined(PA_BIG_ENDIAN)
                                    pBuff[0] = (unsigned char)(value >> 16);
                                    pBuff[1] = (unsigned char)(value >> 8);
                                    pBuff[2] = (unsigned char)(value >> 0);
                            #endif
                            pBuff += 3*iStride;
                        }
                        break;
                    }
                    case int32:
                    {
                        PaInt32 *pBuff = (PaInt32 *)inputBuffer;
                        for(iEntry=0; iEntry<iBufferSize; iEntry++)
                            pBuff[iEntry*iStride] = ((iEntry % 256) - 128) * 256 * 256 * 256;
                        break;
                    }
                    case float32:
                    {
                        float *pBuff = (float *)inputBuffer;
                        for(iEntry=0; iEntry<iBufferSize; iEntry++)
                            pBuff[iEntry*iStride] = ((float)((iEntry % 256) - 128)) / 128.0;
                        break;
                    }
                }

                printf ("Starting %s / size %i / stride %i...\n",
                    table[iConverter].name, iBufferSize, iStride);

                /* Run without acceleration */
                withAcceleration = 0;
                PaUtil_InitializeTriangularDitherState(&dither);
                clock_t tNoAccel = clock();
                for(iRepetition=0; iRepetition<iRetryPerCase; iRepetition++)
                {
                    table[iConverter].pConverter(
                        (void *)outputBuffer,
                        iStride,
                        (void *)inputBuffer,
                        iStride,
                        iBufferSize,
                        &dither );
                }
                float fTimeNoAccel = ((float)(clock() - tNoAccel)) / CLOCKS_PER_SEC;
                printf ("Without acceleration took %f seconds.\n", fTimeNoAccel);

                /* Run with acceleration */
                withAcceleration = 1;
                PaUtil_InitializeTriangularDitherState(&dither);
                clock_t tAccel = clock();
                for(iRepetition=0; iRepetition<iRetryPerCase; iRepetition++)
                {
                    table[iConverter].pConverter(
                        (void *)outputBuffer,
                        iStride,
                        (void *)inputBuffer,
                        iStride,
                        iBufferSize,
                        &dither );
                }
                float fTimeAccel = ((float)(clock() - tAccel)) / CLOCKS_PER_SEC;
                printf ("With acceleration took %f seconds.\n", fTimeAccel);
                printf ("\n");
            }
        }
    }
    return 0;
}
