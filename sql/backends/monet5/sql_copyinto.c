/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0.  If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright 1997 - July 2008 CWI, August 2008 - 2021 MonetDB B.V.
 */

/*
 *  Niels Nes, Martin Kersten
 *
 * Parallel bulk load for SQL
 * The COPY INTO command for SQL is heavily CPU bound, which means
 * that ideally we would like to exploit the multi-cores to do that
 * work in parallel.
 * Complicating factors are the initial record offset, the
 * possible variable length of the input, and the original sort order
 * that should preferable be maintained.
 *
 * The code below consists of a file reader, which breaks up the
 * file into chunks of distinct rows. Then multiple parallel threads
 * grab them, and break them on the field boundaries.
 * After all fields are identified this way, the columns are converted
 * and stored in the BATs.
 *
 * The threads get a reference to a private copy of the READERtask.
 * It includes a list of columns they should handle. This is a basis
 * to distributed cheap and expensive columns over threads.
 *
 * The file reader overlaps IO with updates of the BAT.
 * Also the buffer size of the block stream might be a little small for
 * this task (1MB). It has been increased to 8MB, which indeed improved.
 *
 * The work divider allocates subtasks to threads based on the
 * observed time spending so far.
 */

#include "monetdb_config.h"
#include "sql.h"

#include "sql_copyinto.h"
#include "str.h"
#include "mapi_prompt.h"

#include <string.h>
#include <ctype.h>


typedef struct Column_t {
	const char *name;			/* column title */
	const char *sep;
	const char *rsep;
	int seplen;
	char *type;
	int adt;					/* type index */
	BAT *c;						/* set to NULL when scalar is meant */
	BATiter ci;
	BUN p;
	unsigned int tabs;			/* field size in tab positions */
	const char *nullstr;		/* null representation */
	size_t null_length;			/* its length */
	unsigned int width;			/* actual column width */
	unsigned int maxwidth;		/* permissible width */
	int fieldstart;				/* Fixed character field load positions */
	int fieldwidth;
	int scale, precision;
	void *(*frstr)(struct Column_t *fmt, int type, void **dst, size_t *dst_len, const char *s);
	sql_column *column;
	void *data;
	int skip;					/* only skip to the next field */
	size_t len;
	bit ws;						/* if set we need to skip white space */
	char quote;					/* if set use this character for string quotes */
	const void *nildata;
	size_t nil_len;
	int size;
	void *appendcol;			/* temporary, can probably use Columnt_t.column in the future */
} Column;

/*
 * All table printing is based on building a report structure first.
 * This table structure is private to a client, which made us to
 * keep it in an ADT.
 */

typedef struct Table_t {
	BUN offset;
	BUN nr;						/* allocated space for table loads */
	BUN nr_attrs;				/* attributes found sofar */
	Column *format;				/* remove later */
	str error;					/* last error */
	int tryall;					/* skip erroneous lines */
	str filename;				/* source */
	BAT *complaints;			/* lines that did not match the required input */
} Tablet;


struct directappend {
	mvc *mvc;
	sql_table *t;
	BAT *all_offsets; // all offsets ever generated
	BAT *new_offsets; // as most recently returned by mvc_claim_slots.
	BUN offset;	      // as most recently returned by mvc_claim_slots.
};

static void
directappend_destroy(struct directappend *state)
{
	if (state == NULL)
		return;
	struct directappend *st = state;
	if (st->all_offsets)
		BBPreclaim(st->all_offsets);
	if (st->new_offsets)
		BBPreclaim(st->new_offsets);
}

static str
directappend_init(struct directappend *state, Client cntxt, sql_table *t)
{
	str msg = MAL_SUCCEED;
	*state = (struct directappend) { 0 };
	backend *be;
	mvc *mvc;

	msg = checkSQLContext(cntxt);
	if (msg != MAL_SUCCEED)
		goto bailout;
	be = cntxt->sqlcontext;
	mvc = be->mvc;
	state->mvc = mvc;
	state->t = t;

	state->all_offsets = COLnew(0, TYPE_oid, 0, TRANSIENT);
	if (state->all_offsets == NULL) {
		msg = createException(SQL, "sql.append_from", SQLSTATE(HY013) MAL_MALLOC_FAIL);
		goto bailout;
	}

	assert(msg == MAL_SUCCEED);
	return msg;

bailout:
	assert(msg != MAL_SUCCEED);
	directappend_destroy(state);
	return msg;
}

static str
directappend_claim(void *state_, size_t nrows, size_t ncols, Column *cols[])
{
	str msg = MAL_SUCCEED;

	// these parameters aren't used right now, useful if we ever also move the
	// old bunfastapp-on-temporary-bats scheme to the callback interface
	// too, making SQLload_file fully mechanism agnostic.
	// Then again, maybe just drop them instead.
	(void)ncols;
	(void)cols;

	assert(state_ != NULL);
	struct directappend *state = state_;

	if (state->new_offsets != NULL) {
		// Leftover from previous round, the logic below counts on it not being present.
		// We can change that but have to do so carefully. for now just drop it.
		BBPreclaim(state->new_offsets);
		state->new_offsets = NULL;
	}

	// Allocate room for this batch
	BUN dummy_offset = 424242424242;
	state->offset = dummy_offset;
	sql_trans *tr = state->mvc->session->tr;
	sqlstore *store = tr->store;
	int ret = store->storage_api.claim_tab(tr, state->t, nrows, &state->offset, &state->new_offsets);
	// int ret = mvc_claim_slots(state->mvc->session->tr, state->t, nrows, &state->offset, &state->new_offsets);
	if (ret != LOG_OK) {
		msg = createException(SQL, "sql.append_from", SQLSTATE(3F000) "Could not claim slots");
		goto bailout;
	}

	// Append the batch to all_offsets.
	if (state->new_offsets != NULL) {
		if (BATappend(state->all_offsets, state->new_offsets, NULL, false) != GDK_SUCCEED) {
			msg = createException(SQL, "sql.append_from", SQLSTATE(3F000) "BATappend failed");
			goto bailout;
		}
	} else {
		// Help, there must be a BATfunction for this.
		// Also, maybe we should try to make state->all_offsets a void BAT and only
		// switch to materialized oid's if necessary.
		BUN oldcount = BATcount(state->all_offsets);
		BUN newcount = oldcount + nrows;
		if (BATcapacity(state->all_offsets) < newcount) {
			if (BATextend(state->all_offsets, newcount) != GDK_SUCCEED) {
				msg = createException(SQL, "sql.append_from", SQLSTATE(HY013) MAL_MALLOC_FAIL);
				goto bailout;
			}
		}
		oid * oo = Tloc(state->all_offsets, oldcount);
		for (BUN i = 0; i < nrows; i++)
			*oo++ = state->offset + i;
		BATsetcount(state->all_offsets, newcount);
	}

	// The protocol for mvc_claim is that it returns either a consecutive block,
	// by setting *offset, or a BAT of positions by setting *offsets. However,
	// it is possible and even likely that only the first few items of the BAT
	// are actually scattered positions, while the rest is still a consecutive
	// block at the end. Appending at the end is much cheaper so we peel the
	// consecutive elements off the back of the BAT and treat them separately.
	//
	// In the remainder of the function, 'state->newoffsets' holds 'front_count'
	// positions if it exists, while another 'back_count' positions start at
	// 'back_offset'.
	//
	// TODO this code has become a little convoluted as it evolved.
	// Needs straightening out.
	size_t front_count;
	size_t back_count;
	BUN back_offset;
	if (state->new_offsets != NULL) {
		if (state->new_offsets->tsorted) {
			assert(BATcount(state->new_offsets) >= 1);
			BUN start = BATcount(state->new_offsets) - 1;
			oid at_start = *(oid*)Tloc(state->new_offsets, start);
			while (start > 0) {
				oid below_start = *(oid*)Tloc(state->new_offsets, start - 1);
				if (at_start != below_start + 1)
					break;
				start = start - 1;
				at_start = below_start;
			}
			front_count = start;
			back_count = nrows - start;
			back_offset = at_start;
			BATsetcount(state->new_offsets, start);
		} else {
			front_count = nrows;
			back_count = 0;
			back_offset = dummy_offset;
		}
	} else {
		front_count = 0;
		back_count = nrows;
		back_offset = state->offset;
	}
	state->offset = back_offset;

	// debugging
	(void)front_count;
	(void)back_count;
	// if (front_count > 0) {
	// 	for (size_t j = 0; j < front_count; j++) {
	// 		BUN pos = (BUN)*(oid*)Tloc(state->new_offsets, j);
	// 		fprintf(stderr, "scattered offset[%zu] = " BUNFMT "\n", j, pos);
	// 	}
	// }
	// if (back_count > 0) {
	// 	BUN start = back_offset;
	// 	BUN end = start + (BUN)back_count - 1;
	// 	fprintf(stderr, "consecutive offsets: " BUNFMT " .. " BUNFMT"\n", start, end);
	// }


	assert(msg == MAL_SUCCEED);
	return msg;

bailout:
	assert(msg != MAL_SUCCEED);
	return msg;
}

static BAT*
directappend_get_offsets_bat(struct directappend *state)
{
	return state->all_offsets;
}

static str
directappend_append_one(struct directappend *state, size_t idx, const void *const_data, void *col)
{
	BAT *scattered_offsets = state->new_offsets;
	BUN scattered_count = scattered_offsets ? BATcount(scattered_offsets) : 0;
	BUN off;
	if (idx < scattered_count) {
		off = *(oid*)Tloc(scattered_offsets, idx);
		// fprintf(stderr, "Took offset " BUNFMT " from position %zu of the offsets BAT\n", off, idx);
	} else {
		off = state->offset + (idx - scattered_count);
		// fprintf(stderr, "Took offset " BUNFMT " as %zu plus base " BUNFMT "\n", off, idx, state->offset);
	}

	sql_column *c = col;
	int tpe = c->type.type->localtype;
	sqlstore *store = state->mvc->session->tr->store;

	// unfortunately, append_col_fptr doesn't take const void*.
	void *data = ATOMextern(tpe) ? &const_data : (void*)const_data;
	int	ret = store->storage_api.append_col(state->mvc->session->tr, c, off, NULL, data, 1, tpe);
	if (ret != LOG_OK) {
		throw(SQL, "sql.append", SQLSTATE(42000) "Append failed%s", ret == LOG_CONFLICT ? " due to conflict with another transaction" : "");
	}

	return MAL_SUCCEED;
}

static str
directappend_append_batch(struct directappend *state, const void *const_data, BUN count, int width, void *col)
{
	sqlstore *store = state->mvc->session->tr->store;
	sql_column *c = col;
	int tpe = c->type.type->localtype;

	(void)width;
	assert(width== ATOMsize(tpe));

	BUN scattered_count = state->new_offsets ? BATcount(state->new_offsets) : 0;

	int ret = LOG_OK;

	if (scattered_count > 0) {
		BUN dummy_offset = GDK_oid_max;
		ret = store->storage_api.append_col(
					state->mvc->session->tr, c,
					dummy_offset, state->new_offsets,
					(void*)const_data, scattered_count, tpe
		);
	}

	if (ret == LOG_OK && count > scattered_count) {
		char *remaining_data = (char*)const_data + scattered_count * width;
		BUN remaining_count = count - scattered_count;
		ret = store->storage_api.append_col(
					state->mvc->session->tr, c,
					state->offset, NULL,
					remaining_data, remaining_count, tpe
		);
	}
	if (ret != LOG_OK) {
		throw(SQL, "sql.append", SQLSTATE(42000) "Append failed%s", ret == LOG_CONFLICT ? " due to conflict with another transaction" : "");
	}

	return MAL_SUCCEED;
}


