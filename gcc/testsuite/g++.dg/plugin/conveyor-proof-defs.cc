// Real bodies for conveyor-proof-defs.h's declarations -- a separate
// translation unit from every scenario test (see
// .claude/plans/stateless-jumping-shore.md), so those tests only ever
// see the declarations, never these bodies.  Compiled alongside each
// scenario test via dg-additional-sources; the plugin loaded for that
// test sees this TU too, and checks these bodies against their own
// postconditions.  The analysis never evaluates the opaque conveyor
// predicate check_it, so a body whose postcondition names it returns
// the result of a helper whose never_proven postcondition states the
// fact (trusted as written, and carried to the returned value).

#include "conveyor-proof-defs.h"

void use_positive (int x) { (void) x; }
int  compute_positive () { return 1; }
int  compute_negative () { return -1; }

// conveyor must be repeated identically on every redeclaration
// (check_conveyor_redeclaration) -- unlike a pre/post clause, which
// only needs to appear on one declaration.
bool check_it (int v) conveyor { return v > 0; }
static int one () post<sc::never_proven_conveyor_v> (r: check_it (r))
{ return 1; }
static int minus_one () post<sc::never_proven_conveyor_v> (r: !check_it (r))
{ return -1; }
int  produce () { return one (); }
int  produce_bad () { return minus_one (); }
void consume (int x) { (void) x; }
