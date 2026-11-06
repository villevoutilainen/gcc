// D4324: a user-written control type reports a violation found during
// constant evaluation with std::contracts::constexpr_error_str and
// constexpr_warning_str, as the standard control types do.  The diagnostic
// is reported where they are called, here in user code.  An error stops
// later contracts, and the evaluation's result isn't used, without a
// further "non-constant condition" error; a warning lets both go on.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
#include <string_view>

namespace sc = std::contracts;

template<bool Error>
struct ctl
{
  static constexpr bool is_ignored (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool constify (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool assumable (sc::assertion_static_info) noexcept { return false; }

  constexpr void
  operator() (const sc::assertion_context& c) const noexcept
  {
    if (c.check ())
      return;
    if consteval
      {
	if constexpr (Error)
	  sc::constexpr_error_str ("my_tag", c.comment ()); // { dg-error "constexpr message: x >= 0 \\\[my_tag\\\]" }
	else
	  sc::constexpr_warning_str ("", std::string_view (c.comment ())); // { dg-warning "constexpr message: x >= 0" }
	// { dg-warning "constexpr message: x >= 5" "" { target *-*-* } .-1 }
      }
    else
      {
	__builtin_trap ();
      }
  }
};

inline constexpr ctl<true> err_v{};
inline constexpr ctl<false> warn_v{};

constexpr int f (int x) pre<err_v>(x >= 0) pre<err_v>(x >= 5) { return x; }
constexpr int g (int x) pre<warn_v>(x >= 0) pre<warn_v>(x >= 5) { return x; }

static_assert (f (-1) == -1);
static_assert (g (-1) == -1);
static_assert (f (7) == 7);