#define MAXWORKERS	64
#define MAXBUFFERS 2
/* We restrict the row length to be 32MB for the time being */
#define MAXROWSIZE(X) (X > 32*1024*1024 ? X : 32*1024*1024)

static MT_Lock errorlock = MT_LOCK_INITIALIZER(errorlock);

static BAT *
void_bat_create(int adt, BUN nr)
{
	BAT *b = COLnew(0, adt, nr, TRANSIENT);

	/* check for correct structures */
	if (b == NULL)
		return NULL;
	if ((b = BATsetaccess(b, BAT_APPEND)) == NULL) {
		return NULL;
	}

	/* disable all properties here */
	b->tsorted = false;
	b->trevsorted = false;
	b->tnosorted = 0;
	b->tnorevsorted = 0;
	b->tseqbase = oid_nil;
	b->tkey = false;
	b->tnokey[0] = 0;
	b->tnokey[1] = 0;
	return b;
}

static void
TABLETdestroy_format(Tablet *as)
{
	BUN p;
	Column *fmt = as->format;

	for (p = 0; p < as->nr_attrs; p++) {
		if (fmt[p].c)
			BBPunfix(fmt[p].c->batCacheid);
		if (fmt[p].data)
			GDKfree(fmt[p].data);
	}
	GDKfree(fmt);
}

static str
TABLETcreate_bats(Tablet *as, BUN est)
{
	Column *fmt = as->format;
	BUN i, nr = 0;

	for (i = 0; i < as->nr_attrs; i++) {
		if (fmt[i].skip)
			continue;
		fmt[i].c = void_bat_create(fmt[i].adt, est);
		if (!fmt[i].c) {
			while (i > 0) {
				if (!fmt[--i].skip)
					BBPreclaim(fmt[i].c);
			}
			throw(SQL, "copy", "Failed to create bat of size " BUNFMT "\n", as->nr);
		}
		fmt[i].ci = bat_iterator_nolock(fmt[i].c);
		nr++;
	}
	if (!nr)
		throw(SQL, "copy", "At least one column should be read from the input\n");
	return MAL_SUCCEED;
}

static str
TABLETcollect(BAT **bats, Tablet *as)
{
	Column *fmt = as->format;
	BUN i, j;
	BUN cnt = 0;

	if (bats == NULL)
		throw(SQL, "copy", "Missing container");
	for (i = 0; i < as->nr_attrs && !cnt; i++)
		if (!fmt[i].skip)
			cnt = BATcount(fmt[i].c);
	for (i = 0, j = 0; i < as->nr_attrs; i++) {
		if (fmt[i].skip)
			continue;
		bats[j] = fmt[i].c;
		BBPfix(bats[j]->batCacheid);
		if ((fmt[i].c = BATsetaccess(fmt[i].c, BAT_READ)) == NULL)
			throw(SQL, "copy", "Failed to set access at tablet part " BUNFMT "\n", cnt);
		fmt[i].c->tsorted = fmt[i].c->trevsorted = false;
		fmt[i].c->tkey = false;
		BATsettrivprop(fmt[i].c);

		if (cnt != BATcount(fmt[i].c))
			throw(SQL, "copy", "Count " BUNFMT " differs from " BUNFMT "\n", BATcount(fmt[i].c), cnt);
		j++;
	}
	return MAL_SUCCEED;
}


// the starting quote character has already been skipped
static char *
tablet_skip_string(char *s, char quote, bool escape)
{
	size_t i = 0, j = 0;
	while (s[i]) {
		if (escape && s[i] == '\\' && s[i + 1] != '\0')
			s[j++] = s[i++];
		else if (s[i] == quote) {
			if (s[i + 1] != quote)
				break;
			i++;				/* skip the first quote */
		}
		s[j++] = s[i++];
	}
	assert(s[i] == quote || s[i] == '\0');
	if (s[i] == 0)
		return NULL;
	s[j] = 0;
	return s + i;
}

/* returns TRUE if there is/might be more */
static bool
tablet_read_more(bstream *in, stream *out, size_t n)
{
	if (out) {
		do {
			/* query is not finished ask for more */
			/* we need more query text */
			if (bstream_next(in) < 0)
				return false;
			if (in->eof) {
				if (mnstr_write(out, PROMPT2, sizeof(PROMPT2) - 1, 1) == 1)
					mnstr_flush(out, MNSTR_FLUSH_DATA);
				in->eof = false;
				/* we need more query text */
				if (bstream_next(in) <= 0)
					return false;
			}
		} while (in->len <= in->pos);
	} else if (bstream_read(in, n) <= 0) {
		return false;
	}
	return true;
}



/*
 * Fast Load
 * To speedup the CPU intensive loading of files we have to break
 * the file into pieces and perform parallel analysis. Experimentation
 * against lineitem SF1 showed that half of the time goes into very
 * basis atom analysis (41 out of 102 B instructions).
 * Furthermore, the actual insertion into the BATs takes only
 * about 10% of the total. With multi-core processors around
 * it seems we can gain here significantly.
 *
 * The approach taken is to fork a parallel scan over the text file.
 * We assume that the blocked stream is already
 * positioned correctly at the reading position. The start and limit
 * indicates the byte range to search for tuples.
 * If start> 0 then we first skip to the next record separator.
 * If necessary we read more than 'limit' bytes to ensure parsing a complete
 * record and stop at the record boundary.
 * Beware, we should allocate Tablet descriptors for each file segment,
 * otherwise we end up with a gross concurrency control problem.
 * The resulting BATs should be glued at the final phase.
 *
 * Raw Load
 * Front-ends can bypass most of the overhead in loading the BATs
 * by preparing the corresponding files directly and replace those
 * created by e.g. the SQL frontend.
 * This strategy is only advisable for cases where we have very
 * large files >200GB and/or are created by a well debugged code.
 *
 * To experiment with this approach, the code base responds
 * on negative number of cores by dumping the data directly in BAT
 * storage format into a collections of files on disk.
 * It reports on the actions to be taken to replace BATs.
 * This technique is initially only supported for fixed-sized columns.
 * The rawmode() indicator acts as the internal switch.
 */


/*
 *  Niels Nes, Martin Kersten
 *
 * Parallel bulk load for SQL
 * The COPY INTO command for SQL is heavily CPU bound, which means
 * that ideally we would like to exploit the multi-cores to do that
 * work in parallel.
 * Complicating factors are the initial record offset, the
 * possible variable length of the input, and the original sort order
 * that should preferable be maintained.
 *
 * The code below consists of a file reader, which breaks up the
 * file into chunks of distinct rows. Then multiple parallel threads
 * grab them, and break them on the field boundaries.
 * After all fields are identified this way, the columns are converted
 * and stored in the BATs.
 *
 * The threads get a reference to a private copy of the READERtask.
 * It includes a list of columns they should handle. This is a basis
 * to distributed cheap and expensive columns over threads.
 *
 * The file reader overlaps IO with updates of the BAT.
 * Also the buffer size of the block stream might be a little small for
 * this task (1MB). It has been increased to 8MB, which indeed improved.
 *
 * The work divider allocates subtasks to threads based on the
 * observed time spending so far.
 */

/* #define MLOCK_TST did not make a difference on sf10 */

#define BREAKROW 1
#define UPDATEBAT 2
#define SYNCBAT 3
#define ENDOFCOPY 4

struct scratch_buffer {
	void *data;
	size_t len;
	char backing[3]; // small for testing purposes, should be larger
};

static void
initialize_scratch_buffer(struct scratch_buffer *buf)
{
	buf->len = sizeof(buf->backing);
	buf->data = buf->backing;
}

static void *
adjust_scratch_buffer(struct scratch_buffer *buf, size_t min_size, size_t margin)
{
	if (buf->len >= min_size) {
		return buf->data;
	}
	size_t size = min_size + margin;
	// realloc(NULL) is equivalent to alloc()
	void *old_data = buf->data == buf->backing ? NULL : buf->data;
	void *new_data = GDKrealloc(old_data, size);
	if (!new_data)
		return NULL;
	buf->data = new_data;
	buf->len = size;
	return buf->data;
}

static void
destroy_scratch_buffer(struct scratch_buffer *buf)
{
	if (buf->data != buf->backing)
		GDKfree(buf->data);
}

typedef struct {
	Client cntxt;
	int id;						/* for self reference */
	int state;					/* row break=1 , 2 = update bat */
	int workers;				/* how many concurrent ones */
	int error;					/* error during row break */
	int next;
	int limit;
	BUN cnt, maxrow;			/* first row in file chunk. */
	lng skip;					/* number of lines to be skipped */
	lng *time, wtime;			/* time per col + time per thread */
	int rounds;					/* how often did we divide the work */
	bool ateof;					/* io control */
	bool from_stdin;
	bool escape;				/* whether to handle \ escapes */
	bstream *b;
	stream *out;
	MT_Id tid;
	MT_Sema producer;			/* reader waits for call */
	MT_Sema consumer;			/* reader waits for call */
	MT_Sema sema; /* threads wait for work , negative next implies exit */
	MT_Sema reply;				/* let reader continue */
	Tablet *as;
	char *errbuf;
	const char *csep, *rsep;
	size_t seplen, rseplen;
	char quote;

	char *base[MAXBUFFERS], *input[MAXBUFFERS];	/* buffers for row splitter and tokenizer */
	size_t rowlimit[MAXBUFFERS]; /* determines maximal record length buffer */
	char **rows[MAXBUFFERS];
	lng *startlineno[MAXBUFFERS];
	int top[MAXBUFFERS];		/* number of rows in this buffer */
	int cur;  /* current buffer used by splitter and update threads */

	int *cols;					/* columns to handle */
	char ***fields;
	int besteffort;
	bte *rowerror;
	int errorcnt;
	struct directappend *directappend;
	struct scratch_buffer scratch;
	struct scratch_buffer primary;
	struct scratch_buffer secondary;
} READERtask;

static void
tablet_error(READERtask *task, lng row, lng lineno, int col, const char *msg, const char *fcn)
{
	MT_lock_set(&errorlock);
	if (task->cntxt->error_row != NULL) {
		if (BUNappend(task->cntxt->error_row, &lineno, false) != GDK_SUCCEED ||
			BUNappend(task->cntxt->error_fld, &col, false) != GDK_SUCCEED ||
			BUNappend(task->cntxt->error_msg, msg, false) != GDK_SUCCEED ||
			BUNappend(task->cntxt->error_input, fcn, false) != GDK_SUCCEED)
			task->besteffort = 0;
		if (!is_lng_nil(row) && task->rowerror && row < task->limit)
			task->rowerror[row]++;
	}
	if (task->as->error == NULL) {
		if (msg == NULL)
			task->besteffort = 0;
		else if (!is_lng_nil(lineno)) {
			if (!is_int_nil(col))
				task->as->error = createException(MAL, "sql.copy_from", "line " LLFMT ": column %d: %s", lineno, col + 1, msg);
			else
				task->as->error = createException(MAL, "sql.copy_from", "line " LLFMT ": %s", lineno, msg);
		} else
			task->as->error = createException(MAL, "sql.copy_from", "%s", msg);
	}
	task->errorcnt++;
	MT_lock_unset(&errorlock);
}

