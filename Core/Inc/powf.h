#ifndef __POWF_H
#define __POWF_H

/*
Math-NEON:  Neon Optimised Math Library based on cmath
Contact:    lachlan.ts@gmail.com
Copyright (C) 2009  Lachlan Tychsen - Smith aka Adventus

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 3 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/*
Based on x ^ n = exp(n * log(x))

Test func : powf(x, n)
Test Range: (1,1) < (x, n) < (10, 10)
Peak Error:	~0.0010%
RMS  Error: ~0.0002%
*/


float powf_c(float x, float n);


float powf10_c(float n);

#endif // __POWF_H
