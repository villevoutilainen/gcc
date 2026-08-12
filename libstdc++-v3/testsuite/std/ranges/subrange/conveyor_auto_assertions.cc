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
// D4324: ranges::subrange's own front()/back()/operator[]/data()/
// cbegin()/operator bool()/empty()/size() -- all inherited from
// view_interface, itself implemented (for C++23 and up) via an
// explicit object parameter specifically to eliminate the CRTP
// downcast that otherwise makes them permanently conveyor-
// incompatible -- must compile and run cleanly under
// _GLIBCXX_CONVEYOR_ASSERTIONS. This exercises the customization-point
// objects underneath (ranges::begin/end/size/empty/cbegin/prev, all
// tagged 'conveyor(auto)' rather than plain 'conveyor' specifically
// because they're shared with every other range type, most of which
// aren't conveyor-tagged at all -- see conveyor_auto_shared_cpo.cc for
// confirmation that sharing doesn't regress unrelated, non-conveyor
// range types).  This test acts as this library-side chain's own
// regression pin: if a future change to any function in it stops
// satisfying the mandatory conveyor rules, this file fails to
// *compile* (a loud, immediate signal), not merely to behave
// differently at runtime.
// { dg-options "-D_GLIBCXX_CONVEYOR_ASSERTIONS -fcontracts -fcontract-control-objects" }
// { dg-do run { target c++26 } }

#include <ranges>
#include <testsuite_hooks.h>

using SR = std::ranges::subrange<int*, int*, std::ranges::subrange_kind::sized>;

// Explicit-instantiation pins: 'conveyor(auto)''s own regression-safety
// mechanism (no new syntax -- an explicit instantiation repeating
// plain 'conveyor' asserts a specific specialization must actually
// deduce conveyor). More targeted than the functional checks below:
// each of these fails to compile on its own, right here, if the named
// function's own conveyor-ness ever regresses, rather than only
// showing up indirectly through whichever of subrange's own public
// members happens to call it.
template auto std::ranges::__access::_Begin::operator()(SR&) const conveyor;
template auto std::ranges::__access::_End::operator()(SR&) const conveyor;
template auto std::ranges::__access::_Size::operator()(SR&) const conveyor;
template bool std::ranges::__access::_Empty::operator()(SR&) const conveyor;
template auto std::ranges::__access::_CBegin::operator()(SR&) const conveyor;
template int* std::ranges::__prev_fn::operator()(int*) const conveyor;
template std::basic_const_iterator<int*>::basic_const_iterator(int*) conveyor;
template const int&
  std::basic_const_iterator<int*>::operator*() const conveyor;

int front (SR& sr) conveyor { return sr.front (); }
int back (SR& sr) conveyor { return sr.back (); }
int subscript (SR& sr) conveyor { return sr[2]; }
bool is_empty (SR& sr) conveyor { return sr.empty (); }
std::size_t size (SR& sr) conveyor { return sr.size (); }
bool as_bool (SR& sr) conveyor { return bool (sr); }
bool data_nonnull (SR& sr) conveyor { return sr.data () != nullptr; }
int cbegin_deref (SR& sr) conveyor { return *sr.cbegin (); }

int main ()
{
  int arr[5] = {1, 2, 3, 4, 5};
  SR sr (arr, arr + 5, 5u);
  SR empty_sr (arr, arr, 0u);

  VERIFY (front (sr) == 1);
  VERIFY (back (sr) == 5);
  VERIFY (subscript (sr) == 3);
  VERIFY (!is_empty (sr));
  VERIFY (is_empty (empty_sr));
  VERIFY (size (sr) == 5);
  VERIFY (as_bool (sr));
  VERIFY (!as_bool (empty_sr));
  VERIFY (data_nonnull (sr));
  VERIFY (cbegin_deref (sr) == 1);

  return 0;
}
