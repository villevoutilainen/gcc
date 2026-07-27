// D4324: a control object that declares static constexpr bool
// omit_comment = true tells the compiler it never needs
// assertion_context::comment(), so the compiler must not even embed the
// condition's stringified text for that assertion -- not merely leave it
// unread. A control object that doesn't declare the member (or declares
// it false) keeps getting the real comment, unaffected.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fdump-tree-gimple" }

#include <contracts>

namespace sc = std::contracts;

const char* seen_kept = nullptr;
const char* seen_omitted = "unset";

struct keeps_comment {
  static constexpr bool is_ignored (sc::evaluation_semantic) { return false; }
  static constexpr bool constify  = false;
  static constexpr bool assumable = false;
  void
  operator() (const sc::assertion_context& ctx) const
  {
    seen_kept = ctx.comment ();
    if (!ctx.check ())
      __builtin_abort ();		// the predicate here always holds
  }
};

struct omits_comment {
  static constexpr bool is_ignored (sc::evaluation_semantic) { return false; }
  static constexpr bool constify  = false;
  static constexpr bool assumable = false;
  static constexpr bool omit_comment = true;
  void
  operator() (const sc::assertion_context& ctx) const
  {
    seen_omitted = ctx.comment ();
    if (!ctx.check ())
      __builtin_abort ();		// the predicate here always holds
  }
};

inline constexpr keeps_comment keeps_comment_v{};
inline constexpr omits_comment omits_comment_v{};

int f_keeps (int x) pre<keeps_comment_v>(x >= 0) { return x; }
int f_omits (int y) pre<omits_comment_v>(y >= 1) { return y; }

int main ()
{
  f_keeps (0);
  f_omits (1);
  if (!seen_kept || __builtin_strcmp (seen_kept, "x >= 0") != 0)
    __builtin_abort ();
  // Omitted, not read: still a real, non-null pointer to an empty
  // string, never a null pointer.
  if (!seen_omitted || seen_omitted[0] != '\0')
    __builtin_abort ();
  return 0;
}

// The kept assertion's condition text is still embedded ...
// { dg-final { scan-tree-dump "x >= 0" "gimple" } }
// ... but the omitted one's never is, not just left unread.
// { dg-final { scan-tree-dump-not "y >= 1" "gimple" } }
