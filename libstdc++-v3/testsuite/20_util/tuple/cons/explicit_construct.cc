// { dg-do compile }
// { dg-options "-std=gnu++11" }

// Copyright (C) 2015 Free Software Foundation, Inc.
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

#include <tuple>

struct Explicit
{
  Explicit() = default;
  explicit Explicit(int) {}
};

std::tuple<int> f1a() {return {1};}
std::tuple<int, int> f1b() {return {1,2};}
std::tuple<int, int, int> f1c() {return {1,2,3};}

std::tuple<Explicit, Explicit> f2() {return {1,2};} // { dg-error "explicit" }

std::tuple<long, long> f3() {return std::tuple<int, int>{1,2};}

std::tuple<Explicit, Explicit> f4()
{
  return std::tuple<int, int>{1,2};  // { dg-error "could not convert" }
}

std::tuple<long, long> f5() {return {1,2};}

std::tuple<int, int> v0{1,2};

std::tuple<Explicit, Explicit> v1{1,2};

std::tuple<Explicit, Explicit> v2 = {1,2}; // { dg-error "explicit" }

std::tuple<Explicit, Explicit> v3{std::tuple<int,int>{1,2}};

std::tuple<Explicit, Explicit> v4 =
  std::tuple<int,int>{1,2}; // { dg-error "conversion" }

std::tuple<long, long> v6{1,2};

std::tuple<long, long> v7 = {1,2};

std::tuple<long, long> v8{std::tuple<int,int>{1,2}};

std::tuple<long, long> v9 = std::tuple<int,int>{1,2};

std::tuple<Explicit, Explicit> v10{v0};

std::tuple<Explicit, Explicit> v11 = v0; // { dg-error "conversion" }

std::tuple<long, long> v12{v0};

std::tuple<long, long> v13 = v0;

void f6(std::tuple<Explicit, Explicit>) {}

void f7(std::tuple<long, long>) {}

void test_arg_passing()
{
  f6(v0); // { dg-error "could not convert" }
  f6(v1);
  f6({1,2}); // { dg-error "explicit" }
  f6(std::tuple<Explicit, Explicit>{});
  f6(std::tuple<int, int>{}); // { dg-error "could not convert" }
  f7(v0);
  f7(v6);
  f7({1,2});
  f7(std::tuple<int, int>{});
  f7(std::tuple<long, long>{});
}
