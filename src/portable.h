/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 Copyright (c) 2012  MOGI, Kazuhiro <kazhiro@marumo.ne.jp>

 Permission to use, copy, modify, and/or distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.

 THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 PERFORMANCE OF THIS SOFTWARE.
 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#ifndef PORTABLE_H
#define PORTABLE_H

#if (MSC_VER < 1300) 

typedef unsigned char     uint8_t;
typedef   signed char      int8_t;
typedef unsigned short   uint16_t;
typedef   signed short    int16_t;
typedef unsigned int     uint32_t;
typedef   signed int      int32_t;
typedef unsigned __int64 uint64_t;
typedef   signed __int64  int64_t;

#else

#include <inttypes.h>

#endif

#endif /* PORTABLE_H */
