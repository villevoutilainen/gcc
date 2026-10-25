// D4324: a function body instantiated while a constant expression is
// evaluated (here, for a deduced return type a constraint needs) is still
// subject to the conveyor rules; the constant-expression exemption covers
// only the expression itself.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int plain (int x) { return x; }	// not conveyor

template<class T>
auto g (T x) conveyor { return plain (x); }	// { dg-error "call to .int plain.int.., which is not declared .conveyor." }

template<class T>
concept CanG = requires (T t) { g (t); };

constexpr bool b = CanG<int>;

template<class T>
auto g2 (T x) conveyor { return plain (x); }	// { dg-error "call to .int plain.int.., which is not declared .conveyor." }

int
h (int x)
{
  if constexpr (requires { g2 (x); })
    return 1;
  return 0;
}
