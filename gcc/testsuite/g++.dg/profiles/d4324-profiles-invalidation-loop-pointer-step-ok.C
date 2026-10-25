// P3446R0/P4296R0 Invalidation profile: '++first' on a pointer
// parameter in a loop is 'first = first + 4', whose only reaching
// write is itself.  Tracing where FIRST's value came from went around
// that cycle forever, hanging the compiler (libstdc++'s range
// constructors have this loop).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

struct L { void add (int); };

void f (L &l, const int *first, const int *last)
{
  for (; first != last; ++first)
    l.add (*first);
}
