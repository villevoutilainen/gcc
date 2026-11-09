// { dg-module-do run }
// { dg-additional-options { -std=c++26 -fmodules-ts -fcontracts -fcontract-evaluation-semantic=observe } }
// C++26 contracts across a module boundary: an importer calls the
// per-TU violation wrappers its imported bodies use (it defines its own
// copies), sees the contract specifiers of imported functions, and has
// contracts of its own besides.

export module contracts_plain;
// { dg-module-cmi "contracts_plain" }

export inline int f (int x) { contract_assert (x > 0); return x; }
export inline int g (int x) pre (x > 0) post (r: r > 1) { return x; }
