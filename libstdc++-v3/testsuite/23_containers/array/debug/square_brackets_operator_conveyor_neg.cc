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
// D4324: same shape as square_brackets_operator1_neg.cc, but routing
// __glibcxx_assert through the stricter, conveyor-flavored control
// object instead of the plain runtime one -- confirms the runtime trap
// still fires the same way under _GLIBCXX_CONVEYOR_ASSERTIONS.
// { dg-options "-D_GLIBCXX_CONVEYOR_ASSERTIONS -fcontracts -fcontract-control-objects" }
// { dg-do run { target c++26 xfail *-*-* } }

#include <array>

void test01()
{
  std::array<int, 0> a;
  (void) a[0];
}

int main()
{
  test01();
  return 0;
}
