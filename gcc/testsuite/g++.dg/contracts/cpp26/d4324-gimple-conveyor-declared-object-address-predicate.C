// D4324: the GIMPLE engine validates a declared-only function's conveyor
// precondition through a predicate core function built at the end of the
// file, from the condition with its is_object_address calls resolved --
// so a check calling it doesn't reach the intrinsic -- while trusting
// those is_object_address conjuncts, as the AST engine trusts a
// precondition's own: the dereference they guard is not diagnosed.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>

int derefs (int* p) pre<std::contracts::conveyor_assert_v> (std::is_object_address (p) && *p > 0);
struct S { int m (int* p) pre<std::contracts::conveyor_assert_v> (std::is_object_address (p)); };

int main () { return 0; }
