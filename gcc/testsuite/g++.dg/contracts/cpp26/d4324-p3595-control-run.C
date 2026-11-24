// D4324: a P3595 configuration's "output": "control" chooses the control
// object the compiler creates for a matching assertion whose code chose
// none (a bare assertion), or whose control object's config_replaceable
// query says it may be replaced -- as std::contracts::default_v's (and so
// annotated's) does; one the code chose is kept otherwise.
// Matched here by "namespace" and by "function" (a fully qualified name
// without template arguments, naming all overloads: a non-member, an
// in-class member, a function template, the function holding a
// contract_assert); "function" chooses a semantic as well.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=observe" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{"function":"ign"},"output":{"semantic":"ignore"}},{"match":{"namespace":"mylib"},"output":{"control":"cfg::audit_v"}},{"match":{"function":"replace_me"},"output":{"control":"cfg::audit_v"}},{"match":{"function":"S::m"},"output":{"control":"::cfg::audit_v"}},{"match":{"function":"tf"},"output":{"control":"cfg::audit_v"}},{"match":{"function":"asserts"},"output":{"control":"cfg::audit_v"}}\]} }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

int handler_calls, audit_calls, fixed_calls, replaceable_calls;

void
handle_contract_violation (const sc::contract_violation&)
{
  ++handler_calls;
}

struct base_cco
{
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
};

namespace cfg
{
  // The configuration's choice.
  struct audit : base_cco
  {
    void operator() (const sc::assertion_context& c) const
    { if (!c.check ()) ++audit_calls; }
  };
  inline constexpr audit audit_v {};
}

// Chosen by the code; may be replaced by the configuration.
struct replaceable : base_cco
{
  static constexpr bool config_replaceable (sc::assertion_static_info)
  { return true; }
  void operator() (const sc::assertion_context& c) const
  { if (!c.check ()) ++replaceable_calls; }
};
inline constexpr replaceable replaceable_v {};

// Chosen by the code, with no config_replaceable query: kept.
struct fixed : base_cco
{
  void operator() (const sc::assertion_context& c) const
  { if (!c.check ()) ++fixed_calls; }
};
inline constexpr fixed fixed_v {};

namespace mylib
{
  int a (int x) pre (x > 0) { return x; }
}

int replace_me (int x) pre<replaceable_v> (x > 0) { return x; }
int replace_me (long x) pre<fixed_v> (x > 0) { return x; }
int replace_me (short x) pre<sc::default_v> (x > 0) { return x; }
int replace_me (char x) pre<sc::annotated ("message")> (x > 0) { return x; }

struct S
{
  int m (int x) pre (x > 0) { return x; }
};

template<typename T>
T tf (T x) pre (x > 0) { return x; }

void asserts (int x) { contract_assert (x > 0); }

int ign (int x) pre (x > 0) { return x; }
int plain (int x) pre (x > 0) { return x; }

int
main ()
{
  mylib::a (-1);		// namespace: audit
  replace_me (-1);		// function, config_replaceable: audit
  replace_me (-1L);		// function, not replaceable: fixed
  replace_me ((short) -1);	// function, explicit default_v: audit
  replace_me ((char) -1);	// function, annotated: audit
  S ().m (-1);			// in-class member: audit
  tf (-1);			// function template: audit
  asserts (-1);			// contract_assert in a function: audit
  ign (-1);			// function chooses the semantic: ignored
  plain (-1);			// no match: default_v
  if (audit_calls != 7 || fixed_calls != 1 || replaceable_calls != 0
      || handler_calls != 1)
    __builtin_abort ();
}
