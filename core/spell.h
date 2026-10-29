/*
 * spell.h - spelling (Quill's, shared with Ledger): a word list (one word a line, UTF-8; SCOWL's British English ships with Quill),
 * the user's own words, words to ignore, and suggestions for a word not in the list (the words one
 * edit away: a letter left out, added, changed, or two swapped).
 */
#ifndef LEDGER_SPELL_H
#define LEDGER_SPELL_H

#include "book.h"

typedef struct {
  char *text; u32 *word; u32 n;          /* the list, its words sorted (offsets into text) */
  char **extra; u32 nextra, extracap;    /* the user's words and ignored ones (added at run time) */
} Spell;

int  spell_load(Spell *s, const char *path);       /* 0 if the file can't be read (s stays empty) */
int  spell_load_mem(Spell *s, const char *text, size_t n);  /* from a copy of text; 0 if out of memory */
void spell_free(Spell *s);                         /* frees everything; s is left empty */
void spell_add(Spell *s, const char *word, u32 n); /* known from now on */
/* is this word (UTF-8, n bytes) spelt right? Words with digits, and capitals of a known word, are */
int  spell_ok(const Spell *s, const char *word, u32 n);
/* up to max suggestions for a word, best first, each malloc'd into out; returns how many */
int  spell_suggest(const Spell *s, const char *word, u32 n, char **out, int max);
/* the extent of the word (letters, inner apostrophes) in t at off..: start and end; 0 if none */
int  spell_word_at(const char *t, u32 len, u32 off, u32 *ws, u32 *we);

/* the next misspelt word in the workbook's text cells from *sheet, *r, *c, byte *off (see spell.c);
   1 found: *sheet, *r, *c are its cell, *ws..*we the word, *off where to go on from */
int  spell_next_cell(const Spell *s, Book *b, int all_sheets, int *sheet, i32 *r, i32 *c, u32 *off, u32 *ws, u32 *we);
/* the thesaurus: words of the list sharing word's stem (malloc'd into out); how many */
int  spell_related(const Spell *s, const char *word, char **out, int max);
/* the word at ws..we of a text cell changed to with; and every one like word (whole words, any case) */
int  spell_change(Book *b, int sheet, i32 r, i32 c, u32 ws, u32 we, const char *with);
int  spell_change_all(Book *b, int all_sheets, int sheet, const char *word, const char *with);

#endif