/*
 * The row is broken into pieces directly on their field separators. It assumes that we have
 * the record in the cache already, so we can do most work quickly.
 * Furthermore, it assume a uniform (SQL) pattern, without whitespace skipping, but with quote and separator.
 */

static size_t
mystrlen(const char *s)
{
	/* Calculate and return the space that is needed for the function
	 * mycpstr below to do its work. */
	size_t len = 0;
	const char *s0 = s;

	while (*s) {
		if ((*s & 0x80) == 0) {
			;
		} else if ((*s & 0xC0) == 0x80) {
			/* continuation byte */
			len += 3;
		} else if ((*s & 0xE0) == 0xC0) {
			/* two-byte sequence */
			if ((s[1] & 0xC0) != 0x80)
				len += 3;
			else
				s += 2;
		} else if ((*s & 0xF0) == 0xE0) {
			/* three-byte sequence */
			if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80)
				len += 3;
			else
				s += 3;
		} else if ((*s & 0xF8) == 0xF0) {
			/* four-byte sequence */
			if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80)
				len += 3;
			else
				s += 4;
		} else {
			/* not a valid start byte */
			len += 3;
		}
		s++;
	}
	len += s - s0;
	return len;
}

static char *
mycpstr(char *t, const char *s)
{
	/* Copy the string pointed to by s into the buffer pointed to by
	 * t, and return a pointer to the NULL byte at the end.  During
	 * the copy we translate incorrect UTF-8 sequences to escapes
	 * looking like <XX> where XX is the hexadecimal representation of
	 * the incorrect byte.  The buffer t needs to be large enough to
	 * hold the result, but the correct length can be calculated by
	 * the function mystrlen above.*/
	while (*s) {
		if ((*s & 0x80) == 0) {
			*t++ = *s++;
		} else if ((*s & 0xC0) == 0x80) {
			t += sprintf(t, "<%02X>", (uint8_t) *s++);
		} else if ((*s & 0xE0) == 0xC0) {
			/* two-byte sequence */
			if ((s[1] & 0xC0) != 0x80)
				t += sprintf(t, "<%02X>", (uint8_t) *s++);
			else {
				*t++ = *s++;
				*t++ = *s++;
			}
		} else if ((*s & 0xF0) == 0xE0) {
			/* three-byte sequence */
			if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80)
				t += sprintf(t, "<%02X>", (uint8_t) *s++);
			else {
				*t++ = *s++;
				*t++ = *s++;
				*t++ = *s++;
			}
		} else if ((*s & 0xF8) == 0xF0) {
			/* four-byte sequence */
			if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80)
				t += sprintf(t, "<%02X>", (uint8_t) *s++);
			else {
				*t++ = *s++;
				*t++ = *s++;
				*t++ = *s++;
				*t++ = *s++;
			}
		} else {
			/* not a valid start byte */
			t += sprintf(t, "<%02X>", (uint8_t) *s++);
		}
	}
	*t = 0;
	return t;
}

static str
SQLload_error(READERtask *task, lng idx, BUN attrs)
{
	str line;
	char *s;
	size_t sz = 0;
	BUN i;

	for (i = 0; i < attrs; i++) {
		if (task->fields[i][idx])
			sz += mystrlen(task->fields[i][idx]);
		sz += task->seplen;
	}

	s = line = GDKmalloc(sz + task->rseplen + 1);
	if (line == 0) {
		tablet_error(task, idx, lng_nil, int_nil, "SQLload malloc error", "SQLload_error");
		return 0;
	}
	for (i = 0; i < attrs; i++) {
		if (task->fields[i][idx])
			s = mycpstr(s, task->fields[i][idx]);
		if (i < attrs - 1)
			s = mycpstr(s, task->csep);
	}
	strcpy(s, task->rsep);
	return line;
}

static void report_append_failed(READERtask *task, Column *fmt, int idx, lng col);
static int report_conversion_failed(READERtask *task, Column *fmt, int idx, lng col, char *s);

static inline void
make_it_nil(Column *fmt, void **dst, size_t *dst_len)
{
	if (fmt->c)
		fmt->c->tnonil = false;
	if (*dst_len >= fmt->nil_len)
		memcpy(*dst, fmt->nildata, fmt->nil_len);
	else {
		GDKfree(*dst);
		*dst_len = 0;
	}
}

// -1 means error, 0 means fully done, 1 means please parse *unescaped
static inline int
prepare_conversion(READERtask *task, Column *fmt, int col, int idx, void **dst, size_t *dst_len, char **unescaped) {
	char *s = task->fields[col][idx];

	if (s == NULL) {
		make_it_nil(fmt, dst, dst_len);
		return 0;
	}
	size_t slen = strlen(s);

	if (!task->escape) {
		*unescaped = s;
	} else {
		// reallocate scratch space if necessary
		size_t needed = slen + 1;
		if (adjust_scratch_buffer(&task->scratch, needed, needed / 2) == NULL) {
			int ret = report_conversion_failed(task, fmt, idx, col + 1, "ALLOCATION FAILURE");
			make_it_nil(fmt, dst, dst_len);
			return ret;
		}
		// unescape into the scratch space
		if (GDKstrFromStr((unsigned char*)task->scratch.data, (unsigned char*)s, slen) < 0) {
			int ret = report_conversion_failed(task, fmt, idx, col + 1, s);
			make_it_nil(fmt, dst, dst_len);
			return ret;
		}
		*unescaped = task->scratch.data;
	}

	return 1;
}

static inline int
SQLconvert_val(READERtask *task, Column *fmt, int col, int idx, void **dst, size_t *dst_len) {
	char *unescaped = NULL;
	int ret = prepare_conversion(task, fmt, col, idx, dst, dst_len, &unescaped);
	if (ret <= 0) {
		// < 0 means error, 0 means fully handled
		return ret;
	}

	// convert using the frstr callback.
	void *p = fmt->frstr(fmt, fmt->adt, dst, dst_len, unescaped);
	if (p == NULL) {
		int ret = report_conversion_failed(task, fmt, idx, col + 1, unescaped);
		make_it_nil(fmt, dst, dst_len);
		return ret;
	}

	return 0;
}

static int
report_conversion_failed(READERtask *task, Column *fmt, int idx, lng col, char *s)
{
	char buf[1024];
	lng row = task->cnt + idx + 1;
	snprintf(buf, sizeof(buf), "'%s' expected", fmt->type);
	char *err = SQLload_error(task, idx, task->as->nr_attrs);
	if (task->rowerror) {
		if (s) {
			size_t slen = mystrlen(s);
			char *scpy = GDKmalloc(slen + 1);
			if ( scpy == NULL){
				task->rowerror[idx]++;
				task->errorcnt++;
				task->besteffort = 0; /* no longer best effort */
				if (task->cntxt->error_row == NULL ||
					BUNappend(task->cntxt->error_row, &row, false) != GDK_SUCCEED ||
					BUNappend(task->cntxt->error_fld, &col, false) != GDK_SUCCEED ||
					BUNappend(task->cntxt->error_msg, SQLSTATE(HY013) MAL_MALLOC_FAIL, false) != GDK_SUCCEED ||
					BUNappend(task->cntxt->error_input, err, false) != GDK_SUCCEED) {
					;		/* ignore error here: we're already not best effort */
				}
				GDKfree(err);
				return -1;
			}
			mycpstr(scpy, s);
			s = scpy;
		}
		MT_lock_set(&errorlock);
		snprintf(buf, sizeof(buf),
					"line " LLFMT " field %s '%s' expected%s%s%s",
					task->startlineno[task->cur][idx], fmt->name ? fmt->name : "", fmt->type,
					s ? " in '" : "", s ? s : "", s ? "'" : "");
		GDKfree(s);
		if (task->as->error == NULL && (task->as->error = GDKstrdup(buf)) == NULL)
			task->as->error = createException(MAL, "sql.copy_from", SQLSTATE(HY013) MAL_MALLOC_FAIL);
		task->rowerror[idx]++;
		task->errorcnt++;
		if (task->cntxt->error_row == NULL ||
			BUNappend(task->cntxt->error_row, &row, false) != GDK_SUCCEED ||
			BUNappend(task->cntxt->error_fld, &col, false) != GDK_SUCCEED ||
			BUNappend(task->cntxt->error_msg, buf, false) != GDK_SUCCEED ||
			BUNappend(task->cntxt->error_input, err, false) != GDK_SUCCEED) {
			GDKfree(err);
			task->besteffort = 0; /* no longer best effort */
			MT_lock_unset(&errorlock);
			return -1;
		}
		MT_lock_unset(&errorlock);
	}
	GDKfree(err);
	return task->besteffort ? 0 : -1;
}

static void
report_append_failed(READERtask *task, Column *fmt, int idx, lng col)
{
	char *err = NULL;
	/* failure */
	if (task->rowerror) {
		lng row = BATcount(task->directappend ? directappend_get_offsets_bat(task->directappend) : fmt->c);
		MT_lock_set(&errorlock);
		if (task->cntxt->error_row == NULL ||
			BUNappend(task->cntxt->error_row, &row, false) != GDK_SUCCEED ||
			BUNappend(task->cntxt->error_fld, &col, false) != GDK_SUCCEED ||
			BUNappend(task->cntxt->error_msg, "insert failed", false) != GDK_SUCCEED ||
			(err = SQLload_error(task, idx, task->as->nr_attrs)) == NULL ||
			BUNappend(task->cntxt->error_input, err, false) != GDK_SUCCEED)
			task->besteffort = 0;
		GDKfree(err);
		task->rowerror[idx]++;
		task->errorcnt++;
		MT_lock_unset(&errorlock);
	}
	task->besteffort = 0;		/* no longer best effort */

}

static int
SQLworker_onebyone_column(READERtask *task, int col)
{
	Column *fmt = &task->as->format[col];
	int count = task->top[task->cur];

	for (int i = 0; i < count; i++) {
		if (SQLconvert_val(task, fmt, col, i, &fmt->data, &fmt->len) < 0)
			return -1;
		const void *value = fmt->data ? fmt->data : fmt->nildata;
		str msg = directappend_append_one(task->directappend, i, value, fmt->appendcol);
		if (msg != MAL_SUCCEED) {
			report_append_failed(task, fmt, i, col + 1);
			return -1;
		}
	}
	return 0;
}

#define TMPL_TYPE bte
#define TMPL_FUNC_NAME dec_bte_frstr
#define TMPL_BULK_FUNC_NAME bulk_convert_bte_dec
#include "sql_copyinto_dec_tmpl.h"
#define TMPL_TYPE sht
#define TMPL_FUNC_NAME dec_sht_frstr
#define TMPL_BULK_FUNC_NAME bulk_convert_sht_dec
#include "sql_copyinto_dec_tmpl.h"
#define TMPL_TYPE int
#define TMPL_FUNC_NAME dec_int_frstr
#define TMPL_BULK_FUNC_NAME bulk_convert_int_dec
#include "sql_copyinto_dec_tmpl.h"
#define TMPL_TYPE lng
#define TMPL_FUNC_NAME dec_lng_frstr
#define TMPL_BULK_FUNC_NAME bulk_convert_lng_dec
#include "sql_copyinto_dec_tmpl.h"
#ifdef HAVE_HGE
#define TMPL_TYPE hge
#define TMPL_FUNC_NAME dec_hge_frstr
#define TMPL_BULK_FUNC_NAME bulk_convert_hge_dec
#include "sql_copyinto_dec_tmpl.h"
#endif




