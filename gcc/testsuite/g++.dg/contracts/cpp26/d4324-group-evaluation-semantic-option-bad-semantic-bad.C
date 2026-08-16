// D4324: -fcontracts-group-evaluation-semantic='s <semantic> must be one
// of ignore/observe/enforce/quick_enforce.
// { dg-do compile }
// { dg-options "-fcontracts-group-evaluation-semantic=safety:bogus" }
// { dg-error "invalid contract evaluation semantic .bogus. in .-fcontracts-group-evaluation-semantic=safety:bogus." "" { target *-*-* } 0 }

int main () { return 0; }
