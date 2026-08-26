// D4324/P2680: the member-function/'this' shape is already correct
// (confirmed working before this session's reference-parameter fix,
// since 'this' is POINTER_TYPE, already covered by the pre-existing
// pointer-dereference-validity check) -- kept here as a permanent
// regression test alongside its new reference-parameter sibling
// (d4324-conveyor-assert-reference-deref-bad.C), covering the third
// shape Ville named: "uses a 'this' pointer by accessing a member."
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>

struct S {
  int m_value;
  void f ()
  {
    // The implicit 'this' INDIRECT_REF synthesized for 'this->m_value'
    // has no location of its own, so the error below lands on the
    // function's closing brace rather than this line -- matching this
    // engine's existing, documented behavior for this exact shape (see
    // oa_scan_array_bounds_in_expr's own comment, contracts.cc).
    contract_assert<std::contracts::conveyor_assert_v> (this->m_value < 2048); // { dg-warning "cannot verify .contract_assert. condition" }
  } // { dg-error "pointer dereference of .\\(S\\*\\)this. not provably valid" }
};

int main () { return 0; }
