/*
 * Force-included (-include) when compiling the raw engine (src/raw) for the
 * Game & Watch and for the harness.
 *
 * Redirects malloc/free to the game heap (aw_alloc.c, RAM_EMU) and enables
 * the GNW changes of the engine. System headers are included first so that
 * the macros below only affect the engine's own code.
 */
#ifndef GNW_AW_GLUE_H
#define GNW_AW_GLUE_H

#ifndef GNW
#define GNW 1
#endif

/* The copy protection screen asks for symbols of the code wheel of the
 * original box, which the Game & Watch cannot type: skip it, like the other
 * modern ports. */
#ifndef BYPASS_PROTECTION
#define BYPASS_PROTECTION 1
#endif

/* raw/endian.h */
#define SYS_LITTLE_ENDIAN 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#ifdef __cplusplus
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <new>
#endif
#include "aw_platform.h"

#define malloc(s)     aw_malloc(s)
#define calloc(n, s)  aw_calloc(n, s)
#define realloc(p, s) aw_realloc(p, s)
#define free(p)       aw_free(p)

#endif
