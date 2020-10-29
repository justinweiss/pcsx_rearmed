#ifndef _OP_BLEND_ARM_H_
#define _OP_BLEND_ARM_H_

////////////////////////////////////////////////////////////////////////////////
// Blend bgr555 color in 'uSrc' (foreground) with bgr555 color
//  in 'uDst' (background), returning resulting color.
//
// INPUT:
//  'uSrc','uDst' input: -bbbbbgggggrrrrr
//                       ^ bit 16
// OUTPUT:
//           u16 output: 0bbbbbgggggrrrrr
//                       ^ bit 16
// RETURNS:
// Where '0' is zero-padding, and '-' is don't care
////////////////////////////////////////////////////////////////////////////////
template <int BLENDMODE, bool SKIP_USRC_MSB_MASK>
GPU_INLINE uint_fast16_t gpuBlendingARM(uint_fast16_t uSrc, uint_fast16_t uDst)
{
	// These use Blargg's bitwise modulo-clamping:
	//  http://blargg.8bitalley.com/info/rgb_mixing.html
	//  http://blargg.8bitalley.com/info/rgb_clamped_add.html
	//  http://blargg.8bitalley.com/info/rgb_clamped_sub.html

	uint_fast16_t mix;

	// Clear preserved msb
	asm ("bic %[uDst], %[uDst], #0x8000" : [uDst] "+r" (uDst));

	if (BLENDMODE == 3) {
		// Prepare uSrc for blending ((0.25 * uSrc) & (0.25 * mask))
		asm ("and %[uSrc], %[mask], %[uSrc], lsr #0x2" : [uSrc] "+r" (uSrc) : [mask] "r" (0x1ce7));
	} else if (!SKIP_USRC_MSB_MASK) {
		asm ("bic %[uSrc], %[uSrc], #0x8000" : [uSrc] "+r" (uSrc));
	}


	// 0.5 x Back + 0.5 x Forward
	if (BLENDMODE==0) {
		// mix = ((uSrc + uDst) - ((uSrc ^ uDst) & 0x0421)) >> 1;
		asm ("eor %[mix], %[uSrc], %[uDst]\n\t" // uSrc ^ uDst
		     "and %[mix], %[mix], %[mask]\n\t"  // ... & 0x0421
		     "sub %[mix], %[uDst], %[mix]\n\t"  // uDst - ...
		     "add %[mix], %[uSrc], %[mix]\n\t"  // uSrc + ...
		     "mov %[mix], %[mix], lsr #0x1\n\t" // ... >> 1
		     : [mix] "=&r" (mix)
		     : [uSrc] "r" (uSrc), [uDst] "r" (uDst), [mask] "r" (0x0421));
	}

	if (BLENDMODE == 1 || BLENDMODE == 3) {
		// u32 sum      = uSrc + uDst;
		// u32 low_bits = (uSrc ^ uDst) & 0x0421;
		// u32 carries  = (sum - low_bits) & 0x8420;
		// u32 modulo   = sum - carries;
		// u32 clamp    = carries - (carries >> 5);
		// mix = modulo | clamp;

		u32 sum;

		asm ("add %[sum], %[uSrc], %[uDst]\n\t" // sum = uSrc + uDst
		     "eor %[mix], %[uSrc], %[uDst]\n\t" // uSrc ^ uDst
		     "and %[mix], %[mix], %[mask]\n\t"  // low_bits = (... & 0x0421)
		     "sub %[mix], %[sum], %[mix]\n\t"   // sum - low_bits
		     "and %[mix], %[mix], %[mask], lsl #0x05\n\t"  // carries = ... & 0x8420
		     "sub %[sum], %[sum], %[mix] \n\t"  // modulo = sum - carries
		     "sub %[mix], %[mix], %[mix], lsr #0x05\n\t" // clamp = carries - (carries >> 5)
		     "orr %[mix], %[sum], %[mix]"       // mix = modulo | clamp
		     : [sum] "=&r" (sum), [mix] "=&r" (mix)
		     : [uSrc] "r" (uSrc), [uDst] "r" (uDst), [mask] "r" (0x0421));
	}

	// 1.0 x Back - 1.0 x Forward
	if (BLENDMODE==2) {
		u32 diff;
		// u32 diff     = uDst - uSrc + 0x8420;
		// u32 low_bits = (uDst ^ uSrc) & 0x8420;
		// u32 borrows  = (diff - low_bits) & 0x8420;
		// u32 modulo   = diff - borrows;
		// u32 clamp    = borrows - (borrows >> 5);
		// mix = modulo & clamp;
		asm ("sub %[diff], %[uDst], %[uSrc]\n\t"  // uDst - uSrc
		     "add %[diff], %[diff], %[mask]\n\t"  // diff = ... + 0x8420
		     "eor %[mix], %[uDst], %[uSrc]\n\t"   // uDst ^ uSrc
		     "and %[mix], %[mix], %[mask]\n\t"    // low_bits = ... & 0x8420
		     "sub %[mix], %[diff], %[mix]\n\t"    // diff - low_bits
		     "and %[mix], %[mix], %[mask]\n\t"    // borrows = ... & 0x8420
		     "sub %[diff], %[diff], %[mix]\n\t"   // modulo = diff - borrows
		     "sub %[mix], %[mix], %[mix], lsr #0x05\n\t"  // clamp = borrows - (borrows >> 5)
		     "and %[mix], %[diff], %[mix]"        // mix = modulo & clamp
		     : [diff] "=&r" (diff), [mix] "=&r" (mix)
		     : [uSrc] "r" (uSrc), [uDst] "r" (uDst), [mask] "r" (0x8420));
	}

	// There's not a case where we can get into this function,
	// SKIP_USRC_MSB_MASK is false, and the msb of uSrc is unset.
	if (!SKIP_USRC_MSB_MASK) {
		asm ("orr %[mix], %[mix], #0x8000" : [mix] "+r" (mix));
	}

	return mix;
}

