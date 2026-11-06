// D4324: neither engine reports an immediate function's obligations:
// every call of one is a constant evaluation, which rejects core
// undefined behavior and contract violations itself.  The AST engine
// reported span::operator[]'s precondition here, which the GIMPLE
// engine doesn't analyze (P4445 BUG-35, seen on mdspan's
// __padded_block_begin).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -D_GLIBCXX_ASSERTIONS -D_GLIBCXX_CONVEYOR_ASSERTIONS" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <span>

consteval int
sum (std::span<const int, 3> s, unsigned long n)
{
  int r = 0;
  for (unsigned long i = 0; i < n; ++i)
    r += s[i];
  return r;
}

constexpr int arr[3] = { 1, 2, 3 };
static_assert (sum (arr, 3) == 6);
static_assert (sum (arr, 4) == 6);
// { dg-error "constexpr message: __idx < size\\(\\) \\\[contract\\\]" "" { target *-*-* } 0 }