static int
bulk_convert_frstr(READERtask *task, Column *c, int col, int count, size_t width)
{
	void *cursor = task->primary.data;
	size_t w = width;
	for (int i = 0; i < count; i++) {
		char *unescaped = NULL;
		int ret = prepare_conversion(task, c, col, i, &cursor, &w, &unescaped);
		if (ret > 0) {
			void *p = c->frstr(c, c->adt, &cursor, &w, unescaped);
			if (p == NULL) {
				ret = report_conversion_failed(task, c, i, col + 1, unescaped);
				make_it_nil(c, &cursor, &w);
			}
		}
		if (ret < 0) {
			return -1;
		}
		assert(w == width); // should not have attempted to reallocate!
		cursor = (char*)cursor + width;
	}
	return 0;
}

static int
SQLworker_fixedwidth_column(READERtask *task, int col)
{
	Column *c = &task->as->format[col];
	sql_subtype *t = &c->column->type;
	int count = task->top[task->cur];

	int type = c->adt;
	size_t width = ATOMsize(type);
	size_t allocation_size = count * width;

	if (adjust_scratch_buffer(&task->primary, allocation_size, 0) == NULL) {
		tablet_error(task, lng_nil, lng_nil, int_nil, "cannot allocate memory", "");
		return -1;
	}

	int ret;
	if (c->column->type.type->eclass == EC_DEC) {
		switch (c->adt) {
			case TYPE_bte:
				ret = bulk_convert_bte_dec(task, c, col, count, width, t->digits, t->scale);
				break;
			case TYPE_sht:
				ret = bulk_convert_sht_dec(task, c, col, count, width, t->digits, t->scale);
				break;
			case TYPE_int:
				ret = bulk_convert_int_dec(task, c, col, count, width, t->digits, t->scale);
				break;
			case TYPE_lng:
				ret = bulk_convert_lng_dec(task, c, col, count, width, t->digits, t->scale);
				break;
			case TYPE_hge:
				ret = bulk_convert_hge_dec(task, c, col, count, width, t->digits, t->scale);
				break;
			default:
				assert(0 && "unexpected column type for decimal");
				return -1;
		}
	} else {
		ret = bulk_convert_frstr(task, c, col, count, width);
	}
	if (ret < 0)
		return ret;

	// Now insert it.
	str msg = directappend_append_batch(task->directappend, task->primary.data, count, width, c->appendcol);
	if (msg != MAL_SUCCEED) {
		tablet_error(task, lng_nil, lng_nil, col, "bulk insert failed", msg);
		return -1;
	}

	return 0;
}

static int
SQLworker_str_column(READERtask *task, int col)
{
	Column *c = &task->as->format[col];
	int count = task->top[task->cur];

	int type = c->adt;
	size_t width = ATOMsize(type);
	size_t primary_size = count * width;

	size_t secondary_size = 0;
	for (int i = 0; i < count; i++) {
		size_t max_field_size = c->nil_len;
		char *v = task->fields[col][i];
		if (v) {
			max_field_size += strlen(v) + 1;
		}
		secondary_size += max_field_size;
	}

	if (adjust_scratch_buffer(&task->primary, primary_size, 0) == NULL) {
		tablet_error(task, lng_nil, lng_nil, int_nil, "cannot allocate memory", NULL);
		return -1;
	}
	if (adjust_scratch_buffer(&task->secondary, secondary_size, 0) == NULL) {
		tablet_error(task, lng_nil, lng_nil, int_nil, "cannot allocate memory", NULL);
		return -1;
	}

	char **p = task->primary.data;
	void *s = task->secondary.data;
	void *s_end = (char*)s + task->secondary.len;
	for (int i = 0; i < count; i++) {
		assert(s <= s_end);
		size_t s_len = (char*)s_end - (char*)s;

		char *v = task->fields[col][i];
		if (v == NULL) {
			make_it_nil(c, &s, &s_len);
			*p++ = s;
			s = (char*)s + c->nil_len; // includes trailing NUL byte
			continue;
		}

		// Copy or unescape directly into the secondary buffer.
		size_t len = strlen(v);
		if (!task->escape) {
			memcpy(s, v, len + 1);
		} else if (GDKstrFromStr((unsigned char*)s, (unsigned char *)v, len) >= 0) {
			len = strlen(s);
		} else {
			// GDKstrFromStr failed. How bad is it?
			int ret = report_conversion_failed(task, c, i, col + 1, v);
			make_it_nil(c, &s, &s_len);
			if (ret < 0)
				return ret;
			len = c->nil_len - 1; // trailing NUL byte will be accounted for later
		}

		// Validate against the column's length restriction.
		// Assumes strlen(s) >= UTF8_strlen(s) >= strPrintWidth(s)
		if (c->maxwidth > 0 && len > c->maxwidth && !strNil(s)) {
			if ((unsigned int)UTF8_strlen(s) > c->maxwidth) {
				if ((unsigned int)strPrintWidth(s) > c->maxwidth) {
					int ret = report_conversion_failed(task, c, i, col + 1, v);
					make_it_nil(c, &s, &s_len);
					if (ret < 0)
						return ret;
					len = c->nil_len - 1; // trailing NUL byte will be accounted for later
				}
			}
		}

		// Store it in primary and advance secondary
		*p++ = s;
		s = (char*)s + len + 1; // + 1 because of trailing NUL byte
	}

	// Now insert it.
	str msg = directappend_append_batch(task->directappend, task->primary.data, count, width, c->appendcol);
	if (msg != MAL_SUCCEED) {
		tablet_error(task, lng_nil, lng_nil, col, "bulk insert failed", msg);
		return -1;
	}

	return 0;
}

static int
SQLworker_bat_column(READERtask *task, int col)
{
	Column *c = &task->as->format[col];
	int ret = 0;

	/* do we really need this lock? don't think so */
	MT_lock_set(&mal_copyLock);
	if (BATcapacity(c->c) < BATcount(c->c) + task->next) {
		if (BATextend(c->c, BATgrows(c->c) + task->limit) != GDK_SUCCEED) {
			tablet_error(task, lng_nil, lng_nil, col, "Failed to extend the BAT\n", "SQLworker_column");
			MT_lock_unset(&mal_copyLock);
			return -1;
		}
	}
	MT_lock_unset(&mal_copyLock);

	for (int i = 0; i < task->top[task->cur]; i++) {
		if (SQLconvert_val(task, c, col, i, &c->data, &c->len) < 0) {
			ret = -1;
			break;
		}
		const char *value = c->data ? c->data : c->nildata;
		if (bunfastapp(c->c, value) != GDK_SUCCEED) {
			report_append_failed(task, c, i, col + 1);
			ret = -1;
			break;
		}

	}
	BATsetcount(c->c, BATcount(c->c));
	c->c->theap->dirty |= BATcount(c->c) > 0;

	return ret;
}

static int
SQLworker_column(READERtask *task, int col)
{
	Column *fmt = &task->as->format[col];
	if (fmt->skip)
		return 0;

	if (!task->directappend)
		return SQLworker_bat_column(task, col);

	switch (fmt->adt) {
		case TYPE_str:
			return SQLworker_str_column(task, col);
		default:
			if (ATOMvarsized(fmt->adt))
				return SQLworker_onebyone_column(task, col);
			else
				return SQLworker_fixedwidth_column(task, col);
	}
}

/*
 * The rows are broken on the column separator. Any error is shown and reflected with
 * setting the reference of the offending row fields to NULL.
 * This allows the loading to continue, skipping the minimal number of rows.
 * The details about the locations can be inspected from the error table.
 * We also trim the quotes around strings.
 */
static int
SQLload_parse_row(READERtask *task, int idx)
{
	BUN i;
	char errmsg[BUFSIZ];
	char ch = *task->csep;
	char *row = task->rows[task->cur][idx];
	lng startlineno = task->startlineno[task->cur][idx];
	Tablet *as = task->as;
	Column *fmt = as->format;
	bool error = false;
	str errline = 0;

	assert(idx < task->top[task->cur]);
	assert(row);
	errmsg[0] = 0;

	if (task->quote || task->seplen != 1) {
		for (i = 0; i < as->nr_attrs; i++) {
			bool quote = false;
			task->fields[i][idx] = row;
			/* recognize fields starting with a quote, keep them */
			if (*row && *row == task->quote) {
				quote = true;
				task->fields[i][idx] = row + 1;
				row = tablet_skip_string(row + 1, task->quote, task->escape);

				if (!row) {
					errline = SQLload_error(task, idx, i+1);
					snprintf(errmsg, BUFSIZ, "Quote (%c) missing", task->quote);
					tablet_error(task, idx, startlineno, (int) i + 1, errmsg, errline);
					GDKfree(errline);
					error = true;
					goto errors1;
				} else
					*row++ = 0;
			}

			/* eat away the column separator */
			for (; *row; row++)
				if (*row == '\\') {
					if (row[1])
						row++;
				} else if (*row == ch && (task->seplen == 1 || strncmp(row, task->csep, task->seplen) == 0)) {
					*row = 0;
					row += task->seplen;
					goto endoffieldcheck;
				}

			/* not enough fields */
			if (i < as->nr_attrs - 1) {
				errline = SQLload_error(task, idx, i+1);
				tablet_error(task, idx, startlineno, (int) i + 1, "Column value missing", errline);
				GDKfree(errline);
				error = true;
			  errors1:
				/* we save all errors detected  as NULL values */
				for (; i < as->nr_attrs; i++)
					task->fields[i][idx] = NULL;
				i--;
			}
		  endoffieldcheck:
			;
			/* check for user defined NULL string */
			if ((!quote || !fmt->null_length) && fmt->nullstr && task->fields[i][idx] && strncasecmp(task->fields[i][idx], fmt->nullstr, fmt->null_length + 1) == 0)
				task->fields[i][idx] = 0;
		}
	} else {
		assert(!task->quote);
		assert(task->seplen == 1);
		for (i = 0; i < as->nr_attrs; i++) {
			task->fields[i][idx] = row;

			/* eat away the column separator */
			for (; *row; row++)
				if (*row == '\\') {
					if (row[1])
						row++;
				} else if (*row == ch) {
					*row = 0;
					row++;
					goto endoffield2;
				}

			/* not enough fields */
			if (i < as->nr_attrs - 1) {
				errline = SQLload_error(task, idx,i+1);
				tablet_error(task, idx, startlineno, (int) i + 1, "Column value missing", errline);
				GDKfree(errline);
				error = true;
				/* we save all errors detected */
				for (; i < as->nr_attrs; i++)
					task->fields[i][idx] = NULL;
				i--;
			}
		  endoffield2:
			;
			/* check for user defined NULL string */
			if (fmt->nullstr && task->fields[i][idx] && strncasecmp(task->fields[i][idx], fmt->nullstr, fmt->null_length + 1) == 0) {
				task->fields[i][idx] = 0;
			}
		}
	}
	/* check for too many values as well*/
	if (row && *row && i == as->nr_attrs) {
		errline = SQLload_error(task, idx, task->as->nr_attrs);
		snprintf(errmsg, BUFSIZ, "Leftover data '%s'",row);
		tablet_error(task, idx, startlineno, (int) i + 1, errmsg, errline);
		GDKfree(errline);
		error = true;
	}
	return error ? -1 : 0;
}

