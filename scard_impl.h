#ifndef SCARD_IMPL_H_
#define SCARD_IMPL_H_

#ifdef WIN32
#undef UNICODE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// PC/SC lite
#ifdef __APPLE__
#include <PCSC/winscard.h>
#include <PCSC/wintypes.h>
#else
#include <winscard.h>
#endif

#define MAX_XFER_SIZE           300
// this is not defined in windows
#ifndef MAX_READERNAME
# define MAX_READERNAME         128
#endif

LONG scard_init();
LONG scard_destroy();
LONG scard_reader_find();
bool scard_reader_present();

#endif // SCARD_IMPL_H_
