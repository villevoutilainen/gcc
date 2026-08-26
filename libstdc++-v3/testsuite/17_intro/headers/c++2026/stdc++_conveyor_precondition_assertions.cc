// { dg-options "-D_GLIBCXX_CONVEYOR_ASSERTIONS -D_GLIBCXX_PRECONDITION_ASSERTIONS -fcontracts -fcontract-control-objects" }
// { dg-do compile { target c++26 } }

// Copyright (C) 2007-2026 Free Software Foundation, Inc.
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

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING3.  If not see
// <http://www.gnu.org/licenses/>.

// D4324: same whole-library sanity sweep as stdc++_conveyor_assertions.cc
// in this same directory, with _GLIBCXX_PRECONDITION_ASSERTIONS also
// enabled -- this additionally exercises every __glibcxx_requires_*/
// __glibcxx_assert-guarded precondition check throughout the library
// (which is exactly the combination __glibcxx_assert routes through
// never_proven_conveyor_v for internal, defensive assertions), so it is
// the strictest of the two megaheader sweeps and the one most likely to
// surface a fresh is_object_address/ownership gap in newly-added or
// newly-touched library code. See stdc++_conveyor_assertions.cc for the
// full rationale; this file exists separately (rather than as a second
// dg-options line on one file) to match this testsuite's own convention
// of one flag combination per file (see e.g. 26_numerics/saturation/
// conveyor_assertions.cc vs. div_conveyor_proofs.cc).
//
// Previously xfailed here too for std::barrier's own __tree_barrier_
// base::_M_arrive, which indexes __state[__current].__tickets[__round]
// -- is_object_address composed through pointer ARITHMETIC/INDEXING
// (__state + __current), not just through 'this'-based field access.
// Closed 2026-08-26: contracts.cc gained (1) an oa_provable_p pointer-
// parity fix mirroring oa_scan_array_bounds_in_expr's own existing
// POINTER_TYPE_P branch, and (2) a new assertable array-slot-identity
// mechanism (oa_array_object_identity/array_object_identity_key,
// modeled directly on the existing field_object_identity_key) that lets
// a library author assert is_object_address(&ptr[dynamic_index]) into
// existence for an opaque, heap-allocated array with no traceable
// named-array provenance -- exactly std::barrier's own __state, see
// std::barrier's own updated contract_assert. See gcc/testsuite/g++.dg/
// contracts/cpp26/d4324-array-slot-identity-*.C for the minimal
// regression tests. Clean under this file's own flag combination now;
// see stdc++_conveyor_assertions.cc's own comment for the one, separate,
// already-known gap that remains under ITS weaker flag combination
// specifically (not reached here: this file's own _GLIBCXX_PRECONDITION_
// SUBSCRIPT already self-trusts the 'this' that gap is about).

#include <bits/stdc++.h>
