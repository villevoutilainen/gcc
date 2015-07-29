// { dg-do compile }
// { dg-options "-pedantic -std=c++14" }

struct X
{
  int x;
  int y;
  X() :
    x{42},
    y{666}, // { dg-warning "comma at end of member initializer list" }
  {}
};
