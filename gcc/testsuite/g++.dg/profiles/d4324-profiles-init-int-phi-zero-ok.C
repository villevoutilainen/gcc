// P4222 Initialization profile: an 'int' merged from zeros on two
// paths is not a pointer, so it can't be "a pointer marked
// [[ref_to_uninit]]".  It used to be reported as one: the merge's
// zeros were each taken for a null pointer, which left nothing to
// disqualify it.
// { dg-do compile { target c++11 } }
// { dg-options "-O0" }

[[profiles::enforce(std::init)]];

int
foo (int id)
{
  int result = 0;
  if (id == 0)
    {
      int value = 0;
      result = value;
    }
  return result;
}

long
bar (int id)
{
  long r = 0;
  if (id > 3)
    r = 0;
  else if (id < -3)
    r = 0L;
  return r;
}

int
baz (int id)
{
  if (id == 0)
    {
      int value = 0;
      return value;
    }
  return 0;
}
