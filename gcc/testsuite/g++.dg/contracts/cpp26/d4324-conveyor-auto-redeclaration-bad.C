// D4324: every reachable declaration of a conveyor(auto) function
// template must agree on being 'conveyor(auto)' specifically, not
// just on 'conveyor' being present in some form -- comparing only
// DECL_DECLARED_CONVEYOR_P would miss this, since a still-unresolved
// conveyor(auto) declaration's own bit reads false (indistinguishable
// from an ordinary, never-conveyor declaration) until a specialization
// is actually deduced.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

template<typename _Tp> bool f (_Tp) conveyor;
template<typename _Tp> bool f (_Tp) conveyor(auto) { return true; } // { dg-error "redeclaration .* differs in .conveyor\\(auto\\)." }

int main () { return f (1) ? 0 : 1; }
