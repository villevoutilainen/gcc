// D4324: __cpp_contract_control_objects is defined exactly when
// -fcontract-control-objects is passed, mirroring how __cpp_contracts
// itself is tied to plain -fcontracts.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#ifndef __cpp_contract_control_objects
#error "__cpp_contract_control_objects should be defined"
#endif

#if __cpp_contract_control_objects != 1
#error "__cpp_contract_control_objects should be 1"
#endif
