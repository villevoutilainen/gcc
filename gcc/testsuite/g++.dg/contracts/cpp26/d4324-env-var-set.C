// D4324: paired with d4324-env-var-unset.C. Both tests compile the exact
// same code (d4324-env-var-shared.h); this one sets
// D4324_ENV_VAR_TEST_IGNORE at run time, so my_less_mandatory's
// operator() takes the "ignore via environment variable" branch instead
// of noticing the violated precondition -- proving the same binary
// behavior is driven purely by the environment, not anything baked in at
// compile time.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=observe" }
// { dg-set-target-env-var D4324_ENV_VAR_TEST_IGNORE "1" }

#include "d4324-env-var-shared.h"

// { dg-output "ignored via environment variable(\n|\r\n|\r)" }