static void
SQLworker(void *arg)
{
	READERtask *task = (READERtask *) arg;
	unsigned int i;
	int j, piece;
	lng t0;

	GDKsetbuf(GDKmalloc(GDKMAXERRLEN));	/* where to leave errors */
	GDKclrerr();
	task->errbuf = GDKerrbuf;

	while (task->top[task->cur] >= 0) {
		MT_sema_down(&task->sema);

		/* stage one, break the rows spread the work over the workers */
		switch (task->state) {
		case BREAKROW:
			t0 = GDKusec();
			piece = (task->top[task->cur] + task->workers) / task->workers;

			for (j = piece * task->id; j < task->top[task->cur] && j < piece * (task->id +1); j++)
				if (task->rows[task->cur][j]) {
					if (SQLload_parse_row(task, j) < 0) {
						task->errorcnt++;
						// early break unless best effort
						if (!task->besteffort) {
							for (j++; j < task->top[task->cur] && j < piece * (task->id +1); j++)
								for (i = 0; i < task->as->nr_attrs; i++)
									task->fields[i][j] = NULL;
							break;
						}
					}
				}
			task->wtime = GDKusec() - t0;
			break;
		case UPDATEBAT:
			if (!task->besteffort && task->errorcnt)
				break;
			/* stage two, updating the BATs */
			for (i = 0; i < task->as->nr_attrs; i++)
				if (task->cols[i]) {
					t0 = GDKusec();
					if (SQLworker_column(task, task->cols[i] - 1) < 0)
						break;
					t0 = GDKusec() - t0;
					task->time[i] += t0;
					task->wtime += t0;
				}
			break;
		case SYNCBAT:
			if (!task->besteffort && task->errorcnt)
				break;
			for (i = 0; i < task->as->nr_attrs; i++)
				if (task->cols[i]) {
					BAT *b = task->as->format[task->cols[i] - 1].c;
					if (b == NULL)
						continue;
					t0 = GDKusec();
					if (b->batTransient)
						continue;
					BATmsync(b);
					t0 = GDKusec() - t0;
					task->time[i] += t0;
					task->wtime += t0;
				}
			break;
		case ENDOFCOPY:
			MT_sema_up(&task->reply);
			goto do_return;
		}
		MT_sema_up(&task->reply);
	}
	MT_sema_up(&task->reply);

  do_return:
	GDKfree(GDKerrbuf);
	GDKsetbuf(0);
}

static void
SQLworkdivider(READERtask *task, READERtask *ptask, int nr_attrs, int threads)
{
	int i, j, mi;
	lng loc[MAXWORKERS];

	/* after a few rounds we stick to the work assignment */
	if (task->rounds > 8)
		return;
	/* simple round robin the first time */
	if (threads == 1 || task->rounds++ == 0) {
		for (i = j = 0; i < nr_attrs; i++, j++)
			ptask[j % threads].cols[i] = task->cols[i];
		return;
	}
	memset((char *) loc, 0, sizeof(lng) * MAXWORKERS);
	/* use of load directives */
	for (i = 0; i < nr_attrs; i++)
		for (j = 0; j < threads; j++)
			ptask[j].cols[i] = 0;

	/* now allocate the work to the threads */
	for (i = 0; i < nr_attrs; i++, j++) {
		mi = 0;
		for (j = 1; j < threads; j++)
			if (loc[j] < loc[mi])
				mi = j;

		ptask[mi].cols[i] = task->cols[i];
		loc[mi] += task->time[i];
	}
	/* reset the timer */
	for (i = 0; i < nr_attrs; i++, j++)
		task->time[i] = 0;
}

/*
 * Reading is handled by a separate task as a preparation for more parallelism.
 * A buffer is filled with proper rows.
 * If we are reading from a file then a double buffering scheme ia activated.
 * Reading from the console (stdin) remains single buffered only.
 * If we end up with unfinished records, then the rowlimit will terminate the process.
 */

typedef unsigned char (*dfa_t)[256];

static dfa_t
mkdfa(const unsigned char *sep, size_t seplen)
{
	dfa_t dfa;
	size_t i, j, k;

	dfa = GDKzalloc(seplen * sizeof(*dfa));
	if (dfa == NULL)
		return NULL;
	/* Each character in the separator string advances the state by
	 * one.  If state reaches seplen, the separator was recognized.
	 *
	 * The first loop and the nested loop make sure that if in any
	 * state we encounter an invalid character, but part of what we've
	 * matched so far is a prefix of the separator, we go to the
	 * appropriate state. */
	for (i = 0; i < seplen; i++)
		dfa[i][sep[0]] = 1;
	for (j = 0; j < seplen; j++) {
		dfa[j][sep[j]] = (unsigned char) (j + 1);
		for (k = 0; k < j; k++) {
			for (i = 0; i < j - k; i++)
				if (sep[k + i] != sep[i])
					break;
			if (i == j - k && dfa[j][sep[i]] <= i)
				dfa[j][sep[i]] = (unsigned char) (i + 1);
		}
	}
	return dfa;
}

#ifdef __GNUC__
/* __builtin_expect returns its first argument; it is expected to be
 * equal to the second argument */
#define unlikely(expr)	__builtin_expect((expr) != 0, 0)
#define likely(expr)	__builtin_expect((expr) != 0, 1)
#else
#define unlikely(expr)	(expr)
#define likely(expr)	(expr)
#endif

static void
SQLproducer(void *p)
{
	READERtask *task = (READERtask *) p;
	bool consoleinput = false;
	int cur = 0;		// buffer being filled
	bool blocked[MAXBUFFERS] = { false };
	bool ateof[MAXBUFFERS] = { false };
	BUN cnt = 0, bufcnt[MAXBUFFERS] = { 0 };
	char *end = NULL, *e = NULL, *s = NULL, *base;
	const char *rsep = task->rsep;
	size_t rseplen = strlen(rsep), partial = 0;
	char quote = task->quote;
	dfa_t rdfa;
	lng rowno = 0;
	lng lineno = 1;
	lng startlineno = 1;
	int more = 0;

	MT_sema_down(&task->producer);
	if (task->id < 0) {
		return;
	}

	rdfa = mkdfa((const unsigned char *) rsep, rseplen);
	if (rdfa == NULL) {
		tablet_error(task, lng_nil, lng_nil, int_nil, "cannot allocate memory", "");
		ateof[cur] = true;
		goto reportlackofinput;
	}

/*	TRC_DEBUG(MAL_SERVER, "SQLproducer started size '%zu' and len '%zu'\n", task->b->size, task->b->len);*/

	base = end = s = task->input[cur];
	*s = 0;
	task->cur = cur;
	if (task->as->filename == NULL) {
		consoleinput = true;
		goto parseSTDIN;
	}
	for (;;) {
		startlineno = lineno;
		ateof[cur] = !tablet_read_more(task->b, task->out, task->b->size);

		// we may be reading from standard input and may be out of input
		// warn the consumers
		if (ateof[cur] && partial) {
			if (unlikely(partial)) {
				tablet_error(task, rowno, lineno, int_nil, "incomplete record at end of file", s);
				task->b->pos += partial;
			}
			goto reportlackofinput;
		}

		if (task->errbuf && task->errbuf[0]) {
			if (unlikely(GDKerrbuf && GDKerrbuf[0])) {
				tablet_error(task, rowno, lineno, int_nil, GDKerrbuf, "SQLload_file");
/*				TRC_DEBUG(MAL_SERVER, "Bailout on SQLload\n");*/
				ateof[cur] = true;
				break;
			}
		}

	  parseSTDIN:

		/* copy the stream buffer into the input buffer, which is guaranteed larger, but still limited */
		partial = 0;
		task->top[cur] = 0;
		s = task->input[cur];
		base = end;
		/* avoid too long records */
		if (unlikely(end - s + task->b->len - task->b->pos >= task->rowlimit[cur])) {
			/* the input buffer should be extended, but 'base' is not shared
			   between the threads, which we can not now update.
			   Mimick an ateof instead; */
			tablet_error(task, rowno, lineno, int_nil, "record too long", "");
			ateof[cur] = true;
/*			TRC_DEBUG(MAL_SERVER, "Bailout on SQLload confronted with too large record\n");*/
			goto reportlackofinput;
		}
		memcpy(end, task->b->buf + task->b->pos, task->b->len - task->b->pos);
		end = end + task->b->len - task->b->pos;
		*end = '\0';	/* this is safe, as the stream ensures an extra byte */
		/* Note that we rescan from the start of a record (the last
		 * partial buffer from the previous iteration), even if in the
		 * previous iteration we have already established that there
		 * is no record separator in the first, perhaps significant,
		 * part of the buffer. This is because if the record separator
		 * is longer than one byte, it is too complex (i.e. would
		 * require more state) to be sure what the state of the quote
		 * status is when we back off a few bytes from where the last
		 * scan ended (we need to back off some since we could be in
		 * the middle of the record separator).  If this is too
		 * costly, we have to rethink the matter. */
		if (task->from_stdin && *s == '\n' && task->maxrow == BUN_MAX) {
			ateof[cur] = true;
			goto reportlackofinput;
		}
		for (e = s; *e && e < end && cnt < task->maxrow;) {
			/* tokenize the record completely
			 *
			 * The format of the input should comply to the following
			 * grammar rule [ [[quote][[esc]char]*[quote]csep]*rsep]*
			 * where quote is a single user-defined character.
			 * Within the quoted fields a character may be escaped
			 * with a backslash.  The correct number of fields should
			 * be supplied.  In the first phase we simply break the
			 * rows at the record boundary. */
			int nutf = 0;
			int m = 0;
			bool bs = false;
			char q = 0;
			size_t i = 0;
			while (*e) {
				if (task->skip > 0) {
					/* no interpretation of data we're skipping, just
					 * look for newline */
					if (*e == '\n') {
						lineno++;
						break;
					}
				} else {
					/* check for correctly encoded UTF-8 */
					if (nutf > 0) {
						if (unlikely((*e & 0xC0) != 0x80))
							goto badutf8;
						if (unlikely(m != 0 && (*e & m) == 0))
							goto badutf8;
						m = 0;
						nutf--;
					} else if ((*e & 0x80) != 0) {
						if ((*e & 0xE0) == 0xC0) {
							nutf = 1;
							if (unlikely((e[0] & 0x1E) == 0))
								goto badutf8;
						} else if ((*e & 0xF0) == 0xE0) {
							nutf = 2;
							if ((e[0] & 0x0F) == 0)
								m = 0x20;
						} else if (likely((*e & 0xF8) == 0xF0)) {
							nutf = 3;
							if ((e[0] & 0x07) == 0)
								m = 0x30;
						} else {
							goto badutf8;
						}
					} else if (*e == '\n')
						lineno++;
					/* check for quoting and the row separator */
					if (bs) {
						bs = false;
					} else if (task->escape && *e == '\\') {
						bs = true;
						i = 0;
					} else if (*e == q) {
						q = 0;
					} else if (*e == quote) {
						q = quote;
						i = 0;
					} else if (q == 0) {
						i = rdfa[i][(unsigned char) *e];
						if (i == rseplen)
							break;
					}
				}
				e++;
			}
			if (*e == 0) {
				partial = e - s;
				/* found an incomplete record, saved for next round */
				if (unlikely(s+partial < end)) {
					/* found a EOS in the input */
					tablet_error(task, rowno, startlineno, int_nil, "record too long (EOS found)", "");
					ateof[cur] = true;
					goto reportlackofinput;
				}
				break;
			} else {
				rowno++;
				if (task->skip > 0) {
					task->skip--;
				} else {
					if (cnt < task->maxrow) {
						task->startlineno[cur][task->top[cur]] = startlineno;
						task->rows[cur][task->top[cur]++] = s;
						startlineno = lineno;
						cnt++;
					}
					*(e + 1 - rseplen) = 0;
				}
				s = ++e;
				task->b->pos += (size_t) (e - base);
				base = e;
				if (task->top[cur] == task->limit)
					break;
			}
		}

	  reportlackofinput:
/*	  TRC_DEBUG(MAL_SERVER, "SQL producer got buffer '%d' filled with '%d' records\n", cur, task->top[cur]);*/

		if (consoleinput) {
			task->cur = cur;
			task->ateof = ateof[cur];
			task->cnt = bufcnt[cur];
			/* tell consumer to go ahead */
			MT_sema_up(&task->consumer);
			/* then wait until it is done */
			MT_sema_down(&task->producer);
			if (cnt == task->maxrow) {
				GDKfree(rdfa);
				return;
			}
		} else {
			assert(!blocked[cur]);
			if (blocked[(cur + 1) % MAXBUFFERS]) {
				/* first wait until other buffer is done */
/*				TRC_DEBUG(MAL_SERVER, "Wait for consumers to finish buffer: %d\n", (cur + 1) % MAXBUFFERS);*/

				MT_sema_down(&task->producer);
				blocked[(cur + 1) % MAXBUFFERS] = false;
				if (task->state == ENDOFCOPY) {
					GDKfree(rdfa);
					return;
				}
			}
			/* other buffer is done, proceed with current buffer */
			assert(!blocked[(cur + 1) % MAXBUFFERS]);
			blocked[cur] = true;
			task->cur = cur;
			task->ateof = ateof[cur];
			task->cnt = bufcnt[cur];
			more = !ateof[cur] || (e && e < end && task->top[cur] == task->limit);
/*			TRC_DEBUG(MAL_SERVER, "SQL producer got buffer '%d' filled with '%d' records\n", cur, task->top[cur]);*/

			MT_sema_up(&task->consumer);

			cur = (cur + 1) % MAXBUFFERS;
/*			TRC_DEBUG(MAL_SERVER, "May continue with buffer: %d\n", cur);*/

			if (cnt == task->maxrow) {
				MT_sema_down(&task->producer);
/*				TRC_DEBUG(MAL_SERVER, "Producer delivered all\n");*/
				GDKfree(rdfa);
				return;
			}
		}
/*		TRC_DEBUG(MAL_SERVER, "Continue producer buffer: %d\n", cur);*/

		/* we ran out of input? */
		if (task->ateof && !more) {
/*			TRC_DEBUG(MAL_SERVER, "Producer encountered eof\n");*/
			GDKfree(rdfa);
			return;
		}
		/* consumers ask us to stop? */
		if (task->state == ENDOFCOPY) {
			GDKfree(rdfa);
			return;
		}
		bufcnt[cur] = cnt;
		/* move the non-parsed correct row data to the head of the next buffer */
		end = s = task->input[cur];
	}
	if (unlikely(cnt < task->maxrow && task->maxrow != BUN_NONE)) {
		char msg[256];
		snprintf(msg, sizeof(msg), "incomplete record at end of file:%s\n", s);
		task->as->error = GDKstrdup(msg);
		tablet_error(task, rowno, startlineno, int_nil, "incomplete record at end of file", s);
		task->b->pos += partial;
	}
	GDKfree(rdfa);
	return;

  badutf8:
	tablet_error(task, rowno, startlineno, int_nil, "input not properly encoded UTF-8", "");
	ateof[cur] = true;
	goto reportlackofinput;
}

