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
// D4324: ranges::begin/end/size/empty are shared customization-point
// objects, used by every range type in the library, not just the ones
// deliberately tagged for use from conveyor-restricted code. They are
// tagged 'conveyor(auto)' rather than plain 'conveyor' specifically
// because of that sharing: marking a function plain 'conveyor' makes
// *every* instantiation's body subject to the mandatory rules
// unconditionally, which previously broke exactly this -- calling
// ranges::begin() on a plain std::vector from ordinary, non-conveyor
// code -- while developing this feature. This is that regression's own
// pin, from the opposite direction of conveyor_auto_assertions.cc
// (std::ranges::subrange, this file's sibling): confirms an ordinary,
// entirely untagged type continues to work from ordinary code with
// _GLIBCXX_CONVEYOR_ASSERTIONS defined, unaffected by any of the
// conveyor(auto) deduction happening elsewhere for other types.
// { dg-options "-D_GLIBCXX_CONVEYOR_ASSERTIONS -fcontracts -fcontract-control-objects" }
// { dg-do run { target c++26 } }

#include <ranges>
#include <vector>
#include <testsuite_hooks.h>

int main ()
{
  std::vector<int> v {1, 2, 3};

  VERIFY (*std::ranges::begin (v) == 1);
  VERIFY (std::ranges::end (v) - std::ranges::begin (v) == 3);
  VERIFY (std::ranges::size (v) == 3);
  VERIFY (!std::ranges::empty (v));

  return 0;
}
