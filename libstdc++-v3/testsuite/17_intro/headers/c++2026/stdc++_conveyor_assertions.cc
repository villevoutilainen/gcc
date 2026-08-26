// { dg-options "-D_GLIBCXX_CONVEYOR_ASSERTIONS -fcontracts -fcontract-control-objects" }
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

// D4324: whole-library sanity sweep -- <bits/stdc++.h> should compile
// clean under _GLIBCXX_CONVEYOR_ASSERTIONS (i.e. every is_object_address
// obligation the conveyor engine imposes on the library's own headers,
// via never_proven_conveyor_v/conveyor_assert_v pre/post clauses and the
// real _GLIBCXX_CONVEYOR-tagged member functions such as basic_string's
// size()/data()/etc., is satisfied throughout the library's own call
// graph). This is a compile-only megaheader sweep, not a runtime check --
// see 17_intro/headers/c++2011/stdc++.cc's own identical megaheader
// inclusion for the non-conveyor precedent.
//
// Two known, deferred gaps remain: an is_object_address fact established
// before a loop does not survive into the loop's own body (confirmed in
// two independent, unrelated places: bits/fs_path.h's generic_string()
// range-for loop over *this, and std::barrier's __tree_barrier_base::
// _M_arrive's nested for-loop over __state[__current].__tickets[__round]).
// This is a genuine engine limitation, not a library bug -- neither site
// has any reasonable library-only workaround (the loop variable/pointer
// in each case is otherwise legitimately valid). Remove this dg-xfail-if
// once that engine gap is closed.
// { dg-xfail-if "is_object_address facts don't survive into a loop body" { *-*-* } }

#include <bits/stdc++.h>
