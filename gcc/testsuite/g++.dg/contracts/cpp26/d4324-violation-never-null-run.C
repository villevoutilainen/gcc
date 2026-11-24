// D4324: a violation's comment and message are never null: empty when
// none was given (here, an assertion_info given to
// invoke_violation_handler with a null comment and no message).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <source_location>

namespace sc = std::contracts;

int calls;
bool comment_empty, message_empty, message_kept;

void
handle_contract_violation (const sc::contract_violation& v)
{
  ++calls;
  comment_empty = v.comment () && v.comment ()[0] == '\0';
  message_empty = v.message () && v.message ()[0] == '\0';
  message_kept = v.message () && v.message ()[0] == 'm';
}

int
main ()
{
  sc::assertion_info info (nullptr, std::source_location::current (),
			   sc::evaluation_semantic::observe,
			   sc::assertion_kind::assert);
  sc::invoke_violation_handler (info, sc::detection_mode::predicate_false);
  if (calls != 1 || !comment_empty || !message_empty)
    __builtin_abort ();
  info.set_message ("m");
  sc::invoke_violation_handler (info, sc::detection_mode::predicate_false);
  if (calls != 2 || !comment_empty || !message_kept)
    __builtin_abort ();
}
