/* Copyright 2014. The Regents of the University of California.
 * Copyright 2016. Martin Uecker.
 * All rights reserved. Use of this source code is governed by
 * a BSD-style license which can be found in the LICENSE file.
 *
 * Authors: 
 * 2014-2016 Martin Uecker <martin.uecker@med.uni-goettingen.de>
 */
 
#include <complex.h>
#include <assert.h>
#include <strings.h>

#include "num/multind.h"
#include "num/flpmath.h"

#include "linops/linop.h"

#include "misc/misc.h"

#include "grad.h"






void grad_op(unsigned int D, const long dims[D], unsigned int flags, complex float* out, const complex float* in)
{
	unsigned int N = bitcount(flags);

	assert(N == dims[D - 1]);	// we use the highest dim to store our different partial derivatives

	unsigned int flags2 = flags;

	for (unsigned int i = 0; i < N; i++) {

		unsigned int lsb = ffs(flags2) - 1;
		flags2 = MD_CLEAR(flags2, lsb);

		md_zfdiff(D - 1, dims, lsb, out + i * md_calc_size(D - 1, dims), in);
	}

	assert(0 == flags2);
}


void grad_adjoint(unsigned int D, const long dims[D], unsigned int flags, complex float* out, const complex float* in)
{
	unsigned int N = bitcount(flags);

	assert(N == dims[D - 1]);	// we use the highest dim to store our different partial derivatives

	unsigned int flags2 = flags;

	complex float* tmp = md_alloc_sameplace(D - 1, dims, CFL_SIZE, out);

	md_clear(D - 1, dims, out, CFL_SIZE);
	md_clear(D - 1, dims, tmp, CFL_SIZE);

	for (unsigned int i = 0; i < N; i++) {

		unsigned int lsb = ffs(flags2) - 1;
		flags2 = MD_CLEAR(flags2, lsb);

		md_zfdiff_backwards(D - 1, dims, lsb, tmp, in + i * md_calc_size(D - 1, dims));
	
		md_zadd(D - 1, dims, out, out, tmp);
	}

	md_free(tmp);

	assert(0 == flags2);
}



void grad(unsigned int D, const long dims[D], unsigned int flags, complex float* out, const complex float* in)
{
	unsigned int N = bitcount(flags);

	long dims2[D + 1];
	md_copy_dims(D, dims2, dims);
	dims2[D] = N;

	complex float* tmp = md_alloc_sameplace(D + 1, dims2, CFL_SIZE, out);

	grad_op(D + 1, dims2, flags, tmp, in);

	// rss should be moved elsewhere
	md_zrss(D + 1, dims2, flags, out, tmp);
	md_free(tmp);
}




struct grad_s {

	linop_data_t base;

	unsigned int N;
	long* dims;
	unsigned long flags;
};

static void grad_op_apply(const linop_data_t* _data, complex float* dst, const complex float* src)
{
	const struct grad_s* data = CONTAINER_OF(_data, const struct grad_s, base);

	grad_op(data->N, data->dims, data->flags, dst, src);
}
	
static void grad_op_adjoint(const linop_data_t* _data, complex float* dst, const complex float* src)
{
	const struct grad_s* data = CONTAINER_OF(_data, const struct grad_s, base);

	grad_adjoint(data->N, data->dims, data->flags, dst, src);
}

static void grad_op_normal(const linop_data_t* _data, complex float* dst, const complex float* src)
{
	const struct grad_s* data = CONTAINER_OF(_data, const struct grad_s, base);

	complex float* tmp = md_alloc_sameplace(data->N, data->dims, CFL_SIZE, dst);

	// this could be implemented more efficiently
	grad_op(data->N, data->dims, data->flags, tmp, src);
	grad_adjoint(data->N, data->dims, data->flags, dst, tmp);

	md_free(tmp);
}

static void grad_op_free(const linop_data_t* _data)
{
	const struct grad_s* data = CONTAINER_OF(_data, const struct grad_s, base);

	free(data->dims);
	free((void*)data);
}

struct linop_s* grad_init(long N, const long dims[N], unsigned int flags)
{
	PTR_ALLOC(struct grad_s, data);
	
	data->N = N + 1;
	data->dims = *TYPE_ALLOC(long[N + 1]);
	data->flags = flags;

	md_copy_dims(N, data->dims, dims);
	data->dims[N] = bitcount(flags);
	
	return linop_create(N + 1, data->dims, N, dims, &data->base, grad_op_apply, grad_op_adjoint, grad_op_normal, NULL, grad_op_free);
}