static void
create_rejects_table(Client cntxt)
{
	MT_lock_set(&mal_contextLock);
	if (cntxt->error_row == NULL) {
		cntxt->error_row = COLnew(0, TYPE_lng, 0, TRANSIENT);
		cntxt->error_fld = COLnew(0, TYPE_int, 0, TRANSIENT);
		cntxt->error_msg = COLnew(0, TYPE_str, 0, TRANSIENT);
		cntxt->error_input = COLnew(0, TYPE_str, 0, TRANSIENT);
		if (cntxt->error_row == NULL || cntxt->error_fld == NULL || cntxt->error_msg == NULL || cntxt->error_input == NULL) {
			if (cntxt->error_row)
				BBPunfix(cntxt->error_row->batCacheid);
			if (cntxt->error_fld)
				BBPunfix(cntxt->error_fld->batCacheid);
			if (cntxt->error_msg)
				BBPunfix(cntxt->error_msg->batCacheid);
			if (cntxt->error_input)
				BBPunfix(cntxt->error_input->batCacheid);
			cntxt->error_row = cntxt->error_fld = cntxt->error_msg = cntxt->error_input = NULL;
		}
	}
	MT_lock_unset(&mal_contextLock);
}

static BUN
SQLload_file(Client cntxt, Tablet *as, bstream *b, stream *out, const char *csep, const char *rsep, char quote, lng skip, lng maxrow, int best, bool from_stdin, const char *tabnam, bool escape, struct directappend *directappend)
{
	BUN cnt = 0, cntstart = 0, leftover = 0;
	int res = 0;		/* < 0: error, > 0: success, == 0: continue processing */
	int j;
	BAT *countbat;
	// BUN firstcol;
	BUN i, attr;
	READERtask task;
	READERtask ptask[MAXWORKERS];
	int threads = (maxrow< 0 || maxrow > (1 << 16)) && GDKnr_threads > 1 ? (GDKnr_threads < MAXWORKERS ? GDKnr_threads - 1 : MAXWORKERS - 1) : 1;
	lng tio, t1 = 0;
	char name[MT_NAME_LEN];

	// threads = 1;

/*	TRC_DEBUG(MAL_SERVER, "Prepare copy work for '%d' threads col '%s' rec '%s' quot '%c'\n", threads, csep, rsep, quote);*/

	memset(ptask, 0, sizeof(ptask));
	task = (READERtask) {
		.cntxt = cntxt,
		.from_stdin = from_stdin,
		.as = as,
		.escape = escape,		/* TODO: implement feature!!! */
		.directappend = directappend,
	};

	/* create the reject tables */
	create_rejects_table(task.cntxt);
	if (task.cntxt->error_row == NULL || task.cntxt->error_fld == NULL || task.cntxt->error_msg == NULL || task.cntxt->error_input == NULL) {
		tablet_error(&task, lng_nil, lng_nil, int_nil, "SQLload initialization failed", "");
		goto bailout;
	}

	assert(rsep);
	assert(csep);
	assert(maxrow < 0 || maxrow <= (lng) BUN_MAX);
	task.fields = (char ***) GDKmalloc(as->nr_attrs * sizeof(char **));
	task.cols = (int *) GDKzalloc(as->nr_attrs * sizeof(int));
	task.time = (lng *) GDKzalloc(as->nr_attrs * sizeof(lng));
	if (task.fields == NULL || task.cols == NULL || task.time == NULL) {
		tablet_error(&task, lng_nil, lng_nil, int_nil, "memory allocation failed", "SQLload_file");
		goto bailout;
	}
	task.cur = 0;
	for (i = 0; i < MAXBUFFERS; i++) {
		task.base[i] = GDKmalloc(MAXROWSIZE(2 * b->size) + 2);
		task.rowlimit[i] = MAXROWSIZE(2 * b->size);
		if (task.base[i] == 0) {
			tablet_error(&task, lng_nil, lng_nil, int_nil, SQLSTATE(HY013) MAL_MALLOC_FAIL, "SQLload_file");
			goto bailout;
		}
		task.base[i][0] = task.base[i][b->size + 1] = 0;
		task.input[i] = task.base[i] + 1;	/* wrap the buffer with null bytes */
	}
	task.besteffort = best;

	if (maxrow < 0)
		task.maxrow = BUN_MAX;
	else
		task.maxrow = (BUN) maxrow;

	if (task.fields == 0 || task.cols == 0 || task.time == 0) {
		tablet_error(&task, lng_nil, lng_nil, int_nil, SQLSTATE(HY013) MAL_MALLOC_FAIL, "SQLload_file");
		goto bailout;
	}

	task.skip = skip;
	task.quote = quote;
	task.csep = csep;
	task.seplen = strlen(csep);
	task.rsep = rsep;
	task.rseplen = strlen(rsep);
	task.errbuf = cntxt->errbuf;

	MT_sema_init(&task.producer, 0, "task.producer");
	MT_sema_init(&task.consumer, 0, "task.consumer");
	task.ateof = false;
	task.b = b;
	task.out = out;

#ifdef MLOCK_TST
	mlock(task.fields, as->nr_attrs * sizeof(char *));
	mlock(task.cols, as->nr_attrs * sizeof(int));
	mlock(task.time, as->nr_attrs * sizeof(lng));
	for (i = 0; i < MAXBUFFERS; i++)
		mlock(task.base[i], b->size + 2);
#endif
	as->error = NULL;

	/* there is no point in creating more threads than we have columns */
	if (as->nr_attrs < (BUN) threads)
		threads = (int) as->nr_attrs;

	/* allocate enough space for pointers into the buffer pool.  */
	/* the record separator is considered a column */
	task.limit = (int) (b->size / as->nr_attrs + as->nr_attrs);
	for (i = 0; i < as->nr_attrs; i++) {
		task.fields[i] = GDKmalloc(sizeof(char *) * task.limit);
		if (task.fields[i] == 0) {
			if (task.as->error == NULL)
				as->error = createException(MAL, "sql.copy_from", SQLSTATE(HY013) MAL_MALLOC_FAIL);
			goto bailout;
		}
#ifdef MLOCK_TST
		mlock(task.fields[i], sizeof(char *) * task.limit);
#endif
		task.cols[i] = (int) (i + 1);	/* to distinguish non initialized later with zero */
	}
	for (i = 0; i < MAXBUFFERS; i++) {
		task.rows[i] = GDKzalloc(sizeof(char *) * task.limit);
		task.startlineno[i] = GDKzalloc(sizeof(lng) * task.limit);
		if (task.rows[i] == NULL || task.startlineno[i] == NULL) {
			GDKfree(task.rows[i]);
			GDKfree(task.startlineno[i]);
			tablet_error(&task, lng_nil, lng_nil, int_nil, SQLSTATE(HY013) MAL_MALLOC_FAIL, "SQLload_file:failed to alloc buffers");
			goto bailout;
		}
	}
	task.rowerror = (bte *) GDKzalloc(sizeof(bte) * task.limit);
	if( task.rowerror == NULL){
		tablet_error(&task, lng_nil, lng_nil, int_nil, SQLSTATE(HY013) MAL_MALLOC_FAIL, "SQLload_file:failed to alloc rowerror buffer");
		goto bailout;
	}

	task.id = 0;
	snprintf(name, sizeof(name), "prod-%s", tabnam);
	if ((task.tid = THRcreate(SQLproducer, (void *) &task, MT_THR_JOINABLE, name)) == 0) {
		tablet_error(&task, lng_nil, lng_nil, int_nil, SQLSTATE(42000) "failed to start producer thread", "SQLload_file");
		goto bailout;
	}
/*	TRC_DEBUG(MAL_SERVER, "Parallel bulk load " LLFMT " - " BUNFMT "\n", skip, task.maxrow);*/

	task.workers = threads;
	for (j = 0; j < threads; j++) {
		ptask[j] = task;
		ptask[j].id = j;
		ptask[j].cols = (int *) GDKzalloc(as->nr_attrs * sizeof(int));
		if (ptask[j].cols == 0) {
			tablet_error(&task, lng_nil, lng_nil, int_nil, SQLSTATE(HY013) MAL_MALLOC_FAIL, "SQLload_file");
			task.id = -1;
			MT_sema_up(&task.producer);
			goto bailout;
		}
#ifdef MLOCK_TST
		mlock(ptask[j].cols, sizeof(char *) * task.limit);
#endif
		snprintf(name, sizeof(name), "ptask%d.sema", j);
		MT_sema_init(&ptask[j].sema, 0, name);
		snprintf(name, sizeof(name), "ptask%d.repl", j);
		MT_sema_init(&ptask[j].reply, 0, name);
		snprintf(name, sizeof(name), "wrkr%d-%s", j, tabnam);
		if ((ptask[j].tid = THRcreate(SQLworker, (void *) &ptask[j], MT_THR_JOINABLE, name)) == 0) {
			tablet_error(&task, lng_nil, lng_nil, int_nil, SQLSTATE(42000) "failed to start worker thread", "SQLload_file");
			threads = j;
			for (j = 0; j < threads; j++)
				ptask[j].workers = threads;
		}
		initialize_scratch_buffer(&ptask[j].scratch);
		initialize_scratch_buffer(&ptask[j].primary);
		initialize_scratch_buffer(&ptask[j].secondary);
	}
	if (threads == 0) {
		/* no threads started */
		task.id = -1;
		MT_sema_up(&task.producer);
		goto bailout;
	}
	MT_sema_up(&task.producer);

	tio = GDKusec();
	tio = GDKusec() - tio;
	t1 = GDKusec();
#ifdef MLOCK_TST
	mlock(task.b->buf, task.b->size);
#endif
	countbat = NULL;
	if (directappend) {
		countbat = directappend_get_offsets_bat(directappend);
	} else {
		for (BUN i = 0; i < task.as->nr_attrs; i++) {
			if (task.as->format[i].c != NULL) {
				countbat = task.as->format[i].c;
				break;
			}
		}
	}
	assert(countbat != NULL);

	while (res == 0 && cnt < task.maxrow) {

		// track how many elements are in the aggregated BATs
		cntstart = BATcount(countbat);
		/* block until the producer has data available */
		MT_sema_down(&task.consumer);
		cnt += task.top[task.cur];
		if (task.ateof && !task.top[task.cur])
			break;
		t1 = GDKusec() - t1;
/*		TRC_DEBUG(MAL_SERVER, "Break: %d rows\n", task.top[task.cur]);*/

		t1 = GDKusec();
		if (task.top[task.cur]) {
			/* activate the workers to break rows */
			for (j = 0; j < threads; j++) {
				/* stage one, break the rows in parallel */
				ptask[j].error = 0;
				ptask[j].state = BREAKROW;
				ptask[j].next = task.top[task.cur];
				ptask[j].fields = task.fields;
				ptask[j].limit = task.limit;
				ptask[j].cnt = task.cnt;
				ptask[j].cur = task.cur;
				ptask[j].top[task.cur] = task.top[task.cur];
				MT_sema_up(&ptask[j].sema);
			}
		}

		if (task.top[task.cur] && directappend) {
			/* claim rows in the table while waiting for the workers to finish,  */
			str msg = directappend_claim(directappend, task.top[task.cur], 0, NULL);
			if (msg != MAL_SUCCEED) {
				tablet_error(&task, BATcount(countbat), lng_nil, lng_nil, msg, "SQLload_file");
				res = -1;
				for (j = 0; j < threads; j++)
					MT_sema_down(&ptask[j].reply);
				break;
			}
		}

		if (task.top[task.cur]) {
			/* await completion of row break phase */
			for (j = 0; j < threads; j++) {
				MT_sema_down(&ptask[j].reply);
				if (ptask[j].error) {
					res = -1;
/*					TRC_ERROR(MAL_SERVER, "Error in task: %d %d\n", j, ptask[j].error);*/
				}
			}
		}

/*		TRC_DEBUG(MAL_SERVER,
			"Fill the BATs '%d' " BUNFMT " cap " BUNFMT "\n",
			task.top[task.cur], task.cnt, BATcapacity(as->format[task.cur].c));*/

		if (task.top[task.cur]) {
			if (res == 0) {
				SQLworkdivider(&task, ptask, (int) as->nr_attrs, threads);

				/* activate the workers to update the BATs */
				for (j = 0; j < threads; j++) {
					/* stage two, update the BATs */
					ptask[j].state = UPDATEBAT;
					MT_sema_up(&ptask[j].sema);
				}
			}
		}
		tio = GDKusec();
		tio = t1 - tio;

		/* await completion of the BAT updates */
		if (res == 0 && task.top[task.cur]) {
			for (j = 0; j < threads; j++) {
				MT_sema_down(&ptask[j].reply);
				if (ptask[j].errorcnt > 0 && !ptask[j].besteffort) {
					res = -1;
					best = 0;
				}
			}
		}

		/* trim the BATs discarding error tuples */
#define trimerrors(TYPE)												\
		do {															\
			TYPE *src, *dst;											\
			leftover= BATcount(task.as->format[attr].c);				\
			limit = leftover - cntstart;								\
			dst =src= (TYPE *) BUNtloc(task.as->format[attr].ci,cntstart); \
			for(j = 0; j < (int) limit; j++, src++){					\
				if ( task.rowerror[j]){									\
					leftover--;											\
					continue;											\
				}														\
				*dst++ = *src;											\
			}															\
			BATsetcount(task.as->format[attr].c, leftover );			\
		} while (0)

/*		TRC_DEBUG(MAL_SERVER, "Trim bbest '%d' table size " BUNFMT " - rows found so far " BUNFMT "\n",
					 best, BATcount(as->format[firstcol].c), task.cnt); */

		if (best && BATcount(countbat)) {
			BUN limit;
			int width;

			for (attr = 0; attr < as->nr_attrs; attr++) {
				if (as->format[attr].skip)
					continue;
				width = as->format[attr].c->twidth;
				as->format[attr].ci = bat_iterator_nolock(as->format[attr].c);
				switch (width){
				case 1:
					trimerrors(bte);
					break;
				case 2:
					trimerrors(sht);
					break;
				case 4:
					trimerrors(int);
					break;
				case 8:
					trimerrors(lng);
					break;
#ifdef HAVE_HGE
				case 16:
					trimerrors(hge);
					break;
#endif
				default:
					{
						char *src, *dst;
						leftover= BATcount(task.as->format[attr].c);
						limit = leftover - cntstart;
						dst = src= BUNtloc(task.as->format[attr].ci,cntstart);
						for(j = 0; j < (int) limit; j++, src += width){
							if ( task.rowerror[j]){
								leftover--;
								continue;
							}
							if (dst != src)
								memcpy(dst, src, width);
							dst += width;
						}
						BATsetcount(task.as->format[attr].c, leftover );
					}
					break;
				}
			}
			// re-initialize the error vector;
			memset(task.rowerror, 0, task.limit);
			task.errorcnt = 0;
		}

		if (res < 0) {
			/* producer should stop */
			task.maxrow = cnt;
			task.state = ENDOFCOPY;
		}
		if (task.ateof && task.top[task.cur] < task.limit && cnt != task.maxrow)
			break;
		task.top[task.cur] = 0;
		MT_sema_up(&task.producer);
	}

/*	TRC_DEBUG(MAL_SERVER, "End of block stream eof=%d - res=%d\n", task.ateof, res);*/

	cnt = BATcount(countbat);

	task.ateof = true;
	task.state = ENDOFCOPY;
/*	TRC_DEBUG(MAL_SERVER, "Activate sync on disk\n");*/

	// activate the workers to sync the BATs to disk
	if (res == 0) {
		for (j = 0; j < threads; j++) {
			// stage three, update the BATs
			ptask[j].state = SYNCBAT;
			MT_sema_up(&ptask[j].sema);
		}
	}

	if (!task.ateof || cnt < task.maxrow) {
/*		TRC_DEBUG(MAL_SERVER, "Shut down reader\n");*/
		MT_sema_up(&task.producer);
	}
	MT_join_thread(task.tid);
	if (res == 0) {
		// await completion of the BAT syncs
		for (j = 0; j < threads; j++)
			MT_sema_down(&ptask[j].reply);
	}

/*	TRC_DEBUG(MAL_SERVER, "Activate endofcopy\n");*/

	for (j = 0; j < threads; j++) {
		ptask[j].state = ENDOFCOPY;
		MT_sema_up(&ptask[j].sema);
	}
	/* wait for their death */
	for (j = 0; j < threads; j++)
		MT_sema_down(&ptask[j].reply);

/*	TRC_DEBUG(MAL_SERVER, "Kill the workers\n");*/

	for (j = 0; j < threads; j++) {
		MT_join_thread(ptask[j].tid);
		GDKfree(ptask[j].cols);
		MT_sema_destroy(&ptask[j].sema);
		MT_sema_destroy(&ptask[j].reply);
	}

/*	TRC_DEBUG(MAL_SERVER, "Found " BUNFMT " tuples\n", cnt);*/
/*	TRC_DEBUG(MAL_SERVER, "Leftover input: %.63s\n", task.b->buf + task.b->pos);*/

	for (i = 0; i < as->nr_attrs; i++) {
		BAT *b = task.as->format[i].c;
		if (b)
			BATsettrivprop(b);
		GDKfree(task.fields[i]);
	}
	GDKfree(task.fields);
	GDKfree(task.cols);
	GDKfree(task.time);
	for (i = 0; i < MAXBUFFERS; i++) {
		GDKfree(task.base[i]);
		GDKfree(task.rows[i]);
		GDKfree(task.startlineno[i]);
	}
	if (task.rowerror)
		GDKfree(task.rowerror);
	MT_sema_destroy(&task.producer);
	MT_sema_destroy(&task.consumer);
	for (int t = 0; t < threads; t++) {
		destroy_scratch_buffer(&ptask[t].scratch);
		destroy_scratch_buffer(&ptask[t].primary);
		destroy_scratch_buffer(&ptask[t].secondary);
	}
#ifdef MLOCK_TST
	munlockall();
#endif

	return res < 0 ? BUN_NONE : cnt;

  bailout:
	if (task.fields) {
		for (i = 0; i < as->nr_attrs; i++) {
			if (task.fields[i])
				GDKfree(task.fields[i]);
		}
		GDKfree(task.fields);
	}
	GDKfree(task.time);
	GDKfree(task.cols);
	GDKfree(task.base[task.cur]);
	GDKfree(task.rowerror);
	for (i = 0; i < MAXWORKERS; i++)
		GDKfree(ptask[i].cols);
	for (int t = 0; t < threads; t++) {
		destroy_scratch_buffer(&ptask[t].scratch);
		destroy_scratch_buffer(&ptask[t].primary);
		destroy_scratch_buffer(&ptask[t].secondary);
	}
#ifdef MLOCK_TST
	munlockall();
#endif
	return BUN_NONE;
}

