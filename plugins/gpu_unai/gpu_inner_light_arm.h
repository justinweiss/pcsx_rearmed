#ifndef _OP_LIGHT_ARM_H_
#define _OP_LIGHT_ARM_H_

////////////////////////////////////////////////////////////////////////////////
// Extract bgr555 color from Gouraud u32 fixed-pt 8.3:8.3:8.2 rgb triplet
//
// INPUT:
//  'gCol' input:  rrrrrrrrXXXggggggggXXXbbbbbbbbXX
//                 ^ bit 31
// RETURNS:
//    u16 output:  0bbbbbgggggrrrrr
//                 ^ bit 16
// Where 'r,g,b' are integer bits of colors, 'X' fixed-pt, and '0' zero
////////////////////////////////////////////////////////////////////////////////
GPU_INLINE uint_fast16_t gpuLightingRGBARM(u32 gCol)
{
	uint_fast16_t out = 0x03E0; // don't need the mask after starting to write output
	u32 tmp;

	asm ("and %[tmp], %[gCol], %[out]\n\t"              // tmp holds 0x000000bbbbb00000
	     "and %[out], %[out],  %[gCol], lsr #0x0B\n\t"  // out holds 0x000000ggggg00000
	     "orr %[tmp], %[out],  %[tmp],  lsl #0x05\n\t"  // tmp holds 0x0bbbbbggggg00000
	     "orr %[out], %[tmp],  %[gCol], lsr #0x1B\n\t"  // out holds 0x0bbbbbgggggrrrrr
	     : [out] "+&r" (out), [tmp] "=&r" (tmp)
	     : [gCol] "r"  (gCol)
	     );

	return out;
}

////////////////////////////////////////////////////////////////////////////////
// Convert packed Gouraud u32 fixed-pt 8.3:8.3:8.2 rgb triplet in 'gCol'
//  to padded u32 5.4:5.4:5.4 bgr fixed-pt triplet, suitable for use
//  with HQ 24-bit lighting/quantization.
//
// INPUT:
//       'gCol' input:  rrrrrrrrXXXggggggggXXXbbbbbbbbXX
//                      ^ bit 31
// RETURNS:
//         u32 output:  000bbbbbXXXX0gggggXXXX0rrrrrXXXX
//                      ^ bit 31
//  Where 'X' are fixed-pt bits, '0' zero-padding, and '-' is don't care
////////////////////////////////////////////////////////////////////////////////
GPU_INLINE u32 gpuLightingRGB24ARM(u32 gCol)
{
	u32 tmp1, tmp2, msk = 0x1FF;
	/* return ((gCol<<19) & (0x1FF<<20)) | */
	/*        ((gCol>> 2) & (0x1FF<<10)) | */
	/*         (gCol>>23); */
	                                                     // in   is rrrrrrrrXXXggggggggXXXbbbbbbbbXX
	asm ("and %[tmp1], %[gCol], %[msk],  lsl #0x01 \n\t" // tmp1 is 0000000000000000000000bbbbbbbbb0
	     "and %[tmp2], %[gCol], %[msk],  lsl #0x0C \n\t" // tmp2 is 00000000000ggggggggg000000000000
	     "orr %[tmp2], %[tmp2], %[tmp1], lsl #0x15 \n\t" // tmp2 is 0bbbbbbbbb0ggggggggg000000000000
	     "lsr %[gCol], %[gCol], #0x17 \n\t"              // gCol is 00000000000000000000000rrrrrrrrr
	     "orr %[gCol], %[gCol], %[tmp2], lsr #0x02 \n\t" // gCol is 000bbbbbbbbb0ggggggggg0rrrrrrrrr
	     : [tmp1] "=&r" (tmp1), [tmp2] "=&r" (tmp2), [gCol] "+&r" (gCol)
	     : [msk] "r" (msk));
	return gCol;
}

