// D4324: declaration-level contracts on callable-typed object
// declarations -- binder lists on non-static data members, whose
// contracts are parsed only once the class is complete.  Covers
// several names, a pre with a single name, a post naming the result
// and then the parameters, and a class template member.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

struct labeled {
  const char* label;
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  void
  operator() (const sc::assertion_context& ctx) const
  { if (ctx.check ()) return; seen = label; }
  static const char* seen;
};
const char* labeled::seen = nullptr;

inline constexpr labeled ctrl{"violated"};

struct adder {
  int operator() (int a, int b) const { return a + b; }
};
struct suber {
  int operator() (int a, int b) const { return a - b; }
};
struct neg {
  int operator() (int a) const { return -a; }
};

void two (int, int) { }
int sum (int a, int b) { return a + b; }
int diff (int a, int b) { return a - b; }

struct holder {
  void (*fp) (int, int) pre<ctrl> (x, y: x < y);
  int (*fr) (int, int) post<ctrl> (r, x, y: r == x + y);
  adder add pre<ctrl> (x, y: x > 0 && y > 0);
  suber sub post<ctrl> (r, x, y: r == x + y);
  neg n pre<ctrl> (v: v >= 0);
};

template <class T>
struct tholder {
  T (*fp) (T, T) pre<ctrl> (x, y: x < y);
};

#define EXPECT(call, violated)						\
  do {									\
    labeled::seen = nullptr;						\
    call;								\
    if ((labeled::seen != nullptr) != (violated))			\
      __builtin_abort ();						\
  } while (0)

int
main ()
{
  holder h{two, sum, {}, {}, {}};
  EXPECT (h.fp (1, 2), false);
  EXPECT (h.fp (2, 1), true);
  EXPECT (h.fr (1, 2), false);
  h.fr = diff;
  EXPECT (h.fr (1, 2), true);
  EXPECT (h.add (1, 2), false);
  EXPECT (h.add (-1, 2), true);
  EXPECT (h.sub (1, 2), true);		// 1 - 2 != 1 + 2
  EXPECT (h.sub (1, 0), false);		// 1 - 0 == 1 + 0
  EXPECT (h.n (1), false);
  EXPECT (h.n (-1), true);

  tholder<int> t{diff};
  EXPECT (t.fp (1, 2), false);
  EXPECT (t.fp (2, 1), true);
}
