/***************************************************************************
*   Copyright (C) 2016 PCSX4ALL Team                                      *
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
*   This program is distributed in the hope that it will be useful,       *
*   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
*   GNU General Public License for more details.                          *
*                                                                         *
*   You should have received a copy of the GNU General Public License     *
*   along with this program; if not, write to the                         *
*   Free Software Foundation, Inc.,                                       *
*   51 Franklin Street, Fifth Floor, Boston, MA 02111-1307 USA.           *
***************************************************************************/

#ifndef _OP_DITHER_ARM_H_
#define _OP_DITHER_ARM_H_

////////////////////////////////////////////////////////////////////////////////
// Convert padded u32 5.4:5.4:5.4 bgr fixed-pt triplet to final bgr555 color,
//  applying dithering if specified by template parameter.
//
// INPUT:
//     'uSrc24' input: 000bbbbbXXXX0gggggXXXX0rrrrrXXXX
//                     ^ bit 31
//       'pDst' is a pointer to destination framebuffer pixel, used
//         to determine which DitherMatrix[] entry to apply.
// RETURNS:
//         u16 output: 0bbbbbgggggrrrrr
//                     ^ bit 16
// Where 'X' are fixed-pt bits, '0' is zero-padding, and '-' is don't care
////////////////////////////////////////////////////////////////////////////////
template <int DITHER>
GPU_INLINE u16 gpuColorQuantization24ARM(u32 uSrc24, const u16 *pDst)
{
	if (DITHER)
		{
			u16 fbpos, offset;
			u32 omsk = 0x20080200;

			// u16 fbpos  = (u32)(pDst - gpu_unai.vram);
			// u16 offset = ((fbpos & (0x7 << 10)) >> 7) | (fbpos & 0x7);

			asm ("sub %[fbpos],  %[pDst],  %[vram] \n\t" // fbpos = pDst - gpu_unai.vram
			     "and %[offset], %[fbpos], #0x3800 \n\t" // offset = ... & 0x7 << 10 (+ compensate for 16-bit pointers)
			     "and %[fbpos],  %[fbpos], #0x0E   \n\t" // fbpos &= 0x7 (+ compensate for 16-bit pointers)
			     "orr %[offset], %[fbpos], %[offset], lsr #0x07 \n\t"  // offset = offset >> 7 | fbpos
			     : [offset] "=r" (offset), [fbpos] "=&r" (fbpos)
			     : [pDst] "r" (pDst), [vram] "r" (gpu_unai.vram));

			// //clean overflow flags and add
			// uSrc24 = (uSrc24 & 0x1FF7FDFF) + gpu_unai.DitherMatrix[offset];

			asm ("ldr %[offset], [%[dm], %[offset], lsl #0x01] \n\t" // gpu_unai.DitherMatrix[offset]
			     "bic %[uSrc24], %[uSrc24], %[omsk]   \n\t" // uSrc24 & 0x1FF7FDFF
			     "add %[uSrc24], %[uSrc24], %[offset] \n\t" // ... + ...
			     : [uSrc24] "+r" (uSrc24), [offset] "+&r" (offset)
			     : [omsk] "r" (omsk), [dm] "r" (gpu_unai.DitherMatrix));

			// if (uSrc24 & (1<< 9)) uSrc24 |= (0x1FF    );
			// if (uSrc24 & (1<<19)) uSrc24 |= (0x1FF<<10);
			// if (uSrc24 & (1<<29)) uSrc24 |= (0x1FF<<20);

			// Cap components using a fast-blending-like strategy
			asm ("and %[omsk], %[omsk], %[src]   \n\t" // Test for overflow (AND with high bits)
			     "sub %[omsk], %[omsk], %[omsk], lsr #0x09 \n\t" // For each component, 0x1FF if overflowed, 0x0 if not
			     "orr %[src],  %[src],  %[omsk]  \n\t" // Saturate overflowed components to 0x1FF
			     : [src] "+r" (uSrc24), [omsk] "+r" (omsk));
		}

	u32 tmp, dst;

	// return ((uSrc24>> 4) & (0x1F    ))
	//      | ((uSrc24>> 9) & (0x1F<<5 ))
	//      | ((uSrc24>>14) & (0x1F<<10));
	                                                     // in  is 000bbbbbXXXX0gggggXXXX0rrrrrXXXX
	asm ("and %[tmp], %[uSrc24], #0x7C000          \n\t" // tmp is 0000000000000ggggg00000000000000
	     "and %[dst], %[uSrc24], #0x1F0            \n\t" // dst is 00000000000000000000000rrrrr0000
	     "orr %[dst], %[dst],    %[tmp], lsr #0x05 \n\t" // dst is 000000000000000000gggggrrrrr0000
	     "and %[tmp], %[uSrc24], #0x1F000000       \n\t" // tmp is 000bbbbb000000000000000000000000
	     "lsr %[dst], %[dst],    #0x04             \n\t" // dst is 0000000000000000000000gggggrrrrr
	     "orr %[dst], %[dst],    %[tmp], lsr #0x0E \n\t" // dst is 00000000000000000bbbbbgggggrrrrr
	     : [tmp] "=&r" (tmp), [dst] "=&r" (dst)
	     : [uSrc24] "r" (uSrc24));

	return dst;
}

#endif //_OP_DITHER_ARM_H_