////////////////////////////////////////////////////////////////////////////////
// Apply fast (low-precision) 5-bit lighting to bgr555 texture color:
//
// INPUT:
//	  'r5','g5','b5' are unsigned 5-bit color values, value of 15
//	    is midpoint that doesn't modify that component of texture
//	  'uSrc' input:	 mbbbbbgggggrrrrr
//			 ^ bit 16
// RETURNS:
//	    u16 output:	 mbbbbbgggggrrrrr
// Where 'X' are fixed-pt bits.
////////////////////////////////////////////////////////////////////////////////
GPU_INLINE uint_fast16_t gpuLightingTXTARM(uint_fast16_t uSrc, u8 r5, u8 g5, u8 b5)
{
	uint_fast16_t out = 0x03E0;
	u32 db, dg;

	// Using `g` for src, `G` for dest
	asm ("and    %[dg],  %[out],    %[src]  \n\t"             // dg holds 0x000000ggggg00000
	     "orr    %[dg],  %[dg],     %[g5]   \n\t"             // dg holds 0x000000gggggGGGGG
	     "and    %[db],  %[out],    %[src], lsr #0x05 \n\t"   // db holds 0x000000bbbbb00000
	     "ldrb   %[dg],  [%[lut],   %[dg]]  \n\t"             // dg holds result 0x00000000000ggggg
	     "and    %[out], %[out],    %[src], lsl #0x05 \n\t"   // out holds 0x000000rrrrr00000
	     "orr    %[out], %[out],    %[r5]   \n\t"             // out holds 0x000000rrrrrRRRRR
	     "orr    %[db],  %[db],     %[b5]   \n\t"             // db holds 0x000000bbbbbBBBBB
	     "ldrb   %[out], [%[lut],   %[out]] \n\t"             // out holds result 0x00000000000rrrrr
	     "ldrb   %[db],  [%[lut],   %[db]]  \n\t"             // db holds result 0x00000000000bbbbb
	     "tst    %[src], #0x8000\n\t"                         // check whether msb was set on uSrc
	     "orr    %[out], %[out],    %[dg],  lsl #0x05   \n\t" // out holds 0x000000gggggrrrrr
	     "orrne  %[out], %[out],    #0x8000\n\t"              // add msb to out if set on uSrc
	     "orr    %[out], %[out],    %[db],  lsl #0x0A   \n\t" // out holds 0xmbbbbbgggggrrrrr
	     : [out] "=&r" (out), [db] "=&r" (db), [dg] "=&r" (dg)
	     : [r5] "r" (r5), [g5] "r" (g5),  [b5] "r" (b5),
	       [lut] "r" (gpu_unai.LightLUT), [src] "r" (uSrc), "0" (out)
	     : "cc");
	return out;
}

////////////////////////////////////////////////////////////////////////////////
// Apply fast (low-precision) 5-bit Gouraud lighting to bgr555 texture color:
//
// INPUT:
//  'gCol' is a packed Gouraud u32 fixed-pt 8.3:8.3:8.2 rgb triplet, value of
//     15.0 is midpoint that does not modify color of texture
//	   gCol input :	 rrrrrXXXXXXgggggXXXXXXbbbbbXXXXX
//			 ^ bit 31
//	  'uSrc' input:	 mbbbbbgggggrrrrr
//			 ^ bit 16
// RETURNS:
//	    u16 output:	 mbbbbbgggggrrrrr
// Where 'X' are fixed-pt bits, '0' is zero-padding, and '-' is don't care
////////////////////////////////////////////////////////////////////////////////
GPU_INLINE uint_fast16_t gpuLightingTXTGouraudARM(uint_fast16_t uSrc, u32 gCol)
{
	uint_fast16_t out = 0x03E0; // don't need the mask after starting to write output
	u32 db,dg,gtmp;

	// Using `g` for src, `G` for dest
	asm ("and    %[dg],  %[out],  %[src]   \n\t"           // dg holds 0x000000ggggg00000
	     "and    %[gtmp],%[out],  %[gCol], lsr #0x0B \n\t" // gtmp holds 0x000000GGGGG00000
	     "and    %[db],  %[out],  %[src],  lsr #0x05 \n\t" // db holds 0x000000bbbbb00000
	     "orr    %[dg],  %[dg],   %[gtmp], lsr #0x05 \n\t" // dg holds 0x000000gggggGGGGG
	     "and    %[gtmp],%[out],  %[gCol]  \n\t"           // gtmp holds 0x000000BBBBB00000
	     "ldrb   %[dg],  [%[lut], %[dg]]   \n\t"           // dg holds result 0x00000000000ggggg
	     "and    %[out], %[out],  %[src],  lsl #0x05 \n\t" // out holds 0x000000rrrrr00000
	     "orr    %[out], %[out],  %[gCol], lsr #0x1B \n\t" // out holds 0x000000rrrrrRRRRR
	     "orr    %[db],  %[db],   %[gtmp], lsr #0x05 \n\t" // db holds 0x000000bbbbbBBBBB
	     "ldrb   %[out], [%[lut], %[out]]  \n\t"           // out holds result 0x00000000000rrrrr
	     "ldrb   %[db],  [%[lut], %[db]]   \n\t"           // db holds result 0x00000000000bbbbb
	     "tst    %[src], #0x8000\n\t"                      // check whether msb was set on uSrc
	     "orr    %[out], %[out],  %[dg],   lsl #0x05 \n\t" // out holds 0x000000gggggrrrrr
	     "orrne  %[out], %[out],  #0x8000\n\t"             // add msb to out if set on uSrc
	     "orr    %[out], %[out],  %[db],   lsl #0x0A \n\t" // out holds 0xmbbbbbgggggrrrrr
	     : [out] "=&r" (out), [db] "=&r" (db), [dg] "=&r" (dg),
	       [gtmp] "=&r" (gtmp) \
	     : [gCol] "r" (gCol), [lut] "r" (gpu_unai.LightLUT), "0" (out), [src] "r" (uSrc)
	     : "cc");

	return out;
}

