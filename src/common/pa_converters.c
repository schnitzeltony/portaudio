/*
 * $Id$
 * Portable Audio I/O Library sample conversion mechanism
 *
 * Based on the Open Source API proposed by Ross Bencina
 * Copyright (c) 1999-2002 Phil Burk, Ross Bencina
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

/** @file
 @ingroup common_src

 @brief Conversion function implementations.
 
 If the C9x function lrintf() is available, define PA_USE_C99_LRINTF to use it

 @todo Consider whether functions which dither but don't clip should exist,
 V18 automatically enabled clipping whenever dithering was selected. Perhaps
 we should do the same. 
    see: "require clipping for dithering sample conversion functions?"
    http://www.portaudio.com/trac/ticket/112

 @todo implement the converters marked IMPLEMENT ME: Int32_To_Int24_Dither,
 Int32_To_UInt8_Dither, Int24_To_Int16_Dither, Int24_To_Int8_Dither, 
 Int24_To_UInt8_Dither, Int16_To_Int8_Dither, Int16_To_UInt8_Dither
    see: "some conversion functions are not implemented in pa_converters.c"
    http://www.portaudio.com/trac/ticket/35

 @todo review the converters marked REVIEW: Float32_To_Int32,
 Float32_To_Int32_Dither, Float32_To_Int32_Clip, Float32_To_Int32_DitherClip,
 Int32_To_Int16_Dither, Int32_To_Int8_Dither, Int16_To_Int32
*/


#include "pa_converters.h"
#include "pa_dither.h"
#include "pa_endianness.h"
#include "pa_types.h"

/* allow to switch acceleration on/off */
volatile int withAcceleration = 1;

#ifdef __ARM_NEON__

static inline float32x4_t NeonGetSourceVector(
    float **src, signed int sourceStride)
{
    int lane;
    float32x4_t neonSourceVector;
    switch(sourceStride)
    {
        case 1:
            neonSourceVector = vld1q_f32(*src);
            *src += sourceStride * ARM_NEON_BEST_VECTOR_SIZE;
            break;
        case 2:
        {
            float32x4x2_t neonTmp = vld2q_f32(*src);
            neonSourceVector = neonTmp.val[0];
            *src += sourceStride * ARM_NEON_BEST_VECTOR_SIZE;
            break;
        }
        /* VLDN N>2 decrease performance */
        default:
            for(lane=0; lane<ARM_NEON_BEST_VECTOR_SIZE; lane++)
            {
                neonSourceVector = vld1q_lane_f32(*src, neonSourceVector, lane);
                *src += sourceStride;
            }
            break;
    }
    return neonSourceVector;
}

static inline PaInt32 *NeonWriteDestVectorInt32(
    int32x4_t neonResultVector, PaInt32 *dest, signed int destinationStride)
{
    switch(destinationStride)
    {
        case 1:
            vst1q_s32(dest, neonResultVector);
            dest += destinationStride * ARM_NEON_BEST_VECTOR_SIZE;
            break;
        default:
        {
            int lane;
            for(lane=0; lane<ARM_NEON_BEST_VECTOR_SIZE; lane++)
            {
                vst1q_lane_s32(dest, neonResultVector, lane);
                dest += destinationStride;
            }
            break;
        }
    }
    return dest;
}

static inline unsigned char *NeonWriteDestVectorInt24(
    int32x4_t neonResultVector, unsigned char *dest, signed int destinationStride)
{
    switch(destinationStride)
    {
        /* Did not find big endian ARM NEON machine for test - so disable this path */
        #if defined(PA_LITTLE_ENDIAN)
        case 1:
        {
            /* 1. compress incoming neon data to the center 8 bit lanes
             * 2. move left
             * 3. store in 2 memory transactions only
             *
             * |24Bit0|x|24Bit1|x|24Bit2|x|24Bit3|x|
             *                  |
             *                  v
             * |x|x|24Bit0|24Bit1|24Bit2|24Bit3|x|x|
             *                  |
             *                  v
             * |24Bit0|24Bit1|24Bit2|24Bit3|x|x|x|x|
             * |           96Bit           |x|x|x|x|
             *               store as
             * |      64Bit     |  32Bit   |x|x|x|x|
             *
             * Note 1: Table actions can only be performed on 64 Bit D registers
             *         (see indexes below)
             * Note 2: x (empty/0) -> 8
             *
             * see more detals on neon table permutation at
             * https://community.arm.com/groups/processors/blog/authors/martynarm
             */
            uint8_t compressPositions[] =
            {
            #if defined(PA_LITTLE_ENDIAN)
                8, 8, 1, 2, 3, 5, 6, 7,
                1, 2, 3, 5, 6, 7, 8, 8
            #elif defined(PA_BIG_ENDIAN)
                7, 6, 5, 3, 2, 1, 8, 8,     /* To be tested - have no machin for that */
                8, 8, 7, 6, 5, 3, 1, 2
            #endif
            };
            uint8x16_t neonTableTranslation = vld1q_u8(compressPositions);
            /* table operations are avaliable for 8 Bit lanes only */
            uint8x16_t neonCastedResult = vreinterpretq_u8_s32(neonResultVector);
            /* table magic twice */
            uint8x8_t neonValuesHigh = vtbl1_u8(
                vget_high_u8(neonCastedResult),
                vget_high_u8(neonTableTranslation));
            uint8x8_t neonValuesLow = vtbl1_u8(
                vget_low_u8(neonCastedResult),
                vget_low_u8(neonTableTranslation));
            /* Interpret center compressed back to one Q uint8x16_t */
            uint8x16_t neonCompressed24Result = vcombine_u8(neonValuesLow, neonValuesHigh);
            /* rotate vector (2.) */
            neonCompressed24Result = vextq_u8(neonCompressed24Result, neonCompressed24Result, 2);
            /* store 64+32 */
            /* 1. 64 bits / 8 bytes */
            vst1_u32(dest, vreinterpret_u32_u8(vget_low_u8(neonCompressed24Result)));
            /* 2. 32 bits / 4 bytes */
            vst1q_lane_u32(dest+8, vreinterpretq_u32_u8(neonCompressed24Result), 2);
            dest += 8+4;
            break;
        }
        #endif /* defined(PA_LITTLE_ENDIAN)*/
        default:
        {
            /* Get data out of neon to handle it 'traditional' */
            PaUint32 resultVector32[ARM_NEON_BEST_VECTOR_SIZE];
            vst1q_u32(resultVector32, vreinterpretq_u32_s32(neonResultVector));
            PaUint32 temp;
            int lane;
            for(lane=0; lane<ARM_NEON_BEST_VECTOR_SIZE; lane++)
            {
                temp = resultVector32[lane];
                #if defined(PA_LITTLE_ENDIAN)
                    dest[0] = (unsigned char)(temp >> 8);
                    dest[1] = (unsigned char)(temp >> 16);
                    dest[2] = (unsigned char)(temp >> 24);
                #elif defined(PA_BIG_ENDIAN)
                    dest[0] = (unsigned char)(temp >> 24);
                    dest[1] = (unsigned char)(temp >> 16);
                    dest[2] = (unsigned char)(temp >> 8);
                #endif
                dest += 3*destinationStride;
            }
            break;
        }
    }
    return dest;
}

static inline PaInt16 *NeonWriteDestVectorInt16(
    int16x4_t neonResultVector, PaInt16 *dest, signed int destinationStride)
{
    switch(destinationStride)
    {
        case 1:
            vst1_s16(dest, neonResultVector);
            dest += destinationStride * ARM_NEON_BEST_VECTOR_SIZE;
            break;
        default:
        {
            int lane;
            for(lane=0; lane<ARM_NEON_BEST_VECTOR_SIZE; lane++)
            {
                vst1_lane_s16(dest, neonResultVector, lane);
                dest += destinationStride;
            }
            break;
        }
    }
    return dest;
}

#endif /* __ARM_NEON__ */


PaSampleFormat PaUtil_SelectClosestAvailableFormat(
        PaSampleFormat availableFormats, PaSampleFormat format )
{
    PaSampleFormat result;

    format &= ~paNonInterleaved;
    availableFormats &= ~paNonInterleaved;
    
    if( (format & availableFormats) == 0 )
    {
        /* NOTE: this code depends on the sample format constants being in
            descending order of quality - ie best quality is 0
            FIXME: should write an assert which checks that all of the
            known constants conform to that requirement.
        */

        if( format != 0x01 )
        {
            /* scan for better formats */
            result = format;
            do
            {
                result >>= 1;
            }
            while( (result & availableFormats) == 0 && result != 0 );
        }
        else
        {
            result = 0;
        }
        
        if( result == 0 ){
            /* scan for worse formats */
            result = format;
            do
            {
                result <<= 1;
            }
            while( (result & availableFormats) == 0 && result != paCustomFormat );

            if( (result & availableFormats) == 0 )
                result = paSampleFormatNotSupported;
        }
        
    }else{
        result = format;
    }

    return result;
}

