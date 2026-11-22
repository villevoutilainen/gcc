// D4324: once overload resolution for the call with the condition's
// operands has a viable candidate, the call is formed as usual: an
// ambiguous call or a deleted function is an error, not a fallback to the
// call with the context alone.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

namespace sc = std::contracts;

struct ambiguous_cco
{
  static constexpr bool is_ignored (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool constify (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool assumable (sc::assertion_static_info) noexcept { return false; }
  void operator() (const sc::assertion_context &) const;
  void operator() (const sc::assertion_context &, const int &, long) const;
  void operator() (const sc::assertion_context &, long, const int &) const;
};

struct refusing_cco
{
  static constexpr bool is_ignored (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool constify (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool assumable (sc::assertion_static_info) noexcept { return false; }
  void operator() (const sc::assertion_context &) const;
  void operator() (const sc::assertion_context &, const double &) const = delete;
};

int f (int i, int j) pre<ambiguous_cco{}> (i < j) { return i; } // { dg-error "ambiguous" }
int g (double x) pre<refusing_cco{}> (x > 0) { return 0; } // { dg-error "deleted" }
