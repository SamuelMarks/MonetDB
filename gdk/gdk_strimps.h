/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0.  If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright 1997 - July 2008 CWI, August 2008 - 2021 MonetDB B.V.
 */

#ifndef _GDK_STRIMPS_H_
#define _GDK_STRIMPS_H_

#include <stdint.h>

/* Count the occurences of pairs of bytes. This is a compromise between
 * just handling ASCII and full UTF-8 support.
 */
#define STRIMP_HISTSIZE 256*256
#define STRIMP_SIZE 64

typedef struct {
	// TODO: find a better name for this
	uint16_t bytepairs[STRIMP_SIZE];
} StrimpHeader;

gdk_export gdk_return GDKstrimp_ndigrams(BAT *b, size_t *n); // Remove?
gdk_export gdk_return GDKstrimp_make_histogram(BAT *b, uint64_t *hist, size_t hist_size, size_t *nbins); // make static
// gdk_export gdk_return GDKstrimp_make_header(StrimpHeader *h, uint64_t *hist, size_t hist_size); // make static
gdk_export gdk_return GDKstrimp_make_header(BAT *b);
#endif /* _GDK_STRIMPS_H_ */
