/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Zhichao Huang <hahv@msn.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef GNUPLOT_OVERFLOW_H
#define GNUPLOT_OVERFLOW_H

#include "syscfg.h"

#ifndef __has_builtin
#define __has_builtin(x) (0)
#endif

#ifdef HAVE_STDCKDINT_H
#include <stdckdint.h> /* C23 */
#endif

#ifdef _MSC_VER
#ifndef ENABLE_INTSAFE_SIGNED_FUNCTIONS
#define ENABLE_INTSAFE_SIGNED_FUNCTIONS
#endif
#include <intsafe.h>
#endif

static inline TBOOLEAN
gp_ckd_add_intgr(intgr_t *result, intgr_t a, intgr_t b)
{
#ifdef ckd_add
    return ckd_add(result, a, b);
#elif __has_builtin(__builtin_add_overflow) || __GNUC__ > 5 || \
    (__GNUC__ == 5 && __GNUC_MINOR__ >= 1)
    return __builtin_add_overflow(a, b, result);
#elif defined(_MSC_VER)
#ifdef GNUPLOT_INT64_SUPPORT
    return FAILED(Int64Add(a, b, result));
#else
    return FAILED(IntAdd(a, b, result));
#endif
#else /* !_MSC_VER */
    if (a > 0) {
	if (b > INTGR_MAX - a)
	    return TRUE;
    } else {
	if (b < INTGR_MIN - a)
	    return TRUE;
    }

    *result = a + b;
    return FALSE;
#endif
}

static inline TBOOLEAN
gp_ckd_sub_intgr(intgr_t *result, intgr_t a, intgr_t b)
{
#ifdef ckd_sub
    return ckd_sub(result, a, b);
#elif __has_builtin(__builtin_sub_overflow) || __GNUC__ > 5 || \
    (__GNUC__ == 5 && __GNUC_MINOR__ >= 1)
    return __builtin_sub_overflow(a, b, result);
#elif defined(_MSC_VER)
#ifdef GNUPLOT_INT64_SUPPORT
    return FAILED(Int64Sub(a, b, result));
#else
    return FAILED(IntSub(a, b, result));
#endif
#else /* !_MSC_VER */
    if (a < 0) {
	if (b > INTGR_MAX + a)
	    return TRUE;
    } else {
	if (b < INTGR_MIN + a)
	    return TRUE;
    }

    *result = a - b;
    return FALSE;
#endif
}

static inline TBOOLEAN
gp_ckd_mul_intgr(intgr_t *result, intgr_t a, intgr_t b)
{
#ifdef ckd_mul
    return ckd_mul(result, a, b);
#elif __has_builtin(__builtin_mul_overflow) || __GNUC__ > 5 || \
    (__GNUC__ == 5 && __GNUC_MINOR__ >= 1)
    return __builtin_mul_overflow(a, b, result);
#elif defined(_MSC_VER)
#ifdef GNUPLOT_INT64_SUPPORT
    return FAILED(Int64Mult(a, b, result));
#else
    return FAILED(IntMult(a, b, result));
#endif
#else /* !_MSC_VER */
    if (a == 0 || b == 0) {
	*result = 0;
	return FALSE;
    }

    if (a > 0) {
	if (b > 0) {
	    if (a > INTGR_MAX / b)
		return TRUE;
	} else {
	    if (b < INTGR_MIN / a)
		return TRUE;
	}
    } else {
	if (b > 0) {
	    if (a < INTGR_MIN / b)
		return TRUE;
	} else {
	    if (a < INTGR_MAX / b)
		return TRUE;
	}
    }

    *result = a * b;
    return FALSE;
#endif
}

#endif /* GNUPLOT_OVERFLOW_H */
