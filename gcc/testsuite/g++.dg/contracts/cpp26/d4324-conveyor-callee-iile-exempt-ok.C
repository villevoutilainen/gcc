// D4324: a genuinely immediately-invoked closure call ('[...](...){...}
// ()', the closure object constructed right there in the same
// expression, never stored) is exempt from the callee-must-be-conveyor
// rule entirely -- deliberately, and unlike a stored lambda (see
// d4324-conveyor-callee-lambda-not-conveyor-bad.C): its own body is
// already walked directly by the same analysis, with full access to
// the caller's own established facts (capture-proxy redirection), so
// requiring the keyword would only add a second, context-free,
// standalone analysis of the same body that cannot see anything
// captured by reference -- see contracts.cc's own is_iile_operator_
// call_p comment, and its own regression guard,
// d4324-conveyor-divmod-iile-ok.C, for the concrete case (by-reference-
// captured outer fact) this exemption exists to keep working. See
// .claude/plans/well-we-last-discussed-ethereal-duckling.md.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int x) conveyor
{
  return [&] () { return x + 1; } ();
}

int main () { return f (1) - 2; }
