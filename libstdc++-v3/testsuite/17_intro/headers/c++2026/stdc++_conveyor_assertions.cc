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
// Previously xfailed here for a loop-body is_object_address gap
// (bits/fs_path.h's generic_string() range-for loop over *this) --
// that was actually a real engine bug (oa_handle_call_precondition_
// obligation missing the oa_diagnostics_active guard its own sibling
// scans already had, so oa_handle_loop's speculative per-reassigned-
// decl re-walk could leak a spurious diagnostic), now fixed in
// gcc/cp/contracts.cc; see gcc/testsuite/g++.dg/contracts/cpp26/
// d4324-loop-speculative-rewalk-diagnostics-ok.C for the minimal
// regression test.
//
// Previously xfailed here too for std::barrier's own pointer-indexing
// gap (__state[__current].__tickets[__round]) -- CLOSED 2026-08-26, see
// stdc++_conveyor_precondition_assertions.cc's own updated comment for
// the full explanation of the fix.
//
// Also previously xfailed for a different, unrelated gap reached only
// under this file's own weaker flag combination (CONVEYOR_ASSERTIONS
// without PRECONDITION_ASSERTIONS) once the fix above let std::barrier/
// __unicode's own array access reach std::array::operator[] for the
// first time: operator[]'s own in-body __glibcxx_requires_subscript
// assert (debug/assertions.h) calls this->size() (a _GLIBCXX_CONVEYOR-
// tagged accessor), needing is_object_address(this) -- but operator[]
// is not itself conveyor-declared, and _GLIBCXX_PRECONDITION_SUBSCRIPT
// (bits/c++config.h), which supplies that self-trust as a declared
// pre<>(), only does so under _GLIBCXX_PRECONDITION_ASSERTIONS. Also
// CLOSED 2026-08-26: debug/assertions.h's own in-body __glibcxx_
// requires_subscript/_nonempty now establish the identical is_object_
// address(this) self-trust directly, via a body contract_assert<never_
// proven_conveyor_v>, gated on _GLIBCXX_CONVEYOR_ASSERTIONS alone (not
// on _GLIBCXX_PRECONDITION_ASSERTIONS) -- so this file's own flag
// combination no longer needs the declared-precondition form to supply
// it. Clean under both flag combinations now.

#include <bits/stdc++.h>
