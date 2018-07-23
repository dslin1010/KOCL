/* This work is licensed under the terms of the GNU GPL, version 2.  See
 * the GPL-COPYING file in the top-level directory.
 *
 * Copyright (c) 2010-2011 University of Utah and the Flux Group.
 * All rights reserved.
 * Copyright (c) 2017-2018 NCKU of Taiwan and the ASRLab.
 */
 
/* __jhash_mix -- mix 3 32-bit values reversibly. */

#define __jhash_mix(a, b, c)                    \
{                                               \
        a -= c;  a ^= rol32(c, 4);  c += b;     \
        b -= a;  b ^= rol32(a, 6);  a += c;     \
        c -= b;  c ^= rol32(b, 8);  b += a;     \
        a -= c;  a ^= rol32(c, 16); c += b;     \
        b -= a;  b ^= rol32(a, 19); a += c;     \
        c -= b;  c ^= rol32(b, 4);  b += a;     \
}

/* __jhash_final - final mixing of 3 32-bit values (a,b,c) into c */
#define __jhash_final(a, b, c)                  \
{                                               \
        c ^= b; c -= rol32(b, 14);              \
        a ^= c; a -= rol32(c, 11);              \
        b ^= a; b -= rol32(a, 25);              \
        c ^= b; c -= rol32(b, 16);              \
        a ^= c; a -= rol32(c, 4);               \
        b ^= a; b -= rol32(a, 14);              \
        c ^= b; c -= rol32(b, 24);              \
}

/* An arbitrary initial parameter */
#define JHASH_INITVAL           0xdeadbeef


static inline unsigned int rol32(unsigned int word, unsigned int shift)
{
        return (word << shift) | (word >> (32 - shift));
}

__kernel void jhash(__global char *k , __global unsigned int *out )
{ 
     int idx=get_global_id(0);

        unsigned int j=0;
	unsigned int a, b, c;
	unsigned int length=1024,initval=17;
        /* Set up the internal state */
        a = b = c = JHASH_INITVAL + (length<<2) + initval;

        /* Handle most of the key */
        while (length > 3) {
                a += k[idx*1024+0+j];
                b += k[idx*1024+1+j];
                c += k[idx*1024+2+j];
                __jhash_mix(a, b, c);
                length -= 3;
                //k = k+3+tid*1024;
		j+=3;
        }
        
        /* Handle the last 3 u32's: all the case statements fall through */
        switch (length) {
        case 3: c += k[idx*1024+2+j];
        case 2: b += k[idx*1024+1+j];
        case 1: a += k[idx*1024+0+j];
                __jhash_final(a, b, c);
        case 0: /* Nothing left to add */
                break;
        }
	out[idx] = c ; 
	barrier(CLK_GLOBAL_MEM_FENCE);
}

