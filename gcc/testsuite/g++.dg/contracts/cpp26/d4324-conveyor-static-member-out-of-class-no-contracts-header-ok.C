// D4324: an out-of-class definition of a static conveyor member function
// has no 'this', so it needs no implicit is_object_address precondition --
// and so no <contracts> -- even though the definition, until it is matched
// to the in-class declaration, looks like a non-static member.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

struct S
{
  enum E { a, b };
  static bool is_valid (E e) conveyor;
  static int twice (int x) conveyor;
  template<typename T> static T same (T t) conveyor;
};

inline bool S::is_valid (E e) conveyor
{
  return e == a || e == b;
}

int S::twice (int x) conveyor
{
  return x < 1000 && x > -1000 ? x * 2 : 0;
}

template<typename T>
T S::same (T t) conveyor
{
  return t;
}

template<>
long S::same (long t) conveyor
{
  return t;
}

int main ()
{
  if (!S::is_valid (S::b) || S::twice (21) != 42 || S::same (7) != 7
      || S::same (7L) != 7L)
    __builtin_abort ();
}
