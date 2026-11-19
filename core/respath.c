/* respath.c - see respath.h */
#include <string.h>
#include "respath.h"

static const char *const res_dirs[3][3] = {
  { "PROGDIR:Fonts", "Bureau:Fonts", "FONTS:" },
  { "PROGDIR:TrueType", "Bureau:TrueType", NULL },
  { "PROGDIR:Dict", "Bureau:Dict", NULL }
};

int res_join(char *out, size_t n, const char *dir, const char *name)
{
  size_t d = strlen(dir), m = strlen(name);
  int slash = d > 0 && m > 0 && dir[d - 1] != ':' && dir[d - 1] != '/';
  if (n == 0) return 0;
  if (d + slash + m >= n) { out[0] = 0; return 0; }
  memcpy(out, dir, d);
  if (slash) out[d++] = '/';
  memcpy(out + d, name, m + 1);
  return 1;
}

const char *res_pick(int kind, int (*exists)(const char *dir, void *ud), void *ud)
{
  int i;
  if (kind < RES_FONTS || kind > RES_DICT) return NULL;
  for (i = 0; i < 3 && res_dirs[kind][i]; i++)
    if (!strcmp(res_dirs[kind][i], "FONTS:") || exists(res_dirs[kind][i], ud)) return res_dirs[kind][i];
  return NULL;
}