/* -------------------------------------------------------------------------- */

#define PA_SELECT_FORMAT_( format, float32, int32, int24, int16, int8, uint8 ) \
    switch( format & ~paNonInterleaved ){                                      \
    case paFloat32:                                                            \
        float32                                                                \
    case paInt32:                                                              \
        int32                                                                  \
    case paInt24:                                                              \
        int24                                                                  \
    case paInt16:                                                              \
        int16                                                                  \
    case paInt8:                                                               \
        int8                                                                   \
    case paUInt8:                                                              \
        uint8                                                                  \
    default: return 0;                                                         \
    }

/* -------------------------------------------------------------------------- */

#define PA_SELECT_CONVERTER_DITHER_CLIP_( flags, source, destination )         \
    if( flags & paClipOff ){ /* no clip */                                     \
        if( flags & paDitherOff ){ /* no dither */                             \
            return paConverters. source ## _To_ ## destination;                \
        }else{ /* dither */                                                    \
            return paConverters. source ## _To_ ## destination ## _Dither;     \
        }                                                                      \
    }else{ /* clip */                                                          \
        if( flags & paDitherOff ){ /* no dither */                             \
            return paConverters. source ## _To_ ## destination ## _Clip;       \
        }else{ /* dither */                                                    \
            return paConverters. source ## _To_ ## destination ## _DitherClip; \
        }                                                                      \
    }

/* -------------------------------------------------------------------------- */

#define PA_SELECT_CONVERTER_DITHER_( flags, source, destination )              \
    if( flags & paDitherOff ){ /* no dither */                                 \
        return paConverters. source ## _To_ ## destination;                    \
    }else{ /* dither */                                                        \
        return paConverters. source ## _To_ ## destination ## _Dither;         \
    }

/* -------------------------------------------------------------------------- */

#define PA_USE_CONVERTER_( source, destination )\
    return paConverters. source ## _To_ ## destination;

/* -------------------------------------------------------------------------- */

#define PA_UNITY_CONVERSION_( wordlength )\
    return paConverters. Copy_ ## wordlength ## _To_ ## wordlength;

/* -------------------------------------------------------------------------- */

PaUtilConverter* PaUtil_SelectConverter( PaSampleFormat sourceFormat,
        PaSampleFormat destinationFormat, PaStreamFlags flags )
{
    PA_SELECT_FORMAT_( sourceFormat,
                       /* paFloat32: */
                       PA_SELECT_FORMAT_( destinationFormat,
                                          /* paFloat32: */        PA_UNITY_CONVERSION_( 32 ),
                                          /* paInt32: */          PA_SELECT_CONVERTER_DITHER_CLIP_( flags, Float32, Int32 ),
                                          /* paInt24: */          PA_SELECT_CONVERTER_DITHER_CLIP_( flags, Float32, Int24 ),
                                          /* paInt16: */          PA_SELECT_CONVERTER_DITHER_CLIP_( flags, Float32, Int16 ),
                                          /* paInt8: */           PA_SELECT_CONVERTER_DITHER_CLIP_( flags, Float32, Int8 ),
                                          /* paUInt8: */          PA_SELECT_CONVERTER_DITHER_CLIP_( flags, Float32, UInt8 )
                                        ),
                       /* paInt32: */
                       PA_SELECT_FORMAT_( destinationFormat,
                                          /* paFloat32: */        PA_USE_CONVERTER_( Int32, Float32 ),
                                          /* paInt32: */          PA_UNITY_CONVERSION_( 32 ),
                                          /* paInt24: */          PA_SELECT_CONVERTER_DITHER_( flags, Int32, Int24 ),
                                          /* paInt16: */          PA_SELECT_CONVERTER_DITHER_( flags, Int32, Int16 ),
                                          /* paInt8: */           PA_SELECT_CONVERTER_DITHER_( flags, Int32, Int8 ),
                                          /* paUInt8: */          PA_SELECT_CONVERTER_DITHER_( flags, Int32, UInt8 )
                                        ),
                       /* paInt24: */
                       PA_SELECT_FORMAT_( destinationFormat,
                                          /* paFloat32: */        PA_USE_CONVERTER_( Int24, Float32 ),
                                          /* paInt32: */          PA_USE_CONVERTER_( Int24, Int32 ),
                                          /* paInt24: */          PA_UNITY_CONVERSION_( 24 ),
                                          /* paInt16: */          PA_SELECT_CONVERTER_DITHER_( flags, Int24, Int16 ),
                                          /* paInt8: */           PA_SELECT_CONVERTER_DITHER_( flags, Int24, Int8 ),
                                          /* paUInt8: */          PA_SELECT_CONVERTER_DITHER_( flags, Int24, UInt8 )
                                        ),
                       /* paInt16: */
                       PA_SELECT_FORMAT_( destinationFormat,
                                          /* paFloat32: */        PA_USE_CONVERTER_( Int16, Float32 ),
                                          /* paInt32: */          PA_USE_CONVERTER_( Int16, Int32 ),
                                          /* paInt24: */          PA_USE_CONVERTER_( Int16, Int24 ),
                                          /* paInt16: */          PA_UNITY_CONVERSION_( 16 ),
                                          /* paInt8: */           PA_SELECT_CONVERTER_DITHER_( flags, Int16, Int8 ),
                                          /* paUInt8: */          PA_SELECT_CONVERTER_DITHER_( flags, Int16, UInt8 )
                                        ),
                       /* paInt8: */
                       PA_SELECT_FORMAT_( destinationFormat,
                                          /* paFloat32: */        PA_USE_CONVERTER_( Int8, Float32 ),
                                          /* paInt32: */          PA_USE_CONVERTER_( Int8, Int32 ),
                                          /* paInt24: */          PA_USE_CONVERTER_( Int8, Int24 ),
                                          /* paInt16: */          PA_USE_CONVERTER_( Int8, Int16 ),
                                          /* paInt8: */           PA_UNITY_CONVERSION_( 8 ),
                                          /* paUInt8: */          PA_USE_CONVERTER_( Int8, UInt8 )
                                        ),
                       /* paUInt8: */
                       PA_SELECT_FORMAT_( destinationFormat,
                                          /* paFloat32: */        PA_USE_CONVERTER_( UInt8, Float32 ),
                                          /* paInt32: */          PA_USE_CONVERTER_( UInt8, Int32 ),
                                          /* paInt24: */          PA_USE_CONVERTER_( UInt8, Int24 ),
                                          /* paInt16: */          PA_USE_CONVERTER_( UInt8, Int16 ),
                                          /* paInt8: */           PA_USE_CONVERTER_( UInt8, Int8 ),
                                          /* paUInt8: */          PA_UNITY_CONVERSION_( 8 )
                                        )
                     )
}

/* -------------------------------------------------------------------------- */

#ifdef PA_NO_STANDARD_CONVERTERS

/* -------------------------------------------------------------------------- */

PaUtilConverterTable paConverters = {
    0, /* PaUtilConverter *Float32_To_Int32; */
    0, /* PaUtilConverter *Float32_To_Int32_Dither; */
    0, /* PaUtilConverter *Float32_To_Int32_Clip; */
    0, /* PaUtilConverter *Float32_To_Int32_DitherClip; */

    0, /* PaUtilConverter *Float32_To_Int24; */
    0, /* PaUtilConverter *Float32_To_Int24_Dither; */
    0, /* PaUtilConverter *Float32_To_Int24_Clip; */
    0, /* PaUtilConverter *Float32_To_Int24_DitherClip; */

    0, /* PaUtilConverter *Float32_To_Int16; */
    0, /* PaUtilConverter *Float32_To_Int16_Dither; */
    0, /* PaUtilConverter *Float32_To_Int16_Clip; */
    0, /* PaUtilConverter *Float32_To_Int16_DitherClip; */

    0, /* PaUtilConverter *Float32_To_Int8; */
    0, /* PaUtilConverter *Float32_To_Int8_Dither; */
    0, /* PaUtilConverter *Float32_To_Int8_Clip; */
    0, /* PaUtilConverter *Float32_To_Int8_DitherClip; */

    0, /* PaUtilConverter *Float32_To_UInt8; */
    0, /* PaUtilConverter *Float32_To_UInt8_Dither; */
    0, /* PaUtilConverter *Float32_To_UInt8_Clip; */
    0, /* PaUtilConverter *Float32_To_UInt8_DitherClip; */

    0, /* PaUtilConverter *Int32_To_Float32; */
    0, /* PaUtilConverter *Int32_To_Int24; */
    0, /* PaUtilConverter *Int32_To_Int24_Dither; */
    0, /* PaUtilConverter *Int32_To_Int16; */
    0, /* PaUtilConverter *Int32_To_Int16_Dither; */
    0, /* PaUtilConverter *Int32_To_Int8; */
    0, /* PaUtilConverter *Int32_To_Int8_Dither; */
    0, /* PaUtilConverter *Int32_To_UInt8; */
    0, /* PaUtilConverter *Int32_To_UInt8_Dither; */

    0, /* PaUtilConverter *Int24_To_Float32; */
    0, /* PaUtilConverter *Int24_To_Int32; */
    0, /* PaUtilConverter *Int24_To_Int16; */
    0, /* PaUtilConverter *Int24_To_Int16_Dither; */
    0, /* PaUtilConverter *Int24_To_Int8; */
    0, /* PaUtilConverter *Int24_To_Int8_Dither; */
    0, /* PaUtilConverter *Int24_To_UInt8; */
    0, /* PaUtilConverter *Int24_To_UInt8_Dither; */
    
    0, /* PaUtilConverter *Int16_To_Float32; */
    0, /* PaUtilConverter *Int16_To_Int32; */
    0, /* PaUtilConverter *Int16_To_Int24; */
    0, /* PaUtilConverter *Int16_To_Int8; */
    0, /* PaUtilConverter *Int16_To_Int8_Dither; */
    0, /* PaUtilConverter *Int16_To_UInt8; */
    0, /* PaUtilConverter *Int16_To_UInt8_Dither; */

    0, /* PaUtilConverter *Int8_To_Float32; */
    0, /* PaUtilConverter *Int8_To_Int32; */
    0, /* PaUtilConverter *Int8_To_Int24 */
    0, /* PaUtilConverter *Int8_To_Int16; */
    0, /* PaUtilConverter *Int8_To_UInt8; */

    0, /* PaUtilConverter *UInt8_To_Float32; */
    0, /* PaUtilConverter *UInt8_To_Int32; */
    0, /* PaUtilConverter *UInt8_To_Int24; */
    0, /* PaUtilConverter *UInt8_To_Int16; */
    0, /* PaUtilConverter *UInt8_To_Int8; */

    0, /* PaUtilConverter *Copy_8_To_8; */
    0, /* PaUtilConverter *Copy_16_To_16; */
    0, /* PaUtilConverter *Copy_24_To_24; */
    0  /* PaUtilConverter *Copy_32_To_32; */
};

