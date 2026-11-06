// See d4324-terminate-wrapper-multi-tu.C.
#include <contracts>

int g (int x) pre (x >= 0) { return x; }
