// D4324: a failed default_v check calls the replaceable violation handler,
// with the predicate text and the build's semantic; an exception the
// handler throws propagates out of the check to the caller.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <cstring>

struct oops {};

int calls;
bool text_ok, enforce_ok;

void
handle_contract_violation (const std::contracts::contract_violation& v)
{
  ++calls;
  text_ok = std::strcmp (v.comment (), "x > 0") == 0;
  enforce_ok = v.semantic () == std::contracts::evaluation_semantic::enforce;
  throw oops {};
}

int f (int x) pre<std::contracts::default_v>(x > 0) { return x; }
int g (int x) pre (x > 0) { return x; }		// bare pre: default_v

int main ()
{
  int caught = 0;
  try { f (-1); } catch (const oops&) { ++caught; }
  if (!text_ok || !enforce_ok)
    __builtin_abort ();
  try { g (-1); } catch (const oops&) { ++caught; }
  return caught == 2 && calls == 2 ? 0 : 1;
}
