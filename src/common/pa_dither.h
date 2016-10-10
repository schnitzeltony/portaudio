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

/** @brief State needed to generate a dither signal */
typedef struct PaUtilTriangularDitherGenerator{
    PaUint32 previous;
#ifndef __ARM_NEON__
    PaUint32 randSeed1[1];
    PaUint32 randSeed2[1];
#else
    PaUint32 randSeed1[ARM_NEON_BEST_VECTOR_SIZE];
    PaUint32 randSeed2[ARM_NEON_BEST_VECTOR_SIZE]
#endif
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
    uint32x4_t neonOffset = vdupq_n_u32(907633515);
    uint32x4_t neonMult   = vdupq_n_u32(196314165);
    uint32x4_t neonRandSeed;;
    /* was
    state->randSeed1 = (state->randSeed1[0] * 196314165) + 907633515;
    state->randSeed2 = (state->randSeed2[0] * 196314165) + 907633515;
     */
    /* Seed1 Generate two random numbers. vmla(a,b,c) <-> a+b*c */
    neonRandSeed = vmlaq_u32(neonOffset, vld1q_u32(state->randSeed1), neonMult);
    /* Generate triangular distribution about 0.
     * Shift before adding to prevent overflow which would skew the distribution.
     * Also shift an extra bit for the high pass filter.
     */
    /* was
    current = (((PaInt32)state->randSeed1[0])>>DITHER_SHIFT_) +
              (((PaInt32)state->randSeed2[0])>>DITHER_SHIFT_);
     */
    /* cast to signed and shift */
    int32x4_t neonRandSeedSigned1 = vshrq_n_s32(vreinterpretq_s32_u32(neonRandSeed), DITHER_SHIFT_);
    /* each lane must have full set of calculations */
    neonRandSeed = vmlaq_u32(neonOffset, neonRandSeed, neonMult);
    neonRandSeed = vmlaq_u32(neonOffset, neonRandSeed, neonMult);
    neonRandSeed = vmlaq_u32(neonOffset, neonRandSeed, neonMult);
    vst1q_u32(state->randSeed1, neonRandSeed);


    /* Seed2 Generate two random numbers. vmla(a,b,c) <-> a+b*c */
    neonRandSeed = vmlaq_u32(neonOffset, vld1q_u32(state->randSeed2), neonMult);
    /* cast to signed and shift */
    int32x4_t neonRandSeedSigned2 = vshrq_n_s32(vreinterpretq_s32_u32(neonRandSeed), DITHER_SHIFT_);
    /* each lane must have full run of calculations */
    neonRandSeed = vmlaq_u32(neonOffset, neonRandSeed, neonMult);
    neonRandSeed = vmlaq_u32(neonOffset, neonRandSeed, neonMult);
    neonRandSeed = vmlaq_u32(neonOffset, neonRandSeed, neonMult);
    vst1q_u32(state->randSeed2, neonRandSeed);

    /* calc shifted randSeed sum*/
    int32x4_t neonCurrent = vaddq_s32(neonRandSeedSigned1, neonRandSeedSigned2);

    /* High pass filter to reduce audibility. */
    /*highPass = current - state->previous;
    state->previous = current;
    return highPass;*/
    /* unrolling this is quite simple: move current one position right and fill topmost with
     * previous. Substract this vector from current => highPass vector
     */
    int32x4_t neonPrev = vextq_s32(neonCurrent, neonCurrent, 3);
    /* load old prev scalar into most left lane */
    neonPrev = vld1q_lane_s32(&state->previous, neonPrev, 0);
    /* store most right lane of current to state->previous */
    vst1q_lane_u32(&state->previous, vreinterpretq_u32_s32(neonCurrent), ARM_NEON_BEST_VECTOR_SIZE-1);
    /* sub curr - prev / convert to float / scale -> out */
    return vmulq_n_f32(vcvtq_f32_s32(vqsubq_s32(neonCurrent, neonPrev)), const_float_dither_scale_);
}
#endif


#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* PA_DITHER_H */
