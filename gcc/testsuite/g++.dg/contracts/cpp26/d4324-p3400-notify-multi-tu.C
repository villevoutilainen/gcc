// D4324/P3400: a label that constructs its own, properly-typed custom
// data item and passes it directly to a real, ordinary, ODR-linked
// handler function -- NOT P3400's own queryable_label/
// query_control_object mechanism (which needs void*/key-index type
// erasure only because it must squeeze arbitrary label data through
// ONE universal, generically-compiled interception point,
// handle_contract_violation, whose signature can never change per
// label). No such constraint applies here: label_base::operator()
// gains a new, optional hook (notify_label), consulted the same way
// local_violation_label already is, letting a label call any ordinary,
// strongly-typed function it likes -- declared in a shared header,
// defined in exactly one TU (d4324-p3400-notify-multi-tu-handler.cc),
// called from three TUs total (this one, d4324-p3400-notify-multi-tu-
// user2.cc, and via combined_label's own new chaining) via perfectly
// ordinary one-definition-rule linkage -- no void*, no key, no index,
// and no change to ::handle_contract_violation/contract_violation at
// all.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=observe" }
// { dg-additional-sources "d4324-p3400-notify-multi-tu-handler.cc d4324-p3400-notify-multi-tu-user2.cc" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "d4324-p3400-notify-shared.h"

int f (int x) pre<alice_notify>(x > 0) { return x; }

// Proves the facet composes through combined_label's own new
// chaining: alice_notify's notify() still fires even combined with an
// unrelated, real P3400 facet (review's own compute_semantic).
int h (int x) pre<alice_notify | P3400::review>(x > 50) { return x; }

int
main ()
{
  // f: this TU's own use of the shared handler.
  if (f (-1) != -1)
    __builtin_abort ();
  // g: d4324-p3400-notify-multi-tu-user2.cc's own use of the *same*
  // shared handler, with a different label instance (different owner
  // name) -- the "used in many TUs" case.
  if (g (-1) != -1)
    __builtin_abort ();
  // h: combined_label's new notify() chaining, still reaching the
  // same shared handler.
  if (h (-1) != -1)
    __builtin_abort ();

  if (notify_owner_calls != 3)
    __builtin_abort ();

  if (__builtin_strcmp (notify_owner_names[0], "alice") != 0
      || __builtin_strcmp (notify_owner_comments[0], "x > 0") != 0)
    __builtin_abort ();
  if (__builtin_strcmp (notify_owner_names[1], "bob") != 0
      || __builtin_strcmp (notify_owner_comments[1], "x > 0") != 0)
    __builtin_abort ();
  if (__builtin_strcmp (notify_owner_names[2], "alice") != 0
      || __builtin_strcmp (notify_owner_comments[2], "x > 50") != 0)
    __builtin_abort ();

  return 0;
}
