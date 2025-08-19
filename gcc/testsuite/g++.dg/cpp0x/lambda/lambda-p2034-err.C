// { dg-do compile { target c++23 } }
// { dg-skip-if "requires hosted libstdc++ for cassert" { ! hostedlib } }

void test01()
{
  int x = 42;
  int y = 666;
  auto z = [const x, mutable y]() {
    x = 666; // { dg-error "read-only variable" }
    y = 42;
  };
}

void test02()
{
  const int x = 42;
  int y = 666;
  auto z = [const x, mutable y]() {
    x = 666; // { dg-error "read-only variable" }
    y = 42;
  };
}

void test03()
{
  int x = 42;
  int y = 666;
  auto z = [const &x, mutable y]() {
    x = 666; // { dg-error "read-only reference" }
    y = 42;
  };
}

void test04()
{
  const int x = 42;
  int y = 666;
  auto z = [const &x, mutable y]() {
    x = 666; // { dg-error "read-only reference" }
    y = 42;
  };
}

void test05()
{
  int x = 42;
  int y = 666;
  auto z = [const &x, mutable y]() mutable {
    x = 666; // { dg-error "read-only reference" }
    y = 42;
  };
}

void test06()
{
  const int x = 42;
  int y = 666;
  auto z = [const &x, mutable y]() mutable {
    x = 666; // { dg-error "read-only reference" }
    y = 42;
  };
}

void test07()
{
  int x = 42;
  int y = 666;
  auto z = [const &x, mutable y]() const mutable { // { dg-error "cannot have a storage-class-specifier" }
    x = 666; // { dg-error "read-only reference" }
    y = 42;
  };
}

void test08()
{
  int x = 42;
  int y = 666;
  auto z = [const &x, mutable y]() mutable const { // { dg-error "type-specifier invalid in lambda" }
    x = 666; // { dg-error "read-only reference" }
    y = 42;
  };
}

void test09()
{
  int x = 42;
  int y = 666;
  auto z = []() const static { // { dg-error "cannot have a storage-class-specifier" }
  };
}

void test10()
{
  int x = 42;
  int y = 666;
  auto z = []() static const { // { dg-error "type-specifier invalid in lambda" }
  };
}

void test11()
{
  int x = 42;
  int y = 666;
  auto z = [const x, const mutable y]() { // { dg-error "expected identifier" }
    x = 666; // { dg-error "read-only variable" }
    y = 42; // { dg-error "is not captured" }
  };
}

void test12()
{
  int x = 42;
  int y = 666;
  auto z = [const x, mutable const y]() { // { dg-error "expected identifier" }
    x = 666; // { dg-error "read-only variable" }
    y = 42; // { dg-error "is not captured" }
  };
}

template <class T> void f(T t)
{
  auto z = [const mutable t]() { // { dg-error "expected identifier" }
    t = 666; // { dg-error "is not captured" }
  };
}

template <class T> void f2(T t)
{
  auto z = [mutable const t]() mutable { // { dg-error "expected identifier" }
    t = 666; // { dg-error "is not captured" }
  };
}

template <class T> void f3(T t)
{
  auto z = [t]() const {
    t = 666; // { dg-error "read-only variable" }
  };
}

void test13()
{
  f(42);
  f2(42);
  f3(42);
}

void test14()
{
  int x = 42;
  const int y = 666;
  auto z = [const x, mutable y] { // { dg-error "cannot be declared both .const. and .mutable." }
    x = 666; // { dg-error "read-only variable" }
    y = 42;
  };
}

void test15()
{
  int x = 42;
  const int y = 666;
  auto z = [const x, mutable &y] {
    x = 666; // { dg-error "read-only variable" }
    y = 42;  // { dg-error "read-only reference" }
  };
}

void test16()
{
  int x = 42;
  const int y = 666;
  auto z = [const x, mutable y]() mutable { // { dg-error "cannot be declared both .const. and .mutable." }
    x = 666; // { dg-error "read-only variable" }
    y = 42;
  };
}

void test17()
{
  int x = 42;
  const int y = 666;
  auto z = [const x, mutable &y]() mutable {
    x = 666; // { dg-error "read-only variable" }
    y = 42;  // { dg-error "read-only reference" }
  };
}

void test18()
{
  int x = 42;
  const int y = 666;
  auto z = [const x, mutable z = y]() mutable {
    x = 666; // { dg-error "read-only variable" }
    z = 42;
  };
}

void test19()
{
  int x = 42;
  const int y = 666;
  auto z = [const x, mutable &z = y]() mutable {
    x = 666; // { dg-error "read-only variable" }
    z = 42;  // { dg-error "read-only reference" }
  };
}

void test20()
{
  int x = 42;
  const int y = 666;
  auto z = [const a = x, mutable z = y]() mutable {
    a = 666; // { dg-error "read-only variable" }
    z = 42;
  };
}

void test21()
{
  int x = 42;
  const int y = 666;
  auto z = [const &a = x, mutable &z = y]() mutable {
    a = 666; // { dg-error "read-only reference" }
    z = 42;  // { dg-error "read-only reference" }
  };
}
