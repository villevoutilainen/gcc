// { dg-module-do run }
// { dg-additional-options { -std=c++26 -fmodules-ts -fcontracts -fcontract-evaluation-semantic=observe } }
// C++26 contracts across a module boundary, the importer having none of
// its own: it must define the per-TU violation wrapper its imported
// bodies call (it is not exported; a module streams its declaration).

export module contracts_plain_2;
// { dg-module-cmi "contracts_plain_2" }

export inline int f (int x) { contract_assert (x > 0); return x; }
export inline int g (int x) pre (x > 0) { return x; }
