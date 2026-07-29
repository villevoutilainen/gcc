// D4324: a control object that declares static constexpr bool
// omit_source_location = true tells the compiler it never needs
// assertion_context::location(), so the compiler must not even build the
// source_location's __impl object for that assertion. Unlike the comment
// case, a gimple-dump text match isn't a reliable way to prove this: the
// dump always shows each function's own name regardless of whether that
// function's own assertion embedded a location, since file/function name
// text isn't unique to one assertion the way condition text is. Instead
// this checks the read-back values: an omitted location must read back
// exactly like a default-constructed std::source_location (the library's
// own accessors already null-check defensively), which can only happen
// if the compiler genuinely passed a null impl pointer rather than a
// real one.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

namespace sc = std::contracts;

std::source_location seen_kept;
std::source_location seen_omitted;
bool kept_called = false;
bool omitted_called = false;

struct keeps_location {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  void
  operator() (const sc::assertion_context& ctx) const
  {
    seen_kept = ctx.location ();
    kept_called = true;
    if (!ctx.check ())
      __builtin_abort ();		// the predicate here always holds
  }
};

struct omits_location {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool omit_source_location (sc::assertion_static_info) { return true; }
  void
  operator() (const sc::assertion_context& ctx) const
  {
    seen_omitted = ctx.location ();
    omitted_called = true;
    if (!ctx.check ())
      __builtin_abort ();		// the predicate here always holds
  }
};

inline constexpr keeps_location keeps_location_v{};
inline constexpr omits_location omits_location_v{};

int f_keeps_loc (int x) pre<keeps_location_v>(x >= 0) { return x; }
int f_omits_loc (int y) pre<omits_location_v>(y >= 1) { return y; }

int main ()
{
  f_keeps_loc (0);
  f_omits_loc (1);

  if (!kept_called || !omitted_called)
    __builtin_abort ();

  if (seen_kept.line () == 0
      || seen_kept.file_name ()[0] == '\0'
      || seen_kept.function_name ()[0] == '\0')
    __builtin_abort ();

  if (seen_omitted.line () != 0
      || seen_omitted.column () != 0
      || seen_omitted.file_name ()[0] != '\0'
      || seen_omitted.function_name ()[0] != '\0')
    __builtin_abort ();

  return 0;
}
