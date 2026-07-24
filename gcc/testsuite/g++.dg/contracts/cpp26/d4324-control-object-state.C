// D4324: pre<...> names a constant-expression control object, so a control
// type can carry real per-instance state across distinct uses.  Two
// constexpr objects of the same control type each carry a different
// diagnostic string; naming a different one on two different assertions
// and observing which string the operator() call captures proves the two
// objects are genuinely distinct, not both collapsing to one shared (or
// zero-initialized) instance.  A control object can also be named as an
// anonymous temporary directly in the pre() (no named constexpr variable
// needed), and its operator() can combine the compiler-supplied predicate
// text with a user-provided message at runtime via std::string; that
// concatenation is checked both via a temporary and via a named, pre-made
// constexpr object, so it isn't somehow tied to one form or the other.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <string>

namespace sc = std::contracts;

struct labeled {
  const char* label;
  static constexpr bool is_ignored (sc::evaluation_config) { return false; }
  static constexpr bool constify = false;
  static constexpr bool assumable = false;
  void
  operator() (const sc::assertion_context& ctx) const
  { if (ctx.check ()) return; seen = label; }		// returns -> continue
  static const char* seen;
};
const char* labeled::seen = nullptr;

inline constexpr labeled first{"first diagnostic"};
inline constexpr labeled second{"second diagnostic"};

int f (int x) pre<first>(x > 0) { return x; }
int g (int x) pre<second>(x > 0) { return x; }

// A prvalue temporary, built via aggregate paren-init, named directly in
// pre<...> instead of a separate named constexpr object.
int j (int x) pre<labeled("temp diagnostic")>(x > 0) { return x; }

struct annotated {
  const char* note;
  static constexpr bool is_ignored (sc::evaluation_config) { return false; }
  static constexpr bool constify = false;
  static constexpr bool assumable = false;
  void
  operator() (const sc::assertion_context& ctx) const
  {
    if (ctx.check ())
      return;
    seen = std::string (ctx.comment) + ": " + note;	// returns -> continue
  }
  static std::string seen;
};
std::string annotated::seen;

// The compiler-supplied comment is the predicate's source text ("x > 0");
// the control object concatenates it at runtime with its own user-supplied
// note.  Exercise this via a temporary ...
int k (int x) pre<annotated{"must stay positive"}>(x > 0) { return x; }

// ... and via a named, pre-made constexpr object, same as labeled's first/
// second above, to prove the concatenation isn't somehow specific to the
// temporary form.
inline constexpr annotated note_v{"named object note"};
int l (int x) pre<note_v>(x > 0) { return x; }

int main ()
{
  f (-1);
  if (!labeled::seen || __builtin_strcmp (labeled::seen, "first diagnostic") != 0)
    __builtin_abort ();
  const char* seen_first = labeled::seen;

  g (-1);
  if (!labeled::seen || __builtin_strcmp (labeled::seen, "second diagnostic") != 0)
    __builtin_abort ();

  // The two calls really did observe two distinct strings.
  if (__builtin_strcmp (seen_first, labeled::seen) == 0)
    __builtin_abort ();

  j (-1);
  if (!labeled::seen || __builtin_strcmp (labeled::seen, "temp diagnostic") != 0)
    __builtin_abort ();

  k (-1);
  if (annotated::seen != "x > 0: must stay positive")
    __builtin_abort ();

  l (-1);
  if (annotated::seen != "x > 0: named object note")
    __builtin_abort ();

  return 0;
}
