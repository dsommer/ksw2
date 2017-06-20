#include <stdio.h> // for debugging only
#include "ksw2.h"

typedef struct { int32_t h, e; } eh_t;

void ksw_extz(void *km, int qlen, const uint8_t *query, int tlen, const uint8_t *target, int8_t m, const int8_t *mat, int8_t gapo, int8_t gape, int w, int zdrop, int flag, ksw_extz_t *ez)
{
	eh_t *eh;
	int8_t *qp; // query profile
	int32_t i, j, k, max_j = 0, gapoe = gapo + gape, n_col, *off = 0, with_cigar = !(flag&KSW_EZ_NO_CIGAR);
	uint8_t *z = 0; // backtrack matrix; in each cell: f<<4|e<<2|h; in principle, we can halve the memory, but backtrack will be more complex

	ez->max_q = ez->max_t = ez->mqe_t = ez->mte_q = -1;
	ez->max = ez->mqe = ez->mte = KSW_NEG_INF;
	ez->n_cigar = 0;

	// allocate memory
	n_col = qlen < 2*w+1? qlen : 2*w+1; // maximum #columns of the backtrack matrix
	qp = (int8_t*)kmalloc(km, qlen * m);
	eh = (eh_t*)kcalloc(km, qlen + 1, 8);
	if (with_cigar) {
		z = (uint8_t*)kmalloc(km, (size_t)n_col * tlen);
		off = (int32_t*)kcalloc(km, tlen, 4);
	}

	// generate the query profile
	for (k = i = 0; k < m; ++k) {
		const int8_t *p = &mat[k * m];
		for (j = 0; j < qlen; ++j) qp[i++] = p[query[j]];
	}

	// fill the first row
	eh[0].h = 0, eh[0].e = -gapoe - gapoe;
	for (j = 1; j <= qlen && j <= w; ++j)
		eh[j].h = -(gapoe + gape * j), eh[j].e = -(gapoe + gapoe + gape * j);
	for (; j <= qlen; ++j) eh[j].h = eh[j].e = KSW_NEG_INF; // everything is -inf outside the band

	// DP loop
	for (i = 0; i < tlen; ++i) { // target sequence is in the outer loop
		int32_t f, h1, st, en, max = KSW_NEG_INF;
		int8_t *q = &qp[target[i] * qlen];
		st = i > w? i - w : 0;
		en = i + w < qlen - 1? i + w : qlen - 1;
		h1 = st > 0? KSW_NEG_INF : -(gapoe + gape * i);
		f  = st > 0? KSW_NEG_INF : -(gapoe + gapoe + gape * i);
		if (!with_cigar) {
			for (j = st; j <= en; ++j) {
				eh_t *p = &eh[j];
				int32_t h = p->h, e = p->e;
				p->h = h1;
				h += q[j];
				h = h >= e? h : e;
				h = h >= f? h : f;
				h1 = h;
				max_j = max > h? max_j : j;
				max   = max > h? max   : h;
				h -= gapoe;
				e -= gape;
				e  = e > h? e : h;
				p->e = e;
				f -= gape;
				f  = f > h? f : h;
			}
		} else if (!(flag&KSW_EZ_RIGHT)) {
			uint8_t *zi = &z[(long)i * n_col];
			off[i] = st;
			for (j = st; j <= en; ++j) {
				eh_t *p = &eh[j];
				int32_t h = p->h, e = p->e;
				uint8_t d; // direction
				p->h = h1;
				h += q[j];
				d = h >= e? 0 : 1;
				h = h >= e? h : e;
				d = h >= f? d : 2;
				h = h >= f? h : f;
				h1 = h;
				max_j = max > h? max_j : j;
				max   = max > h? max   : h;
				h -= gapoe;
				e -= gape;
				d |= e > h? 1<<2 : 0;
				e  = e > h? e    : h;
				p->e = e;
				f -= gape;
				d |= f > h? 2<<4 : 0; // if we want to halve the memory, use one bit only, instead of two
				f  = f > h? f    : h;
				zi[j - st] = d; // z[i,j] keeps h for the current cell and e/f for the next cell
			}
		} else {
			uint8_t *zi = &z[(long)i * n_col];
			off[i] = st;
			for (j = st; j <= en; ++j) {
				eh_t *p = &eh[j];
				int32_t h = p->h, e = p->e;
				uint8_t d; // direction
				p->h = h1;
				h += q[j];
				d = h > e? 0 : 1;
				h = h > e? h : e;
				d = h > f? d : 2;
				h = h > f? h : f;
				h1 = h;
				max_j = max >= h? max_j : j;
				max   = max >= h? max   : h;
				h -= gapoe;
				e -= gape;
				d |= e >= h? 1<<2 : 0;
				e  = e >= h? e    : h;
				p->e = e;
				f -= gape;
				d |= f >= h? 2<<4 : 0; // if we want to halve the memory, use one bit only, instead of two
				f  = f >= h? f    : h;
				zi[j - st] = d; // z[i,j] keeps h for the current cell and e/f for the next cell
			}
		}
		eh[j].h = h1, eh[j].e = KSW_NEG_INF;
		// update ez
		if (en == qlen - 1 && eh[qlen].h > ez->mqe)
			ez->mqe = eh[qlen].h, ez->mqe_t = i;
		if (i == tlen - 1)
			ez->mte = max, ez->mte_q = max_j;
		if (max > ez->max) {
			ez->max = max, ez->max_t = i, ez->max_q = max_j;
		} else if (max_j > ez->max_q) {
			int tl = i - ez->max_t, ql = max_j - ez->max_q, l;
			l = tl > ql? tl - ql : ql - tl;
			if (ez->max - max > zdrop + l * gape)
				break;
		}
		if (i == tlen - 1 && en == qlen - 1)
			ez->score = eh[qlen].h;
	}
	kfree(km, qp); kfree(km, eh);
	if (with_cigar) {
		if (ez->score > KSW_NEG_INF) ksw_backtrack(km, 0, z, off, n_col, tlen-1, qlen-1, &ez->m_cigar, &ez->n_cigar, &ez->cigar);
		else ksw_backtrack(km, 1, z, off, n_col, ez->max_t, ez->max_q, &ez->m_cigar, &ez->n_cigar, &ez->cigar);
		kfree(km, z); kfree(km, off);
	}
}