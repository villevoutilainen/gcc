// D4324: std::contracts::annotated, default_control with a group, a
// message, or both: annotated("g"group, "m"), annotated("m"),
// annotated("g"group).  Its group is matched by a configuration's
// "group" rules; its message reaches the violation handler alongside
// the comment.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{"group":"perf"},"output":{"semantic":"observe"}},{"match":{"group":"quiet"},"output":{"semantic":"ignore"}}\]} }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <cstring>
using namespace std::contracts;
using namespace std::contracts::literals;

int calls;
const char *last_message;
const char *last_comment;

void
handle_contract_violation (const contract_violation& v)
{
  ++calls;
  last_message = v.message ();
  last_comment = v.comment ();
  if (v.semantic () == evaluation_semantic::enforce)
    throw 0;
}

int both (int x) pre<annotated ("perf"group, "x must be positive")>(x > 0) { return x; }
int group_only (int x) pre<annotated ("perf"group)>(x > 0) { return x; }
int message_only (int x) pre<annotated ("x must be positive")>(x > 0) { return x; }
int quiet (int x) pre<annotated ("quiet"group, "never seen")>(x > 0) { return x; }

int
assert_in_body (int x)
{
  contract_assert<annotated ("perf"group, "x must be even")>(x % 2 == 0);
  return x;
}

static bool
is (const char *s, const char *t)
{
  return s && std::strcmp (s, t) == 0;
}

// The accessors, at compile time.
static_assert (annotated ("g"group, "m").group ()[0] == 'g');
static_assert (annotated ("m").group () == nullptr);
static_assert (annotated ("g"group).message () == nullptr);

int
main ()
{
  // In perf: observed, with the message and the comment.
  if (both (-1) != -1 || calls != 1
      || !is (last_message, "x must be positive") || !is (last_comment, "x > 0"))
    __builtin_abort ();
  // A group, no message.
  if (group_only (-1) != -1 || calls != 2 || last_message)
    __builtin_abort ();
  // A message, no group: the build's semantic, enforce.
  try { message_only (-1); __builtin_abort (); } catch (int) {}
  if (calls != 3 || !is (last_message, "x must be positive"))
    __builtin_abort ();
  // In quiet: ignored.
  if (quiet (-1) != -1 || calls != 3)
    __builtin_abort ();
  // A contract_assert, likewise.
  if (assert_in_body (3) != 3 || calls != 4 || !is (last_message, "x must be even"))
    __builtin_abort ();
  // Satisfied: no call.
  if (both (1) != 1 || message_only (1) != 1 || calls != 4)
    __builtin_abort ();
}
