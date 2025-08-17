// { dg-do compile { target c++11 } }
// { dg-skip-if "requires hosted libstdc++ for cassert" { ! hostedlib } }

void test01()
{
  int x = 42;
  int y = 666;
  auto z = [mutable x, mutable y]() {
    x = 666;
    y = 42;
  };
}

void test02()
{
  int x = 42;
  int y = 666;
  auto z = [mutable x, mutable y]() mutable {
    x = 666;
    y = 42;
  };
}

void test03()
{
  int x = 42;
  int y = 666;
  auto z = [mutable x, mutable y]() const {
    x = 666;
    y = 42;
  };
}

template <class T> void f(T t)
{
  auto z = [mutable t]() {
    t = 666;
  };
}

template <class T> void f2(T t)
{
  auto z = [mutable t]() mutable {
    t = 666;
  };
}

template <class T> void f3(T t)
{
  auto z = [mutable t]() const {
    t = 666;
  };
}

void test04()
{
  f(42);
  f2(42);
  f3(42);
}

void test05()
{
  int x = 42;
  int y = 666;
  auto z = [const x, mutable y]() {
    y = 42;
  };
}

void test06()
{
  int x = 42;
  int y = 666;
  auto z = [const x, mutable y]() mutable {
    y = 42;
  };
}

void test07()
{
  int x = 42;
  int y = 666;
  auto z = [const x, mutable y]() const {
    y = 42;
  };
}

void test08()
{
  const int x = 42;
  int y = 666;
  auto z = [const x, mutable y]() {
    y = 42;
  };
}

void test09()
{
  const int x = 42;
  int y = 666;
  auto z = [const x, mutable y]() mutable {
    y = 42;
  };
}

void test10()
{
  const int x = 42;
  int y = 666;
  auto z = [const x, mutable y]() const {
    y = 42;
  };
}

void test11()
{
  const int x = 42;
  int y = 666;
  auto z = [const x, mutable &y]() const {
    y = 42;
  };
}
