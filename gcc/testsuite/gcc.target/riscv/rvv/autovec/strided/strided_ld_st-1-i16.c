/* { dg-do compile } */
/* { dg-options "-march=rv64gcv -mabi=lp64d -mno-vector-strict-align -fno-vect-cost-model -fdump-rtl-expand-details" } */

#include "strided_ld_st.h"

DEF_STRIDED_LD_ST_FORM_1(int16_t)

/* { dg-final { scan-rtl-dump-times ".MASK_LEN_STRIDED_LOAD " 4 "expand" { target {
     any-opts "-O3"
     no-opts "-mrvv-vector-bits=zvl"
   } } } } */
/* { dg-final { scan-rtl-dump-times ".MASK_LEN_STRIDED_STORE " 4 "expand" { target {
     any-opts "-O3"
     no-opts "-mrvv-vector-bits=zvl"
   } } } } */

/* { dg-final { scan-rtl-dump-times ".MASK_LEN_STRIDED_LOAD " 2 "expand" { target {
     any-opts "-O2"
     no-opts "-mrvv-vector-bits=zvl"
   } } } } */
/* { dg-final { scan-rtl-dump-times ".MASK_LEN_STRIDED_STORE " 2 "expand" { target {
     any-opts "-O2"
     no-opts "-mrvv-vector-bits=zvl"
   } } } } */

/* { dg-final { scan-assembler-times {vlse16.v} 1 { target {
     no-opts "-mrvv-vector-bits=zvl"
   } } } } */
/* { dg-final { scan-assembler-times {vsse16.v} 1 { target {
     no-opts "-mrvv-vector-bits=zvl"
   } } } } */
