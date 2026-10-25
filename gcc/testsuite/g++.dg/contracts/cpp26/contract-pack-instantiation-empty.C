// A contract on a function template with a function parameter pack: its
// instantiation's parameters needn't pair up with the template's, and the
// pack may expand to none (check_postconditions_in_redecl walked off the
// end of them).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts" }

template <class... A> int f (A &&...a) pre (sizeof... (a) < 3)
{ return sizeof... (a); }

struct S
{
  template <class... A> int g (A &&...a) pre (true)
  { return sizeof... (a); }
};

int main ()
{
  S s;
  if (f () + f (1) + f (1, 2) + s.g () + s.g (1) != 4)
    __builtin_abort ();
}
