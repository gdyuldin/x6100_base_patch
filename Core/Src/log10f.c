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
Based on:

		log10(x) = log10((1+m) * (2^n))
		log(x) = n * log10(2) + log10(1 + m)
		log(1+m) = Poly(1+m)

		where Poly(x) is the Minimax approximation of log10(x) over the
		range [1, 2]

Test func : log10f(x)
Test Range: 1 < x < 10000
Peak Error:	~0.000040%
RMS  Error: ~0.000008%
*/

const float __log10f_rng =  0.3010299957f;

const float __log10f_lut[8] = {
	-0.99697286229624, 		//p0
	-1.07301643912502, 		//p4
	-2.46980061535534, 		//p2
	-0.07176870463131, 		//p6
	2.247870219989470, 		//p1
	0.366547581117400, 		//p5
	1.991005185100089, 		//p3
	0.006135635201050,		//p7
};

inline __attribute__((always_inline)) float log10f_c(float x)
{
	float a, b, c, d, xx;
	int m;

	union {
		float   f;
		int 	i;
	} r;

	//extract exponent
	r.f = x;
	m = (r.i >> 23);
	m = m - 127;
	r.i = r.i - (m << 23);

	//Taylor Polynomial (Estrins)
	xx = r.f * r.f;
	a = (__log10f_lut[4] * r.f) + (__log10f_lut[0]);
	b = (__log10f_lut[6] * r.f) + (__log10f_lut[2]);
	c = (__log10f_lut[5] * r.f) + (__log10f_lut[1]);
	d = (__log10f_lut[7] * r.f) + (__log10f_lut[3]);
	a = a + b * xx;
	c = c + d * xx;
	xx = xx * xx;
	r.f = a + c * xx;

	//add exponent
	r.f = r.f + ((float) m) * __log10f_rng;

	return r.f;
}