/* return the latest reject table, to be on the safe side we should
 * actually create copies within a critical section. Ignored for now. */
str
COPYrejects(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	bat *row = getArgReference_bat(stk, pci, 0);
	bat *fld = getArgReference_bat(stk, pci, 1);
	bat *msg = getArgReference_bat(stk, pci, 2);
	bat *inp = getArgReference_bat(stk, pci, 3);

	create_rejects_table(cntxt);
	if (cntxt->error_row == NULL)
		throw(MAL, "sql.rejects", "No reject table available");
	BBPretain(*row = cntxt->error_row->batCacheid);
	BBPretain(*fld = cntxt->error_fld->batCacheid);
	BBPretain(*msg = cntxt->error_msg->batCacheid);
	BBPretain(*inp = cntxt->error_input->batCacheid);
	(void) mb;
	return MAL_SUCCEED;
}

str
COPYrejects_clear(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	if (cntxt->error_row) {
		MT_lock_set(&errorlock);
		BATclear(cntxt->error_row, true);
		if(cntxt->error_fld) BATclear(cntxt->error_fld, true);
		if(cntxt->error_msg) BATclear(cntxt->error_msg, true);
		if(cntxt->error_input) BATclear(cntxt->error_input, true);
		MT_lock_unset(&errorlock);
	}
	(void) mb;
	(void) stk;
	(void) pci;
	return MAL_SUCCEED;
}


