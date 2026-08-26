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
// One known, deferred gap remains, only reached under this file's own
// stricter flag combination: std::barrier's __tree_barrier_base::
// _M_arrive indexes __state[__current].__tickets[__round], which needs
// is_object_address composed through pointer ARITHMETIC/INDEXING
// (__state + __current), not just through 'this'-based field access --
// a distinct, deeper, already-known-and-deferred engine limitation
// (see project memory: "full array-offset tracking deferred"),
// unrelated to the loop-body diagnostics-suppression bug
// stdc++_conveyor_assertions.cc's own comment describes (that one is
// fixed; confirmed this file's previously-xfailed fs_path.h error is
// gone too, this is the one error site that remains). No library-only
// workaround exists (the pointer/index are otherwise legitimately
// valid). Remove this dg-xfail-if once that engine gap is closed.
// { dg-xfail-if "is_object_address can't compose through pointer indexing" { *-*-* } }

#include <bits/stdc++.h>
