// D4324, Increment S: check_narrowing's conveyor gate already covers
// every call site automatically -- confirmed here for the spaceship-
// operator converted-constant-expression call site (typeck.cc's
// cp_build_binary_op, SPACESHIP_EXPR arithmetic-operand narrowing
// check), which previously had no dedicated test. A minimal mock of
// std::strong_ordering is used so this doesn't depend on the real
// <compare> header being installed, matching the existing precedent
// in g++.dg/cpp2a/spaceship-narrowing1.C.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

namespace std {
struct strong_ordering {
  int _v;
  constexpr strong_ordering (int v) : _v (v) {}
  constexpr operator int (void) const conveyor { return _v; }
  static const strong_ordering less;
  static const strong_ordering equal;
  static const strong_ordering greater;
};
constexpr strong_ordering strong_ordering::less = -1;
constexpr strong_ordering strong_ordering::equal = 0;
constexpr strong_ordering strong_ordering::greater = 1;
}

int f (int a, unsigned b) conveyor
{
  auto c = (a <=> b); // { dg-error "narrowing conversion of .a. from .int. to .unsigned int. not permitted in a conveyor function or predicate" }
  return (int) c;
}
