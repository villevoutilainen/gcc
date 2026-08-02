// D4324, Increment O: an unconditionally-infinite loop that always
// eventually returns must still be accepted -- oa_stmt_terminates_p's
// own WHILE_STMT case (Increment L) already recognizes this shape.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int n) conveyor
{
  while (true)
    {
      if (n > 0)
	return n;
      n++;
    }
}

int main () { return f (1) - 1; }
