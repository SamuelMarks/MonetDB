/*
 * SPDX-License-Identifier: MPL-2.0
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0.  If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright 2024 MonetDB Foundation;
 * Copyright August 2008 - 2023 MonetDB B.V.;
 * Copyright 1997 - July 2008 CWI.
 */

#include "monetdb_config.h"
#include "gdk.h"
#include "gdk_private.h"

/* Calculate a cross product between bats l and r with optional
 * candidate lists sl for l and sr for r.
 * The result is two bats r1 and r2 which contain the OID (head
 * values) of the input bats l and r.
 * If max_one is set, r can have at most one row. */
static gdk_return
BATcrossci(BAT **r1p, BAT **r2p, struct canditer *ci1, struct canditer *ci2)
{
	BAT *bn1, *bn2 = NULL;
	oid *restrict p;
	BUN i, j;

	QryCtx *qry_ctx = MT_thread_get_qry_ctx();

	/* first some special cases */
	if (ci1->ncand == 0 || ci2->ncand == 0) {
		if ((bn1 = BATdense(0, 0, 0)) == NULL)
			return GDK_FAIL;
		if (r2p) {
			if ((bn2 = BATdense(0, 0, 0)) == NULL) {
				BBPreclaim(bn1);
				return GDK_FAIL;
			}
			*r2p = bn2;
		}
		*r1p = bn1;
		return GDK_SUCCEED;
	}
	if (ci2->ncand == 1) {
		if ((bn1 = canditer_slice(ci1, 0, ci1->ncand)) == NULL)
			return GDK_FAIL;
		if (r2p) {
			if (ci1->ncand == 1) {
				bn2 = canditer_slice(ci2, 0, ci2->ncand);
			} else {
				bn2 = BATconstant(0, TYPE_oid, &ci2->seq, ci1->ncand, TRANSIENT);
			}
			if (bn2 == NULL) {
				BBPreclaim(bn1);
				return GDK_FAIL;
			}
			*r2p = bn2;
		}
		*r1p = bn1;
		return GDK_SUCCEED;
	}
	if (ci1->ncand == 1) {
		bn1 = BATconstant(0, TYPE_oid, &ci1->seq, ci2->ncand, TRANSIENT);
		if (bn1 == NULL)
			return GDK_FAIL;
		if (r2p) {
			bn2 = canditer_slice(ci2, 0, ci2->ncand);
			if (bn2 == NULL) {
				BBPreclaim(bn1);
				return GDK_FAIL;
			}
			*r2p = bn2;
		}
		*r1p = bn1;
		return GDK_SUCCEED;
	}

	bn1 = COLnew(0, TYPE_oid, ci1->ncand * ci2->ncand, TRANSIENT);
	if (r2p)
		bn2 = COLnew(0, TYPE_oid, ci1->ncand * ci2->ncand, TRANSIENT);
	if (bn1 == NULL || (r2p && bn2 == NULL)) {
		BBPreclaim(bn1);
		BBPreclaim(bn2);
		return GDK_FAIL;
	}
	if (ci1->ncand > 0 && ci2->ncand > 0) {
		BATsetcount(bn1, ci1->ncand * ci2->ncand);
		bn1->tsorted = true;
		bn1->trevsorted = ci1->ncand <= 1;
		bn1->tkey = ci2->ncand <= 1;
		bn1->tnil = false;
		bn1->tnonil = true;
		p = (oid *) Tloc(bn1, 0);
		for (i = 0; i < ci1->ncand; i++) {
			GDK_CHECK_TIMEOUT_BODY(qry_ctx, GOTO_LABEL_TIMEOUT_HANDLER(bailout, qry_ctx));
			oid x = canditer_next(ci1);
			for (j = 0; j < ci2->ncand; j++) {
				*p++ = x;
			}
		}
		BATtseqbase(bn1, ci2->ncand == 1 ? *(oid *) Tloc(bn1, 0) : oid_nil);

		if (bn2) {
			BATsetcount(bn2, ci1->ncand * ci2->ncand);
			bn2->tsorted = ci1->ncand <= 1 || ci2->ncand <= 1;
			bn2->trevsorted = ci2->ncand <= 1;
			bn2->tkey = ci1->ncand <= 1;
			bn2->tnil = false;
			bn2->tnonil = true;
			p = (oid *) Tloc(bn2, 0);
			for (i = 0; i < ci1->ncand; i++) {
				GDK_CHECK_TIMEOUT_BODY(qry_ctx, GOTO_LABEL_TIMEOUT_HANDLER(bailout, qry_ctx));
				for (j = 0; j < ci2->ncand; j++) {
					*p++ = canditer_next(ci2);
				}
				canditer_reset(ci2);
			}
			BATtseqbase(bn2, ci1->ncand == 1 ? *(oid *) Tloc(bn2, 0) : oid_nil);
		}
	}
	*r1p = bn1;
	if (r2p)
		*r2p = bn2;
	if (r2p)
		TRC_DEBUG(ALGO, "BATsubcross()=(" ALGOBATFMT "," ALGOBATFMT ")\n", ALGOBATPAR(bn1), ALGOBATPAR(bn2));
	else
		TRC_DEBUG(ALGO, "BATsubcross()=(" ALGOBATFMT ")\n", ALGOBATPAR(bn1));
	return GDK_SUCCEED;

  bailout:
	BBPreclaim(bn1);
	BBPreclaim(bn2);
	return GDK_FAIL;
}

gdk_return
BATsubcross(BAT **r1p, BAT **r2p, BAT *l, BAT *r, BAT *sl, BAT *sr, bool max_one)
{
	struct canditer ci1, ci2;

	canditer_init(&ci1, l, sl);
	canditer_init(&ci2, r, sr);
	if (max_one && ci1.ncand > 0 && ci2.ncand > 1) {
		GDKerror("more than one match");
		return GDK_FAIL;
	}
	return BATcrossci(r1p, r2p, &ci1, &ci2);
}

/* [left] outer cross */
gdk_return
BAToutercross(BAT **r1p, BAT **r2p, BAT *l, BAT *r, BAT *sl, BAT *sr, bool max_one)
{
	struct canditer ci1, ci2;

	canditer_init(&ci1, l, sl);
	canditer_init(&ci2, r, sr);
	if (max_one && ci1.ncand > 0 && ci2.ncand > 1) {
		GDKerror("more than one match");
		return GDK_FAIL;
	}

	if (ci1.ncand == 0) {
		BAT *bn = COLnew(0, TYPE_void, 0, TRANSIENT);
		if (bn == NULL)
			return GDK_FAIL;
		BATtseqbase(bn, oid_nil);
		*r1p = bn;
		if (r2p) {
			bn = COLnew(0, TYPE_void, 0, TRANSIENT);
			if (bn == NULL) {
				BBPreclaim(*r1p);
				return GDK_FAIL;
			}
			BATtseqbase(bn, oid_nil);
			*r2p = bn;
		}
		return GDK_SUCCEED;
	}
	if (ci2.ncand == 0) {
		BAT *bn = canditer_slice(&ci1, 0, ci1.ncand);
		if (bn == NULL)
			return GDK_FAIL;
		*r1p = bn;
		if (r2p) {
			BAT *bn = COLnew(0, TYPE_void, ci1.ncand, TRANSIENT);
			if (bn == NULL)
				return GDK_FAIL;
			BATtseqbase(bn, oid_nil);
			BATsetcount(bn, ci1.ncand);
			*r2p = bn;
		}
		return GDK_SUCCEED;
	}
	return BATcrossci(r1p, r2p, &ci1, &ci2);
}
