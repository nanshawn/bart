/* Copyright 2014. The Regents of the University of California.
 * All rights reserved. Use of this source code is governed by 
 * a BSD-style license which can be found in the LICENSE file.
 *
 * Authors:
 * 2014 Martin Uecker <uecker@eecs.berkeley.edu>
 * 2014 Frank Ong <frankong@berkeley.edu>
 */

#include <complex.h>
#include <stdbool.h>
#include <assert.h>

#include "num/multind.h"
#include "num/flpmath.h"
#include "num/ops.h"

#include "misc/misc.h"

#include "linop.h"





struct shared_data_s {

	void* data;

	struct shared_data_s* next;
	struct shared_data_s* prev;

	operator_del_t del;

	union {

		operator_fun_t apply;
		operator_p_fun_t apply_p;
	} u;
};

static void shared_unlink(struct shared_data_s* data)
{
	data->next->prev = data->prev;
	data->prev->next = data->next;
}

static void shared_del(const void* _data)
{
	struct shared_data_s* data = (struct shared_data_s*)_data;

	if (data->next == data) {

		assert(data == data->prev);
		data->del(data->data);

	} else {

		shared_unlink(data);
	}
	
	free(data);
}

static void shared_apply(const void* _data, complex float* dst, const complex float* src)
{
	struct shared_data_s* data = (struct shared_data_s*)_data;

	data->u.apply(data->data, dst, src);
}

static void shared_apply_p(const void* _data, float lambda, complex float* dst, const complex float* src)
{
	struct shared_data_s* data = (struct shared_data_s*)_data;

	data->u.apply_p(data->data, lambda, dst, src);
}


/**
 * Create a linear operator (with strides)
 */
struct linop_s* linop_create2(unsigned int N, const long odims[N], const long ostrs[N], 
				const long idims[N], const long istrs[N], void* data,
				op_fun_t forward, op_fun_t adjoint, op_fun_t normal, op_p_fun_t pinverse, del_fun_t del)
{
	struct linop_s* lo = xmalloc(sizeof(struct linop_s));

	struct shared_data_s* shared_data[4];

	for (unsigned int i = 0; i < 4; i++) 
		shared_data[i] = xmalloc(sizeof(struct shared_data_s));

	for (unsigned int i = 0; i < 4; i++) {

		shared_data[i]->data = data;
		shared_data[i]->del = del;

		// circular double-linked list
		shared_data[i]->next = shared_data[(i + 1) % 4];
		shared_data[i]->prev = shared_data[(i + 3) % 4];
	}

	shared_data[0]->u.apply = forward;
	shared_data[1]->u.apply = adjoint;
	shared_data[2]->u.apply = normal;
	shared_data[3]->u.apply_p = pinverse;

	assert((NULL != forward));
	assert((NULL != adjoint));

	lo->forward = operator_create2(N, odims, ostrs, idims, istrs, shared_data[0], shared_apply, shared_del);
	lo->adjoint = operator_create2(N, idims, istrs, odims, ostrs, shared_data[1], shared_apply, shared_del);

	if (NULL != normal) {

		lo->normal = operator_create2(N, idims, istrs, idims, istrs, shared_data[2], shared_apply, shared_del);

	} else {

		shared_unlink(shared_data[2]);
		free(shared_data[2]);
		lo->normal = NULL;
	}

	if (NULL != pinverse) {

		lo->pinverse = operator_p_create2(N, idims, istrs, odims, ostrs, shared_data[3], shared_apply_p, shared_del);
	
	} else {

		shared_unlink(shared_data[3]);
		free(shared_data[3]);
		lo->pinverse = NULL;
	}

	return lo;
}


/**
 * Create a linear operator (without strides)
 *
 * @param N number of dimensions
 * @param odims dimensions of output (codomain)
 * @param idims dimensions of input (domain)
 * @param data data for applying the operator
 * @param forward function for applying the forward operation, A
 * @param adjoint function for applying the adjoint operation, A^H
 * @param normal function for applying the normal equations operation, A^H A
 * @param pinverse function for applying the pseudo-inverse operation, (A^H A + mu I)^-1
 * @param del function for freeing the data
 */
