// { dg-do run { target c++11 } }
// Below C++26, "structural" must remain an ordinary identifier, usable
// as a variable name, member name, or anywhere else a plain identifier
// is valid -- it only becomes a context-sensitive keyword in a
// class-head at C++26 or later.

int structural = 1;

struct S
{
  int structural;
};

int
main ()
{
  S s { structural };
  if (s.structural != 1)
    __builtin_abort ();
  return 0;
}