////////////////////////////////////////////////////////////////////////////////
// Apply high-precision 8-bit lighting to bgr555 texture color,
//  returning a padded u32 5.4:5.4:5.4 bgr fixed-pt triplet
//  suitable for use with HQ 24-bit lighting/quantization.
//
// INPUT:
//        'r8','g8','b8' are unsigned 8-bit color component values, value of
//          127 is midpoint that doesn't modify that component of texture
//
//         uSrc input: -bbbbbgggggrrrrr
//                     ^ bit 16
// RETURNS:
//         u32 output: 000bbbbbXXXX0gggggXXXX0rrrrrXXXX
//                     ^ bit 31
// Where 'X' are fixed-pt bits, '0' is zero-padding, and '-' is don't care
////////////////////////////////////////////////////////////////////////////////
GPU_INLINE u32 gpuLightingTXT24ARM(uint_fast16_t uSrc, u8 r8, u8 g8, u8 b8)
{
	uint_fast16_t msk = 0x20080200;
	u32 db, dg;

	                                                       // src is 00000000000000000bbbbbgggggrrrrr
	asm ("and    %[db],  %[src],    #0x7C00 \n\t"          // db  is 00000000000000000bbbbb0000000000
	     "and    %[dg],  %[src],    #0x03E0 \n\t"          // dg  is 0000000000000000000000ggggg00000
	     "smulbb %[db],  %[db],     %[b8]   \n\t"          // db  is 000000000BBBBBBBBBBBBB0000000000
	     "smulbb %[dg],  %[dg],     %[g8]   \n\t"          // dg  is 00000000000000GGGGGGGGGGGGG00000
	     "and    %[src], %[src],    #0x001F \n\t"          // src is 000000000000000000000000000rrrrr
	     "lsr    %[db],  %[db],     #0x0D   \n\t"          // db  is 0000000000000000000000BBBBBBBBBB
	     "smulbb %[src], %[src],    %[r8]   \n\t"          // src is 0000000000000000000RRRRRRRRRRRRR
	     "lsr    %[dg],  %[dg],     #0x08   \n\t"          // dg  is 0000000000000000000000GGGGGGGGGG
	     "orr    %[dg],  %[dg],     %[db], lsl #0x0A \n\t" // dg  is 000000000000BBBBBBBBBBGGGGGGGGGG
	     "lsr    %[src], %[src],    #0x03   \n\t"          // src is 0000000000000000000000RRRRRRRRRR
	     "orr    %[src], %[src],    %[dg], lsl #0x0A \n\t" // src is 00BBBBBBBBBBGGGGGGGGGGRRRRRRRRRR
	     : [src] "+&r" (uSrc), [db] "=&r" (db), [dg] "=&r" (dg)
	     : [r8] "r" (r8), [g8] "r" (g8),  [b8] "r" (b8));

	// Cap components using a fast-blending-like strategy
	asm ("and    %[msk], %[msk],   %[src]   \n\t"          // Test for overflow (AND with high bits)
	     "sub    %[msk], %[msk],   %[msk], lsr #0x09 \n\t" // For each component, 0x1FF if overflowed
	     "orr    %[src], %[src],   %[msk]   \n\t"          // Saturate overflowed components to 0x1FF
	     : [src] "+r" (uSrc), [msk] "+r" (msk));

	// Assume either blending or quantization will clear the high bits
	return uSrc;
}