struct linop_s* linop_create(unsigned int N, const long odims[N], const long idims[N], void* data,
				op_fun_t forward, op_fun_t adjoint, op_fun_t normal, op_p_fun_t pinverse, del_fun_t del)
{
	long ostrs[N];
	long istrs[N];
	md_calc_strides(N, ostrs, odims, CFL_SIZE);
	md_calc_strides(N, istrs, idims, CFL_SIZE);

	return linop_create2(N, odims, ostrs, idims, istrs, data, forward, adjoint, normal, pinverse, del);
}

/**
 * Return the data associated with the linear operator
 * 
 * @param ptr linear operator
 */
const void* linop_get_data(const struct linop_s* ptr)
{
	return ((struct shared_data_s*) operator_get_data(ptr->forward))->data;
}


/**
 * Make a copy of a linear operator
 * @param x linear operator
 */
extern const struct linop_s* linop_clone(const struct linop_s* x)
{
	struct linop_s* lo = xmalloc(sizeof(struct linop_s));

	lo->forward = operator_ref(x->forward);
	lo->adjoint = operator_ref(x->adjoint);
	lo->normal = operator_ref(x->normal);
	lo->pinverse = operator_p_ref(x->pinverse);

	return lo;
}

/**
 * Apply the forward operation of a linear operator: y = A x
 * Checks that dimensions are consistent for the linear operator
 *
 * @param op linear operator
 * @param N number of dimensions
 * @param ddims dimensions of the output (codomain)
 * @param dst output data
 * @param sdims dimensions of the input (domain)
 * @param src input data
 */
void linop_forward(const struct linop_s* op, unsigned int N, const long ddims[N], complex float* dst, 
			const long sdims[N], const complex float* src)
{
	UNUSED(ddims); UNUSED(sdims); UNUSED(N);
	linop_forward_unchecked(op, dst, src);
}


/**
 * Apply the adjoint operation of a linear operator: y = A^H x
 * Checks that dimensions are consistent for the linear operator
 *
 * @param op linear operator
 * @param N number of dimensions
 * @param ddims dimensions of the output (domain)
 * @param dst output data
 * @param sdims dimensions of the input (codomain)
 * @param src input data
 */
void linop_adjoint(const struct linop_s* op, unsigned int N, const long ddims[N], complex float* dst,
			const long sdims[N], const complex float* src)
{
	UNUSED(ddims); UNUSED(sdims); UNUSED(N);
	linop_adjoint_unchecked(op, dst, src);
}


/**
 * Apply the normal equations operation of a linear operator: y = A^H A x
 * Checks that dimensions are consistent for the linear operator
 *
 * @param op linear operator
 * @param N number of dimensions
 * @param ddims dimensions of the output (domain)
 * @param dst output data
 * @param sdims dimensions of the input (domain)
 * @param src input data
 */
void linop_normal(const struct linop_s* op, unsigned int N, const long ddims[N], complex float* dst,
			const long sdims[N], const complex float* src)
{
	UNUSED(ddims); UNUSED(sdims); UNUSED(N);
	linop_normal_unchecked(op, dst, src);
}


/**
 * Apply the forward operation of a linear operator: y = A x
 * Does not check that the dimensions are consistent for the linear operator
 *
 * @param op linear operator
 * @param dst output data
 * @param src input data
 */
void linop_forward_unchecked(const struct linop_s* op, complex float* dst, const complex float* src)
{
	operator_apply_unchecked(op->forward, dst, src);
}


/**
 * Apply the adjoint operation of a linear operator: y = A^H x
 * Does not check that the dimensions are consistent for the linear operator
 *
 * @param op linear operator
 * @param dst output data
 * @param src input data
 */
