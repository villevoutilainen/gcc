// D4324: the GIMPLE engine keeps a predicate a call's postcondition
// establishes about the scalar it returns ('post (r: is_open (r))'),
// across copies of the value, until the value is handed to a call that
// may change what the predicate says ('close_it (fd)').
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-proofs -fcontract-analysis-engine=gimple" }

#include <contracts>
namespace sc = std::contracts;

bool is_open (int fd) symbolic;
int open_it () post<sc::symbolic_assert_v> (r: is_open (r));
void close_it (int fd);
void use_it (int fd) pre<sc::symbolic_assert_v> (is_open (fd));

void ok () { int fd = open_it (); int copy = fd; use_it (copy); }
void closed () { int fd = open_it (); close_it (fd); use_it (fd); } // { dg-warning "cannot verify that .bool is_open.int.. ..fd.. holds" }
