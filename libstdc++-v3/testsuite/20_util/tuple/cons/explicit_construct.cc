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

std::tuple<Explicit> f2_a() {return {1};} // { dg-error "explicit" }
std::tuple<Explicit, Explicit> f2_b() {return {1,2};} // { dg-error "explicit" }
std::tuple<Explicit, Explicit, Explicit> f2_c() {return {1,2,3};} // { dg-error "explicit" }

std::tuple<long> f3_a() {return std::tuple<int>{1};}
std::tuple<long, long> f3_b() {return std::tuple<int, int>{1,2};}
std::tuple<long, long, long> f3_c() {return std::tuple<int, int, int>{1,2,3};}

std::tuple<Explicit> f4_a()
{
  return std::tuple<int>{1};  // { dg-error "could not convert" }
}
std::tuple<Explicit, Explicit> f4_b()
{
  return std::tuple<int, int>{1,2};  // { dg-error "could not convert" }
}
std::tuple<Explicit, Explicit, Explicit> f4_c()
{
  return std::tuple<int, int,int>{1,2,3};  // { dg-error "could not convert" }
}

std::tuple<long> f5_a() {return {1};}
std::tuple<long, long> f5_b() {return {1,2};}
std::tuple<long, long, long> f5_c() {return {1,2,3};}


std::tuple<int> v0_a{1};
std::tuple<int, int> v0_b{1,2};
std::tuple<int, int, int> v0_c{1,2,3};

std::tuple<Explicit> v1_a{1};
std::tuple<Explicit, Explicit> v1_b{1,2};
std::tuple<Explicit, Explicit, Explicit> v1_c{1,2,3};

std::tuple<Explicit> v2_a = {1}; // { dg-error "explicit" }
std::tuple<Explicit, Explicit> v2_b = {1,2}; // { dg-error "explicit" }
std::tuple<Explicit, Explicit, Explicit> v2_c = {1,2,3}; // { dg-error "explicit" }

std::tuple<Explicit> v3_a{std::tuple<int>{1}};
std::tuple<Explicit, Explicit> v3_b{std::tuple<int,int>{1,2}};
std::tuple<Explicit, Explicit, Explicit> v3_c{std::tuple<int,int,int>{1,2,3}};

std::tuple<Explicit, Explicit> v4_a =
  std::tuple<int>{1}; // { dg-error "conversion" }
std::tuple<Explicit, Explicit> v4_b =
  std::tuple<int,int>{1,2}; // { dg-error "conversion" }
std::tuple<Explicit, Explicit, Explicit> v4_c =
  std::tuple<int,int,int>{1,2,3}; // { dg-error "conversion" }

std::tuple<long> v6_a{1};
std::tuple<long, long> v6_b{1,2};
std::tuple<long, long, long> v6_c{1,2,3};

std::tuple<long> v7_a = {1};
std::tuple<long, long> v7_b = {1,2};
std::tuple<long, long, long> v7_c = {1,2,3};

std::tuple<long> v8_a{std::tuple<int>{1}};
std::tuple<long, long> v8_b{std::tuple<int,int>{1,2}};
std::tuple<long, long, long> v8_c{std::tuple<int,int,int>{1,2,3}};

std::tuple<long> v9_a = std::tuple<int>{1};
std::tuple<long, long> v9_b = std::tuple<int,int>{1,2};
std::tuple<long, long, long> v9_c = std::tuple<int,int,int>{1,2,3};

std::tuple<Explicit> v10_a{v0_a};
std::tuple<Explicit, Explicit> v10_b{v0_b};
std::tuple<Explicit, Explicit, Explicit> v10_c{v0_c};

std::tuple<Explicit> v11_a = v0_a; // { dg-error "conversion" }
std::tuple<Explicit, Explicit> v11_b = v0_b; // { dg-error "conversion" }
std::tuple<Explicit, Explicit, Explicit> v11_c = v0_c; // { dg-error "conversion" }

std::tuple<long> v12_a{v0_a};
std::tuple<long, long> v12_b{v0_b};
std::tuple<long, long, long> v12_c{v0_c};

std::tuple<long> v13_a = v0_a;
std::tuple<long, long> v13_b = v0_b;
std::tuple<long, long, long> v13_c = v0_c;

void f6_a(std::tuple<Explicit>) {}
void f6_b(std::tuple<Explicit, Explicit>) {}
void f6_c(std::tuple<Explicit, Explicit, Explicit>) {}

void f7_a(std::tuple<long>) {}
void f7_b(std::tuple<long, long>) {}
void f7_c(std::tuple<long, long, long>) {}

void test_arg_passing()
{
  f6_a(v0_a); // { dg-error "could not convert" }
  f6_b(v0_b); // { dg-error "could not convert" }
  f6_c(v0_c); // { dg-error "could not convert" }

  f6_a(v1_a);
  f6_b(v1_b);
  f6_c(v1_c);

  f6_a({1}); // { dg-error "explicit" }
  f6_b({1,2}); // { dg-error "explicit" }
  f6_c({1,2,3}); // { dg-error "explicit" }

  f6_a(std::tuple<Explicit>{});
  f6_b(std::tuple<Explicit, Explicit>{});
  f6_c(std::tuple<Explicit, Explicit, Explicit>{});

  f6_a(std::tuple<int>{}); // { dg-error "could not convert" }
  f6_b(std::tuple<int, int>{}); // { dg-error "could not convert" }
  f6_c(std::tuple<int, int, int>{}); // { dg-error "could not convert" }

  f7_a(v0_a);
  f7_b(v0_b);
  f7_c(v0_c);

  f7_a(v6_a);
  f7_b(v6_b);
  f7_c(v6_c);

  f7_a({1});
  f7_b({1,2});
  f7_c({1,2,3});

  f7_a(std::tuple<int>{});
  f7_b(std::tuple<int, int>{});
  f7_c(std::tuple<int, int, int>{});

  f7_a(std::tuple<long>{});
  f7_b(std::tuple<long, long>{});
  f7_c(std::tuple<long, long, long>{});
}
