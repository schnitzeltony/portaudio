#ifndef PA_DITHER_H
#define PA_DITHER_H
/*
 * $Id$
 * Portable Audio I/O Library triangular dither generator
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

 @brief Functions for generating dither noise
*/

#include "pa_types.h"


#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

/* Note that the linear congruential algorithm requires 32 bit integers
 * because it uses arithmetic overflow. So use PaUint32 instead of
 * unsigned long so it will work on 64 bit systems.
 */

/* Must be multiple of 4 !! */
#define DITHER_BUFF_SIZE 32768

/** @brief State needed to generate a dither signal */
typedef struct PaUtilTriangularDitherGenerator{
    PaUint32 previous;
    PaUint32 randSeed1;
    PaUint32 randSeed2;
    PaUint32 posInAccelBuff;
    PaInt16 AccelBuff[DITHER_BUFF_SIZE];
} PaUtilTriangularDitherGenerator;


/** @brief Initialize dither state */
void PaUtil_InitializeTriangularDitherState( PaUtilTriangularDitherGenerator *ditherState );


/**
 @brief Calculate 2 LSB dither signal with a triangular distribution.
 Ranged for adding to a 1 bit right-shifted 32 bit integer
 prior to >>15. eg:
<pre>
    signed long in = *
    signed long dither = PaUtil_Generate16BitTriangularDither( ditherState );
    signed short out = (signed short)(((in>>1) + dither) >> 15);
</pre>
 @return
 A signed 32-bit integer with a range of +32767 to -32768
*/
PaInt32 PaUtil_Generate16BitTriangularDither( PaUtilTriangularDitherGenerator *ditherState );


/**
 @brief Calculate 2 LSB dither signal with a triangular distribution.
 Ranged for adding to a pre-scaled float.
<pre>
    float in = *
    float dither = PaUtil_GenerateFloatTriangularDither( ditherState );
    // use smaller scaler to prevent overflow when we add the dither
    signed short out = (signed short)(in*(32766.0f) + dither );
</pre>
 @return
 A float with a range of -2.0 to +1.99999.
*/
float PaUtil_GenerateFloatTriangularDither( PaUtilTriangularDitherGenerator *ditherState );



/* Note that the linear congruential algorithm requires 32 bit integers
 * because it uses arithmetic overflow. So use PaUint32 instead of
 * unsigned long so it will work on 64 bit systems.
 */

#define PA_DITHER_BITS_   (15)

/* Multiply by PA_FLOAT_DITHER_SCALE_ to get a float between -2.0 and +1.99999 */
#define PA_FLOAT_DITHER_SCALE_  (1.0f / ((1<<PA_DITHER_BITS_)-1))
static const float const_float_dither_scale_ = PA_FLOAT_DITHER_SCALE_;

#define DITHER_SHIFT_  ((sizeof(PaInt32)*8 - PA_DITHER_BITS_) + 1)


#ifdef __ARM_NEON__
static inline float32x4_t PaUtil_GenerateFloatTriangularDitherVector( PaUtilTriangularDitherGenerator *state)
{
    int16x4_t neonDither16 = vld1_s16(state->AccelBuff + state->posInAccelBuff);
    if(state->posInAccelBuff >= DITHER_BUFF_SIZE - ARM_NEON_BEST_VECTOR_SIZE)
        state->posInAccelBuff = 0;
    else
        state->posInAccelBuff += ARM_NEON_BEST_VECTOR_SIZE;
    return vmulq_n_f32(vcvtq_f32_s32(vmovl_s16(neonDither16)), const_float_dither_scale_);
}
#endif


#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* PA_DITHER_H */
