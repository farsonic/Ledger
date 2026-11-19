/* respathtest.c - core/respath.c: Amiga paths joined, and the shared fonts' and dictionaries'
   drawers looked for beside the program, in Bureau: and in FONTS: (included by coretest.c) */
#include "../core/respath.h"

/* the drawers that "exist": a list of names ending in NULL; every name asked for is counted */
typedef struct { const char *const *have; int asked; } RpWorld;
static int rp_exists(const char *dir, void *ud)
{
  RpWorld *w = ud;
  int i;
  w->asked++;
  for (i = 0; w->have[i]; i++) if (!strcmp(w->have[i], dir)) return 1;
  return 0;
}
static const char *rp_pick(int kind, const char *const *have, int *asked)
{
  RpWorld w;
  const char *r;
  w.have = have; w.asked = 0;
  r = res_pick(kind, rp_exists, &w);
  if (asked) *asked = w.asked;
  return r;
}

static void respath_tests(void)
{
  static const char *const all[] = { "PROGDIR:Fonts", "PROGDIR:TrueType", "PROGDIR:Dict", "Bureau:Fonts", "Bureau:TrueType", "Bureau:Dict", NULL };
  static const char *const suite[] = { "Bureau:Fonts", "Bureau:TrueType", "Bureau:Dict", NULL };
  static const char *const none[] = { NULL };
  char out[32], small[8];
  int asked;
  const char *r;
  /* joins */
  ok("respath: join after ':'", res_join(out, sizeof out, "FONTS:", "x.font") && !strcmp(out, "FONTS:x.font"));
  ok("respath: join after '/'", res_join(out, sizeof out, "Work:Bureau/", "Dict") && !strcmp(out, "Work:Bureau/Dict"));
  ok("respath: join after a plain dir", res_join(out, sizeof out, "PROGDIR:TrueType", "fonts.list") && !strcmp(out, "PROGDIR:TrueType/fonts.list"));
  ok("respath: join a sub-path", res_join(out, sizeof out, "Bureau:Fonts", "QuillSans/13") && !strcmp(out, "Bureau:Fonts/QuillSans/13"));
  ok("respath: join with an empty dir or name", res_join(out, sizeof out, "", "Fonts") && !strcmp(out, "Fonts") &&
     res_join(out, sizeof out, "Bureau:Dict", "") && !strcmp(out, "Bureau:Dict"));
  ok("respath: join that just fits", res_join(small, sizeof small, "A:", "bcdef") && !strcmp(small, "A:bcdef"));
  strcpy(small, "junk");
  ok("respath: join one byte too long gives \"\"", !res_join(small, sizeof small, "A:", "bcdefg") && small[0] == 0);
  strcpy(small, "junk");
  ok("respath: join counts the '/'", !res_join(small, sizeof small, "AB:c", "def") && small[0] == 0 &&
     res_join(small, sizeof small, "AB:c", "de") && !strcmp(small, "AB:c/de"));
  ok("respath: join into nothing", !res_join(small, 0, "A:", "b"));
  /* beside the program first */
  r = rp_pick(RES_FONTS, all, &asked);
  ok("respath: fonts beside the program first", r && !strcmp(r, "PROGDIR:Fonts") && asked == 1);
  r = rp_pick(RES_TRUETYPE, all, NULL);
  ok("respath: TrueType beside the program first", r && !strcmp(r, "PROGDIR:TrueType"));
  r = rp_pick(RES_DICT, all, NULL);
  ok("respath: Dict beside the program first", r && !strcmp(r, "PROGDIR:Dict"));
  /* the suite's drawers second */
  r = rp_pick(RES_FONTS, suite, &asked);
  ok("respath: fonts in Bureau:", r && !strcmp(r, "Bureau:Fonts") && asked == 2);
  r = rp_pick(RES_TRUETYPE, suite, NULL);
  ok("respath: TrueType in Bureau:", r && !strcmp(r, "Bureau:TrueType"));
  r = rp_pick(RES_DICT, suite, NULL);
  ok("respath: Dict in Bureau:", r && !strcmp(r, "Bureau:Dict"));
  /* no Bureau: and nothing beside the program: FONTS: for screen fonts (not asked), nothing for the rest */
  r = rp_pick(RES_FONTS, none, &asked);
  ok("respath: fonts fall to FONTS: without asking", r && !strcmp(r, "FONTS:") && asked == 2);
  ok("respath: no TrueType or Dict anywhere", !rp_pick(RES_TRUETYPE, none, NULL) && !rp_pick(RES_DICT, none, NULL));
  ok("respath: an unknown kind", !rp_pick(7, all, NULL) && !rp_pick(-1, all, NULL));
}
