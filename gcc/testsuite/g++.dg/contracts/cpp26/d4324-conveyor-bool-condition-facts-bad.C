// D4324: what a tested bool call doesn't establish.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <climits>
#include <contracts>
using std::contracts::conveyor_assert_v;

bool ok_sum (const int l, const int r) conveyor
  post<conveyor_assert_v> (res: !res || (l > 0 ? r <= INT_MAX - l : r >= INT_MIN - l));
bool small (const int x) conveyor post<conveyor_assert_v> (res: res == (x < 100));
bool tiny (const int x) conveyor;
void use_tiny (int x) conveyor pre<conveyor_assert_v> (tiny (x));
void use100 (int x) conveyor pre<conveyor_assert_v> (x < 100);

int other_operand (int l, int r, int z) conveyor
{ if (ok_sum (l, z)) return l + r; return 0; }	// { dg-error "not provably free of overflow" }

int else_branch (int x) conveyor { if (small (x)) return 0; return x + 1; }	// { dg-error "not provably free of overflow" }

int reassigned (int x, int y) conveyor { if (small (x)) { x = y; return x + 1; } return 0; }	// { dg-error "not provably free of overflow" }

void pred_reassigned (int x, int y) conveyor { if (tiny (x)) { x = y; use_tiny (x); } }	// { dg-error "precondition" }

void pred_negated (int x) conveyor { if (!tiny (x)) use_tiny (x); }	// { dg-error "precondition" }

void pred_other (int x, int y) conveyor { if (tiny (y)) use_tiny (x); }	// { dg-error "precondition" }

void value_negated (int x) conveyor { if (!small (x)) use100 (x); }	// { dg-error "precondition" }

int side_effect (int x) conveyor { if (small (x++)) return x + 1; return 0; }	// { dg-error "not provably free of overflow" }
