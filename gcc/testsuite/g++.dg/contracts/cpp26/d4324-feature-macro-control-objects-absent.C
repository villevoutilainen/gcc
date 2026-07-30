// D4324: without -fcontract-control-objects, __cpp_contract_control_objects
// must not be defined -- this is what bits/c++config's __glibcxx_assert
// rewrite gates on to decide whether contract_assert<noexcept_assert_v>
// is even nameable in this TU.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts" }

#ifdef __cpp_contract_control_objects
#error "__cpp_contract_control_objects should not be defined without -fcontract-control-objects"
#endif
