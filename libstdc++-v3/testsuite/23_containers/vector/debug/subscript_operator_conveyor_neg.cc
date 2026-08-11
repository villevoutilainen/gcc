// Copyright (C) 2010-2026 Free Software Foundation, Inc.
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
// D4324: same shape as std::array's own square_brackets_operator_
// conveyor_neg.cc, for std::vector -- confirms the runtime trap still
// fires under _GLIBCXX_CONVEYOR_ASSERTIONS.
// { dg-options "-D_GLIBCXX_CONVEYOR_ASSERTIONS -fcontracts -fcontract-control-objects" }
// { dg-do run { target c++26 xfail *-*-* } }

#include <vector>

void test01()
{
  std::vector<int> v;
  (void) v[0];
}

int main()
{
  test01();
  return 0;
}
