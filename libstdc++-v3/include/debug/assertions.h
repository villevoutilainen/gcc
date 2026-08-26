// Debugging support implementation -*- C++ -*-

// Copyright (C) 2003-2026 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 3, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// Under Section 7 of GPL version 3, you are granted additional
// permissions described in the GCC Runtime Library Exception, version
// 3.1, as published by the Free Software Foundation.

// You should have received a copy of the GNU General Public License and
// a copy of the GCC Runtime Library Exception along with this program;
// see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
// <http://www.gnu.org/licenses/>.

/** @file debug/assertions.h
 *  This file is a GNU debug extension to the Standard C++ Library.
 */

#ifndef _GLIBCXX_DEBUG_ASSERTIONS_H
#define _GLIBCXX_DEBUG_ASSERTIONS_H 1

#include <bits/c++config.h>

#ifndef _GLIBCXX_DEBUG
// Under _GLIBCXX_PRECONDITION_ASSERTIONS, these three shared, mechanical
// shapes are instead declared as pre<>() on the calling function's own
// declaration (see bits/c++config's own _GLIBCXX_PRECONDITION_SUBSCRIPT
// and friends) -- so the in-body assert below is disabled here, rather
// than double-checking the same condition both as a declared precondition
// and again inside the body.
# if defined _GLIBCXX_PRECONDITION_ASSERTIONS
#  define __glibcxx_requires_non_empty_range(_First,_Last)
#  define __glibcxx_requires_subscript(_N)
#  define __glibcxx_requires_nonempty()
# else
// Verify that [_First, _Last) forms a non-empty iterator range.
//
// Both of these conditions mix a parameter substituted from the real
// call site with literal text written directly in this macro's own
// body -- unlike a condition entirely from one origin (however many
// macro layers it passes through), that defeats __glibcxx_assert's own
// "recover the real source text" step under contracts (see
// __glibcxx_assert_msg's own comment in bits/c++config), so an
// explicit message is supplied here instead, built the same
// macro-expansion-independent way the pre-contracts diagnostic always
// was: # stringification of just the real, call-site-supplied tokens,
// string-literal-concatenated with this macro's own literal text.
#  define __glibcxx_requires_non_empty_range(_First,_Last)	\
   __glibcxx_assert_msg(_First != _Last, #_First " != " #_Last)
// SUBSCRIPT/NONEMPTY's own condition calls size()/empty(), which may be
// _GLIBCXX_CONVEYOR-tagged (see e.g. std/array) -- under the reference/
// this self-trust soundness fix, that mandates is_object_address(this)
// proven at THIS call's own site too, same as _GLIBCXX_PRECONDITION_
// SUBSCRIPT/_NONEMPTY already establish it for the declared-precondition
// form (bits/c++config.h) -- but this in-body form is exactly what's
// still active when _GLIBCXX_PRECONDITION_ASSERTIONS is NOT defined
// (see the #if just above), so it needs the identical self-trust
// established here too, or a caller relying on the in-body form alone
// hits an unprovable is_object_address(this) the declared-precondition
// form would have quietly supplied. Uses std::contracts::never_proven_
// conveyor_v, deliberately: 'this' being a live object is always true
// by construction for an ordinary, already-called member function, the
// same reasoning bits/c++config.h's own identical addition uses.
// Gated on _GLIBCXX_CONVEYOR_ASSERTIONS specifically (not just this
// branch's own guard), matching every other conveyor-only addition in
// this file's own sibling headers -- under plain _GLIBCXX_ASSERTIONS
// (no conveyor), is_object_address has no meaning to assert at all.
#  if defined(_GLIBCXX_CONVEYOR_ASSERTIONS) && defined(__cpp_contract_control_objects)
#   define __glibcxx_requires_subscript(_N)	\
    contract_assert<std::contracts::never_proven_conveyor_v>(std::is_object_address (this)); \
    __glibcxx_assert_msg(_N < this->size(), #_N " < this->size()")
#  else
#   define __glibcxx_requires_subscript(_N)	\
    __glibcxx_assert_msg(_N < this->size(), #_N " < this->size()")
#  endif
// Verify that the container is nonempty. Unlike the two above, this
// condition is entirely this macro's own literal text -- no call-site
// tokens involved at all -- so the usual __glibcxx_assert already
// reports it correctly with no message needed.
#  if defined(_GLIBCXX_CONVEYOR_ASSERTIONS) && defined(__cpp_contract_control_objects)
#   define __glibcxx_requires_nonempty()		\
    contract_assert<std::contracts::never_proven_conveyor_v>(std::is_object_address (this)); \
    __glibcxx_assert(!this->empty())
#  else
#   define __glibcxx_requires_nonempty()		\
    __glibcxx_assert(!this->empty())
#  endif
# endif
#else // Use the more verbose Debug Mode checks.
# define __glibcxx_requires_non_empty_range(_First,_Last) \
  __glibcxx_check_non_empty_range(_First,_Last)
# define __glibcxx_requires_nonempty() \
  __glibcxx_check_nonempty()
# define __glibcxx_requires_subscript(_N) \
  __glibcxx_check_subscript(_N)
#endif

// Verify that a divisor is nonzero. Unlike the three shapes above, this
// one has no Debug Mode counterpart to route to under _GLIBCXX_DEBUG --
// it isn't about container/iterator bounds at all, just a plain scalar
// argument to a free function (see bits/sat_arith.h's own saturating_div),
// so it doesn't participate in the _GLIBCXX_DEBUG split above; the same
// definition applies whether or not Debug Mode is active. Still disabled
// under _GLIBCXX_PRECONDITION_ASSERTIONS, for the same double-checking
// reason as the three shapes above (see bits/c++config's own
// _GLIBCXX_PRECONDITION_NONZERO_DIVISOR).
#if defined _GLIBCXX_PRECONDITION_ASSERTIONS
# define __glibcxx_requires_nonzero_divisor(_Y)
#else
# define __glibcxx_requires_nonzero_divisor(_Y)	\
  __glibcxx_assert_msg(_Y != 0, #_Y " != 0")
#endif

#if defined _GLIBCXX_DEBUG && _GLIBCXX_HOSTED

# define _GLIBCXX_DEBUG_ASSERT(_Condition) __glibcxx_assert(_Condition)

# ifdef _GLIBCXX_DEBUG_PEDANTIC
#  define _GLIBCXX_DEBUG_PEDASSERT(_Condition) _GLIBCXX_DEBUG_ASSERT(_Condition)
# else
#  define _GLIBCXX_DEBUG_PEDASSERT(_Condition)
# endif

# define _GLIBCXX_DEBUG_ONLY(_Statement) _Statement

#else
# define _GLIBCXX_DEBUG_ASSERT(_Condition)
# define _GLIBCXX_DEBUG_PEDASSERT(_Condition)
# define _GLIBCXX_DEBUG_ONLY(_Statement)
#endif

#endif // _GLIBCXX_DEBUG_ASSERTIONS
