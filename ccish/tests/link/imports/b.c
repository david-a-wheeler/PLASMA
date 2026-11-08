/* The other half of imports: see a.c. */
struct t_cmdsys { int sysver; };
extern struct t_cmdsys cmdsys __asm__("CMDSYS");
struct t_cmdsys *pcmd = &cmdsys;    /* an import's address, in data */

/* CMDSYS's version, read through the pointer in data. */
int b_version(void)
{
  return pcmd->sysver;
}
