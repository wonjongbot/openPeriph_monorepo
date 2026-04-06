#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>

#ifndef OPENPERIPH_EPD_DEBUG
#define OPENPERIPH_EPD_DEBUG 0
#endif

#if OPENPERIPH_EPD_DEBUG
#define Debug(__info, ...) printf("Debug: " __info, ##__VA_ARGS__)
#else
#define Debug(__info, ...)
#endif

#endif
