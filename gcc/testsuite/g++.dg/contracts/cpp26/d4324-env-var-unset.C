// D4324: paired with d4324-env-var-set.C. Both tests compile the exact
// same code (d4324-env-var-shared.h); this one deliberately leaves
// D4324_ENV_VAR_TEST_IGNORE unset at run time, so my_less_mandatory's
// operator() instead notices the violated precondition via ctx.check() --
// proving the same binary behavior is driven purely by the environment,
// not anything baked in at compile time.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=observe" }

#include "d4324-env-var-shared.h"

// { dg-output "would terminate: precondition violated(\n|\r\n|\r)" }
