// D4324: a predicate can name two entities of the same name --
// std::ranges::size and a class's size, say, as libstdc++'s <simd> does --
// and a control object's check passes each to the predicate's core
// function as a parameter of its own: the second gets a name no source
// can spell, not a conflicting declaration of the first.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=observe" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <type_traits>

int reports;
void handle_contract_violation (const std::contracts::contract_violation&)
{ ++reports; }

namespace n
{
  struct sz_t { constexpr int operator() (int x) const { return x; } };
  inline constexpr sz_t size{};
}
struct V { static constexpr std::integral_constant<int, 16> size{}; };

void f (int x) { contract_assert (n::size (x) >= V::size ()); }
template <class T> void g (T x) { contract_assert (n::size (x) >= V::size ()); }
int h (int size) pre (n::size (size) >= V::size ()) { return size; }

int
main ()
{
  f (16); g (16); h (16);
  if (reports != 0)
    __builtin_abort ();
  f (15); g (15); h (15);
  if (reports != 3)
    __builtin_abort ();
}
