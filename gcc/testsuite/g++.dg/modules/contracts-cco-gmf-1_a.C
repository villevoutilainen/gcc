// { dg-module-do run }
// { dg-additional-options { -std=c++26 -fmodules-ts -fcontracts -fcontract-control-objects } }
// D4324: the checks of functions from the global module fragment --
// implicitly inline, compiled before the TU knows it is a module
// interface -- have helpers a module can stream.

module;
#include "contracts-cco-gmf-1.h"
export module contracts_cco_gmf;
// { dg-module-cmi "contracts_cco_gmf" }

export using ::Q;
export using ::R;
export using ::make_r;
export using ::gmf_fails;
