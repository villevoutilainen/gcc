// D4324: every control object in std::contracts::controls takes an
// optional group and message: C("g"group, "m"), C("g"group), C("m"),
// C().  The group is matched by a configuration's "group" rules and by
// -fcontracts-group-evaluation-semantic= rules (hierarchically); the
// message, then the condition with its operands' values, reaches the
// violation handler alongside the comment.
// annotated is default_control.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce -fcontracts-group-evaluation-semantic=lib:observe" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{"group":"perf"},"output":{"semantic":"observe"}},{"match":{"group":"quiet"},"output":{"semantic":"ignore"}}\]} }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <cstring>
using namespace std::contracts;
using namespace std::contracts::controls;

int calls;
const char *last_message;
char last_message_buf[512];
evaluation_semantic last_semantic;

void
handle_contract_violation (const contract_violation& v)
{
  ++calls;
  // Copied: the message, like the violation, lives only during the call.
  if (v.message ())
    {
      std::strcpy (last_message_buf, v.message ());
      last_message = last_message_buf;
    }
  else
    last_message = nullptr;
  last_semantic = v.semantic ();
  if (v.semantic () == evaluation_semantic::enforce)
    throw 0;
}

static bool
is (const char *s, const char *t)
{
  return s && std::strcmp (s, t) == 0;
}

static_assert (__is_same (annotated, default_control));
static_assert (default_control ("g"group, "m").group ()[0] == 'g');
static_assert (review ("m").group ()[0] == '\0');
static_assert (mandatory ("g"group).message ()[0] == '\0');
static_assert (noexcept_assert ().message ()[0] == '\0');

int d_perf (int x) pre<default_control ("perf"group, "d perf")>(x > 0) { return x; }
int d_quiet (int x) pre<default_control ("quiet"group)>(x > 0) { return x; }
int d_lib (int x) pre<default_control ("lib.io"group, "d lib")>(x > 0) { return x; }
int d_msg (int x) pre<default_control ("d msg")>(x > 0) { return x; }
int r_msg (int x) pre<review ("r msg")>(x > 0) { return x; }
int r_quiet (int x) pre<review ("quiet"group, "never")>(x > 0) { return x; }
int m_msg (int x) pre<mandatory ("app"group, "m msg")>(x > 0) { return x; }

int
main ()
{
  // perf, by the configuration: observed, with the message.
  if (d_perf (-1) != -1 || calls != 1 || !is (last_message, "d perf: -1 > 0")
      || last_semantic != evaluation_semantic::observe)
    __builtin_abort ();
  // quiet, by the configuration: ignored.
  if (d_quiet (-1) != -1 || calls != 1)
    __builtin_abort ();
  // lib.io, by the lib:observe rule: observed.
  if (d_lib (-1) != -1 || calls != 2 || !is (last_message, "d lib: -1 > 0")
      || last_semantic != evaluation_semantic::observe)
    __builtin_abort ();
  // No group: the build's semantic, enforce.
  try { d_msg (-1); __builtin_abort (); } catch (int) {}
  if (calls != 3 || !is (last_message, "d msg: -1 > 0"))
    __builtin_abort ();
  // review always observes, and passes its message.
  if (r_msg (-1) != -1 || calls != 4 || !is (last_message, "r msg: -1 > 0"))
    __builtin_abort ();
  // ... unless its group is ignored.
  if (r_quiet (-1) != -1 || calls != 4)
    __builtin_abort ();
  // mandatory enforces, and passes its message.
  try { m_msg (-1); __builtin_abort (); } catch (int) {}
  if (calls != 5 || !is (last_message, "m msg: -1 > 0"))
    __builtin_abort ();
}