void linop_adjoint_unchecked(const struct linop_s* op, complex float* dst, const complex float* src)
{
	assert(op->adjoint);
	operator_apply_unchecked(op->adjoint, dst, src);
}


/**
 * Apply the normal equations operation of a linear operator: y = A^H A x
 * Does not check that the dimensions are consistent for the linear operator
 *
 * @param op linear operator
 * @param dst output data
 * @param src input data
 */
void linop_normal_unchecked(const struct linop_s* op, complex float* dst, const complex float* src)
{
	assert(op->normal);
	operator_apply_unchecked(op->normal, dst, src);
}


/**
 * Apply the pseudo-inverse operation of a linear operator: y = (A^H A + lambda I)^-1 x
 * Does not check that the dimensions are consistent for the linear operator
 *
 * @param op linear operator
 * @param dst output data
 * @param src input data
 */
void linop_pinverse_unchecked(const struct linop_s* op, float lambda, complex float* dst, const complex float* src)
{
	operator_p_apply_unchecked(op->pinverse, lambda, dst, src);
}


/**
 * Return the dimensions and strides of the domain of a linear operator
 *
 * @param op linear operator
 */
const struct iovec_s* linop_domain(const struct linop_s* op)
{
	return operator_domain(op->forward);
}


/**
 * Return the dimensions and strides of the codomain of a linear operator
 *
 * @param op linear operator
 */
const struct iovec_s* linop_codomain(const struct linop_s* op)
{
	return operator_codomain(op->forward);
}


/**
 * Create chain of linear operators.
 * C = B A 
 * C^H = A^H B^H
 * C^H C = A^H B^H B A
 */
struct linop_s* linop_chain(const struct linop_s* a, const struct linop_s* b)
{
	struct linop_s* c = xmalloc(sizeof(struct linop_s));

	c->forward = operator_chain(a->forward, b->forward);
	c->adjoint = operator_chain(b->adjoint, a->adjoint);

	if (NULL == b->normal) {

		c->normal = operator_chain(c->forward, c->adjoint);

	} else {

		const struct operator_s* top = operator_chain(b->normal, a->adjoint);
		c->normal = operator_chain(a->forward, top);
		operator_free(top);
	}

	c->pinverse = NULL;

	return c;
}


/**
 * Free the linear operator and associated data,
 * Note: only frees the data if its reference count is zero
 *
 * @param op linear operator
 */
void linop_free(const struct linop_s* op)
{
	operator_free(op->forward);
	operator_free(op->adjoint);
	operator_free(op->normal);
	operator_p_free(op->pinverse);
	free((void*)op);
}


/**
 * Wrapper for calling forward operation using italgos.
 */
extern void linop_forward_iter(void* _o, float* _dst, const float* _src )
{
	struct linop_s* o = _o;
	complex float* dst = (complex float*) _dst;
	const complex float* src = (complex float*) _src;

	linop_forward_unchecked(o, dst, src);
}

/**
 * Wrapper for calling adjoint operation using italgos.
 */
extern void linop_adjoint_iter(void* _o, float* _dst, const float* _src)
{
	struct linop_s* o = _o;
	complex float* dst = (complex float*) _dst;
	const complex float* src = (complex float*) _src;

	linop_adjoint_unchecked(o, dst, src);
}

/**
 * Wrapper for calling normal equations operation using italgos.
 */
extern void linop_normal_iter(void* _o, float* _dst, const float* _src)
{
	struct linop_s* o = _o;
	complex float* dst = (complex float*) _dst;
	const complex float* src = (complex float*) _src;

	linop_normal_unchecked(o, dst, src);
}

/**
 * Wrapper for calling pseudo-inverse operation using italgos.
 */
extern void linop_pinverse_iter( void* _o, float lambda, float* _dst, const float* _src )
{
	struct linop_s* o = _o;
	complex float* dst = (complex float*)_dst;
	const complex float* src = (complex float*)_src;

	linop_pinverse_unchecked(o, lambda, dst, src);
}