static void *
generic_dec_frstr(Column *c, int type, void **dst, size_t *dst_len, const char *s)
{
	sql_subtype *t = &c->column->type;
	/* support dec map to bte, sht, int and lng */
	if( strcmp(s,"nil")== 0)
		return NULL;
	if (type == TYPE_bte) {
		return dec_bte_frstr(*dst, *dst_len, s, t->digits, t->scale);
	} else if (type == TYPE_sht) {
		return dec_sht_frstr(*dst, *dst_len, s, t->digits, t->scale);
	} else if (type == TYPE_int) {
		return dec_int_frstr(*dst, *dst_len, s, t->digits, t->scale);
	} else if (type == TYPE_lng) {
		return dec_lng_frstr(*dst, *dst_len, s, t->digits, t->scale);
#ifdef HAVE_HGE
	} else if (type == TYPE_hge) {
		return dec_hge_frstr(*dst, *dst_len, s, t->digits, t->scale);
#endif
	}
	return NULL;
}

static void *
sec_frstr(Column *c, int type, void **dst, size_t *dst_len, const char *s)
{
	/* read a sec_interval value
	 * this knows that the stored scale is always 3 */
	unsigned int i, neg = 0;
	lng *r;
	lng res = 0;

	assert(*dst_len >= sizeof(lng));(void)dst_len;

	(void) c;
	(void) type;
	assert(type == TYPE_lng);

	if (*s == '-') {
		neg = 1;
		s++;
	} else if (*s == '+') {
		neg = 0;
		s++;
	}
	for (i = 0; i < (19 - 3) && *s && *s != '.'; i++, s++) {
		if (!isdigit((unsigned char) *s))
			return NULL;
		res *= 10;
		res += (*s - '0');
	}
	i = 0;
	if (*s) {
		if (*s != '.')
			return NULL;
		s++;
		for (; *s && i < 3; i++, s++) {
			if (!isdigit((unsigned char) *s))
				return NULL;
			res *= 10;
			res += (*s - '0');
		}
	}
	if (*s)
		return NULL;
	for (; i < 3; i++) {
		res *= 10;
	}
	r = *dst;
	if (neg)
		*r = -res;
	else
		*r = res;
	return (void *) r;
}

static int
has_whitespace(const char *s)
{
	if (*s == ' ' || *s == '\t')
		return 1;
	while (*s)
		s++;
	s--;
	if (*s == ' ' || *s == '\t')
		return 1;
	return 0;
}

/* Literal parsing for SQL all pass through this routine */
static void *
_ASCIIadt_frStr(Column *c, int type, void **dst, size_t *dst_len, const char *s)
{
	ssize_t len;

	len = (*BATatoms[type].atomFromStr) (s, dst_len, dst, false);
	if (len < 0)
		return NULL;
	switch (type) {
	case TYPE_bte:
	case TYPE_int:
	case TYPE_lng:
	case TYPE_sht:
#ifdef HAVE_HGE
	case TYPE_hge:
#endif
		if (len == 0 || s[len]) {
			/* decimals can be converted to integers when *.000 */
			if (s[len++] == '.') {
				while (s[len] == '0')
					len++;
				if (s[len] == 0)
					return *dst;
			}
			return NULL;
		}
		break;
	case TYPE_str: {
		sql_subtype *type = &c->column->type;
		int slen;

		s = *dst;
		slen = strNil(s) ? int_nil : UTF8_strlen(s);
		if (type->digits > 0 && len > 0 && slen > (int) type->digits) {
			len = strPrintWidth(*dst);
			if (len > (ssize_t) type->digits)
				return NULL;
		}
		break;
	}
	default:
		break;
	}
	return *dst;
}

str
mvc_import_table(Client cntxt, BAT ***bats, mvc *m, bstream *bs, sql_table *t, const char *sep, const char *rsep, const char *ssep, const char *ns, lng sz, lng offset, int best, bool from_stdin, bool escape, bool append_directly)
{
	int i = 0, j;
	node *n;
	Tablet as;
	Column *fmt;
	str msg = MAL_SUCCEED;


	struct directappend directappend_state = { 0 };
	struct directappend *directappend = NULL;
	if (append_directly) {
		msg = directappend_init(&directappend_state, cntxt, t);
		if (msg != MAL_SUCCEED)
			return msg;
		directappend = &directappend_state;
	}

	*bats =0;	// initialize the receiver

	if (!bs)
		throw(IO, "sql.copy_from", SQLSTATE(42000) "No stream (pointer) provided");
	if (mnstr_errnr(bs->s)) {
		mnstr_error_kind errnr = mnstr_errnr(bs->s);
		char *stream_msg = mnstr_error(bs->s);
		msg = createException(IO, "sql.copy_from", SQLSTATE(42000) "Stream not open %s: %s", mnstr_error_kind_name(errnr), stream_msg ? stream_msg : "unknown error");
		free(stream_msg);
		directappend_destroy(directappend);
		return msg;
	}
	if (offset < 0 || offset > (lng) BUN_MAX) {
		directappend_destroy(directappend);
		throw(IO, "sql.copy_from", SQLSTATE(42000) "Offset out of range");
	}

	if (offset > 0)
		offset--;
	if (ol_first_node(t->columns)) {
		stream *out = m->scanner.ws;

		as = (Tablet) {
			.nr_attrs = ol_length(t->columns),
			.nr = (sz < 1) ? BUN_NONE : (BUN) sz,
			.offset = (BUN) offset,
			.error = NULL,
			.tryall = 0,
			.complaints = NULL,
			.filename = m->scanner.rs == bs ? NULL : "",
		};
		fmt = GDKzalloc(sizeof(Column) * (as.nr_attrs + 1));
		if (fmt == NULL) {
			directappend_destroy(&directappend_state);
			throw(IO, "sql.copy_from", SQLSTATE(HY013) MAL_MALLOC_FAIL);
		}
		as.format = fmt;
		if (!isa_block_stream(bs->s))
			out = NULL;

		// temporary
		node *n2;
		n2 = append_directly ? ol_first_node(directappend_state.t->columns) : NULL;
		for (n = ol_first_node(t->columns), i = 0; n; n = n->next, i++) {
			sql_column *col = n->data;
			// temporary
			if (n2 != NULL) {
				sql_column *col2 = n2->data;
				assert(strcmp(col->base.name, col2->base.name) == 0);
				assert(strcmp(col->type.type->base.name, col2->type.type->base.name) == 0);
				assert( (n->next == NULL) == (n2->next == NULL)  );
				fmt[i].appendcol = col2;
				n2 = n2->next;
			}

			fmt[i].name = col->base.name;
			fmt[i].sep = (n->next) ? sep : rsep;
			fmt[i].rsep = rsep;
			fmt[i].seplen = _strlen(fmt[i].sep);
			fmt[i].type = sql_subtype_string(m->ta, &col->type);
			fmt[i].adt = ATOMindex(col->type.type->impl);
			fmt[i].frstr = &_ASCIIadt_frStr;
			fmt[i].column = col;
			fmt[i].len = ATOMlen(fmt[i].adt, ATOMnilptr(fmt[i].adt));
			fmt[i].data = GDKzalloc(fmt[i].len);
			if(fmt[i].data == NULL || fmt[i].type == NULL) {
				for (j = 0; j < i; j++) {
					GDKfree(fmt[j].data);
					BBPunfix(fmt[j].c->batCacheid);
				}
				GDKfree(fmt[i].data);
				directappend_destroy(&directappend_state);
				throw(IO, "sql.copy_from", SQLSTATE(HY013) MAL_MALLOC_FAIL);
			}
			fmt[i].c = NULL;
			fmt[i].ws = !has_whitespace(fmt[i].sep);
			fmt[i].quote = ssep ? ssep[0] : 0;
			fmt[i].nullstr = ns;
			fmt[i].null_length = strlen(ns);
			fmt[i].nildata = ATOMnilptr(fmt[i].adt);
			fmt[i].nil_len = ATOMlen(fmt[i].adt, fmt[i].nildata);
			fmt[i].skip = (col->base.name[0] == '%');
			if (col->type.type->eclass == EC_DEC) {
				fmt[i].frstr = &generic_dec_frstr;
			} else if (col->type.type->eclass == EC_SEC) {
				fmt[i].frstr = &sec_frstr;
			}
			fmt[i].size = ATOMsize(fmt[i].adt);
			fmt[i].maxwidth = col->type.digits;
		}

		// do .. while (false) allows us to use 'break' to drop out at any point
		do {
			if (!directappend) {
				msg = TABLETcreate_bats(&as, (BUN) (sz < 0 ? 1000 : sz));
				if (msg != MAL_SUCCEED)
					break;
			}

			if (sz != 0) {
				BUN count = SQLload_file(cntxt, &as, bs, out, sep, rsep, ssep ? ssep[0] : 0, offset, sz, best, from_stdin, t->base.name, escape, directappend);
				if (count == BUN_NONE)
					break;
				if (as.error && !best)
					break;
			}

			size_t nreturns = directappend ? 1 : as.nr_attrs;
			*bats = (BAT**) GDKzalloc(sizeof(BAT *) * nreturns);
			if ( *bats == NULL) {
				TABLETdestroy_format(&as);
				directappend_destroy(&directappend_state);
				throw(IO, "sql.copy_from", SQLSTATE(HY013) MAL_MALLOC_FAIL);
			}
			if (directappend) {
				// Return a single result, the all_offsets BAT.
				BAT *oids_bat = directappend_state.all_offsets;
				oids_bat->tnil = false;
				oids_bat->tnonil = true;
				oids_bat->tsorted = true;
				oids_bat->trevsorted = false;
				oids_bat->tkey = true;
				oids_bat->tseqbase = oid_nil;
				directappend_state.all_offsets = NULL; // otherwise we'll try to reclaim it later
				BBPfix(oids_bat->batCacheid);
				(*bats)[0] = oids_bat;
			} else {
				msg = TABLETcollect(*bats,&as);
			}

		} while (false);

		if (as.error) {
			if( !best) msg = createException(SQL, "sql.copy_from", SQLSTATE(42000) "Failed to import table '%s', %s", t->base.name, getExceptionMessage(as.error));
			freeException(as.error);
			as.error = NULL;
		}
		for (n = ol_first_node(t->columns), i = 0; n; n = n->next, i++) {
			fmt[i].sep = NULL;
			fmt[i].rsep = NULL;
			fmt[i].nullstr = NULL;
		}
		TABLETdestroy_format(&as);
	}

	directappend_destroy(&directappend_state);
	return msg;
}
