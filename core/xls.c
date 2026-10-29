#include "xls.h"
#include <stdio.h>
Book *xls_load(const u8 *data, size_t len, char *err, size_t errcap) { (void)data; (void)len; if (err) snprintf(err, errcap, "-"); return NULL; }
