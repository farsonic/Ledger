#include "xls.h"
#include <stdio.h>
Book *ods_load(const u8 *data, size_t len, char *err, size_t errcap) { (void)data; (void)len; if (err) snprintf(err, errcap, "-"); return NULL; }
int ods_is(const u8 *data, size_t len) { (void)data; (void)len; return 0; }
