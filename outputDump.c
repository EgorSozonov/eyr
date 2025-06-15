struct Str;

struct Str
{
  const const char * c;
  __int32_t len;
};

extern int
printf (const char * format); /* (imported) */

extern void *
malloc (__int32_t p); /* (imported) */

extern int
main (int argc, const char * * argv)
{
  __int32_t i;
  __int32_t j;

<UNNAMED BLOCK 0x5c2c0833bf00>:
  i = (__int32_t)1;
  goto <UNNAMED BLOCK 0x5c2c0833c1c0>;

<UNNAMED BLOCK 0x5c2c0833c170>:
  return (__int32_t)0;

<UNNAMED BLOCK 0x5c2c0833c1c0>:
  if (i < (__int32_t)4) goto <UNNAMED BLOCK 0x5c2c0833c300>; else goto <UNNAMED BLOCK 0x5c2c0833c170>;

<UNNAMED BLOCK 0x5c2c0833c300>:
  j = (__int32_t)100;
  goto <UNNAMED BLOCK 0x5c2c0833c630>;

<UNNAMED BLOCK 0x5c2c0833c590>:
  (void)printf ("%d\n", i);
  i = i + (__int32_t)1;
  goto <UNNAMED BLOCK 0x5c2c0833c1c0>;

<UNNAMED BLOCK 0x5c2c0833c630>:
  if (j > (__int32_t)97) goto <UNNAMED BLOCK 0x5c2c0833c770>; else goto <UNNAMED BLOCK 0x5c2c0833c590>;

<UNNAMED BLOCK 0x5c2c0833c770>:
  (void)printf ("%d\n", j);
  j = j - (__int32_t)1;
  goto <UNNAMED BLOCK 0x5c2c0833c630>;
}

