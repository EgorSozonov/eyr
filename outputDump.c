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

<UNNAMED BLOCK 0x62cb59f93f00>:
  i = (__int32_t)1;
  goto <UNNAMED BLOCK 0x62cb59f941c0>;

<UNNAMED BLOCK 0x62cb59f94170>:
  return (__int32_t)0;

<UNNAMED BLOCK 0x62cb59f941c0>:
  if (i < (__int32_t)4) goto <UNNAMED BLOCK 0x62cb59f94300>; else goto <UNNAMED BLOCK 0x62cb59f94170>;

<UNNAMED BLOCK 0x62cb59f94300>:
  j = (__int32_t)100;
  goto <UNNAMED BLOCK 0x62cb59f94630>;

<UNNAMED BLOCK 0x62cb59f94590>:
  (void)printf ("%d\n", i);
  i = i + (__int32_t)1;
  goto <UNNAMED BLOCK 0x62cb59f941c0>;

<UNNAMED BLOCK 0x62cb59f94630>:
  if (j > (__int32_t)97) goto <UNNAMED BLOCK 0x62cb59f94770>; else goto <UNNAMED BLOCK 0x62cb59f94590>;

<UNNAMED BLOCK 0x62cb59f94770>:
  (void)printf ("%d\n", j);
  j = j - (__int32_t)1;
  goto <UNNAMED BLOCK 0x62cb59f94630>;
}

