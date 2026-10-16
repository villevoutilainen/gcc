// D4324: declaration-level contracts on callable-typed object
// declarations -- a single-name postcondition binder list (post(n:
// cond)) on a callable that returns void and takes exactly one
// parameter names that parameter, since there is no result for it to
// name (a result name on a void function is ill-formed in C++26, so
// this gives meaning only to a previously ill-formed form).  On a
// non-void callable, the lone name keeps naming the result.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <functional>

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

inline constexpr labeled post_ctrl{"post violated"};

void sink (int) { }
int twice (int x) { return 2 * x; }
int negate (int x) { return -x; }

// Unnamed parameter, void return: 'x' names the parameter.
void (*fp) (int) post<post_ctrl> (x: x > 0) = sink;

// Function parameter of that type.
void
call_through (void (*q) (int) post<post_ctrl> (v: v > 0), int a)
{
  q (a);
}

// Non-static data member (parsed after the class is complete).
struct holder {
  void (*fp) (int) post<post_ctrl> (x: x > 0);
};

// Pointer to member function: 'this' is not counted as a parameter.
struct C {
  void set (int) { }
};
void (C::*mfp) (int) post<post_ctrl> (x: x > 0) = &C::set;

// Class-type callable with a void operator().
struct consumer {
  void operator() (int) const { }
};
consumer cons post<post_ctrl> (v: v >= 0);

// std::function<void(int)>.
std::function<void (int)> fn post<post_ctrl> (v: v >= 0);

// Variable template: the lone name names the parameter for R = void,
// and the result for R = int.
template <class R>
R (*pf) (int) post<post_ctrl> (n: n > 0);

void vsink (int) { }

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
  EXPECT (fp (1), false);
  EXPECT (fp (-1), true);

  EXPECT (call_through (sink, 1), false);
  EXPECT (call_through (sink, -1), true);

  holder h{sink};
  EXPECT (h.fp (1), false);
  EXPECT (h.fp (-1), true);

  C c;
  EXPECT ((c.*mfp) (1), false);
  EXPECT ((c.*mfp) (-1), true);

  EXPECT (cons (0), false);
  EXPECT (cons (-1), true);

  fn = consumer{};
  EXPECT (fn (0), false);
  EXPECT (fn (-1), true);

  pf<void> = vsink;
  EXPECT (pf<void> (1), false);
  EXPECT (pf<void> (-1), true);

  // R = int: 'n' is the result.
  pf<int> = twice;
  EXPECT (pf<int> (1), false);	// result 2 > 0
  pf<int> = negate;
  EXPECT (pf<int> (1), true);	// result -1, argument 1 > 0
  EXPECT (pf<int> (-1), false);	// result 1, argument -1
}
