/* { dg-do compile { target { ! riscv_abi_e } } } */
/* { dg-options "-mcpu=thead-c906" }  */
long foo(long x) { return x ^ 0x80000000; }

