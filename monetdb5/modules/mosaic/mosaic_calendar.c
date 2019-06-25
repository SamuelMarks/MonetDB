/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0.  If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright 1997 - July 2008 CWI, August 2008 - 2018 MonetDB B.V.
 */

/*
 * 2014-2016 author Martin Kersten
 * Global calendar calendar encoding
 * A dictionary of year-month values is collected.
 * It leads to a compact n-bit representation.
 */

#include "monetdb_config.h"
#include "gdk.h"
#include "gdk_bitvector.h"
#include "mosaic.h"
#include "mosaic_calendar.h"
#include "mosaic_private.h"

#define MASKDAY 037
#define MASKBITS 5

bool MOStypes_calendar(BAT* b) {
	return ATOMbasetype(getBatType(b->ttype)) == TYPE_date /* TODO: || type == TYPE_timestamp || type == TYPE_daytime*/;
}

void
MOSadvance_calendar(MOStask task)
{
	int *dst = (int*)  MOScodevector(task);
	BUN cnt = MOSgetCnt(task->blk);
	long bytes;

	assert(cnt > 0);
	task->start += (oid) cnt;
	task->stop = task->stop;
	bytes =  (long) (cnt * task->hdr->bits)/8 + (((cnt * task->hdr->bits) %8) != 0);
	task->blk = (MosaicBlk) (((char*) dst)  + wordaligned(bytes, int)); 
}

void
MOSlayout_calendar_hdr(MOStask task, BAT *btech, BAT *bcount, BAT *binput, BAT *boutput, BAT *bproperties)
{
	lng zero=0;
	int i;
	char buf[BUFSIZ];

	for(i=0; i< task->hdr->dictsize; i++){
		snprintf(buf, BUFSIZ,"calendar[%d]",i);
		if( BUNappend(btech, buf, false)!= GDK_SUCCEED ||
			BUNappend(bcount, &zero, false)!= GDK_SUCCEED ||
			BUNappend(binput, &zero, false)!= GDK_SUCCEED ||
			BUNappend(boutput, &task->hdr->dictfreq[i], false)!= GDK_SUCCEED ||
			BUNappend(bproperties, buf, false)!= GDK_SUCCEED) 
			return;
	}
}


void
MOSlayout_calendar(MOStask task, BAT *btech, BAT *bcount, BAT *binput, BAT *boutput, BAT *bproperties)
{
	MosaicBlk blk = task->blk;
	lng cnt = MOSgetCnt(blk), input=0, output= 0;

	input = cnt * ATOMsize(task->type);
	output =  MosaicBlkSize + (cnt * task->hdr->bits)/8 + (((cnt * task->hdr->bits) %8) != 0);
	if(	BUNappend(btech, "calendar blk", false) != GDK_SUCCEED ||
		BUNappend(bcount, &cnt, false) != GDK_SUCCEED ||
		BUNappend(binput, &input, false) != GDK_SUCCEED ||
		BUNappend(boutput, &output, false) != GDK_SUCCEED ||
		BUNappend(bproperties, "", false) != GDK_SUCCEED)
		return;
}

void
MOSskip_calendar(MOStask task)
{
	MOSadvance_calendar(task);
	if ( MOSgetTag(task->blk) == MOSAIC_EOL)
		task->blk = 0; // ENDOFLIST
}

#define MOSfind(Res,DICT,VAL,F,L)\
{ int m,f= F, l=L; \
   while( l-f > 0 ) { \
	m = f + (l-f)/2;\
	if ( VAL < DICT[m] ) l=m-1; else f= m;\
	if ( VAL > DICT[m] ) f=m+1; else l= m;\
   }\
   Res= f;\
}

// Create a larger calendar buffer then we allow for in the mosaic header first
// Store the most frequent ones in the compressed heap header directly based on estimated savings
#define TMPDICT 16*256