////////////////////////////////////////////////////////////////////////////////
// Convert bgr555 color in uSrc to padded u32 5.4:5.4:5.4 bgr fixed-pt
//  color triplet suitable for use with HQ 24-bit quantization.
//
// INPUT:
//       'uDst' input: -bbbbbgggggrrrrr
//                     ^ bit 16
// RETURNS:
//         u32 output: 000bbbbbXXXX0gggggXXXX0rrrrrXXXX
//                     ^ bit 31
// Where 'X' are fixed-pt bits, '0' is zero-padding, and '-' is don't care
////////////////////////////////////////////////////////////////////////////////
GPU_INLINE u32 gpuGetRGB24ARM(uint_fast16_t uSrc)
{
	u32 tmp, out;

	// return ((uSrc & 0x7C00)<<14)
	//      | ((uSrc & 0x03E0)<< 9)
	//      | ((uSrc & 0x001F)<< 4);

	asm ("and    %[out],  %[src], #0x7C00 \n\t"           // out is 00000000000000000bbbbb0000000000
	     "and    %[tmp],  %[src], #0x03E0 \n\t"           // tmp is 0000000000000000000000ggggg00000
	     "orr    %[out],  %[tmp], %[out], lsl #0x05 \n\t" // out is 000000000000bbbbb00000ggggg00000
	     "and    %[src],  %[src], #0x001F \n\t"           // src is 000000000000000000000000000rrrrr
	     "orr    %[out],  %[src], %[out], lsl #0x05 \n\t" // out is 0000000bbbbb00000ggggg00000rrrrr
	     "lsl    %[out],  %[out], #0x04   \n\t"           // out is 000bbbbb00000ggggg00000rrrrr0000
	     : [src] "+&r" (uSrc), [tmp] "=&r" (tmp), [out] "=&r" (out));
	return out;
}


