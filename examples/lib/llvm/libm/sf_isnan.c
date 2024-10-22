/* sf_c_isnan.c -- float version of s_c_isnan.c.
 */

/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice 
 * is preserved.
 * ====================================================
 */

/*
 * isnanf(x) returns 1 is x is nan, else 0;
 *
 * isnanf is an extension declared in <ieeefp.h> and <math.h>.
 */

#include "fdlibm.h"

int
//_DEFUN (isnanf, (x), float x)
isnanf ( float x)
{
	__int32_t ix;
	GET_FLOAT_WORD(ix,x);
	ix &= 0x7fffffff;
	return FLT_UWORD_IS_NAN(ix);
}

#ifdef _DOUBLE_IS_32BITS

#undef isnan

int
//_DEFUN (isnan, (x), double x)
isnanx(double x)
{
	return isnanf((float) x);
}

#endif /* defined(_DOUBLE_IS_32BITS) */
