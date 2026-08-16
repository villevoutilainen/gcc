// D4324: -fcontracts-group-evaluation-semantic= requires a
// <group>:<semantic> argument; a missing ':' is a real, diagnosed
// command-line error (this option is real, not the library's own
// best-effort _P3400_FAKE_GROUP_CONFIG macro emulation it replaces).
// { dg-do compile }
// { dg-options "-fcontracts-group-evaluation-semantic=nocolonhere" }
// { dg-error "invalid argument .nocolonhere. to .-fcontracts-group-evaluation-semantic=." "" { target *-*-* } 0 }

int main () { return 0; }