////////////////////////////////////////////////////////////////////////////////
// Apply high-precision 8-bit lighting to bgr555 texture color in 'uSrc',
//  returning a padded u32 5.4:5.4:5.4 bgr fixed-pt triplet
//  suitable for use with HQ 24-bit lighting/quantization.
//
// INPUT:
//       'uSrc' input: -bbbbbgggggrrrrr
//                     ^ bit 16
//       'gCol' input: rrrrrrrrXXXggggggggXXXbbbbbbbbXX
//                     ^ bit 31
// RETURNS:
//         u32 output: 000bbbbbXXXX0gggggXXXX0rrrrrXXXX
//                     ^ bit 31
// Where 'X' are fixed-pt bits, '0' is zero-padding, and '-' is don't care
////////////////////////////////////////////////////////////////////////////////
GPU_INLINE uint_fast16_t gpuLightingTXT24GouraudARM(uint_fast16_t uSrc, u32 gCol)
{
	u32 db,dg,gtmp;
	u32 gmsk = 0x00FF, omsk = 0x20080200;
                                                           // src  is 00000000000000000bbbbbgggggrrrrr
	asm ("and    %[db],   %[src],   #0x7C00 \n\t"            // db   is 00000000000000000bbbbb0000000000
	     "and    %[gtmp], %[gmsk],  %[gCol], lsr #0x02 \n\t" // gtmp is 000000000000000000000000bbbbbbbb
	     "and    %[dg],   %[src],   #0x03E0 \n\t"            // src  is 00000000000000000bbbbbgggggrrrrr
	     "smulbb %[db],   %[db],    %[gtmp] \n\t"            // db   is 000000000BBBBBBBBBBBBB0000000000
	     "and    %[gtmp], %[gmsk],  %[gCol], lsr #0x0D \n\t" // gtmp is 000000000000000000000000gggggggg
	     "and    %[src],  %[src],   #0x001F \n\t"            // src  is 000000000000000000000000000rrrrr
	     "smulbb %[dg],   %[dg],    %[gtmp] \n\t"            // dg   is 00000000000000GGGGGGGGGGGGG00000
	     "and    %[gtmp], %[gmsk],  %[gCol], lsr #0x18 \n\t" // gtmp is 000000000000000000000000rrrrrrrr
	     "lsr    %[db],   %[db],    #0x0D   \n\t"            // db   is 0000000000000000000000BBBBBBBBBB
	     "smulbb %[src],  %[src],   %[gtmp] \n\t"            // src  is 0000000000000000000RRRRRRRRRRRRR
	     "lsr    %[dg],   %[dg],    #0x08   \n\t"            // dg   is 0000000000000000000000GGGGGGGGGG
	     "orr    %[dg],   %[dg],    %[db],   lsl #0x0A \n\t" // dg   is 000000000000BBBBBBBBBBGGGGGGGGGG
	     "lsr    %[src],  %[src],   #0x03   \n\t"            // src  is 0000000000000000000000RRRRRRRRRR
	     "orr    %[src],  %[src],   %[dg],   lsl #0x0A \n\t" // src  is 00BBBBBBBBBBGGGGGGGGGGRRRRRRRRRR
	     : [src] "+&r" (uSrc), [db] "=&r" (db), [dg] "=&r" (dg), [gtmp] "=&r" (gtmp)
	     : [gmsk] "r" (gmsk), [gCol] "r" (gCol));

	// Cap components using a fast-blending-like strategy
	asm ("and    %[omsk], %[omsk], %[src]  \n\t"            // Test for overflow (AND with high bits)
	     "sub    %[omsk], %[omsk], %[omsk], lsr #0x09 \n\t" // For each component, 0x1FF if overflowed
	     "orr    %[src], %[src],   %[omsk] \n\t"            // Saturate overflowed components to 0x1FF
	     : [src] "+r" (uSrc), [omsk] "+r" (omsk));

	// Assume either blending or quantization will clear the high bits
	return uSrc;
}

#endif  //_OP_LIGHT_ARM_H_
