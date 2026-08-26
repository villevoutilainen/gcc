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
// Same two known, deferred loop-body gaps as stdc++_conveyor_assertions.cc
// (is_object_address facts don't survive into a loop body); see that
// file's own comment for the full explanation. Remove this dg-xfail-if
// once that engine gap is closed.
// { dg-xfail-if "is_object_address facts don't survive into a loop body" { *-*-* } }

#include <bits/stdc++.h>
