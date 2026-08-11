// D4324/P2680 item 8: an assignment nested under '||'s conditionally-
// evaluated operand must NOT be tracked -- whether it actually executes
// depends on short-circuit evaluation of the earlier operand ('a'
// here), so treating it as always having updated i's facts would be
// unsound. oa_track_condition_assignment deliberately only recognizes
// a top-level assignment (the condition itself, or one operand of a
// top-level comparison/negation), never one nested inside &&/||.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int compute (int) conveyor;

int f (int q, bool a) conveyor
{
  int i = 0;
  if (a || (i = compute (q)) > 0)
    return 10 / i; // { dg-error "divisor .i. not provably nonzero in a conveyor function" }
  return 0;
}

int main () { return f (5, true); }
