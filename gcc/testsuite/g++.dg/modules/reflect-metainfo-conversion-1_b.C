// { dg-do compile { target c++26 } }
// { dg-additional-options "-fmodules -freflection -freflection-structural-metainfo-conversion" }

#include <meta>
#include <type_traits>
import M;

static_assert (std::is_same_v<A12, A<Frac (2, 4)>>);
static_assert (A<Frac (2, 4)>::v.num () == 1);	// canonical
static_assert (A<Frac (6, 8)>::v.num () == 3);	// canonical, made here
static_assert (std::is_same_v<AO, A<Outer{Frac (3, 6), 7}>>);
static_assert (A<Outer{Frac (3, 6), 7}>::v.f.num () == 1);
static_assert (!std::is_same_v<A12, A<Frac (1, 3)>>);