#define makeCalendar(TPE)\
{	TPE *val = ((TPE*)task->src) + task->start,v,w;\
	BUN limit = task->stop - task->start > MOSAICMAXCNT? MOSAICMAXCNT: task->stop - task->start;\
	lng cw,cv;\
	for(i = 0; i< limit; i++, val++){\
		v= *val & ~MASKDAY; \
		MOSfind(j,dict.val##TPE,v,0,dictsize);\
		if(j == dictsize && dictsize == 0 ){\
			dict.val##TPE[j]= v;\
			cnt[j] = 1;\
			dictsize++;\
		} else  \
		if( dictsize < TMPDICT && dict.val##TPE[j] != v){\
			w= v; cw= 1;\
			for( ; j< dictsize; j++)\
			if (dict.val##TPE[j] > w){\
				v =dict.val##TPE[j];\
				dict.val##TPE[j]= w;\
				w = v;\
				cv = cnt[j];\
				cnt[j]= cw;\
				cw = cv;\
			}\
			dictsize++;\
			dict.val##TPE[j]= w;\
			cnt[j] = 1;\
		} else if (dictsize < TMPDICT) cnt[j]++;\
} }

/* there are only three kinds of calendardictionaries */
void
MOScreatecalendar(MOStask task)
{
	BUN i;
	int j, k, max;
	MosaicHdr hdr = task->hdr;
    union{
        int valint[TMPDICT];
        lng vallng[TMPDICT];
    }dict;
	lng cnt[TMPDICT];
	bte keep[TMPDICT];
	int dictsize = 0;

	memset((char*) &dict, 0, TMPDICT * sizeof(lng));
	memset((char*) cnt, 0, sizeof(cnt));
	memset((char*) keep, 0, sizeof(keep));

	// compress by factoring out the year-month, keeping 5 bits for the day
	if( task->type == TYPE_date)
		makeCalendar(int)
	if( task->type == TYPE_daytime){
		// TODO:
	}
	if( task->type == TYPE_timestamp){
		// TODO:
	}

	if( dictsize == 0)
		return;
	/* find the 256 most frequent values and save them in the mosaic header */
	if( dictsize < 256){
#ifdef HAVE_HGE
		memcpy((char*)&task->hdr->dict,  (char*)&dict, dictsize * sizeof(hge));
#else
		memcpy((char*)&task->hdr->dict,  (char*)&dict, dictsize * sizeof(lng));
#endif
		memcpy((char*)task->hdr->dictfreq,  (char*)&cnt, dictsize * sizeof(lng));
		hdr->dictsize = dictsize;
	} else {
		/* brute force search of the top-k */
		for(j=0; j< 256; j++){
			for(max = 0; max <dictsize && keep[max]; max++){}
			for(k=0; k< dictsize; k++)
			if( keep[k]==0){
				if( cnt[k]> cnt[max]) max = k;
			}
			keep[max]=1;
		}
		/* keep the top-k, in order */
		for( j=k=0; k<dictsize; k++)
		if( keep[k]){
			task->hdr->dict.vallng[j] = dict.vallng[k];
		}
		hdr->dictsize = j;
		assert(j<256);
	}
	/* calculate the bit-width */
	hdr->bits = MASKBITS + 1;
	hdr->mask =1;	// only for dictionary part
	for( j=2 ; j < dictsize; j *=2){
		hdr->bits++;
		hdr->mask = (hdr->mask <<1) | 1;
	}
}

// calculate the expected reduction using DICT in terms of elements compressed
flt
MOSestimate_calendar(MOStask task)
{	
	BUN i = 0;
	int j;
	flt factor= 1.0;
	MosaicHdr hdr = task->hdr;

	if( task->type == TYPE_date){
		int *val = ((int*)task->src) + task->start, v;
		BUN limit = task->stop - task->start > MOSAICMAXCNT? MOSAICMAXCNT: task->stop - task->start;
		if( task->range[MOSAIC_CALENDAR] > task->start){
			i = task->range[MOSAIC_CALENDAR] - task->start;
			if ( i > MOSAICMAXCNT ) i = MOSAICMAXCNT;
			if( i * sizeof(int) <= wordaligned( MosaicBlkSize + (i * hdr->bits)/8,int))
				return 0.0;
			if( task->dst +  wordaligned(MosaicBlkSize + (i * hdr->bits)/8,sizeof(int)) >= task->bsrc->tmosaic->base + task->bsrc->tmosaic->size)
				return 0.0;
			if(i) factor = ((flt) i * sizeof(int))/ wordaligned(MosaicBlkSize + sizeof(int) + (i * hdr->bits)/8,int);
			return factor;
		}
		for(i =0; i<limit; i++, val++){
			v= *val & ~MASKDAY;
			MOSfind(j,hdr->dict.valint,v,0,hdr->dictsize);
			if( j == hdr->dictsize || hdr->dict.valint[j] != v )
				break;
		}
		if( i * sizeof(int) <= wordaligned( MosaicBlkSize + (i * hdr->bits)/8 ,int))
			return 0.0;
		if(i) factor = (flt) ((int)i * sizeof(int)) / wordaligned( MosaicBlkSize + (i * hdr->bits)/8,int);
	}

	task->factor[MOSAIC_CALENDAR] = factor;
	task->range[MOSAIC_CALENDAR] = task->start + i;
	return factor; 
}

// insert a series of values into the compressor block using calendar
#define CALcompress(TPE)\
{	TPE *val = ((TPE*)task->src) + task->start, v;\
	BitVector base = (BitVector) MOScodevector(task);\
	BUN limit = task->stop - task->start > MOSAICMAXCNT? MOSAICMAXCNT: task->stop - task->start;\
	for(i =0; i<limit; i++, val++){\
		v = *val & ~MASKDAY;\
		MOSfind(j,task->hdr->dict.val##TPE,v,0,hdr->dictsize);\
		if(j == (unsigned int) hdr->dictsize || task->hdr->dict.val##TPE[j] !=  v) \
			break;\
		else {\
			hdr->checksum.sum##TPE += v;\
			hdr->dictfreq[j]++;\
			MOSincCnt(blk,1);\
			setBitVector(base, i, hdr->bits , (unsigned int)( ((j & hdr->mask)<< MASKBITS) | (*val & MASKDAY)) );\
		}\
	}\
	assert(i);\
}

// the inverse operator, extend the src
#define CALdecompress(TPE)\
{	BUN lim = MOSgetCnt(blk);\
	base = (BitVector) MOScodevector(task);\
	for(i = 0; i < lim; i++){\
		j= getBitVector(base,i,(int) hdr->bits ); \
		((TPE*)task->src)[i] = task->hdr->dict.val##TPE[ (j>> MASKBITS) & task->hdr->mask] | (j & MASKDAY);\
		hdr->checksum2.sum##TPE += task->hdr->dict.val##TPE[ (j >> MASKBITS) & task->hdr->mask];\
	}\
	task->src += i * sizeof(TPE);\
}

void
MOScompress_calendar(MOStask task)
{
	BUN i;
	unsigned int j;
	MosaicBlk blk = task->blk;
	MosaicHdr hdr = task->hdr;

	task->dst = MOScodevector(task);

	MOSsetTag(blk,MOSAIC_CALENDAR);
	MOSsetCnt(blk,0);

	if( task->type == TYPE_date)
		CALcompress(int);
	if( task->type == TYPE_daytime){
		// TODO:
	}
	if( task->type == TYPE_timestamp){
		// TODO:
	}
}

void
MOSdecompress_calendar(MOStask task)
{
	MosaicBlk blk = task->blk;
	MosaicHdr hdr = task->hdr;
	BUN i;
	unsigned int j;
	BitVector base;

	if( task->type == TYPE_date){
		CALdecompress(int);
	}
	if( task->type == TYPE_daytime){
		// TODO:
	}
	if( task->type == TYPE_timestamp){
		// TODO:
	}
}

// perform relational algebra operators over non-compressed chunks
// They are bound by an oid range and possibly a candidate list

#define select_calendar(TPE,BITS,MASK) {\
	base = (BitVector) MOScodevector(task);\
	if( !*anti){\
		if( is_nil(TPE, *(TPE*) low) && is_nil(TPE, *(TPE*) hgh)){\
			for( ; first < last; first++){\
				MOSskipit();\
				*o++ = (oid) first;\
			}\
		} else\
		if( is_nil(TPE, *(TPE*) low) ){\
			for(i=0 ; first < last; first++, i++){\
				MOSskipit();\
				j= getBitVector(base,i,(int) hdr->bits); \
				cmp  =  ((*hi && (task->hdr->dict.val##TPE[j>>BITS] | (j & MASK)) <= * (TPE*)hgh ) || (!*hi && (task->hdr->dict.val##TPE[j>>BITS] | (j & MASK)) < *(TPE*)hgh ));\
				if (cmp )\
					*o++ = (oid) first;\
			}\
		} else\
		if( is_nil(TPE, *(TPE*) hgh) ){\
			for(i=0; first < last; first++, i++){\
				MOSskipit();\
				j= getBitVector(base,i,(int) hdr->bits ); \
				cmp  =  ((*li && (task->hdr->dict.val##TPE[j>>BITS] | (j & MASK)) >= * (TPE*)low ) || (!*li && (task->hdr->dict.val##TPE[j>>BITS] | (j & MASK)) > *(TPE*)low ));\
				if (cmp )\
					*o++ = (oid) first;\
			}\
		} else{\
			for(i=0 ; first < last; first++, i++){\
				MOSskipit();\
				j= getBitVector(base,i,(int) hdr->bits ); \
				cmp  =  ((*hi && (task->hdr->dict.val##TPE[j>>BITS] | (j & MASK)) <= * (TPE*)hgh ) || (!*hi && (task->hdr->dict.val##TPE[j>>BITS] | (j & MASK)) < *(TPE*)hgh )) &&\
						((*li && (task->hdr->dict.val##TPE[j>>BITS] | (j & MASK)) >= * (TPE*)low ) || (!*li && (task->hdr->dict.val##TPE[j>>BITS] | (j & MASK)) > *(TPE*)low ));\
				if (cmp )\
					*o++ = (oid) first;\
			}\
		}\
	} else {\
		if( is_nil(TPE, *(TPE*) low) && is_nil(TPE, *(TPE*) hgh)){\
			/* nothing is matching */\
		} else\
		if( is_nil(TPE, *(TPE*) low) ){\
			for(i=0 ; first < last; first++, i++){\
				MOSskipit();\
				j= getBitVector(base,i,(int) hdr->bits ); \
				cmp  =  ((*hi && (task->hdr->dict.val##TPE[j>>BITS] | (j & MASK)) <= * (TPE*)hgh ) || (!*hi && (task->hdr->dict.val##TPE[j>>BITS] | (j & MASK)) < *(TPE*)hgh ));\
				if ( !cmp )\
					*o++ = (oid) first;\
			}\
		} else\
		if( is_nil(TPE, *(TPE*) hgh) ){\
			for(i=0 ; first < last; first++, i++){\
				MOSskipit();\
				j= getBitVector(base,i,(int) hdr->bits); \
				cmp  =  ((*li && (task->hdr->dict.val##TPE[j>>BITS] | (j & MASK)) >= * (TPE*)low ) || (!*li && (task->hdr->dict.val##TPE[j>>BITS] | (j & MASK)) > *(TPE*)low ));\
				if ( !cmp )\
					*o++ = (oid) first;\
			}\
		} else{\
			for(i=0 ; first < last; first++, i++){\
				MOSskipit();\
				j= getBitVector(base,i,(int) hdr->bits); \
				cmp  =  ((*hi && (task->hdr->dict.val##TPE[j>>BITS] | (j & MASK)) <= * (TPE*)hgh ) || (!*hi && (task->hdr->dict.val##TPE[j>>BITS] | (j & MASK)) < *(TPE*)hgh )) &&\
						((*li && (task->hdr->dict.val##TPE[j>>BITS] | (j & MASK)) >= * (TPE*)low ) || (!*li && (task->hdr->dict.val##TPE[j>>BITS] | (j & MASK)) > *(TPE*)low ));\
				if ( !cmp )\
					*o++ = (oid) first;\
			}\
		}\
	}\
}

str
MOSselect_calendar( MOStask task, void *low, void *hgh, bit *li, bit *hi, bit *anti)
{
	oid *o;
	BUN i, first,last;
	MosaicHdr hdr = task->hdr;
	int cmp;
	bte j;
	BitVector base;

	// set the oid range covered and advance scan range
	first = task->start;
	last = first + MOSgetCnt(task->blk);

	if (task->cl && *task->cl > last){
		MOSskip_calendar(task);
		return MAL_SUCCEED;
	}
	o = task->lb;

	if( task->type == TYPE_date){
		select_calendar(int, MASKBITS,MASKDAY); 
	}
	if( task->type == TYPE_daytime){
		// TODO:
	}
	if( task->type == TYPE_timestamp){
		// TODO:
	}
	MOSskip_calendar(task);
	task->lb = o;
	return MAL_SUCCEED;
}

#define thetaselect_calendar(TPE)\
{ 	TPE low,hgh;\
	base = (BitVector) MOScodevector(task);\
	low= hgh = TPE##_nil;\
	if ( strcmp(oper,"<") == 0){\
		hgh= *(TPE*) val;\
		hgh = PREVVALUE##TPE(hgh);\
	} else\
	if ( strcmp(oper,"<=") == 0){\
		hgh= *(TPE*) val;\
	} else\
	if ( strcmp(oper,">") == 0){\
		low = *(TPE*) val;\
		low = NEXTVALUE##TPE(low);\
	} else\
	if ( strcmp(oper,">=") == 0){\
		low = *(TPE*) val;\
	} else\
	if ( strcmp(oper,"!=") == 0){\
		hgh= low= *(TPE*) val;\
		anti++;\
	} else\
	if ( strcmp(oper,"==") == 0){\
		hgh= low= *(TPE*) val;\
	} \
	for( ; first < last; first++){\
		MOSskipit();\
		j= getBitVector(base,first,(int) hdr->bits ); \
		if( (is_nil(TPE, low) || (task->hdr->dict.val##TPE[(j>>MASKBITS) & task->hdr->mask] | (j & MASKDAY)) >= low) && \
			((task->hdr->dict.val##TPE[(j>>MASKBITS) & task->hdr->mask] | (j & MASKDAY))  <= hgh || is_nil(TPE, hgh)) ){\
			if ( !anti) {\
				*o++ = (oid) first;\
			}\
		} else\
			if( anti){\
				*o++ = (oid) first;\
			}\
	}\
} 

str
MOSthetaselect_calendar( MOStask task, void *val, str oper)
{
	oid *o;
	int anti=0;
	BUN first,last;
	MosaicHdr hdr = task->hdr;
	bte j;
	BitVector base;
	
	// set the oid range covered and advance scan range
	first = task->start;
	last = first + MOSgetCnt(task->blk);

	if (task->cl && *task->cl > last){
		MOSskip_calendar(task);
		return MAL_SUCCEED;
	}
	o = task->lb;

	if( task->type == TYPE_date){
		thetaselect_calendar(int); 
	}
	if( task->type == TYPE_daytime){
		// TODO:
	}
	if( task->type == TYPE_timestamp){
		// TODO:
	}
	MOSskip_calendar(task);
	task->lb =o;
	return MAL_SUCCEED;
}

#define projection_calendar(TPE,MASK)\
{	TPE *v;\
	base = (BitVector) MOScodevector(task);\
	v= (TPE*) task->src;\
	for(i=0; first < last; first++,i++){\
		MOSskipit();\
		j= getBitVector(base,i,(int) hdr->bits ); \
		*v++ = task->hdr->dict.val##TPE[(j>>MASKBITS) & task->hdr->mask] | ( j & MASKDAY);\
		task->cnt++;\
	}\
	task->src = (char*) v;\
}

str
MOSprojection_calendar( MOStask task)
{
	BUN i,first,last;
	MosaicHdr hdr = task->hdr;
	unsigned short j;
	BitVector base;
	// set the oid range covered and advance scan range
	first = task->start;
	last = first + MOSgetCnt(task->blk);

	if( task->type == TYPE_date){
		projection_calendar(int, MASKDAY); 
	}
	if( task->type == TYPE_daytime){
		// TODO:
	}
	if( task->type == TYPE_timestamp){
		// TODO:
	}
	MOSskip_calendar(task);
	return MAL_SUCCEED;
}

#define join_calendar_str(TPE)\
	throw(MAL,"mosaic.calendar","TBD");

#define join_calendar(TPE)\
{	TPE  *w;\
	BitVector base = (BitVector) MOScodevector(task);\
	w = (TPE*) task->src;\
	limit= MOSgetCnt(task->blk);\
	for( o=0, n= task->stop; n-- > 0; o++,w++ ){\
		for(oo = task->start,i=0; i < limit; i++,oo++){\
			j= getBitVector(base,i,(int) hdr->bits); \
			if ( *w == (task->hdr->dict.val##TPE[(j>>MASKBITS) & task->hdr->mask] | (j & MASKDAY))){\
				if( BUNappend(task->lbat, &oo, false)!= GDK_SUCCEED ||\
				BUNappend(task->rbat, &o, false)!= GDK_SUCCEED) \
				throw(MAL,"mosaic.calendar",MAL_MALLOC_FAIL);\
			}\
		}\
	}\
}

str
MOSjoin_calendar( MOStask task)
{
	BUN i,n,limit;
	oid o, oo;
	MosaicHdr hdr = task->hdr;
	int j;

	if( task->type == TYPE_date){
		join_calendar(int); 
	}
	if( task->type == TYPE_daytime){
		// TODO:
	}
	if( task->type == TYPE_timestamp){
		// TODO:
	}
	MOSskip_calendar(task);
	return MAL_SUCCEED;
}
