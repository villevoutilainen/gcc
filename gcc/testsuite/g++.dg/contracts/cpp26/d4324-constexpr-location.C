// D4324: assertion_context::location() reports a real, sensible
// std::source_location during constant evaluation, exactly as it does at
// runtime (see d4324-kind-mapping-run.C / d4324-cfg-mapping-run.C for
// the analogous kind/semantic checks). A control object can't return a
// value from operator() (it's void), so values are observed here via the
// same C++26 constexpr throw/catch fidelity d4324-constexpr-kind-cfg.C
// establishes: throw the observed value out to a small wrapper that
// catches it and returns it, checkable via static_assert.
//
// This exercises all four std::source_location accessors
// (line/column/file_name/function_name) under constant evaluation for a
// plain control object -- a regression test for a fixed ICE
// (internal compiler error: in cxx_eval_indirect_ref, at
// cp/constexpr.cc:7634) that used to fire the moment any of them was
// read on a real (kept) location built by
// build_real_source_location_value.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=observe" }

#include <contracts>

namespace sc = std::contracts;

struct probe_line {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }

  constexpr void
  operator() (const sc::assertion_context& ctx) const
  {
    if (ctx.check ())
      return;
    throw ctx.location ().line ();
  }
};

struct probe_column {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }

  constexpr void
  operator() (const sc::assertion_context& ctx) const
  {
    if (ctx.check ())
      return;
    throw ctx.location ().column ();
  }
};

struct probe_file_name {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }

  constexpr void
  operator() (const sc::assertion_context& ctx) const
  {
    if (ctx.check ())
      return;
    throw ctx.location ().file_name ();
  }
};

struct probe_function_name {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }

  constexpr void
  operator() (const sc::assertion_context& ctx) const
  {
    if (ctx.check ())
      return;
    throw ctx.location ().function_name ();
  }
};

inline constexpr probe_line probe_line_v{};
inline constexpr probe_column probe_column_v{};
inline constexpr probe_file_name probe_file_name_v{};
inline constexpr probe_function_name probe_function_name_v{};

constexpr int fl (int x) pre<probe_line_v>(x >= 0) { return x; }
constexpr int fc (int x) pre<probe_column_v>(x >= 0) { return x; }
constexpr int ff (int x) pre<probe_file_name_v>(x >= 0) { return x; }
constexpr int fn (int x) pre<probe_function_name_v>(x >= 0) { return x; }

constexpr unsigned
observe_line ()
{
  try { fl (-1); } catch (unsigned l) { return l; }
  return 0; // unreachable
}

constexpr unsigned
observe_column ()
{
  try { fc (-1); } catch (unsigned c) { return c; }
  return 0; // unreachable
}

constexpr const char*
observe_file_name ()
{
  try { ff (-1); } catch (const char* f) { return f; }
  return nullptr; // unreachable
}

constexpr const char*
observe_function_name ()
{
  try { fn (-1); } catch (const char* f) { return f; }
  return nullptr; // unreachable
}

static_assert (observe_line () != 0);
static_assert (observe_column () != 0);
static_assert (observe_file_name () != nullptr);
static_assert (observe_file_name ()[0] != '\0');
static_assert (observe_function_name () != nullptr);
static_assert (observe_function_name ()[0] != '\0');
