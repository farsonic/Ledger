/*
 * lxpriv.h - what layouttab.c, viewtab.c and formtab.c share: MUI helpers in the dialogs' style
 * (as dialogs.c has them) and each file's entry points for lx_command.
 */
#ifndef LEDGER_LXPRIV_H
#define LEDGER_LXPRIV_H

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <libraries/asl.h>
#include <graphics/gfxmacros.h>
#ifndef MAKE_ID
#define MAKE_ID(a,b,c,d) ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))
#endif
#include <clib/alib_protos.h>
#include "app.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"
#include "layout.h"
#include "../core/pagesetup.h"

#undef Label1
#undef LLabel1
#define Label1(l)  QLabel(l, MUIO_Label_SingleFrame)
#define LLabel1(l) QLabel(l, MUIO_Label_LeftAligned | MUIO_Label_SingleFrame)

ULONG       lx_xget(Object *o, ULONG attr);
const char *lx_sget(Object *o);
Object     *lx_str(const char *init, int max);
Object     *lx_int(long v, int max);
Object     *lx_check(int on);
Object     *lx_cycle(const char **labels, int active);
const char **lx_trl(const char *const *en, const char **out);
void        lx_latin1_utf8(const char *s, char *u, size_t cap);
void        lx_utf8_latin1(const char *u, char *s, size_t cap);
/* the ASL file requester (as main.c's): path replaced by the choice; 1 if one was made */
int         lx_file_request(int save, const char *title, char *path, size_t cap, const char *pattern);
/* the ribbon list's choice for a list command, or -1 when it came from a menu (no list) */
LONG        lx_value(void);
/* a list from the user: title, prompt, entries (Latin-1); the one chosen or -1 */
int         lx_pick(const char *title, const char *prompt, const char *const *labels, int n, int active);
/* a cell address or range typed (A1, $A$1:$B$4, 1:2, A:B, Sheet!A1); 0 if it isn't one */
int         lx_parse_range(const char *latin1, Rect *r, u8 *flags);
void        lx_range_text(Rect r, int rows_only, int cols_only, char *out);

/* layouttab.c */
void lt_command(ULONG id, LONG v);
void lt_page_setup(int page);
/* viewtab.c */
void vt_command(ULONG id, LONG v);
int  vt_bit(int b);
void vt_update(void);
void vt_switch_list(void);
/* formtab.c */
void ft_command(ULONG id, LONG v);
int  ft_bit(int b);
void ft_update(void);
void ft_init(void);
void ft_cleanup(void);
void ft_names_list(void);
void ft_recent_list(void);
void ft_used_function(const char *name);
/* the grid's overlays (viewtab.c draws the pages, formtab.c the arrows and marks) */
void vt_draw(struct RastPort *rp, const GridGeom *g);
void ft_draw(struct RastPort *rp, const GridGeom *g);

/* main.c's clipboard (Paste Special) */
Clip *app_clip(void);

#endif
