/*
 * comments.h - threaded comments, as GridCraft splits them from notes: a comment is a note marked
 * threaded, with replies under it, that can be resolved. In .xlsx a comment is written as Excel
 * writes it: a threadedComments part (with the persons part) and, for older readers, a legacy note
 * saying what the thread says.
 */
#ifndef LEDGER_COMMENTS_H
#define LEDGER_COMMENTS_H

#include "book.h"
#include "xml.h"
#include "zip.h"

/* a comment started on a cell (or its first entry changed): an undo step; text NULL or "" deletes it */
int  book_set_comment(Book *b, int sheet, i32 r, i32 c, const char *text, const char *author);
/* a reply added under the cell's comment; 0 if there is no comment there */
int  book_add_reply(Book *b, int sheet, i32 r, i32 c, const char *text, const char *author);
int  book_resolve_comment(Book *b, int sheet, i32 r, i32 c, int resolved);
/* every note on the sheet (sheet -1: the workbook) turned into a comment; how many */
int  book_convert_notes(Book *b, int sheet);
/* a comment's replies: how many, and the k-th (author and text copied into the buffers) */
int  comment_nreplies(const Note *n);
int  comment_reply(const Note *n, int k, char *author, size_t acap, char *text, size_t tcap);
/* the next (or previous) cell with a comment (threaded only) or note (threaded 0), in reading order
   across the sheets from sheet, r, c, wrapping; 0 if there is none */
int  comment_step(Book *b, int threaded, int forward, int *sheet, i32 *r, i32 *c);

/* (xlsx.c's hooks) */
int  cm_sheet_has(Sheet *s);                                  /* the sheet has threaded comments */
void cm_legacy(const Note *n, int si, int i, Buf *o, char *author, size_t cap);   /* the legacy note's text (and author "tc={id}") */
void cm_types(Book *b, Buf *o);                               /* [Content_Types].xml entries */
void cm_book_rels(Book *b, Buf *o, int id);                   /* workbook.xml.rels: the persons part */
void cm_sheet_rels(Sheet *s, int si, Buf *o);                 /* a sheet's rels: its threadedComments part */
void cm_parts(Book *b, ZipWriter *w);                         /* the threadedComments and persons parts */
void cm_read(Book *b, ZipReader *z, const char *path, int sheet);   /* a sheet's threadedComments part */

#endif
