// D4324: outside conveyor(auto) deduction, a conveyor violation is never a
// substitution failure: it must not silently remove an overload candidate.
// A non-type template argument is a converted constant expression and isn't
// subject to the conveyor rules at all, so overload resolution in a conveyor
// function picks what it picks in ordinary code.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -Wno-vla" }

constexpr int plain (int x) { return x; }	// not conveyor
int runtime (int x) { return x; }		// not conveyor

template<class T, int = plain (sizeof (T))> int h (T) conveyor { return 1; }
long h (long) conveyor { return 2; }

template<class T, class = int[plain (sizeof (T))]> int h2 (T) conveyor { return 1; }
long h2 (long) conveyor { return 2; }

template<int N> int k () conveyor { return N; }
template<class T> int k2 (T) conveyor { return k<plain (sizeof (T))> (); }

int
f (int x) conveyor
{
  auto r = h (x);			// evaluated: the conveyor rules apply
  static_assert (__is_same (decltype (r), int));
  auto r2 = h2 (x);
  static_assert (__is_same (decltype (r2), int));
  return k<plain (3)> ();
}

int
g (int x) conveyor
{
  return k2 (x);
}

// A variable-length array's bound is evaluated at run time: still checked.
template<class T>
int
vla (T n) conveyor
{
  char a[runtime (n)] = {};	// { dg-error "call to .int runtime.int.., which is not declared .conveyor." }
  return sizeof (a) != 0;
}

int
use_vla (int n)
{
  return vla (n);
}
