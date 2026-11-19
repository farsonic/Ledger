/* respath.h - where the shared fonts and dictionaries are: beside the program, in the Bureau suite
   (Bureau:), or for screen fonts the system's FONTS:. Portable C99, no AmigaOS calls. */
#ifndef RESPATH_H
#define RESPATH_H
#include <stddef.h>

enum { RES_FONTS, RES_TRUETYPE, RES_DICT };

/* dir and name joined into an Amiga path in out (n bytes): no '/' after a dir ending in ':' or '/'
   ("FONTS:" + "x.font" is "FONTS:x.font"). 1, or 0 if it didn't fit (out is "" then). */
int res_join(char *out, size_t n, const char *dir, const char *name);

/* the first drawer for kind (RES_FONTS: PROGDIR:Fonts, Bureau:Fonts, FONTS:; RES_TRUETYPE:
   PROGDIR:TrueType, Bureau:TrueType; RES_DICT: PROGDIR:Dict, Bureau:Dict) that exists says is
   there, or NULL. FONTS: is taken without asking. */
const char *res_pick(int kind, int (*exists)(const char *dir, void *ud), void *ud);

#endif
