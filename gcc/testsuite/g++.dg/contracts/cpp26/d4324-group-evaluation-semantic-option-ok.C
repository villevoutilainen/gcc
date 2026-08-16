// D4324: -fcontracts-group-evaluation-semantic=group:semantic is a real,
// repeatable command-line option (not yet wired to anything consuming
// it -- that's a separate change); this just confirms one or several
// well-formed occurrences are accepted without any diagnostic.
// { dg-do compile }
// { dg-options "-fcontracts-group-evaluation-semantic=safety:observe -fcontracts-group-evaluation-semantic=perf:ignore -fcontracts-group-evaluation-semantic=safety:enforce" }

int main () { return 0; }
