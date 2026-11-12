// D4324 (P4445 2.4.2.1): -Wfunction-pointer-contract-mismatch also warns
// where a contracted function pointer is copied somewhere that can't have a
// contract of its own -- a return value, an element of an array or of an
// initializer list (a std::vector's initializer), a member of an aggregate --
// so that calls through the copy aren't checked against it.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -Wfunction-pointer-contract-mismatch" }

#include <functional>
#include <vector>

void f (int, int) {}
void (*p) (int a, int b) pre (a <= b) = f;
using fn = void (*) (int, int);

fn ret () { return p; }			// { dg-warning "returning .p., which has its own contract" }
fn &ret_ref () { return p; }		// { dg-warning "returning .p., which has its own contract" }
std::vector<fn> vec{p};			// { dg-warning "copying .p., which has its own contract, into an element" }
fn arr[] = {p};				// { dg-warning "copying .p., which has its own contract, into an element" }
struct holder { fn cb; };
holder h{p};				// { dg-warning "assigning .p., which has its own contract, to .holder::cb." }

// A function, or a pointer without a contract, has nothing to lose.
void (*plain) (int, int) = f;
fn ret_plain () { return plain; }
fn ret_fn () { return f; }
fn arr2[] = {plain, f};