////////////////////////////////////////////////////////////////////////////////
// Blend padded u32 5.4:5.4:5.4 bgr fixed-pt color triplet in 'uSrc24'
//  (foreground color) with bgr555 color in 'uDst' (background color),
//  returning the resulting u32 5.4:5.4:5.4 color.
//
// INPUT:
//     'uSrc24' input: 000bbbbbXXXX0gggggXXXX0rrrrrXXXX
//                     ^ bit 31
//       'uDst' input: -bbbbbgggggrrrrr
//                     ^ bit 16
// RETURNS:
//         u32 output: 000bbbbbXXXX0gggggXXXX0rrrrrXXXX
//                     ^ bit 31
// Where 'X' are fixed-pt bits, '0' is zero-padding, and '-' is don't care
////////////////////////////////////////////////////////////////////////////////
template <int BLENDMODE>
GPU_INLINE u32 gpuBlending24ARM(u32 uSrc24, uint_fast16_t uDst)
{
	// These use techniques adapted from Blargg's techniques mentioned in
	//  in gpuBlending() comments above. Not as much bitwise trickery is
	//  necessary because of presence of 0 padding in uSrc24 format.

	u32 uDst24 = gpuGetRGB24ARM(uDst);
	u32 omsk = 0x20080200;

	// Clear any carries left over flom lighting
	asm ("bic %[src], %[src], %[omsk]" : [src] "+r" (uSrc24) : [omsk] "r" (omsk));

	if (BLENDMODE == 3) {
		// Prepare uSrc for blending ((0.25 * uSrc) & (0.25 * mask))
		asm ("and %[src], %[mask], %[src], lsr #0x2" : [src] "+r" (uSrc24) : [mask] "r" (0x07F1FC7F));
	}

	// 0.5 x Back + 0.5 x Forward
	if (BLENDMODE==0) {
		// const u32 uMsk = 0x1FE7F9FE;
		// // Only need to mask LSBs of uSrc24, uDst24's LSBs are 0 already
		// mix = (uDst24 + (uSrc24 & uMsk)) >> 1;

		asm ("and %[src], %[src], %[mask]\n\t" // uSrc24 & uMsk
		     "add %[src], %[src], %[dst]\n\t"  // uDst24 + ...
		     "lsr %[src], %[src], #0x1\n\t"    // ... >> 1
		     : [src] "+&r" (uSrc24)
		     : [dst] "r" (uDst24), [mask] "r" (0x1FE7F9FE));
	}

	// 1.0 x Back + 1.0 x Forward
	if (BLENDMODE==1 || BLENDMODE==3) {
		// u32 sum     = uSrc24 + uDst24;
		// u32 carries = sum & 0x20080200;
		// u32 modulo  = sum - carries;
		// u32 clamp   = carries - (carries >> 9);
		// mix = modulo | clamp;

		// Some of these steps can be skipped, assuming quantization
		// clears the carry bits.
		asm ("add    %[src],  %[src],    %[dst]   \n\t"   // sum = uSrc24 + uDst24
		     "and    %[omsk], %[omsk],   %[src]   \n\t"   // carries = sum & 0x20080200
		     "sub    %[omsk], %[omsk],   %[omsk], lsr #0x09 \n\t" // clamp = carries - (carries >> 9)
		     "orr    %[src],  %[src],    %[omsk]  \n\t"   // mix = sum | clamp
		     : [src] "+r" (uSrc24), [omsk] "+r" (omsk)
		     : [dst] "r" (uDst24));
	}

	// 1.0 x Back - 1.0 x Forward
	if (BLENDMODE==2) {
		// // Insert ones in 0-padded borrow slot of color to be subtracted from
		// uDst24 |= 0x20080200;
		// u32 diff    = uDst24 - uSrc24;
		// u32 borrows = diff & 0x20080200;
		// u32 clamp   = borrows - (borrows >> 9);
		// mix = diff & clamp;

		asm ("orr    %[dst],  %[dst],   %[omsk]  \n\t" // uDst24 |= 0x20080200
		     "sub    %[src],  %[dst],   %[src]   \n\t" // diff = uDst24 - uSrc24
		     "and    %[omsk], %[omsk],  %[src]   \n\t" // borrows = diff & 0x20080200
		     "sub    %[omsk], %[omsk],  %[omsk], lsr #0x09   \n\t" // clamp = borrows - (borrows >> 9)
		     "and    %[src],  %[src],   %[omsk]  \n\t" // mix = diff & clamp
		     : [src] "+r" (uSrc24), [omsk] "+r" (omsk), [dst] "+r" (uDst24));
	}

	return uSrc24;
}

#endif  //_OP_BLEND_ARM_H_
