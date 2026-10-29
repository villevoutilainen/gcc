// D4324: a counted loop whose condition is 'i != N' -- the variable starts
// at or below N, only the loop's own increment changes it, by one -- bounds
// the variable in the body as 'i < N' would, so an array element indexed by
// it is proven.  So does counting down to N, and so does a variable whose
// address is only taken to read it (printed through a const reference).  A
// loop that starts past N, that can step past it, whose N isn't known, or
// whose variable can be written through a pointer, bounds nothing.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>

struct S { int v; };

void show (const int &);
void poke (int *);

int use (const S &s)
  pre<std::contracts::conveyor_assert_v> (std::is_object_address (&s))
{ return s.v; }

int up ()
{
  const S arr[4] = { {1}, {2}, {3}, {4} };
  int n = 0;
  for (int i = 0; i != 4; ++i)
    n += use (arr[i]);
  return n;
}

int down ()
{
  const S arr[4] = { {1}, {2}, {3}, {4} };
  int n = 0;
  for (int i = 3; i != -1; --i)
    n += use (arr[i]);
  return n;
}

int unsigned_up ()
{
  const S arr[4] = { {1}, {2}, {3}, {4} };
  int n = 0;
  for (unsigned i = 0; i != 4; i++)
    n += use (arr[i]);
  return n;
}

int past ()
{
  const S arr[4] = { {1}, {2}, {3}, {4} };
  int n = 0;
  for (int i = 0; i != 5; ++i)
    n += use (arr[i]); // { dg-error "cannot prove" }
  return n;
}

int skips ()
{
  const S arr[4] = { {1}, {2}, {3}, {4} };
  int n = 0;
  for (int i = 0; i != 4; ++i)
    {
      n += use (arr[i]); // { dg-error "cannot prove" }
      ++i;
    }
  return n;
}

int unknown (int m)
{
  const S arr[4] = { {1}, {2}, {3}, {4} };
  int n = 0;
  for (int i = 0; i != m; ++i)
    n += use (arr[i]); // { dg-error "cannot prove" }
  return n;
}

int starts_past ()
{
  const S arr[4] = { {1}, {2}, {3}, {4} };
  int n = 0;
  for (int i = 6; i != 4; ++i)
    n += use (arr[i]); // { dg-error "cannot prove" }
  return n;
}

int printed ()
{
  const S arr[4] = { {1}, {2}, {3}, {4} };
  int n = 0;
  for (int i = 0; i != 4; ++i)
    {
      show (i);
      n += use (arr[i]);
    }
  return n;
}

int escapes_before ()
{
  const S arr[4] = { {1}, {2}, {3}, {4} };
  int n = 0;
  int i;
  int *p = &i;
  for (i = 0; i != 4; ++i)
    {
      *p = 9;
      n += use (arr[i]); // { dg-error "cannot prove" }
    }
  return n;
}

int escapes_in_body ()
{
  const S arr[4] = { {1}, {2}, {3}, {4} };
  int n = 0;
  for (int i = 0; i != 4; ++i)
    {
      poke (&i);
      n += use (arr[i]); // { dg-error "cannot prove" }
    }
  return n;
}
