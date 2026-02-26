// nanojpeg.h - Header for NanoJPEG decoder (freestanding kernel use)
#ifndef NANOJPEG_H
#define NANOJPEG_H

// Include naojpeg.c in header-only mode to get the type/function declarations
#define _NJ_INCLUDE_HEADER_ONLY
#include "nanojpeg.c"
#undef _NJ_INCLUDE_HEADER_ONLY

#endif // NANOJPEG_H
