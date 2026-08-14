// D4324, item 8's overflow check: infrastructure-only smoke test for the
// "type-bound witness" fact (oa_type_bound_fact/oa_match_type_bounded_
// comparison/oa_refine_single_comparison's own new establishment block --
// see .claude/plans/lazy-stirring-pearl.md). This commit has no
// diagnostic-producing consumer of the fact yet (oa_scan_overflow_in_expr
// lands in a later commit), so there is nothing to observe here beyond
// "this still compiles exactly as before" -- confirmed separately, during
// development, via temporary debug instrumentation that the witness is
// actually established for a plain-variable loop guard ('i < n'), a
// while-loop guarded by a CALL_EXPR bound ('i < v.size ()'), and an
// if-guarded manual increment ('if (i < n) i = i + 1;'). Real behavioral
// coverage of the fact arrives with oa_scan_overflow_in_expr's own tests.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

struct S { int size () const conveyor { return 5; } };

int use_for_loop_guard (int n) conveyor
{
  int b = 3;
  for (int i = 0; i < n; ++i)
    b = 5;
  return 10 / b;
}

int use_while_loop_call_bound (S& v, int i) conveyor
{
  while (i < v.size ())
    i = i + 1;
  return 0;
}

int use_if_guarded_manual_increment (int i, int n) conveyor
{
  if (i < n)
    i = i + 1;
  return i;
}

int main ()
{
  S v;
  return use_for_loop_guard (5) + use_while_loop_call_bound (v, 0)
	 + use_if_guarded_manual_increment (0, 5);
}
