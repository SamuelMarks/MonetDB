/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0.  If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright 1997 - July 2008 CWI, August 2008 - 2019 MonetDB B.V.
 */


/*
 * authors Martin Kersten, A. Koning
 * Global dictionary encoding
 * Index value zero is not used to easy detection of filler values
 * The dictionary index size is derived from the number of entries covered.
 * It leads to a compact n-bit representation.
 * Floating points are not expected to be replicated.
 * A limit of 256 elements is currently assumed.
 */

#include "monetdb_config.h"
#include "gdk.h"
#include "gdk_bitvector.h"
#include "mosaic.h"
#include "mosaic_dict256.h"
#include "mosaic_private.h"
#include "group.h"

bool MOStypes_dict256(BAT* b) {
	switch (b->ttype){
	case TYPE_bit: return true; // Will be mapped to bte
	case TYPE_bte: return true;
	case TYPE_sht: return true;
	case TYPE_int: return true;
	case TYPE_lng: return true;
	case TYPE_oid: return true;
	case TYPE_flt: return true;
	case TYPE_dbl: return true;
#ifdef HAVE_HGE
	case TYPE_hge: return true;
#endif
	default:
		if (b->ttype == TYPE_date) {return true;} // Will be mapped to int
		if (b->ttype == TYPE_daytime) {return true;} // Will be mapped to lng
		if (b->ttype == TYPE_timestamp) {return true;} // Will be mapped to lng
	}

	return false;
}

#define CAPPEDDICT 256

// Create a larger dict256 buffer then we allow for in the mosaic header first
// Store the most frequent ones in the compressed heap header directly based on estimated savings
// Improve by using binary search rather then linear scan
#define TMPDICT 16*CAPPEDDICT

typedef union{
	bte valbte[TMPDICT];
	sht valsht[TMPDICT];
	int valint[TMPDICT];
	lng vallng[TMPDICT];
	flt valflt[TMPDICT];
	dbl valdbl[TMPDICT];
#ifdef HAVE_HGE
	hge valhge[TMPDICT];
#endif
} _DictionaryData;

typedef struct _CappedParameters_t {
	MosaicBlkRec base;
} MosaicBlkHeader_dict256_t;

#define METHOD dict256
#define METHOD_TEMPLATES_INCLUDE "mosaic_dictionary_templates.h"
#define PREPARATION_DEFINITION
#include METHOD_TEMPLATES_INCLUDE
#undef PREPARATION_DEFINITION

#define COMPRESSION_DEFINITION
#define TPE bte
#include METHOD_TEMPLATES_INCLUDE
#undef TPE
#define TPE sht
#include METHOD_TEMPLATES_INCLUDE
#undef TPE
#define TPE int
#include METHOD_TEMPLATES_INCLUDE
#undef TPE
#define TPE lng
#include METHOD_TEMPLATES_INCLUDE
#undef TPE
#define TPE flt
#include METHOD_TEMPLATES_INCLUDE
#undef TPE
#define TPE dbl
#include METHOD_TEMPLATES_INCLUDE
#undef TPE
#ifdef HAVE_HGE
#define TPE hge
#include METHOD_TEMPLATES_INCLUDE
#undef TPE
#endif
#undef COMPRESSION_DEFINITION

#define LAYOUT_DEFINITION
#include METHOD_TEMPLATES_INCLUDE
#define TPE bte
#include METHOD_TEMPLATES_INCLUDE
#undef TPE
#define TPE sht
#include METHOD_TEMPLATES_INCLUDE
#undef TPE
#define TPE int
#include METHOD_TEMPLATES_INCLUDE
#undef TPE
#define TPE lng
#include METHOD_TEMPLATES_INCLUDE
#undef TPE
#define TPE flt
#include METHOD_TEMPLATES_INCLUDE
#undef TPE
#define TPE dbl
#include METHOD_TEMPLATES_INCLUDE
#undef TPE
#ifdef HAVE_HGE
#define TPE hge
#include METHOD_TEMPLATES_INCLUDE
#undef TPE
#endif
#undef LAYOUT_DEFINITION

#define TPE bte
#include "mosaic_select_template.h"
#include "mosaic_projection_template.h"
#undef TPE
#define TPE sht
#include "mosaic_select_template.h"
#include "mosaic_projection_template.h"
#undef TPE
#define TPE int
#include "mosaic_select_template.h"
#include "mosaic_projection_template.h"
#undef TPE
#define TPE lng
#include "mosaic_select_template.h"
#include "mosaic_projection_template.h"
#undef TPE
#define TPE flt
#include "mosaic_select_template.h"
#include "mosaic_projection_template.h"
#undef TPE
#define TPE dbl
#include "mosaic_select_template.h"
#include "mosaic_projection_template.h"
#undef TPE
#ifdef HAVE_HGE
#define TPE hge
#include "mosaic_select_template.h"
#include "mosaic_projection_template.h"
#undef TPE
#endif

#define outer_loop_dict256(HAS_NIL, NIL_MATCHES, TPE, LEFT_CI_NEXT, RIGHT_CI_NEXT) \
    outer_loop_dictionary(HAS_NIL, NIL_MATCHES, dict256, TPE, LEFT_CI_NEXT, RIGHT_CI_NEXT)

MOSjoin_COUI_DEF(dict256, bte)
MOSjoin_COUI_DEF(dict256, sht)
MOSjoin_COUI_DEF(dict256, int)
MOSjoin_COUI_DEF(dict256, lng)
MOSjoin_COUI_DEF(dict256, flt)
MOSjoin_COUI_DEF(dict256, dbl)
#ifdef HAVE_HGE
MOSjoin_COUI_DEF(dict256, hge)
#endif

#undef METHOD_TEMPLATES_INCLUDE
#undef METHOD
