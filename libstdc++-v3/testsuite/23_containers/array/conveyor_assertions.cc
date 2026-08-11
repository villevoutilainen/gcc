// Copyright (C) 2012-2026 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 3, or (at your option)
// any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING3.  If not see
// <http://www.gnu.org/licenses/>.
//
// D4324: a well-formed, in-bounds std::array access still compiles and
// runs cleanly under _GLIBCXX_CONVEYOR_ASSERTIONS -- array::size() is
// tagged conveyor (see std/array), so array::operator[]'s own
// __glibcxx_requires_subscript assertion (which calls size()) is
// itself legal, mandatory-UB-freedom-checked code, not just an
// ordinary runtime check.
// { dg-options "-D_GLIBCXX_CONVEYOR_ASSERTIONS -fcontracts -fcontract-control-objects" }
// { dg-do run { target c++26 } }

#include <array>
#include <testsuite_hooks.h>

int main()
{
  std::array<int, 3> a{1, 2, 3};
  VERIFY(a[1] == 2);
  VERIFY(a.size() == 3);
  return 0;
}
