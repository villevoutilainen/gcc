// Real bodies for conveyor-proof-defs.h's declarations -- a separate
// translation unit from every scenario test (see
// .claude/plans/stateless-jumping-shore.md), so those tests only ever
// see the declarations, never these bodies.  Compiled alongside each
// scenario test via dg-additional-sources; the plugin loaded for that
// test sees this TU too, but there is nothing here for it to flag
// (these are plain definitions, not calls to anything contracted).

#include "conveyor-proof-defs.h"

void use_positive (int x) { (void) x; }
int  compute_positive () { return 1; }
int  compute_negative () { return -1; }

// conveyor must be repeated identically on every redeclaration
// (check_conveyor_redeclaration) -- unlike a pre/post clause, which
// only needs to appear on one declaration.
bool check_it (int v) conveyor { return v > 0; }
int  produce () { return 1; }
int  produce_bad () { return -1; }
void consume (int x) { (void) x; }
