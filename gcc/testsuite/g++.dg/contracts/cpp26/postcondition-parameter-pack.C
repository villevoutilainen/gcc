// A postcondition naming a function parameter pack: the template's
// condition is substituted on instantiation, not at its definition, where
// the pack can't be expanded (this used to ICE, also with no arguments).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts" }

template <class... A>
int
h (const A... a) post (r: r == sizeof... (a))
{
  return sizeof... (a);
}

template <class T>
T
k (const T x) post (r: r == x)
{
  return x;
}

int
main ()
{
  if (h (1, 2) != 2 || h () != 0 || h ('a', 1.0, 2L) != 3 || k (5) != 5)
    __builtin_abort ();
}