/* -------------------------------------------------------------------------- */

#else /* PA_NO_STANDARD_CONVERTERS is not defined */

/* -------------------------------------------------------------------------- */

#define PA_CLIP_( val, min, max )\
    { val = ((val) < (min)) ? (min) : (((val) > (max)) ? (max) : (val)); }


static const float const_1_div_128_ = 1.0f / 128.0f;  /* 8 bit multiplier */

static const float const_1_div_32768_ = 1.0f / 32768.f; /* 16 bit multiplier */

static const double const_1_div_2147483648_ = 1.0 / 2147483648.0; /* 32 bit multiplier */

/* -------------------------------------------------------------------------- */

static void Float32_To_Int32(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    float *src = (float*)sourceBuffer;
    PaInt32 *dest =  (PaInt32*)destinationBuffer;
    (void)ditherGenerator; /* unused parameter */

#ifdef __ARM_NEON__
    if(withAcceleration /*&& sourceStride == 1 && destinationStride == 1*/)
    {
        float32x4_t neonSourceVector, neonScaled;
        int32x4_t neonResultVector;
        float32x4_t neonMult = vdupq_n_f32(0x7FFFFFFF);
        while(count >= ARM_NEON_BEST_VECTOR_SIZE)
        {
            /* get source vector */
            neonSourceVector = NeonGetSourceVector(&src, sourceStride);
            /* scale */
            neonScaled = vmulq_f32(neonSourceVector, neonMult);
            /* convert vector - rounded towards zero */
            neonResultVector = vcvtq_s32_f32(neonScaled);
            /* write result */
            dest = NeonWriteDestVectorInt32(neonResultVector, dest, destinationStride);
            count -= ARM_NEON_BEST_VECTOR_SIZE;
        }
    }
#endif

    while( count-- )
    {
        /* REVIEW */
#if defined (PA_USE_C99_LRINTF) && !defined (__ARM_NEON__)
        float scaled = *src * 0x7FFFFFFF;
        *dest = lrintf(scaled-0.5f);
#else
        double scaled = *src * 0x7FFFFFFF;
        *dest = (PaInt32) scaled;        
#endif
        
        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Float32_To_Int32_Dither(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    float *src = (float*)sourceBuffer;
    PaInt32 *dest =  (PaInt32*)destinationBuffer;

#ifdef __ARM_NEON__
    if(withAcceleration)
    {
        /* ARM NEON does not support doubles so don't dither at 32Bit - there
         * is no hardware avavailable creating less noise than 32Bit dither...
         */
        Float32_To_Int32(
            destinationBuffer,
            destinationStride,
            sourceBuffer,
            sourceStride,
            count,
            ditherGenerator );
        return;
    }
#endif

    while( count-- )
    {
        /* REVIEW */
#if defined (PA_USE_C99_LRINTF) && !defined (__ARM_NEON__)
        float dither  = PaUtil_GenerateFloatTriangularDither( ditherGenerator );
        /* use smaller scaler to prevent overflow when we add the dither */
        float dithered = ((float)*src * (2147483646.0f)) + dither;
        *dest = lrintf(dithered - 0.5f);
#else
        double dither  = PaUtil_GenerateFloatTriangularDither( ditherGenerator );
        /* use smaller scaler to prevent overflow when we add the dither */
        double dithered = ((double)*src * (2147483646.0)) + dither;
        *dest = (PaInt32) dithered;
#endif
        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Float32_To_Int32_Clip(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    float *src = (float*)sourceBuffer;
    PaInt32 *dest =  (PaInt32*)destinationBuffer;
    (void) ditherGenerator; /* unused parameter */
    
#ifdef __ARM_NEON__
    if(withAcceleration)
    {
        float32x4_t neonSourceVector, neonScaled;
        int32x4_t neonResultVector;
        float32x4_t neonMult = vdupq_n_f32(0x7FFFFFFF);
        while(count >= ARM_NEON_BEST_VECTOR_SIZE)
        {
            /* get source vector */
            neonSourceVector = NeonGetSourceVector(&src, sourceStride);
            /* scale vector */
            neonScaled = vmulq_f32(neonSourceVector, neonMult);
            /* clip vector */
            neonScaled = vmaxq_f32(neonScaled, vdupq_n_f32(-2147483648.f));
            neonScaled = vminq_f32(neonScaled, vdupq_n_f32(2147483647.f));
            /* convert vector - rounded towards zero */
            neonResultVector = vcvtq_s32_f32(neonScaled);
            /* write result */
            dest = NeonWriteDestVectorInt32(neonResultVector, dest, destinationStride);
            count-=ARM_NEON_BEST_VECTOR_SIZE;
        }
    }
#endif

    while( count-- )
    {
        /* REVIEW */
#if defined (PA_USE_C99_LRINTF) && !defined (__ARM_NEON__)
        float scaled = *src * 0x7FFFFFFF;
        PA_CLIP_( scaled, -2147483648.f, 2147483647.f  );
        *dest = lrintf(scaled-0.5f);
#else
        double scaled = *src * 0x7FFFFFFF;
        PA_CLIP_( scaled, -2147483648., 2147483647.  );
        *dest = (PaInt32) scaled;
#endif

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Float32_To_Int32_DitherClip(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    float *src = (float*)sourceBuffer;
    PaInt32 *dest =  (PaInt32*)destinationBuffer;

#ifdef __ARM_NEON__
    if(withAcceleration)
    {
        /* ARM NEON does not support doubles so don't dither at 32Bit - there
         * is no hardware avavailable creating less noise than 32Bit dither...
         */
        Float32_To_Int32(
            destinationBuffer,
            destinationStride,
            sourceBuffer,
            sourceStride,
            count,
            ditherGenerator );
        return;
    }
#endif

    while( count-- )
    {
        /* REVIEW */
#if defined (PA_USE_C99_LRINTF) && !defined (__ARM_NEON__)
        float dither  = PaUtil_GenerateFloatTriangularDither( ditherGenerator );
        /* use smaller scaler to prevent overflow when we add the dither */
        float dithered = ((float)*src * (2147483646.0f)) + dither;
        PA_CLIP_( dithered, -2147483648.f, 2147483647.f  );
        *dest = lrintf(dithered-0.5f);
#else
        double dither  = PaUtil_GenerateFloatTriangularDither( ditherGenerator );
        /* use smaller scaler to prevent overflow when we add the dither */
        double dithered = ((double)*src * (2147483646.0)) + dither;
        PA_CLIP_( dithered, -2147483648., 2147483647.  );
        *dest = (PaInt32) dithered;
#endif

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Float32_To_Int24(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    float *src = (float*)sourceBuffer;
    unsigned char *dest = (unsigned char*)destinationBuffer;
    PaInt32 temp;

    (void) ditherGenerator; /* unused parameter */
    
#ifdef __ARM_NEON__
    if(withAcceleration)
    {
        float32x4_t neonSourceVector, neonScaled;
        int32x4_t neonResultVector;
        float32x4_t neonMult = vdupq_n_f32(2147483647.0);
        while(count >= ARM_NEON_BEST_VECTOR_SIZE)
        {
            /* get source vector */
            neonSourceVector = NeonGetSourceVector(&src, sourceStride);
            /* scale vector */
            neonScaled = vmulq_f32(neonSourceVector, neonMult);
            /* convert vector - rounded towards zero */
            neonResultVector = vcvtq_s32_f32(neonScaled);
            /* write result */
            dest = NeonWriteDestVectorInt24(neonResultVector, dest, destinationStride);
            count-=ARM_NEON_BEST_VECTOR_SIZE;
        }
    }
#endif

    while( count-- )
    {
        /* convert to 32 bit and drop the low 8 bits */
        double scaled = (double)(*src) * 2147483647.0;
        temp = (PaInt32) scaled;
        
#if defined(PA_LITTLE_ENDIAN)
        dest[0] = (unsigned char)(((PaUint32)temp) >> 8);
        dest[1] = (unsigned char)(((PaUint32)temp) >> 16);
        dest[2] = (unsigned char)(((PaUint32)temp) >> 24);
#elif defined(PA_BIG_ENDIAN)
        dest[0] = (unsigned char)(((PaUint32)temp) >> 24);
        dest[1] = (unsigned char)(((PaUint32)temp) >> 16);
        dest[2] = (unsigned char)(((PaUint32)temp) >> 8);
#endif

        src += sourceStride;
        dest += destinationStride * 3;
    }
}

/* -------------------------------------------------------------------------- */

static void Float32_To_Int24_Dither(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    float *src = (float*)sourceBuffer;
    unsigned char *dest = (unsigned char*)destinationBuffer;
    PaInt32 temp;

#ifdef __ARM_NEON__
    if(withAcceleration)
    {
        float32x4_t neonSourceVector, neonScaled, neonDither;
        int32x4_t neonResultVector;
        int32_t resultVector32[ARM_NEON_BEST_VECTOR_SIZE];
        float32x4_t neonMult = vdupq_n_f32(2147483646.0f);
        while( count >= ARM_NEON_BEST_VECTOR_SIZE)
        {
            /* get source vector */
            neonSourceVector = NeonGetSourceVector(&src, sourceStride);
            /* get dither */
            neonDither = PaUtil_GenerateFloatTriangularDitherVector(
                ditherGenerator, const_float_dither_scale_ * 256.0);
            /* scale vector + add dither vmla(a,b,c) <-> a+b*c */
            neonScaled = vmlaq_f32(neonDither, neonSourceVector, neonMult);
            /* convert vector - rounded towards zero */
            neonResultVector = vcvtq_s32_f32(neonScaled);
            /* write result */
            dest = NeonWriteDestVectorInt24(neonResultVector, dest, destinationStride);
            count-=ARM_NEON_BEST_VECTOR_SIZE;
        }
    }
#endif

    while( count-- )
    {
        /* convert to 32 bit and drop the low 8 bits */

        double dither  = PaUtil_GenerateFloatTriangularDither24( ditherGenerator );
        /* use smaller scaler to prevent overflow when we add the dither */
        double dithered = ((double)*src * (2147483646.0)) + dither;
        
        temp = (PaInt32) dithered;

#if defined(PA_LITTLE_ENDIAN)
        dest[0] = (unsigned char)(((PaUint32)temp) >> 8);
        dest[1] = (unsigned char)(((PaUint32)temp) >> 16);
        dest[2] = (unsigned char)(((PaUint32)temp) >> 24);
#elif defined(PA_BIG_ENDIAN)
        dest[0] = (unsigned char)(((PaUint32)temp) >> 24);
        dest[1] = (unsigned char)(((PaUint32)temp) >> 16);
        dest[2] = (unsigned char)(((PaUint32)temp) >> 8);
#endif

        src += sourceStride;
        dest += destinationStride * 3;
    }
}

/* -------------------------------------------------------------------------- */

static void Float32_To_Int24_Clip(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    float *src = (float*)sourceBuffer;
    unsigned char *dest = (unsigned char*)destinationBuffer;
    PaInt32 temp;

    (void) ditherGenerator; /* unused parameter */
    
#ifdef __ARM_NEON__
    if(withAcceleration)
    {
        float32x4_t neonSourceVector, neonScaled;
        int32x4_t neonResultVector;
        int32_t resultVector32[ARM_NEON_BEST_VECTOR_SIZE];
        float32x4_t neonMult = vdupq_n_f32(0x7FFFFFFF);
        while(count >= ARM_NEON_BEST_VECTOR_SIZE)
        {
            /* get source vector */
            neonSourceVector = NeonGetSourceVector(&src, sourceStride);
            /* scale vector */
            neonScaled = vmulq_f32(neonSourceVector, neonMult);
            /* clip vector */
            neonScaled = vmaxq_f32(neonScaled, vdupq_n_f32(-2147483648.f));
            neonScaled = vminq_f32(neonScaled, vdupq_n_f32(2147483647.f));
            /* convert vector - rounded towards zero */
            neonResultVector = vcvtq_s32_f32(neonScaled);
            /* write result */
            dest = NeonWriteDestVectorInt24(neonResultVector, dest, destinationStride);
            count-=ARM_NEON_BEST_VECTOR_SIZE;
        }
    }
#endif

    while( count-- )
    {
        /* convert to 32 bit and drop the low 8 bits */
        double scaled = *src * 0x7FFFFFFF;
        PA_CLIP_( scaled, -2147483648., 2147483647.  );
        temp = (PaInt32) scaled;

#if defined(PA_LITTLE_ENDIAN)
        dest[0] = (unsigned char)(((PaUint32)temp) >> 8);
        dest[1] = (unsigned char)(((PaUint32)temp) >> 16);
        dest[2] = (unsigned char)(((PaUint32)temp) >> 24);
#elif defined(PA_BIG_ENDIAN)
        dest[0] = (unsigned char)(((PaUint32)temp) >> 24);
        dest[1] = (unsigned char)(((PaUint32)temp) >> 16);
        dest[2] = (unsigned char)(((PaUint32)temp) >> 8);
#endif

        src += sourceStride;
        dest += destinationStride * 3;
    }
}

/* -------------------------------------------------------------------------- */

static void Float32_To_Int24_DitherClip(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    float *src = (float*)sourceBuffer;
    unsigned char *dest = (unsigned char*)destinationBuffer;
    PaInt32 temp;

#ifdef __ARM_NEON__
    if(withAcceleration)
    {
        float32x4_t neonSourceVector, neonScaled, neonDither;
        int32x4_t neonResultVector;
        int32_t resultVector32[ARM_NEON_BEST_VECTOR_SIZE];
        float32x4_t neonMult = vdupq_n_f32(2147483646.0f);
        while( count >= ARM_NEON_BEST_VECTOR_SIZE)
        {
            /* get source vector */
            neonSourceVector = NeonGetSourceVector(&src, sourceStride);
            /* get dither */
            neonDither = PaUtil_GenerateFloatTriangularDitherVector(
                ditherGenerator, const_float_dither_scale_ * 256.0);
            /* scale vector + add dither vmla(a,b,c) <-> a+b*c */
            neonScaled = vmlaq_f32(neonDither, neonSourceVector, neonMult);
            /* clip vector */
            neonScaled = vmaxq_f32(neonScaled, vdupq_n_f32(-2147483648.f));
            neonScaled = vminq_f32(neonScaled, vdupq_n_f32(2147483647.f));
            /* convert vector - rounded towards zero */
            neonResultVector = vcvtq_s32_f32(neonScaled);
            /* write result */
            dest = NeonWriteDestVectorInt24(neonResultVector, dest, destinationStride);
            count-=ARM_NEON_BEST_VECTOR_SIZE;
        }
    }
#endif

    while( count-- )
    {
        /* convert to 32 bit and drop the low 8 bits */
        
        double dither  = PaUtil_GenerateFloatTriangularDither24( ditherGenerator );
        /* use smaller scaler to prevent overflow when we add the dither */
        double dithered = ((double)*src * (2147483646.0)) + dither;
        PA_CLIP_( dithered, -2147483648., 2147483647.  );
        
        temp = (PaInt32) dithered;

#if defined(PA_LITTLE_ENDIAN)
        dest[0] = (unsigned char)(((PaUint32)temp) >> 8);
        dest[1] = (unsigned char)(((PaUint32)temp) >> 16);
        dest[2] = (unsigned char)(((PaUint32)temp) >> 24);
#elif defined(PA_BIG_ENDIAN)
        dest[0] = (unsigned char)(((PaUint32)temp) >> 24);
        dest[1] = (unsigned char)(((PaUint32)temp) >> 16);
        dest[2] = (unsigned char)(((PaUint32)temp) >> 8);
#endif

        src += sourceStride;
        dest += destinationStride * 3;
    }
}

/* -------------------------------------------------------------------------- */

static void Float32_To_Int16(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    float *src = (float*)sourceBuffer;
    PaInt16 *dest =  (PaInt16*)destinationBuffer;
    (void)ditherGenerator; /* unused parameter */

#ifdef __ARM_NEON__
    if(withAcceleration)
    {
        float32x4_t neonSourceVector, neonScaled;
        int32x4_t neonResultVector;
        float32x4_t neonMult = vdupq_n_f32(32767.0f);
        while(count >= ARM_NEON_BEST_VECTOR_SIZE)
        {
            /* get source vector */
            neonSourceVector = NeonGetSourceVector(&src, sourceStride);
            /* scale */
            neonScaled = vmulq_f32(neonSourceVector, neonMult);
            /* convert vector - rounded towards zero */
            neonResultVector = vcvtq_s32_f32(neonScaled);
            /* write result */
            dest = NeonWriteDestVectorInt16(vmovn_s32(neonResultVector), dest, destinationStride);
            count -= ARM_NEON_BEST_VECTOR_SIZE;
        }
    }
#endif

    while( count-- )
    {
#ifdef PA_USE_C99_LRINTF
        float tempf = (*src * (32767.0f)) ;
        *dest = lrintf(tempf-0.5f);
#else
        short samp = (short) (*src * (32767.0f));
        *dest = samp;
#endif

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Float32_To_Int16_Dither(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    float *src = (float*)sourceBuffer;
    PaInt16 *dest = (PaInt16*)destinationBuffer;

#ifdef __ARM_NEON__
    if(withAcceleration)
    {
        float32x4_t neonSourceVector, neonScaled, neonDither;
        int32x4_t neonResultVector;
        float32x4_t neonMult = vdupq_n_f32(32766.0f);
        while(count >= ARM_NEON_BEST_VECTOR_SIZE)
        {
            /* get source vector */
            neonSourceVector = NeonGetSourceVector(&src, sourceStride);
            /* get dither */
            neonDither = PaUtil_GenerateFloatTriangularDitherVector(
                ditherGenerator, const_float_dither_scale_);
            /* scale vector + add dither vmla(a,b,c) <-> a+b*c */
            neonScaled = vmlaq_f32(neonDither, neonSourceVector, neonMult);
            /* convert vector - rounded towards zero */
            neonResultVector = vcvtq_s32_f32(neonScaled);
            /* write result */
            dest = NeonWriteDestVectorInt16(vmovn_s32(neonResultVector), dest, destinationStride);
            count -= ARM_NEON_BEST_VECTOR_SIZE;
        }
    }
#endif

    while( count-- )
    {

        float dither  = PaUtil_GenerateFloatTriangularDither( ditherGenerator );
        /* use smaller scaler to prevent overflow when we add the dither */
        float dithered = (*src * (32766.0f)) + dither;

#ifdef PA_USE_C99_LRINTF
        *dest = lrintf(dithered-0.5f);
#else
        *dest = (PaInt16) dithered;
#endif

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Float32_To_Int16_Clip(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    float *src = (float*)sourceBuffer;
    PaInt16 *dest =  (PaInt16*)destinationBuffer;
    (void)ditherGenerator; /* unused parameter */

#ifdef __ARM_NEON__
    if(withAcceleration)
    {
        float32x4_t neonSourceVector, neonScaled;
        int32x4_t neonResultVector;
        float32x4_t neonMult = vdupq_n_f32(32767.0f);
        while(count >= ARM_NEON_BEST_VECTOR_SIZE)
        {
            /* get source vector */
            neonSourceVector = NeonGetSourceVector(&src, sourceStride);
            /* scale */
            neonScaled = vmulq_f32(neonSourceVector, neonMult);
            /* convert vector - rounded towards zero */
            neonResultVector = vcvtq_s32_f32(neonScaled);
            /* write clipped result */
            dest = NeonWriteDestVectorInt16(vqmovn_s32(neonResultVector), dest, destinationStride);
            count -= ARM_NEON_BEST_VECTOR_SIZE;
        }
    }
#endif

    while( count-- )
    {
#ifdef PA_USE_C99_LRINTF
        long samp = lrintf((*src * (32767.0f)) -0.5f);
#else
        long samp = (PaInt32) (*src * (32767.0f));
#endif
        PA_CLIP_( samp, -0x8000, 0x7FFF );
        *dest = (PaInt16) samp;

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Float32_To_Int16_DitherClip(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    float *src = (float*)sourceBuffer;
    PaInt16 *dest =  (PaInt16*)destinationBuffer;
    (void)ditherGenerator; /* unused parameter */

#ifdef __ARM_NEON__
    if(withAcceleration)
    {
        float32x4_t neonSourceVector, neonScaled, neonDither;
        int32x4_t neonResultVector;
        float32x4_t neonMult = vdupq_n_f32(32766.0f);
        while(count >= ARM_NEON_BEST_VECTOR_SIZE)
        {
            /* get source vector */
            neonSourceVector = NeonGetSourceVector(&src, sourceStride);
            /* get dither */
            neonDither = PaUtil_GenerateFloatTriangularDitherVector(
                ditherGenerator, const_float_dither_scale_);
            /* scale vector + add dither vmla(a,b,c) <-> a+b*c */
            neonScaled = vmlaq_f32(neonDither, neonSourceVector, neonMult);
            /* convert vector - rounded towards zero */
            neonResultVector = vcvtq_s32_f32(neonScaled);
            /* write clipped result */
            dest = NeonWriteDestVectorInt16(vqmovn_s32(neonResultVector), dest, destinationStride);
            count -= ARM_NEON_BEST_VECTOR_SIZE;
        }
    }
#endif

    while( count-- )
    {

        float dither  = PaUtil_GenerateFloatTriangularDither( ditherGenerator );
        /* use smaller scaler to prevent overflow when we add the dither */
        float dithered = (*src * (32766.0f)) + dither;
        PaInt32 samp = (PaInt32) dithered;
        PA_CLIP_( samp, -0x8000, 0x7FFF );
#ifdef PA_USE_C99_LRINTF
        *dest = lrintf(samp-0.5f);
#else
        *dest = (PaInt16) samp;
#endif

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Float32_To_Int8(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    float *src = (float*)sourceBuffer;
    signed char *dest =  (signed char*)destinationBuffer;
    (void)ditherGenerator; /* unused parameter */

    while( count-- )
    {
        signed char samp = (signed char) (*src * (127.0f));
        *dest = samp;

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Float32_To_Int8_Dither(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    float *src = (float*)sourceBuffer;
    signed char *dest =  (signed char*)destinationBuffer;
    
    while( count-- )
    {
        float dither  = PaUtil_GenerateFloatTriangularDither( ditherGenerator );
        /* use smaller scaler to prevent overflow when we add the dither */
        float dithered = (*src * (126.0f)) + dither;
        PaInt32 samp = (PaInt32) dithered;
        *dest = (signed char) samp;

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Float32_To_Int8_Clip(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    float *src = (float*)sourceBuffer;
    signed char *dest =  (signed char*)destinationBuffer;
    (void)ditherGenerator; /* unused parameter */

    while( count-- )
    {
        PaInt32 samp = (PaInt32)(*src * (127.0f));
        PA_CLIP_( samp, -0x80, 0x7F );
        *dest = (signed char) samp;

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Float32_To_Int8_DitherClip(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    float *src = (float*)sourceBuffer;
    signed char *dest =  (signed char*)destinationBuffer;
    (void)ditherGenerator; /* unused parameter */

    while( count-- )
    {
        float dither  = PaUtil_GenerateFloatTriangularDither( ditherGenerator );
        /* use smaller scaler to prevent overflow when we add the dither */
        float dithered = (*src * (126.0f)) + dither;
        PaInt32 samp = (PaInt32) dithered;
        PA_CLIP_( samp, -0x80, 0x7F );
        *dest = (signed char) samp;

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Float32_To_UInt8(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    float *src = (float*)sourceBuffer;
    unsigned char *dest =  (unsigned char*)destinationBuffer;
    (void)ditherGenerator; /* unused parameter */

    while( count-- )
    {
        unsigned char samp = (unsigned char)(128 + ((unsigned char) (*src * (127.0f))));
        *dest = samp;

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Float32_To_UInt8_Dither(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    float *src = (float*)sourceBuffer;
    unsigned char *dest =  (unsigned char*)destinationBuffer;
    
    while( count-- )
    {
        float dither  = PaUtil_GenerateFloatTriangularDither( ditherGenerator );
        /* use smaller scaler to prevent overflow when we add the dither */
        float dithered = (*src * (126.0f)) + dither;
        PaInt32 samp = (PaInt32) dithered;
        *dest = (unsigned char) (128 + samp);
        
        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Float32_To_UInt8_Clip(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    float *src = (float*)sourceBuffer;
    unsigned char *dest =  (unsigned char*)destinationBuffer;
    (void)ditherGenerator; /* unused parameter */

    while( count-- )
    {
        PaInt32 samp = 128 + (PaInt32)(*src * (127.0f));
        PA_CLIP_( samp, 0x0000, 0x00FF );
        *dest = (unsigned char) samp;

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Float32_To_UInt8_DitherClip(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    float *src = (float*)sourceBuffer;
    unsigned char *dest =  (unsigned char*)destinationBuffer;
    (void)ditherGenerator; /* unused parameter */

    while( count-- )
    {
        float dither  = PaUtil_GenerateFloatTriangularDither( ditherGenerator );
        /* use smaller scaler to prevent overflow when we add the dither */
        float dithered = (*src * (126.0f)) + dither;
        PaInt32 samp = 128 + (PaInt32) dithered;
        PA_CLIP_( samp, 0x0000, 0x00FF );
        *dest = (unsigned char) samp;

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Int32_To_Float32(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    PaInt32 *src = (PaInt32*)sourceBuffer;
    float *dest =  (float*)destinationBuffer;
    (void)ditherGenerator; /* unused parameter */

    while( count-- )
    {
        *dest = (float) ((double)*src * const_1_div_2147483648_);

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Int32_To_Int24(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    PaUint32 *src    = (PaInt32*)sourceBuffer;
    unsigned char *dest = (unsigned char*)destinationBuffer;
    (void) ditherGenerator; /* unused parameter */
    
	while( count-- )
    {
		/* REVIEW */
#if defined(PA_LITTLE_ENDIAN)
        dest[0] = (unsigned char)(*src >> 8);
        dest[1] = (unsigned char)(*src >> 16);
        dest[2] = (unsigned char)(*src >> 24);
#elif defined(PA_BIG_ENDIAN)
        dest[0] = (unsigned char)(*src >> 24);
        dest[1] = (unsigned char)(*src >> 16);
        dest[2] = (unsigned char)(*src >> 8);
#endif
        src += sourceStride;
        dest += destinationStride * 3;
    }
}

/* -------------------------------------------------------------------------- */

static void Int32_To_Int24_Dither(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    (void) destinationBuffer; /* unused parameters */
    (void) destinationStride; /* unused parameters */
    (void) sourceBuffer; /* unused parameters */
    (void) sourceStride; /* unused parameters */
    (void) count; /* unused parameters */
    (void) ditherGenerator; /* unused parameters */
    /* IMPLEMENT ME */
}

/* -------------------------------------------------------------------------- */

static void Int32_To_Int16(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    PaInt32 *src = (PaInt32*)sourceBuffer;
    PaInt16 *dest =  (PaInt16*)destinationBuffer;
    (void)ditherGenerator; /* unused parameter */

    while( count-- )
    {
        *dest = (PaInt16) ((*src) >> 16);

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Int32_To_Int16_Dither(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    PaInt32 *src = (PaInt32*)sourceBuffer;
    PaInt16 *dest =  (PaInt16*)destinationBuffer;
    PaInt32 dither;

    while( count-- )
    {
        /* REVIEW */
        dither = PaUtil_Generate16BitTriangularDither( ditherGenerator );
        *dest = (PaInt16) ((((*src)>>1) + dither) >> 15);

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Int32_To_Int8(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    PaInt32 *src = (PaInt32*)sourceBuffer;
    signed char *dest =  (signed char*)destinationBuffer;
    (void)ditherGenerator; /* unused parameter */

    while( count-- )
    {
        *dest = (signed char) ((*src) >> 24);

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Int32_To_Int8_Dither(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    PaInt32 *src = (PaInt32*)sourceBuffer;
    signed char *dest =  (signed char*)destinationBuffer;
    PaInt32 dither;

    while( count-- )
    {
        /* REVIEW */
        dither = PaUtil_Generate16BitTriangularDither( ditherGenerator );
        *dest = (signed char) ((((*src)>>1) + dither) >> 23);

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Int32_To_UInt8(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    PaInt32 *src = (PaInt32*)sourceBuffer;
    unsigned char *dest =  (unsigned char*)destinationBuffer;
    (void)ditherGenerator; /* unused parameter */

    while( count-- )
    {
		(*dest) = (unsigned char)(((*src) >> 24) + 128); 

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Int32_To_UInt8_Dither(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    PaInt32 *src = (PaInt32*)sourceBuffer;
    unsigned char *dest =  (unsigned char*)destinationBuffer;
    (void)ditherGenerator; /* unused parameter */

    while( count-- )
    {
        /* IMPLEMENT ME */

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Int24_To_Float32(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    unsigned char *src = (unsigned char*)sourceBuffer;
    float *dest = (float*)destinationBuffer;
    PaInt32 temp;

    (void) ditherGenerator; /* unused parameter */
    
    while( count-- )
    {

#if defined(PA_LITTLE_ENDIAN)
        temp = (((PaInt32)src[0]) << 8);  
        temp = temp | (((PaInt32)src[1]) << 16);
        temp = temp | (((PaInt32)src[2]) << 24);
#elif defined(PA_BIG_ENDIAN)
        temp = (((PaInt32)src[0]) << 24);
        temp = temp | (((PaInt32)src[1]) << 16);
        temp = temp | (((PaInt32)src[2]) << 8);
#endif

        *dest = (float) ((double)temp * const_1_div_2147483648_);

        src += sourceStride * 3;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Int24_To_Int32(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    unsigned char *src  = (unsigned char*)sourceBuffer;
    PaInt32 *dest = (PaInt32*)  destinationBuffer;
    PaInt32 temp;

    (void) ditherGenerator; /* unused parameter */
    
    while( count-- )
    {

#if defined(PA_LITTLE_ENDIAN)
        temp = (((PaInt32)src[0]) << 8);  
        temp = temp | (((PaInt32)src[1]) << 16);
        temp = temp | (((PaInt32)src[2]) << 24);
#elif defined(PA_BIG_ENDIAN)
        temp = (((PaInt32)src[0]) << 24);
        temp = temp | (((PaInt32)src[1]) << 16);
        temp = temp | (((PaInt32)src[2]) << 8);
#endif

        *dest = temp;

        src += sourceStride * 3;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Int24_To_Int16(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    unsigned char *src = (unsigned char*)sourceBuffer;
    PaInt16 *dest = (PaInt16*)destinationBuffer;
    
    PaInt16 temp;

    (void) ditherGenerator; /* unused parameter */
        
    while( count-- )
    {
		
#if defined(PA_LITTLE_ENDIAN)
		/* src[0] is discarded */
        temp = (((PaInt16)src[1]));
        temp = temp | (PaInt16)(((PaInt16)src[2]) << 8);
#elif defined(PA_BIG_ENDIAN)
		/* src[2] is discarded */
        temp = (PaInt16)(((PaInt16)src[0]) << 8);
        temp = temp | (((PaInt16)src[1]));
#endif

        *dest = temp;

        src += sourceStride * 3;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Int24_To_Int16_Dither(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    unsigned char *src = (unsigned char*)sourceBuffer;
    PaInt16 *dest = (PaInt16*)destinationBuffer;

    PaInt32 temp, dither;

    while( count-- )
    {

#if defined(PA_LITTLE_ENDIAN)
        temp = (((PaInt32)src[0]) << 8);  
        temp = temp | (((PaInt32)src[1]) << 16);
        temp = temp | (((PaInt32)src[2]) << 24);
#elif defined(PA_BIG_ENDIAN)
        temp = (((PaInt32)src[0]) << 24);
        temp = temp | (((PaInt32)src[1]) << 16);
        temp = temp | (((PaInt32)src[2]) << 8);
#endif

        /* REVIEW */
        dither = PaUtil_Generate16BitTriangularDither( ditherGenerator );
        *dest = (PaInt16) (((temp >> 1) + dither) >> 15);

        src  += sourceStride * 3;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Int24_To_Int8(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    unsigned char *src = (unsigned char*)sourceBuffer;
    signed char  *dest = (signed char*)destinationBuffer;
    
    (void) ditherGenerator; /* unused parameter */
        
    while( count-- )
    {	
	
#if defined(PA_LITTLE_ENDIAN)
		/* src[0] is discarded */
		/* src[1] is discarded */
        *dest = src[2];
#elif defined(PA_BIG_ENDIAN)
		/* src[2] is discarded */
		/* src[1] is discarded */
		*dest = src[0];
#endif

        src += sourceStride * 3;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Int24_To_Int8_Dither(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    unsigned char *src = (unsigned char*)sourceBuffer;
    signed char  *dest = (signed char*)destinationBuffer;
    
    PaInt32 temp, dither;

    while( count-- )
    {

#if defined(PA_LITTLE_ENDIAN)
        temp = (((PaInt32)src[0]) << 8);  
        temp = temp | (((PaInt32)src[1]) << 16);
        temp = temp | (((PaInt32)src[2]) << 24);
#elif defined(PA_BIG_ENDIAN)
        temp = (((PaInt32)src[0]) << 24);
        temp = temp | (((PaInt32)src[1]) << 16);
        temp = temp | (((PaInt32)src[2]) << 8);
#endif

        /* REVIEW */
        dither = PaUtil_Generate16BitTriangularDither( ditherGenerator );
        *dest = (signed char) (((temp >> 1) + dither) >> 23);

        src += sourceStride * 3;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Int24_To_UInt8(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    unsigned char *src = (unsigned char*)sourceBuffer;
    unsigned char *dest = (unsigned char*)destinationBuffer;
    
    (void) ditherGenerator; /* unused parameter */
        
    while( count-- )
    {
		
#if defined(PA_LITTLE_ENDIAN)
		/* src[0] is discarded */
		/* src[1] is discarded */
        *dest = (unsigned char)(src[2] + 128);
#elif defined(PA_BIG_ENDIAN)
        *dest = (unsigned char)(src[0] + 128);
		/* src[1] is discarded */
		/* src[2] is discarded */		
#endif

        src += sourceStride * 3;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Int24_To_UInt8_Dither(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    (void) destinationBuffer; /* unused parameters */
    (void) destinationStride; /* unused parameters */
    (void) sourceBuffer; /* unused parameters */
    (void) sourceStride; /* unused parameters */
    (void) count; /* unused parameters */
    (void) ditherGenerator; /* unused parameters */
    /* IMPLEMENT ME */
}

/* -------------------------------------------------------------------------- */

static void Int16_To_Float32(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    PaInt16 *src = (PaInt16*)sourceBuffer;
    float *dest =  (float*)destinationBuffer;
    (void)ditherGenerator; /* unused parameter */

    while( count-- )
    {
        float samp = *src * const_1_div_32768_; /* FIXME: i'm concerned about this being asymetrical with float->int16 -rb */
        *dest = samp;

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Int16_To_Int32(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    PaInt16 *src = (PaInt16*)sourceBuffer;
    PaInt32 *dest =  (PaInt32*)destinationBuffer;
    (void)ditherGenerator; /* unused parameter */

    while( count-- )
    {
        /* REVIEW: we should consider something like
            (*src << 16) | (*src & 0xFFFF)
        */
        
        *dest = *src << 16;

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Int16_To_Int24(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    PaUint16 *src   = (PaUint16*) sourceBuffer;
    unsigned char *dest = (unsigned char*)destinationBuffer;
    PaInt16 temp;

    (void) ditherGenerator; /* unused parameter */
    
    while( count-- )
    {
        temp = *src;
        
#if defined(PA_LITTLE_ENDIAN)
        dest[0] = 0;
        dest[1] = (unsigned char)(temp);
        dest[2] = (unsigned char)(temp >> 8);
#elif defined(PA_BIG_ENDIAN)
        dest[0] = (unsigned char)(temp >> 8);
        dest[1] = (unsigned char)(temp);
        dest[2] = 0;
#endif

        src += sourceStride;
        dest += destinationStride * 3;
    }
}

/* -------------------------------------------------------------------------- */

static void Int16_To_Int8(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    PaInt16 *src = (PaInt16*)sourceBuffer;
    signed char *dest =  (signed char*)destinationBuffer;
    (void)ditherGenerator; /* unused parameter */

    while( count-- )
    {
        (*dest) = (signed char)((*src) >> 8);

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Int16_To_Int8_Dither(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    PaInt16 *src = (PaInt16*)sourceBuffer;
    signed char *dest =  (signed char*)destinationBuffer;
    (void)ditherGenerator; /* unused parameter */

    while( count-- )
    {
        /* IMPLEMENT ME */

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Int16_To_UInt8(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    PaInt16 *src = (PaInt16*)sourceBuffer;
    unsigned char *dest =  (unsigned char*)destinationBuffer;
    (void)ditherGenerator; /* unused parameter */

    while( count-- )
    {
		(*dest) = (unsigned char)(((*src) >> 8) + 128); 

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Int16_To_UInt8_Dither(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    PaInt16 *src = (PaInt16*)sourceBuffer;
    unsigned char *dest =  (unsigned char*)destinationBuffer;
    (void)ditherGenerator; /* unused parameter */

    while( count-- )
    {
        /* IMPLEMENT ME */

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Int8_To_Float32(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    signed char *src = (signed char*)sourceBuffer;
    float *dest =  (float*)destinationBuffer;
    (void)ditherGenerator; /* unused parameter */

    while( count-- )
    {
        float samp = *src * const_1_div_128_;
        *dest = samp;

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Int8_To_Int32(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    signed char *src = (signed char*)sourceBuffer;
    PaInt32 *dest =  (PaInt32*)destinationBuffer;
    (void)ditherGenerator; /* unused parameter */

    while( count-- )
    {
		(*dest) = (*src) << 24;

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Int8_To_Int24(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    signed char *src = (signed char*)sourceBuffer;
    unsigned char *dest =  (unsigned char*)destinationBuffer;
    (void)ditherGenerator; /* unused parameter */

    while( count-- )
    {

#if defined(PA_LITTLE_ENDIAN)
        dest[0] = 0;
        dest[1] = 0;
        dest[2] = (*src);
#elif defined(PA_BIG_ENDIAN)
        dest[0] = (*src);
        dest[1] = 0;
        dest[2] = 0;
#endif

        src += sourceStride;
        dest += destinationStride * 3;
    }
}

/* -------------------------------------------------------------------------- */

static void Int8_To_Int16(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    signed char *src = (signed char*)sourceBuffer;
    PaInt16 *dest =  (PaInt16*)destinationBuffer;
    (void)ditherGenerator; /* unused parameter */

    while( count-- )
    {
        (*dest) = (PaInt16)((*src) << 8);

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Int8_To_UInt8(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    signed char *src = (signed char*)sourceBuffer;
    unsigned char *dest =  (unsigned char*)destinationBuffer;
    (void)ditherGenerator; /* unused parameter */

    while( count-- )
    {
        (*dest) = (unsigned char)(*src + 128);

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void UInt8_To_Float32(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    unsigned char *src = (unsigned char*)sourceBuffer;
    float *dest =  (float*)destinationBuffer;
    (void)ditherGenerator; /* unused parameter */

    while( count-- )
    {
        float samp = (*src - 128) * const_1_div_128_;
        *dest = samp;

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void UInt8_To_Int32(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    unsigned char *src = (unsigned char*)sourceBuffer;
    PaInt32 *dest = (PaInt32*)destinationBuffer;
    (void)ditherGenerator; /* unused parameter */

    while( count-- )
    {
		(*dest) = (*src - 128) << 24;

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void UInt8_To_Int24(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
	unsigned char *src  = (unsigned char*)sourceBuffer;
    unsigned char *dest = (unsigned char*)destinationBuffer;
    (void) ditherGenerator; /* unused parameters */
    
	while( count-- )
    {

#if defined(PA_LITTLE_ENDIAN)
        dest[0] = 0;
        dest[1] = 0;
        dest[2] = (unsigned char)(*src - 128);
#elif defined(PA_BIG_ENDIAN)
        dest[0] = (unsigned char)(*src - 128);
        dest[1] = 0;
        dest[2] = 0;
#endif
		
        src += sourceStride;
        dest += destinationStride * 3;    
	}
}

/* -------------------------------------------------------------------------- */

static void UInt8_To_Int16(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    unsigned char *src = (unsigned char*)sourceBuffer;
    PaInt16 *dest =  (PaInt16*)destinationBuffer;
    (void)ditherGenerator; /* unused parameter */

    while( count-- )
    {
        (*dest) = (PaInt16)((*src - 128) << 8);

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void UInt8_To_Int8(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    unsigned char *src = (unsigned char*)sourceBuffer;
    signed char  *dest = (signed char*)destinationBuffer;
    (void)ditherGenerator; /* unused parameter */

    while( count-- )
    {
        (*dest) = (signed char)(*src - 128);

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Copy_8_To_8(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    unsigned char *src = (unsigned char*)sourceBuffer;
    unsigned char *dest = (unsigned char*)destinationBuffer;
                                                      
    (void) ditherGenerator; /* unused parameter */

    while( count-- )
    {
        *dest = *src;

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Copy_16_To_16(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    PaUint16 *src = (PaUint16 *)sourceBuffer;
    PaUint16 *dest = (PaUint16 *)destinationBuffer;
                                                        
    (void) ditherGenerator; /* unused parameter */
    
    while( count-- )
    {
        *dest = *src;

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Copy_24_To_24(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    unsigned char *src = (unsigned char*)sourceBuffer;
    unsigned char *dest = (unsigned char*)destinationBuffer;

    (void) ditherGenerator; /* unused parameter */
    
    while( count-- )
    {
        dest[0] = src[0];
        dest[1] = src[1];
        dest[2] = src[2];

        src += sourceStride * 3;
        dest += destinationStride * 3;
    }
}

/* -------------------------------------------------------------------------- */

static void Copy_32_To_32(
    void *destinationBuffer, signed int destinationStride,
    void *sourceBuffer, signed int sourceStride,
    unsigned int count, struct PaUtilTriangularDitherGenerator *ditherGenerator )
{
    PaUint32 *dest = (PaUint32 *)destinationBuffer;
    PaUint32 *src = (PaUint32 *)sourceBuffer;

    (void) ditherGenerator; /* unused parameter */
    
    while( count-- )
    {
        *dest = *src;

        src += sourceStride;
        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

PaUtilConverterTable paConverters = {
    Float32_To_Int32,              /* PaUtilConverter *Float32_To_Int32; */
    Float32_To_Int32_Dither,       /* PaUtilConverter *Float32_To_Int32_Dither; */
    Float32_To_Int32_Clip,         /* PaUtilConverter *Float32_To_Int32_Clip; */
    Float32_To_Int32_DitherClip,   /* PaUtilConverter *Float32_To_Int32_DitherClip; */

    Float32_To_Int24,              /* PaUtilConverter *Float32_To_Int24; */
    Float32_To_Int24_Dither,       /* PaUtilConverter *Float32_To_Int24_Dither; */
    Float32_To_Int24_Clip,         /* PaUtilConverter *Float32_To_Int24_Clip; */
    Float32_To_Int24_DitherClip,   /* PaUtilConverter *Float32_To_Int24_DitherClip; */
    
    Float32_To_Int16,              /* PaUtilConverter *Float32_To_Int16; */
    Float32_To_Int16_Dither,       /* PaUtilConverter *Float32_To_Int16_Dither; */
    Float32_To_Int16_Clip,         /* PaUtilConverter *Float32_To_Int16_Clip; */
    Float32_To_Int16_DitherClip,   /* PaUtilConverter *Float32_To_Int16_DitherClip; */

    Float32_To_Int8,               /* PaUtilConverter *Float32_To_Int8; */
    Float32_To_Int8_Dither,        /* PaUtilConverter *Float32_To_Int8_Dither; */
    Float32_To_Int8_Clip,          /* PaUtilConverter *Float32_To_Int8_Clip; */
    Float32_To_Int8_DitherClip,    /* PaUtilConverter *Float32_To_Int8_DitherClip; */

    Float32_To_UInt8,              /* PaUtilConverter *Float32_To_UInt8; */
    Float32_To_UInt8_Dither,       /* PaUtilConverter *Float32_To_UInt8_Dither; */
    Float32_To_UInt8_Clip,         /* PaUtilConverter *Float32_To_UInt8_Clip; */
    Float32_To_UInt8_DitherClip,   /* PaUtilConverter *Float32_To_UInt8_DitherClip; */

    Int32_To_Float32,              /* PaUtilConverter *Int32_To_Float32; */
    Int32_To_Int24,                /* PaUtilConverter *Int32_To_Int24; */
    Int32_To_Int24_Dither,         /* PaUtilConverter *Int32_To_Int24_Dither; */
    Int32_To_Int16,                /* PaUtilConverter *Int32_To_Int16; */
    Int32_To_Int16_Dither,         /* PaUtilConverter *Int32_To_Int16_Dither; */
    Int32_To_Int8,                 /* PaUtilConverter *Int32_To_Int8; */
    Int32_To_Int8_Dither,          /* PaUtilConverter *Int32_To_Int8_Dither; */
    Int32_To_UInt8,                /* PaUtilConverter *Int32_To_UInt8; */
    Int32_To_UInt8_Dither,         /* PaUtilConverter *Int32_To_UInt8_Dither; */

    Int24_To_Float32,              /* PaUtilConverter *Int24_To_Float32; */
    Int24_To_Int32,                /* PaUtilConverter *Int24_To_Int32; */
    Int24_To_Int16,                /* PaUtilConverter *Int24_To_Int16; */
    Int24_To_Int16_Dither,         /* PaUtilConverter *Int24_To_Int16_Dither; */
    Int24_To_Int8,                 /* PaUtilConverter *Int24_To_Int8; */
    Int24_To_Int8_Dither,          /* PaUtilConverter *Int24_To_Int8_Dither; */
    Int24_To_UInt8,                /* PaUtilConverter *Int24_To_UInt8; */
    Int24_To_UInt8_Dither,         /* PaUtilConverter *Int24_To_UInt8_Dither; */

    Int16_To_Float32,              /* PaUtilConverter *Int16_To_Float32; */
    Int16_To_Int32,                /* PaUtilConverter *Int16_To_Int32; */
    Int16_To_Int24,                /* PaUtilConverter *Int16_To_Int24; */
    Int16_To_Int8,                 /* PaUtilConverter *Int16_To_Int8; */
    Int16_To_Int8_Dither,          /* PaUtilConverter *Int16_To_Int8_Dither; */
    Int16_To_UInt8,                /* PaUtilConverter *Int16_To_UInt8; */
    Int16_To_UInt8_Dither,         /* PaUtilConverter *Int16_To_UInt8_Dither; */

    Int8_To_Float32,               /* PaUtilConverter *Int8_To_Float32; */
    Int8_To_Int32,                 /* PaUtilConverter *Int8_To_Int32; */
    Int8_To_Int24,                 /* PaUtilConverter *Int8_To_Int24 */
    Int8_To_Int16,                 /* PaUtilConverter *Int8_To_Int16; */
    Int8_To_UInt8,                 /* PaUtilConverter *Int8_To_UInt8; */

    UInt8_To_Float32,              /* PaUtilConverter *UInt8_To_Float32; */
    UInt8_To_Int32,                /* PaUtilConverter *UInt8_To_Int32; */
    UInt8_To_Int24,                /* PaUtilConverter *UInt8_To_Int24; */
    UInt8_To_Int16,                /* PaUtilConverter *UInt8_To_Int16; */
    UInt8_To_Int8,                 /* PaUtilConverter *UInt8_To_Int8; */

    Copy_8_To_8,                   /* PaUtilConverter *Copy_8_To_8; */
    Copy_16_To_16,                 /* PaUtilConverter *Copy_16_To_16; */
    Copy_24_To_24,                 /* PaUtilConverter *Copy_24_To_24; */
    Copy_32_To_32                  /* PaUtilConverter *Copy_32_To_32; */
};

/* -------------------------------------------------------------------------- */

#endif /* PA_NO_STANDARD_CONVERTERS */

/* -------------------------------------------------------------------------- */

PaUtilZeroer* PaUtil_SelectZeroer( PaSampleFormat destinationFormat )
{
    switch( destinationFormat & ~paNonInterleaved ){
    case paFloat32:
        return paZeroers.Zero32;
    case paInt32:
        return paZeroers.Zero32;
    case paInt24:
        return paZeroers.Zero24;
    case paInt16:
        return paZeroers.Zero16;
    case paInt8:
        return paZeroers.Zero8;
    case paUInt8:
        return paZeroers.ZeroU8;
    default: return 0;
    }
}

/* -------------------------------------------------------------------------- */

#ifdef PA_NO_STANDARD_ZEROERS

/* -------------------------------------------------------------------------- */

PaUtilZeroerTable paZeroers = {
    0,  /* PaUtilZeroer *ZeroU8; */
    0,  /* PaUtilZeroer *Zero8; */
    0,  /* PaUtilZeroer *Zero16; */
    0,  /* PaUtilZeroer *Zero24; */
    0,  /* PaUtilZeroer *Zero32; */
};

/* -------------------------------------------------------------------------- */

#else /* PA_NO_STANDARD_ZEROERS is not defined */

/* -------------------------------------------------------------------------- */

static void ZeroU8( void *destinationBuffer, signed int destinationStride,
        unsigned int count )
{
    unsigned char *dest = (unsigned char*)destinationBuffer;

    while( count-- )
    {
        *dest = 128;

        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Zero8( void *destinationBuffer, signed int destinationStride,
        unsigned int count )
{
    unsigned char *dest = (unsigned char*)destinationBuffer;

    while( count-- )
    {
        *dest = 0;

        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Zero16( void *destinationBuffer, signed int destinationStride,
        unsigned int count )
{
    PaUint16 *dest = (PaUint16 *)destinationBuffer;

    while( count-- )
    {
        *dest = 0;

        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

static void Zero24( void *destinationBuffer, signed int destinationStride,
        unsigned int count )
{
    unsigned char *dest = (unsigned char*)destinationBuffer;

    while( count-- )
    {
        dest[0] = 0;
        dest[1] = 0;
        dest[2] = 0;

        dest += destinationStride * 3;
    }
}

/* -------------------------------------------------------------------------- */

static void Zero32( void *destinationBuffer, signed int destinationStride,
        unsigned int count )
{
    PaUint32 *dest = (PaUint32 *)destinationBuffer;

    while( count-- )
    {
        *dest = 0;

        dest += destinationStride;
    }
}

/* -------------------------------------------------------------------------- */

PaUtilZeroerTable paZeroers = {
    ZeroU8,  /* PaUtilZeroer *ZeroU8; */
    Zero8,  /* PaUtilZeroer *Zero8; */
    Zero16,  /* PaUtilZeroer *Zero16; */
    Zero24,  /* PaUtilZeroer *Zero24; */
    Zero32,  /* PaUtilZeroer *Zero32; */
};

/* -------------------------------------------------------------------------- */

#endif /* PA_NO_STANDARD_ZEROERS */
