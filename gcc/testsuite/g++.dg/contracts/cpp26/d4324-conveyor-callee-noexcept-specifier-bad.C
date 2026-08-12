// D4324: companion negative case for d4324-conveyor-callee-noexcept-
// specifier-ok.C -- confirms the deferred-noexcept-specifier exemption
// is narrow: calling the exact same, still-non-conveyor helper from the
// function's own *body* (genuinely executed code, not just its
// noexcept-specifier's operand) is still rejected, even though the
// identical call inside the noexcept-specifier itself is exempt.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

namespace not_conveyor
{
  constexpr bool helper (int x) { return x > 0; } // not declared conveyor
}

template<typename _Tp>
bool
f (_Tp x) noexcept (not_conveyor::helper (sizeof (_Tp))) conveyor // { dg-error "with non-.void. return type must contain a .return. statement" }
{ return not_conveyor::helper (x); } // { dg-error "not declared .conveyor." }

int main () { return f (1) ? 0 : 1; }
