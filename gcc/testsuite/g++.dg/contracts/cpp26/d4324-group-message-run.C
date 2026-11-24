// D4324: an assertion with both a group and a custom message.  A
// combined label forwards fixed_message_label_t's message to the
// violation handler, and a user's own control object can carry a group
// and a message as data, set by a constructor, with the members it
// isn't given left null by their default member initializers.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{"group":"perf"},"output":{"semantic":"observe"}},{"match":{"group":"quiet"},"output":{"semantic":"ignore"}}\]} }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <contract_labels>
#include <cstring>
#include <span>
using namespace std::contracts;
using namespace std::contracts::P3400;

int calls;
const char *last_message;

void
handle_contract_violation (const contract_violation& v)
{
  ++calls;
  last_message = v.message ();
  if (v.semantic () == evaluation_semantic::enforce)
    throw 0;
}

// A group and a message, given by a constructor; whichever isn't given
// stays null.  groups() returns a range of names: the group, or none.
struct grouped
{
  const char *group = nullptr;
  const char *message = nullptr;

  constexpr grouped (const char *g) : group (g) { }
  constexpr grouped (const char *g, const char *m) : group (g), message (m) { }

  constexpr std::span<const char *const> groups () const
  { return { &group, group != nullptr }; }

  static constexpr bool is_ignored (assertion_static_info i)
  { return i.semantic () == evaluation_semantic::ignore; }
  static constexpr bool constify (assertion_static_info) { return false; }
  static constexpr bool assumable (assertion_static_info) { return false; }

  void operator() (const assertion_context& c) const
  {
    if (c.check ())
      return;
    assertion_info i = c.info ();
    if (message)
      i.set_message (message);
    invoke_violation_handler (i, detection_mode::predicate_false);
    if (i.semantic () == evaluation_semantic::enforce)
      __builtin_abort ();	// the handler throws first
  }
};

int lbl (int x)
  pre<"perf"group | fixed_message_label_t{"x must be positive"}>(x > 0)
{ return x; }
int msg (int x) pre<grouped{"perf", "x must be positive"}>(x > 0) { return x; }
int nomsg (int x) pre<grouped ("perf")>(x > 0) { return x; }
int nogroup (int x) pre<grouped (nullptr)>(x > 0) { return x; }
int quiet (int x) pre<grouped{"quiet", "never seen"}>(x > 0) { return x; }
int enf (int x) pre<grouped{"app", "enforced"}>(x > 0) { return x; }

static bool
is (const char *s, const char *t)
{
  return s && std::strcmp (s, t) == 0;
}

int
main ()
{
  // In perf: observed, the label's message delivered.
  if (lbl (-1) != -1 || calls != 1 || !is (last_message, "x must be positive"))
    __builtin_abort ();
  if (msg (-1) != -1 || calls != 2 || !is (last_message, "x must be positive"))
    __builtin_abort ();
  // No message given: an empty one reported.
  if (nomsg (-1) != -1 || calls != 3 || *last_message)
    __builtin_abort ();
  // No group: the build's semantic, enforce; an empty message.
  try { nogroup (-1); __builtin_abort (); } catch (int) {}
  if (calls != 4 || *last_message)
    __builtin_abort ();
  // In quiet: ignored.
  if (quiet (-1) != -1 || calls != 4)
    __builtin_abort ();
  // No rule for app: the build's semantic, enforce.
  try { enf (-1); __builtin_abort (); } catch (int) {}
  if (calls != 5 || !is (last_message, "enforced"))
    __builtin_abort ();
}
