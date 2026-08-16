// D4324: -fcontracts-group-evaluation-semantic= requires a non-empty
// group name before the ':'.
// { dg-do compile }
// { dg-options "-fcontracts-group-evaluation-semantic=:observe" }
// { dg-error "invalid argument .:observe. to .-fcontracts-group-evaluation-semantic=." "" { target *-*-* } 0 }

int main () { return 0; }
