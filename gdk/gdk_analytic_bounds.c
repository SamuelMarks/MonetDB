/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0.  If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright 1997 - July 2008 CWI, August 2008 - 2020 MonetDB B.V.
 */

#include "monetdb_config.h"
#include "gdk.h"
#include "gdk_analytic.h"
#include "gdk_time.h"
#include "gdk_calc_private.h"

#define ANALYTICAL_DIFF_IMP(TPE)				\
	do {							\
		TPE *restrict bp = (TPE*)Tloc(b, 0), prev = bp[0];		\
		if (np) {					\
			for (; i < cnt; i++) {	\
				TPE next = bp[i]; \
				if (next != prev) {		\
					rb[i] = TRUE;		\
					prev = next;		\
				} else {	\
					rb[i] = np[i];			\
				}			\
			}					\
		} else {					\
			for (; i < cnt; i++) {		\
				TPE next = bp[i]; \
				if (next == prev) {		\
					rb[i] = FALSE;		\
				} else {			\
					rb[i] = TRUE;		\
					prev = next;		\
				}				\
			}				\
		}				\
	} while (0)

/* We use NaN for floating point null values, which always output false on equality tests */
#define ANALYTICAL_DIFF_FLOAT_IMP(TPE)					\
	do {								\
		TPE *restrict bp = (TPE*)Tloc(b, 0), prev = bp[0];		\
		if (np) {						\
			for (; i < cnt; i++) {		\
				TPE next = bp[i]; \
				if (next != prev && (!is_##TPE##_nil(next) || !is_##TPE##_nil(prev))) { \
					rb[i] = TRUE;			\
					prev = next;			\
				} else {	\
					rb[i] = np[i];			\
				}			\
			}						\
		} else {						\
			for (; i < cnt; i++) {			\
				TPE next = bp[i]; \
				if (next == prev || (is_##TPE##_nil(next) && is_##TPE##_nil(prev))) { \
					rb[i] = FALSE;			\
				} else {				\
					rb[i] = TRUE;			\
					prev = next;		\
				}				\
			}				\
		}				\
	} while (0)

gdk_return
GDKanalyticaldiff(BAT *r, BAT *b, BAT *p, int tpe)
{
	BUN i = 0, cnt = BATcount(b);
	bit *restrict rb = (bit *) Tloc(r, 0), *restrict np = p ? (bit *) Tloc(p, 0) : NULL;

	switch (ATOMbasetype(tpe)) {
	case TYPE_bte:
		ANALYTICAL_DIFF_IMP(bte);
		break;
	case TYPE_sht:
		ANALYTICAL_DIFF_IMP(sht);
		break;
	case TYPE_int:
		ANALYTICAL_DIFF_IMP(int);
		break;
	case TYPE_lng:
		ANALYTICAL_DIFF_IMP(lng);
		break;
#ifdef HAVE_HGE
	case TYPE_hge:
		ANALYTICAL_DIFF_IMP(hge);
		break;
#endif
	case TYPE_flt: {
		if (b->tnonil) {
			ANALYTICAL_DIFF_IMP(flt);
		} else { /* Because of NaN values, use this path */
			ANALYTICAL_DIFF_FLOAT_IMP(flt);
		}
	} break;
	case TYPE_dbl: {
		if (b->tnonil) {
			ANALYTICAL_DIFF_IMP(dbl);
		} else { /* Because of NaN values, use this path */
			ANALYTICAL_DIFF_FLOAT_IMP(dbl);
		}
	} break;
	default:{
		BATiter it = bat_iterator(b);
		ptr v = BUNtail(it, 0), next;
		int (*atomcmp) (const void *, const void *) = ATOMcompare(tpe);
		if (np) {
			for (i = 0; i < cnt; i++) {
				rb[i] = np[i];
				next = BUNtail(it, i);
				if (atomcmp(v, next) != 0) {
					rb[i] = TRUE;
					v = next;
				}
			}
		} else {
			for (i = 0; i < cnt; i++) {
				next = BUNtail(it, i);
				if (atomcmp(v, next) != 0) {
					rb[i] = TRUE;
					v = next;
				} else {
					rb[i] = FALSE;
				}
			}
		}
	}
	}
	BATsetcount(r, (BUN) cnt);
	r->tnonil = true;
	r->tnil = false;
	return GDK_SUCCEED;
}

#define ANALYTICAL_WINDOW_BOUNDS_ROWS_PRECEDING(TPE, LIMIT, UPCAST)			\
	do {								\
		lng calc1 = 0, calc2 = 0, rlimit = 0;					\
		j = k;							\
		for (; k < i; k++) {				\
			TPE olimit = LIMIT;	\
			if (is_##TPE##_nil(olimit) || olimit < 0)	\
				goto invalid_bound;	\
			rlimit = UPCAST;	\
			SUB_WITH_CHECK(k, rlimit, lng, calc1, GDK_lng_max, goto calc_overflow); \
			ADD_WITH_CHECK(calc1, !first_half, lng, calc2, GDK_lng_max, goto calc_overflow); \
			rb[k] = MAX(calc2, j);				\
		}							\
	} while (0)

#define ANALYTICAL_WINDOW_BOUNDS_ROWS_FOLLOWING(TPE, LIMIT, UPCAST)			\
	do {								\
		lng calc1 = 0, calc2 = 0, rlimit = 0;					\
		for (; k < i; k++) {				\
			TPE olimit = LIMIT;	\
			if (is_##TPE##_nil(olimit) || olimit < 0)	\
				goto invalid_bound;	\
			rlimit = UPCAST;	\
			ADD_WITH_CHECK(rlimit, k, lng, calc1, GDK_lng_max, goto calc_overflow); \
			ADD_WITH_CHECK(calc1, !first_half, lng, calc2, GDK_lng_max, goto calc_overflow); \
			rb[k] = MIN(calc2, i);				\
		}							\
	} while (0)

#define ANALYTICAL_WINDOW_BOUNDS_BRANCHES_ROWS(IMP, TPE, LIMIT, UPCAST)	\
	do {								\
		if (p) {						\
			for (; i < cnt; i++) {			\
				if (np[i]) 			\
					ANALYTICAL_WINDOW_BOUNDS_ROWS##IMP(TPE, LIMIT, UPCAST);	\
			}						\
		} 		\
		i = cnt;					\
		ANALYTICAL_WINDOW_BOUNDS_ROWS##IMP(TPE, LIMIT, UPCAST);	\
	} while (0)

#define ANALYTICAL_WINDOW_BOUNDS_GROUPS_PRECEDING(TPE, LIMIT, UPCAST)			\
	do {								\
		lng m = k - 1, rlimit = 0;						\
		for (; k < i; k++) {		\
			TPE olimit = LIMIT;	\
			if (is_##TPE##_nil(olimit) || olimit < 0)	\
				goto invalid_bound;	\
			rlimit = UPCAST;	\
			for (j = k; ; j--) {		\
				if (j == m) {		\
					j++; \
					break;		\
				} \
				if (bp[j]) {		\
					if (rlimit == 0)		\
						break;		\
					rlimit--;		\
				}				\
			}				\
			rb[k] =j;		\
		}					\
	} while (0)

#define ANALYTICAL_WINDOW_BOUNDS_GROUPS_FOLLOWING(TPE, LIMIT, UPCAST)			\
	do {								\
		lng rlimit = 0;	\
		for (; k < i; k++) {		\
			TPE olimit = LIMIT;	\
			if (is_##TPE##_nil(olimit) || olimit < 0)	\
				goto invalid_bound;	\
			rlimit = UPCAST;	\
			for (j = k + 1; j < i; j++) {	\
				if (bp[j]) {		\
					if (rlimit == 0)		\
						break;		\
					rlimit--;		\
				}		\
			}		\
			rb[k] = j;		\
		}		\
	} while (0)

#define ANALYTICAL_WINDOW_BOUNDS_BRANCHES_GROUPS(IMP, TPE, LIMIT, UPCAST)	\
	do {								\
		if (p) {						\
			for (; i < cnt; i++) {			\
				if (np[i]) 			\
					ANALYTICAL_WINDOW_BOUNDS_GROUPS##IMP(TPE, LIMIT, UPCAST);	\
			}						\
		}				\
		i = cnt;					\
		ANALYTICAL_WINDOW_BOUNDS_GROUPS##IMP(TPE, LIMIT, UPCAST);	\
	} while (0)

#define ANALYTICAL_WINDOW_BOUNDS_FIXED_RANGE_PRECEDING(TPE1, LIMIT, TPE2) \
	do {								\
		lng m = k - 1;						\
		TPE1 v, calc;						\
		TPE2 rlimit;						\
		if (b->tnonil) {					\
			for (; k < i; k++) {			\
				TPE1 olimit = LIMIT;	\
				if (is_##TPE1##_nil(olimit) || olimit < 0)	\
					goto invalid_bound;	\
				rlimit = (TPE2) olimit;			\
				v = bp[k];				\
				for (j = k; ; j--) {			\
					if (j == m)			\
						break;			\
					SUB_WITH_CHECK(v, bp[j], TPE1, calc, GDK_##TPE1##_max, goto calc_overflow); \
					if ((TPE2)(ABSOLUTE(calc)) > rlimit) \
						break;			\
				}					\
				rb[k] = ++j;				\
			}						\
		} else {						\
			for (; k < i; k++) {			\
				TPE1 olimit = LIMIT;	\
				if (is_##TPE1##_nil(olimit) || olimit < 0)	\
					goto invalid_bound;	\
				rlimit = (TPE2) olimit;			\
				v = bp[k];				\
				if (is_##TPE1##_nil(v)) {		\
					for (j = k; ; j--) {		\
						if (j == m)		\
							break;		\
						if (!is_##TPE1##_nil(bp[j])) \
							break;		\
					}				\
				} else {				\
					for (j = k; ; j--) {		\
						if (j == m)		\
							break;		\
						if (is_##TPE1##_nil(bp[j])) \
							break;		\
						SUB_WITH_CHECK(v, bp[j], TPE1, calc, GDK_##TPE1##_max, goto calc_overflow); \
						if ((TPE2)(ABSOLUTE(calc)) > rlimit) \
							break;		\
					}				\
				}					\
				rb[k] = ++j;				\
			}						\
		}							\
	} while (0)

#define ANALYTICAL_WINDOW_BOUNDS_FIXED_RANGE_FOLLOWING(TPE1, LIMIT, TPE2) \
	do {								\
		TPE1 v, calc;						\
		TPE2 rlimit;						\
		if (b->tnonil) {					\
			for (; k < i; k++) {			\
				TPE1 olimit = LIMIT;	\
				if (is_##TPE1##_nil(olimit) || olimit < 0)	\
					goto invalid_bound;	\
				rlimit = (TPE2) olimit;			\
				v = bp[k];				\
				for (j = k + 1; j < i; j++) {		\
					SUB_WITH_CHECK(v, bp[j], TPE1, calc, GDK_##TPE1##_max, goto calc_overflow); \
					if ((TPE2)(ABSOLUTE(calc)) > rlimit) \
						break;			\
				}					\
				rb[k] = j;				\
			}						\
		} else {						\
			for (; k < i; k++) {			\
				TPE1 olimit = LIMIT;	\
				if (is_##TPE1##_nil(olimit) || olimit < 0)	\
					goto invalid_bound;	\
				rlimit = (TPE2) olimit;			\
				v = bp[k];				\
				if (is_##TPE1##_nil(v)) {		\
					for (j =k + 1; j < i; j++) {	\
						if (!is_##TPE1##_nil(bp[j])) \
							break;		\
					}				\
				} else {				\
					for (j = k + 1; j < i; j++) {	\
						if (is_##TPE1##_nil(bp[j])) \
							break;		\
						SUB_WITH_CHECK(v, bp[j], TPE1, calc, GDK_##TPE1##_max, goto calc_overflow); \
						if ((TPE2)(ABSOLUTE(calc)) > rlimit) \
							break;		\
					}				\
				}					\
				rb[k] = j;				\
			}						\
		}							\
	} while (0)

#define ANALYTICAL_WINDOW_BOUNDS_CALC_FIXED(TPE1, IMP, LIMIT, TPE2)	\
	do {								\
		TPE1 *restrict bp = (TPE1*)Tloc(b, 0);			\
		if (np) {						\
			for (; i < cnt; i++) {			\
				if (np[i])				\
					IMP(TPE1, LIMIT, TPE2);		\
			}						\
		} 	\
		i = cnt;					\
		IMP(TPE1, LIMIT, TPE2);				\
	} while (0)

#define ANALYTICAL_WINDOW_BOUNDS_VARSIZED_RANGE_PRECEDING(LIMIT, TPE)	\
	do {								\
		lng m = k - 1;						\
		TPE rlimit = 0; 	\
		if (b->tnonil) {					\
			for (; k < i; k++) {			\
				TPE olimit = LIMIT;	\
				if (is_##TPE##_nil(olimit) || olimit < 0)	\
					goto invalid_bound;	\
				rlimit = (TPE) olimit;			\
				void *v = BUNtail(bpi, (BUN) k);	\
				for (j = k; ; j--) {			\
					void *next;			\
					if (j == m)			\
						break;			\
					next = BUNtail(bpi, (BUN) j);	\
					if (ABSOLUTE((TPE) atomcmp(v, next)) > olimit) \
						break;			\
				}					\
				rb[k] = ++j;					\
			}						\
		} else {						\
			for (; k < i; k++) {			\
				TPE olimit = LIMIT;	\
				if (is_##TPE##_nil(olimit) || olimit < 0)	\
					goto invalid_bound;	\
				rlimit = (TPE) olimit;			\
				void *v = BUNtail(bpi, (BUN) k);	\
				if (atomcmp(v, nil) == 0) {		\
					for (j = k; ; j--) {		\
						if (j == m)		\
							break;		\
						if (atomcmp(BUNtail(bpi, (BUN) j), nil) != 0) \
							break;		\
					}				\
				} else {				\
					for (j = k; ; j--) {		\
						void *next;		\
						if (j == m)		\
							break;		\
						next = BUNtail(bpi, (BUN) j); \
						if (atomcmp(next, nil) == 0) \
							break;		\
						if (ABSOLUTE((TPE) atomcmp(v, next)) > rlimit) \
							break;		\
					}				\
				}					\
				rb[k] = ++j;					\
			}						\
		}							\
	} while (0)

#define ANALYTICAL_WINDOW_BOUNDS_VARSIZED_RANGE_FOLLOWING(LIMIT, TPE)	\
	do {								\
		if (b->tnonil) {					\
			for (; k < i; k++) {			\
				void *v = BUNtail(bpi, (BUN) k);	\
				for (j = k + 1; j < i; j++) {		\
					void *next = BUNtail(bpi, (BUN) j); \
					if (ABSOLUTE((TPE) atomcmp(v, next)) > (TPE) LIMIT) \
						break;			\
				}					\
				rb[k] = j;				\
			}						\
		} else {						\
			for (; k < i; k++) {			\
				void *v = BUNtail(bpi, (BUN) k);	\
				if (atomcmp(v, nil) == 0) {		\
					for (j = k + 1; j < i; j++) {	\
						if (atomcmp(BUNtail(bpi, (BUN) j), nil) != 0) \
							break;		\
					}				\
				} else {				\
					for (j = k + 1; j < i; j++) {	\
						void *next = BUNtail(bpi, (BUN) j); \
						if (atomcmp(next, nil) == 0) \
							break;		\
						if (ABSOLUTE((TPE) atomcmp(v, next)) > (TPE) LIMIT) \
							break;		\
					}				\
				}					\
				rb[k] = j;				\
			}						\
		}							\
	} while (0)

#define ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_NUM(IMP, LIMIT, CAST)	\
	do {								\
		switch (tp1) {						\
		case TYPE_bit:						\
		case TYPE_flt:						\
		case TYPE_dbl:						\
			goto type_not_supported;			\
		case TYPE_bte:						\
			ANALYTICAL_WINDOW_BOUNDS_CALC_FIXED(bte, ANALYTICAL_WINDOW_BOUNDS_FIXED_RANGE##IMP, LIMIT, lng); \
			break;						\
		case TYPE_sht:						\
			ANALYTICAL_WINDOW_BOUNDS_CALC_FIXED(sht, ANALYTICAL_WINDOW_BOUNDS_FIXED_RANGE##IMP, LIMIT, lng); \
			break;						\
		case TYPE_int:						\
			ANALYTICAL_WINDOW_BOUNDS_CALC_FIXED(int, ANALYTICAL_WINDOW_BOUNDS_FIXED_RANGE##IMP, LIMIT, lng); \
			break;						\
		case TYPE_lng:						\
			ANALYTICAL_WINDOW_BOUNDS_CALC_FIXED(lng, ANALYTICAL_WINDOW_BOUNDS_FIXED_RANGE##IMP, LIMIT, lng); \
			break;						\
		default: {						\
			if (p) {					\
				np = (bit*)Tloc(p, 0);			\
				for (; i < cnt; i++) {			\
					if (np[i]) 			\
						ANALYTICAL_WINDOW_BOUNDS_VARSIZED_RANGE##IMP(LIMIT, CAST); \
				}					\
			} 					\
			i = cnt;				\
			ANALYTICAL_WINDOW_BOUNDS_VARSIZED_RANGE##IMP(LIMIT, CAST); \
		}							\
		}							\
	} while (0)

#define ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_FLT(IMP, LIMIT)		\
	do {								\
		switch (tp1) {						\
			case TYPE_flt:					\
				ANALYTICAL_WINDOW_BOUNDS_CALC_FIXED(flt, ANALYTICAL_WINDOW_BOUNDS_FIXED_RANGE##IMP, LIMIT, flt); \
				break;					\
			default:					\
				goto type_not_supported;		\
		}							\
	} while (0)

#define ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_DBL(IMP, LIMIT)		\
	do {								\
		switch (tp1) {						\
			case TYPE_dbl:					\
				ANALYTICAL_WINDOW_BOUNDS_CALC_FIXED(dbl, ANALYTICAL_WINDOW_BOUNDS_FIXED_RANGE##IMP, LIMIT, dbl); \
				break;					\
			default:					\
				goto type_not_supported;		\
		}							\
	} while (0)

#ifdef HAVE_HGE
#define ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_HGE(IMP, LIMIT)		\
	do {								\
		switch (tp1) {						\
		case TYPE_bit:						\
		case TYPE_flt:						\
		case TYPE_dbl:						\
			goto type_not_supported;			\
		case TYPE_bte:						\
			ANALYTICAL_WINDOW_BOUNDS_CALC_FIXED(bte, ANALYTICAL_WINDOW_BOUNDS_FIXED_RANGE##IMP, LIMIT, hge); \
			break;						\
		case TYPE_sht:						\
			ANALYTICAL_WINDOW_BOUNDS_CALC_FIXED(sht, ANALYTICAL_WINDOW_BOUNDS_FIXED_RANGE##IMP, LIMIT, hge); \
			break;						\
		case TYPE_int:						\
			ANALYTICAL_WINDOW_BOUNDS_CALC_FIXED(int, ANALYTICAL_WINDOW_BOUNDS_FIXED_RANGE##IMP, LIMIT, hge); \
			break;						\
		case TYPE_lng:						\
			ANALYTICAL_WINDOW_BOUNDS_CALC_FIXED(lng, ANALYTICAL_WINDOW_BOUNDS_FIXED_RANGE##IMP, LIMIT, hge); \
			break;						\
		case TYPE_hge:						\
			ANALYTICAL_WINDOW_BOUNDS_CALC_FIXED(hge, ANALYTICAL_WINDOW_BOUNDS_FIXED_RANGE##IMP, LIMIT, hge); \
			break;						\
		default: {						\
			if (p) {					\
				np = (bit*)Tloc(p, 0);		\
				for (; i < cnt; i++) {		\
					if (np[i])			\
						ANALYTICAL_WINDOW_BOUNDS_VARSIZED_RANGE##IMP(LIMIT, hge); \
				}					\
			} 					\
			i = cnt;				\
			ANALYTICAL_WINDOW_BOUNDS_VARSIZED_RANGE##IMP(LIMIT, hge); \
		}							\
		}							\
	} while (0)
#endif

#define date_sub_month(D,M)			date_add_month(D,-(M))
#define timestamp_sub_month(T,M)	timestamp_add_month(T,-(M))

#define daytime_add_msec(D,M)		daytime_add_usec(D, 1000*(M))
#define daytime_sub_msec(D,M)		daytime_add_usec(D, -1000*(M))
#define date_add_msec(D,M)			date_add_day(D,(int) ((M)/(24*60*60*1000)))
#define date_sub_msec(D,M)			date_add_day(D,(int) (-(M)/(24*60*60*1000)))
#define timestamp_add_msec(T,M)		timestamp_add_usec(T, (M)*1000)
#define timestamp_sub_msec(T,M)		timestamp_add_usec(T, -(M)*1000)

#define ANALYTICAL_WINDOW_BOUNDS_FIXED_RANGE_MTIME_PRECEDING(TPE1, LIMIT, TPE2, SUB, ADD) \
	do {																\
		lng m = k - 1;													\
		TPE1 v, vmin, vmax;												\
		if (b->tnonil) {												\
			for (; k < i; k++) {									\
				TPE2 rlimit = LIMIT;	\
				if (is_##TPE1##_nil(rlimit) || rlimit < 0)	\
					goto invalid_bound;	\
				v = bp[k];												\
				vmin = SUB(v, rlimit);									\
				vmax = ADD(v, rlimit);									\
				for (j=k; ; j--) {										\
					if (j == m)											\
						break;											\
					if ((!is_##TPE1##_nil(vmin) && bp[j] < vmin) ||		\
						(!is_##TPE1##_nil(vmax) && bp[j] > vmax))		\
						break;											\
				}														\
				j++;													\
				rb[k] = j;												\
			}															\
		} else {														\
			for (; k < i; k++) {									\
				TPE2 rlimit = LIMIT;	\
				if (is_##TPE1##_nil(rlimit) || rlimit < 0)	\
					goto invalid_bound;	\
				v = bp[k];												\
				if (is_##TPE1##_nil(v)) {								\
					for (j=k; ; j--) {									\
						if (!is_##TPE1##_nil(bp[j]))					\
							break;										\
					}													\
				} else {												\
					vmin = SUB(v, rlimit);								\
					vmax = ADD(v, rlimit);								\
					for (j=k; ; j--) {									\
						if (j == m)										\
							break;										\
						if (is_##TPE1##_nil(bp[j]))						\
							break;										\
						if ((!is_##TPE1##_nil(vmin) && bp[j] < vmin) ||	\
							(!is_##TPE1##_nil(vmax) && bp[j] > vmax))	\
							break;										\
					}													\
				}														\
				j++;													\
				rb[k] = j;												\
			}															\
		} \
	} while(0)

#define ANALYTICAL_WINDOW_BOUNDS_FIXED_RANGE_MTIME_FOLLOWING(TPE1, LIMIT, TPE2, SUB, ADD) \
	do {																\
		TPE1 v, vmin, vmax;												\
		if (b->tnonil) {												\
			for (; k < i; k++) {									\
				TPE2 rlimit = LIMIT;	\
				if (is_##TPE1##_nil(rlimit) || rlimit < 0)	\
					goto invalid_bound;	\
				v = bp[k];												\
				vmin = SUB(v, rlimit);									\
				vmax = ADD(v, rlimit);									\
				for (j=k+1; j<i; j++) {									\
					if ((!is_##TPE1##_nil(vmin) && bp[j] < vmin) ||		\
						(!is_##TPE1##_nil(vmax) && bp[j] > vmax))		\
						break;											\
				}														\
				rb[k] = j;												\
			}															\
		} else {														\
			for (; k < i; k++) {									\
				TPE2 rlimit = LIMIT;	\
				if (is_##TPE1##_nil(rlimit) || rlimit < 0)	\
					goto invalid_bound;	\
				v = bp[k];												\
				if (is_##TPE1##_nil(v)) {								\
					for (j=k+1; j<i; j++) {								\
						if (!is_##TPE1##_nil(bp[j]))					\
							break;										\
					}													\
				} else {												\
					vmin = SUB(v, rlimit);								\
					vmax = ADD(v, rlimit);								\
					for (j=k+1; j<i; j++) {								\
						if (is_##TPE1##_nil(bp[j]))						\
							break;										\
						if ((!is_##TPE1##_nil(vmin) && bp[j] < vmin) ||	\
							(!is_##TPE1##_nil(vmax) && bp[j] > vmax))	\
							break;										\
					}													\
				}														\
				rb[k] = j;												\
			}															\
		} \
	} while(0)

#define ANALYTICAL_WINDOW_BOUNDS_CALC_FIXED_MTIME(TPE1, IMP, LIMIT, TPE2, SUB, ADD) \
	do { \
		TPE1 *restrict bp = (TPE1*)Tloc(b, 0); \
		if (p) {						\
			for (; i < cnt; i++) {			\
				if (np[i]) 			\
					IMP(TPE1, LIMIT, TPE2, SUB, ADD); \
			}						\
		}		\
		i = cnt;					\
		IMP(TPE1, LIMIT, TPE2, SUB, ADD); \
	} while(0)

#define ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_MTIME_MONTH_INTERVAL(IMP, LIMIT) \
	do { \
		if (tp1 == TYPE_date) { \
			ANALYTICAL_WINDOW_BOUNDS_CALC_FIXED_MTIME(date, ANALYTICAL_WINDOW_BOUNDS_FIXED_RANGE_MTIME##IMP, LIMIT, int, date_sub_month, date_add_month); \
		} else if (tp1 == TYPE_timestamp) { \
			ANALYTICAL_WINDOW_BOUNDS_CALC_FIXED_MTIME(timestamp, ANALYTICAL_WINDOW_BOUNDS_FIXED_RANGE_MTIME##IMP, LIMIT, int, timestamp_sub_month, timestamp_add_month); \
		} else { \
			goto type_not_supported; \
		} \
	} while(0)

#define ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_MTIME_SEC_INTERVAL(IMP, LIMIT) \
	do { \
		if (tp1 == TYPE_daytime) { \
			ANALYTICAL_WINDOW_BOUNDS_CALC_FIXED_MTIME(daytime, ANALYTICAL_WINDOW_BOUNDS_FIXED_RANGE_MTIME##IMP, LIMIT, lng, daytime_sub_msec, daytime_add_msec); \
		} else if (tp1 == TYPE_date) { \
			ANALYTICAL_WINDOW_BOUNDS_CALC_FIXED_MTIME(date, ANALYTICAL_WINDOW_BOUNDS_FIXED_RANGE_MTIME##IMP, LIMIT, lng, date_sub_msec, date_add_msec); \
		} else if (tp1 == TYPE_timestamp) { \
			ANALYTICAL_WINDOW_BOUNDS_CALC_FIXED_MTIME(timestamp, ANALYTICAL_WINDOW_BOUNDS_FIXED_RANGE_MTIME##IMP, LIMIT, lng, timestamp_sub_msec, timestamp_add_msec); \
		} else { \
			goto type_not_supported; \
		} \
	} while(0)

static gdk_return
GDKanalyticalallbounds(BAT *r, BAT *b, BAT *p, bool preceding)
{
	lng *restrict rb = (lng *) Tloc(r, 0), i = 0, k = 0, j = 0, cnt = (lng) BATcount(b);
	bit *restrict np = p ? (bit *) Tloc(p, 0) : NULL;

	if (preceding) {
		if (np) {
			for (; i < cnt; i++) {
				if (np[i]) {
					j = k;
					for (; k < i; k++)
						rb[k] = j;
				}
			}
		}
		i = cnt;
		j = k;
		for (; k < i; k++)
			rb[k] = j;
	} else {	/* following */
		if (np) {
			for (; i < cnt; i++) {
				if (np[i]) {
					for (; k < i; k++)
						rb[k] = i;
				}
			}
		}
		i = cnt;
		for (; k < i; k++)
			rb[k] = i;
	}

	BATsetcount(r, (BUN) cnt);
	r->tnonil = false;
	r->tnil = false;
	return GDK_SUCCEED;
}

static gdk_return
GDKanalyticalrowbounds(BAT *r, BAT *b, BAT *p, BAT *l, const void *restrict bound, int tp2, bool preceding, lng first_half)
{
	lng cnt = (BUN) BATcount(b), nils = 0, *restrict rb = (lng *) Tloc(r, 0), i = 0, k = 0, j = 0;
	bit *restrict np = p ? (bit*)Tloc(p, 0) : NULL;
	int abort_on_error = 1;

	if (l) {		/* dynamic bounds */
		if (l->tnil)
			goto invalid_bound;
		switch (tp2) {
		case TYPE_bte:{
			bte *restrict limit = (bte *) Tloc(l, 0);
			if (preceding) {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_ROWS(_PRECEDING, bte, limit[k], (lng) olimit);
			} else {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_ROWS(_FOLLOWING, bte, limit[k], (lng) olimit);
			}
			break;
		}
		case TYPE_sht:{
			sht *restrict limit = (sht *) Tloc(l, 0);
			if (preceding) {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_ROWS(_PRECEDING, sht, limit[k], (lng) olimit);
			} else {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_ROWS(_FOLLOWING, sht, limit[k], (lng) olimit);
			}
			break;
		}
		case TYPE_int:{
			int *restrict limit = (int *) Tloc(l, 0);
			if (preceding) {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_ROWS(_PRECEDING, int, limit[k], (lng) olimit);
			} else {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_ROWS(_FOLLOWING, int, limit[k], (lng) olimit);
			}
			break;
		}
		case TYPE_lng:{
			lng *restrict limit = (lng *) Tloc(l, 0);
			if (preceding) {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_ROWS(_PRECEDING, lng, limit[k], olimit);
			} else {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_ROWS(_FOLLOWING, lng, limit[k], olimit);
			}
			break;
		}
#ifdef HAVE_HGE
		case TYPE_hge:{
			hge *restrict limit = (hge *) Tloc(l, 0);
			if (preceding) {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_ROWS(_PRECEDING, hge, limit[k], (olimit > (hge) GDK_lng_max) ? GDK_lng_max : (lng) olimit);
			} else {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_ROWS(_FOLLOWING, hge, limit[k], (olimit > (hge) GDK_lng_max) ? GDK_lng_max : (lng) olimit);
			}
			break;
		}
#endif
		default:
			goto bound_not_supported;
		}
	} else {	/* static bounds, all the limits are cast to lng */
		lng limit;
		switch (tp2) {
		case TYPE_bte:
			limit = is_bte_nil(*(bte *) bound) ? lng_nil : (lng) *(bte *) bound;
			break;
		case TYPE_sht:
			limit = is_sht_nil(*(sht *) bound) ? lng_nil : (lng) *(sht *) bound;
			break;
		case TYPE_int:
			limit = is_int_nil(*(int *) bound) ? lng_nil : (lng) *(int *) bound;
			break;
		case TYPE_lng:
			limit = (lng) (*(lng *) bound);
			break;
#ifdef HAVE_HGE
		case TYPE_hge: {
			hge nval = *(hge *) bound;
			limit = is_hge_nil(nval) ? lng_nil : (nval > (hge) GDK_lng_max) ? GDK_lng_max : (lng) nval;
			break;
		}
#endif
		default:
			goto bound_not_supported;
		}
		if (limit == GDK_lng_max) {
			return GDKanalyticalallbounds(r, b, p, preceding);
		} else if (is_lng_nil(limit) || limit < 0) {
			goto invalid_bound;	
		} else if (preceding) {
			ANALYTICAL_WINDOW_BOUNDS_BRANCHES_ROWS(_PRECEDING, lng, limit, olimit);
		} else {
			ANALYTICAL_WINDOW_BOUNDS_BRANCHES_ROWS(_FOLLOWING, lng, limit, olimit);
		}
	}

	BATsetcount(r, (BUN) cnt);
	r->tnonil = (nils == 0);
	r->tnil = (nils > 0);
	return GDK_SUCCEED;
      bound_not_supported:
	GDKerror("42000!rows frame bound type %s not supported.\n", ATOMname(tp2));
	return GDK_FAIL;
      calc_overflow:
	GDKerror("22003!overflow in calculation.\n");
	return GDK_FAIL;
      invalid_bound:
	GDKerror("42000!row frame bound must be non negative and non null.\n");
	return GDK_FAIL;
}

static gdk_return
GDKanalyticalrangebounds(BAT *r, BAT *b, BAT *p, BAT *l, const void *restrict bound, int tp1, int tp2, bool preceding)
{
	lng cnt = (lng) BATcount(b), nils = 0, *restrict rb = (lng *) Tloc(r, 0), i = 0, k = 0, j = 0;
	bit *restrict np = p ? (bit *) Tloc(p, 0) : NULL;
	BATiter bpi = bat_iterator(b);
	int (*atomcmp) (const void *, const void *) = ATOMcompare(tp1);
	const void *nil = ATOMnilptr(tp1);
	int abort_on_error = 1;

	if (l) {		/* dynamic bounds */
		if (l->tnil)
			goto invalid_bound;
		switch (tp2) {
		case TYPE_bte:{
			bte *restrict limit = (bte *) Tloc(l, 0);
			if (preceding) {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_NUM(_PRECEDING, limit[k], int);
			} else {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_NUM(_FOLLOWING, limit[k], int);
			}
			break;
		}
		case TYPE_sht:{
			sht *restrict limit = (sht *) Tloc(l, 0);
			if (preceding) {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_NUM(_PRECEDING, limit[k], int);
			} else {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_NUM(_FOLLOWING, limit[k], int);
			}
			break;
		}
		case TYPE_int:{
			int *restrict limit = (int *) Tloc(l, 0);
			if (tp1 == TYPE_daytime || tp1 == TYPE_date || tp1 == TYPE_timestamp) {
				if (preceding) {
					ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_MTIME_MONTH_INTERVAL(_PRECEDING, limit[k]);
				} else {
					ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_MTIME_MONTH_INTERVAL(_FOLLOWING, limit[k]);
				}
			} else if (preceding) {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_NUM(_PRECEDING, limit[k], int);
			} else {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_NUM(_FOLLOWING, limit[k], int);
			}
			break;
		}
		case TYPE_lng:{
			lng *restrict limit = (lng *) Tloc(l, 0);
			if (tp1 == TYPE_daytime || tp1 == TYPE_date || tp1 == TYPE_timestamp) {
				if (preceding) {
					ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_MTIME_SEC_INTERVAL(_PRECEDING, limit[k]);
				} else {
					ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_MTIME_SEC_INTERVAL(_FOLLOWING, limit[k]);
				}
			} else if (preceding) {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_NUM(_PRECEDING, limit[k], lng);
			} else {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_NUM(_FOLLOWING, limit[k], lng);
			}
			break;
		}
		case TYPE_flt:{
			flt *restrict limit = (flt *) Tloc(l, 0);
			if (preceding) {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_FLT(_PRECEDING, limit[k]);
			} else {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_FLT(_FOLLOWING, limit[k]);
			}
			break;
		}
		case TYPE_dbl:{
			dbl *restrict limit = (dbl *) Tloc(l, 0);
			if (preceding) {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_DBL(_PRECEDING, limit[k]);
			} else {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_DBL(_FOLLOWING, limit[k]);
			}
			break;
		}
#ifdef HAVE_HGE
		case TYPE_hge:{
			hge *restrict limit = (hge *) Tloc(l, 0);
			if (preceding) {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_HGE(_PRECEDING, limit[k]);
			} else {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_HGE(_FOLLOWING, limit[k]);
			}
			break;
		}
#endif
		default:
			goto bound_not_supported;
		}
	} else {		/* static bounds */
		switch (tp2) {
		case TYPE_bte:
		case TYPE_sht:
		case TYPE_int:
		case TYPE_lng:{
			lng limit = 0;
			switch (tp2) {
			case TYPE_bte:{
				bte ll = (*(bte *) bound);
				if (ll == GDK_bte_max)	/* UNBOUNDED PRECEDING and UNBOUNDED FOLLOWING cases, avoid overflow */
					return GDKanalyticalallbounds(r, b, p, preceding);
				else
					limit = is_bte_nil(ll) ? lng_nil : (lng) ll;
				break;
			}
			case TYPE_sht:{
				sht ll = (*(sht *) bound);
				if (ll == GDK_sht_max)
					return GDKanalyticalallbounds(r, b, p, preceding);
				else
					limit = is_sht_nil(ll) ? lng_nil : (lng) ll;
				break;
			}
			case TYPE_int:{
				int ll = (*(int *) bound);
				if (ll == GDK_int_max)
					return GDKanalyticalallbounds(r, b, p, preceding);
				else
					limit = is_int_nil(ll) ? lng_nil : (lng) ll;
				break;
			}
			case TYPE_lng:{
				lng ll = (*(lng *) bound);
				if (ll == GDK_lng_max)
					return GDKanalyticalallbounds(r, b, p, preceding);
				else
					limit = is_lng_nil(ll) ? lng_nil : (lng) ll;
				break;
			}
			default:
				assert(0);
			}
			if (is_lng_nil(limit) || limit < 0) {
				goto invalid_bound;
			} else if (tp1 == TYPE_daytime || tp1 == TYPE_date || tp1 == TYPE_timestamp) {
				if (tp2 == TYPE_int) {
					if (preceding) {
						ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_MTIME_MONTH_INTERVAL(_PRECEDING, limit);
					} else {
						ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_MTIME_MONTH_INTERVAL(_FOLLOWING, limit);
					}
				} else {
					if (preceding) {
						ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_MTIME_SEC_INTERVAL(_PRECEDING, limit);
					} else {
						ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_MTIME_SEC_INTERVAL(_FOLLOWING, limit);
					}
				}
			} else if (preceding) {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_NUM(_PRECEDING, limit, lng);
			} else {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_NUM(_FOLLOWING, limit, lng);
			}
			break;
		}
		case TYPE_flt:{
			flt limit = (*(flt *) bound);
			if (is_flt_nil(limit) || limit < 0) {
				goto invalid_bound;
			} else if (limit == GDK_flt_max) {
				return GDKanalyticalallbounds(r, b, p, preceding);
			} else if (preceding) {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_FLT(_PRECEDING, limit);
			} else {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_FLT(_FOLLOWING, limit);
			}
			break;
		}
		case TYPE_dbl:{
			dbl limit = (*(dbl *) bound);
			if (is_dbl_nil(limit) || limit < 0) {
				goto invalid_bound;
			} else if (limit == GDK_dbl_max) {
				return GDKanalyticalallbounds(r, b, p, preceding);
			} else if (preceding) {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_DBL(_PRECEDING, limit);
			} else {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_DBL(_FOLLOWING, limit);
			}
			break;
		}
#ifdef HAVE_HGE
		case TYPE_hge:{
			hge limit = (*(hge *) bound);
			if (is_hge_nil(limit) || limit < 0) {
				goto invalid_bound;
			} else if (limit == GDK_hge_max) {
				return GDKanalyticalallbounds(r, b, p, preceding);
			} else if (preceding) {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_HGE(_PRECEDING, limit);
			} else {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_RANGE_HGE(_FOLLOWING, limit);
			}
			break;
		}
#endif
		default:
			goto bound_not_supported;
		}
	}
	BATsetcount(r, (BUN) cnt);
	r->tnonil = (nils == 0);
	r->tnil = (nils > 0);
	return GDK_SUCCEED;
      bound_not_supported:
	GDKerror("42000!range frame bound type %s not supported.\n", ATOMname(tp2));
	return GDK_FAIL;
      type_not_supported:
	GDKerror("42000!type %s not supported for %s frame bound type.\n", ATOMname(tp1), ATOMname(tp2));
	return GDK_FAIL;
      calc_overflow:
	GDKerror("22003!overflow in calculation.\n");
	return GDK_FAIL;
      invalid_bound:
	GDKerror("42000!range frame bound must be non negative and non null.\n");
	return GDK_FAIL;
}

static gdk_return
GDKanalyticalgroupsbounds(BAT *r, BAT *b, BAT *p, BAT *l, const void *restrict bound, int tp2, bool preceding)
{
	lng cnt = (lng) BATcount(b), *restrict rb = (lng *) Tloc(r, 0), i = 0, k = 0, j = 0;
	bit *restrict np = p ? (bit*)Tloc(p, 0) : NULL, *restrict bp = (bit*) Tloc(b, 0);

	if (b->ttype != TYPE_bit) {
		GDKerror("42000!groups frame bound type must be of type bit.\n");
		return GDK_FAIL;
	}

	if (l) {		/* dynamic bounds */
		if (l->tnil)
			goto invalid_bound;
		switch (tp2) {
		case TYPE_bte:{
			bte *restrict limit = (bte *) Tloc(l, 0);
			if (preceding) {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_GROUPS(_PRECEDING, bte, limit[k], (lng) olimit);
			} else {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_GROUPS(_FOLLOWING, bte, limit[k], (lng) olimit);
			}
			break;
		}
		case TYPE_sht:{
			sht *restrict limit = (sht *) Tloc(l, 0);
			if (preceding) {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_GROUPS(_PRECEDING, sht, limit[k], (lng) olimit);
			} else {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_GROUPS(_FOLLOWING, sht, limit[k], (lng) olimit);
			}
			break;
		}
		case TYPE_int:{
			int *restrict limit = (int *) Tloc(l, 0);
			if (preceding) {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_GROUPS(_PRECEDING, int, limit[k], (lng) olimit);
			} else {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_GROUPS(_FOLLOWING, int, limit[k], (lng) olimit);
			}
			break;
		}
		case TYPE_lng:{
			lng *restrict limit = (lng *) Tloc(l, 0);
			if (preceding) {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_GROUPS(_PRECEDING, lng, limit[k], olimit);
			} else {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_GROUPS(_FOLLOWING, lng, limit[k], olimit);
			}
			break;
		}
#ifdef HAVE_HGE
		case TYPE_hge:{
			hge *restrict limit = (hge *) Tloc(l, 0);
			if (preceding) {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_GROUPS(_PRECEDING, hge, limit[k], (olimit > (hge) GDK_lng_max) ? GDK_lng_max : (lng) olimit);
			} else {
				ANALYTICAL_WINDOW_BOUNDS_BRANCHES_GROUPS(_FOLLOWING, hge, limit[k], (olimit > (hge) GDK_lng_max) ? GDK_lng_max : (lng) olimit);
			}
			break;
		}
#endif
		default:
			goto bound_not_supported;
		}
	} else {	/* static bounds, all the limits are cast to lng */
		lng limit;
		switch (tp2) {
		case TYPE_bte:
			limit = is_bte_nil(*(bte *) bound) ? lng_nil : (lng) *(bte *) bound;
			break;
		case TYPE_sht:
			limit = is_sht_nil(*(sht *) bound) ? lng_nil : (lng) *(sht *) bound;
			break;
		case TYPE_int:
			limit = is_int_nil(*(int *) bound) ? lng_nil : (lng) *(int *) bound;
			break;
		case TYPE_lng:
			limit = (lng) (*(lng *) bound);
			break;
#ifdef HAVE_HGE
		case TYPE_hge: {
			hge nval = *(hge *) bound;
			limit = is_hge_nil(nval) ? lng_nil : (nval > (hge) GDK_lng_max) ? GDK_lng_max : (lng) nval;
			break;
		}
#endif
		default:
			goto bound_not_supported;
		}
		if (limit == GDK_lng_max) {
			return GDKanalyticalallbounds(r, b, p, preceding);
		} else if (is_lng_nil(limit) || limit < 0) {
			goto invalid_bound;	
		} else if (preceding) {
			ANALYTICAL_WINDOW_BOUNDS_BRANCHES_GROUPS(_PRECEDING, lng, limit, olimit);
		} else {
			ANALYTICAL_WINDOW_BOUNDS_BRANCHES_GROUPS(_FOLLOWING, lng, limit, olimit);
		}
	}
	BATsetcount(r, (BUN) cnt);
	return GDK_SUCCEED;
      bound_not_supported:
	GDKerror("42000!groups frame bound type %s not supported.\n", ATOMname(tp2));
	return GDK_FAIL;
      invalid_bound:
	GDKerror("42000!groups frame bound must be non negative and non null.\n");
	return GDK_FAIL;
}

gdk_return
GDKanalyticalwindowbounds(BAT *r, BAT *b, BAT *p, BAT *l, const void *restrict bound, int tp1, int tp2, int unit, bool preceding, lng first_half)
{
	assert((l && !bound) || (!l && bound));

	switch (unit) {
	case 0:
		return GDKanalyticalrowbounds(r, b, p, l, bound, tp2, preceding, first_half);
	case 1:
		return GDKanalyticalrangebounds(r, b, p, l, bound, tp1, tp2, preceding);
	case 2:
		return GDKanalyticalgroupsbounds(r, b, p, l, bound, tp2, preceding);
	default:
		assert(0);
	}
	GDKerror("42000!unit type %d not supported (this is a bug).\n", unit);
	return GDK_FAIL;
}
