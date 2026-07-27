// D4324: omit_comment/omit_source_location are honored during constant
// evaluation too (build_contract_control_constexpr_check), not just at
// runtime (see d4324-omit-comment.C / d4324-omit-source-location.C). A
// control object can't return a value from operator() (it's void), so
// values are observed here via the same C++26 constexpr throw/catch
// fidelity d4324-constexpr-kind-cfg.C establishes: throw the observed
// value out to a small wrapper that catches it and returns it, checkable
// via static_assert.
//
// Only the *kept* (non-omitted) location case is not exercised here:
// calling any of std::source_location's accessors (line()/column()/
// file_name()/function_name(), all of the shape "_M_impl ? _M_impl->_M_x
// : dflt") on a thrown-and-caught *real* source_location value under
// constant evaluation hits a pre-existing, unrelated ICE in
// cxx_eval_indirect_ref -- confirmed to reproduce identically with a
// control object that doesn't declare omit_source_location at all, so
// it is not a regression from this feature and not this feature's bug to
// fix. The *omitted* location case below -- the one this feature must
// actually get right -- is unaffected: a null _M_impl never reaches
// cxx_eval_indirect_ref at all (the ternary takes its default branch),
// confirmed to compile and evaluate cleanly in isolation.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=observe" }

#include <contracts>

namespace sc = std::contracts;

struct probe_keeps_comment {
  static constexpr bool is_ignored (sc::evaluation_semantic) { return false; }
  static constexpr bool constify = false;
  static constexpr bool assumable = false;

  constexpr void
  operator() (const sc::assertion_context& ctx) const
  {
    if (ctx.check ())
      return;
    throw ctx.comment ();
  }
};

struct probe_omits_comment {
  static constexpr bool is_ignored (sc::evaluation_semantic) { return false; }
  static constexpr bool constify = false;
  static constexpr bool assumable = false;
  static constexpr bool omit_comment = true;

  constexpr void
  operator() (const sc::assertion_context& ctx) const
  {
    if (ctx.check ())
      return;
    throw ctx.comment ();
  }
};

struct probe_omits_location {
  static constexpr bool is_ignored (sc::evaluation_semantic) { return false; }
  static constexpr bool constify = false;
  static constexpr bool assumable = false;
  static constexpr bool omit_source_location = true;

  constexpr void
  operator() (const sc::assertion_context& ctx) const
  {
    if (ctx.check ())
      return;
    throw ctx.location ().line ();
  }
};

inline constexpr probe_keeps_comment probe_keeps_comment_v{};
inline constexpr probe_omits_comment probe_omits_comment_v{};
inline constexpr probe_omits_location probe_omits_location_v{};

constexpr int fkc (int x) pre<probe_keeps_comment_v>(x >= 0) { return x; }
constexpr int foc (int x) pre<probe_omits_comment_v>(x >= 0) { return x; }
constexpr int fol (int x) pre<probe_omits_location_v>(x >= 0) { return x; }

constexpr const char*
observe_kept_comment ()
{
  try { fkc (-1); } catch (const char* c) { return c; }
  return "unreachable";
}

constexpr const char*
observe_omitted_comment ()
{
  try { foc (-1); } catch (const char* c) { return c; }
  return "unreachable";
}

constexpr unsigned
observe_omitted_location_line ()
{
  try { fol (-1); } catch (unsigned l) { return l; }
  return 999; // unreachable
}

static_assert (observe_kept_comment () != nullptr);
// Omitted, not read: still a real, non-null pointer to an empty string,
// never a null pointer.
static_assert (observe_omitted_comment () != nullptr);
static_assert (observe_omitted_comment ()[0] == '\0');
static_assert (observe_omitted_location_line () == 0);
