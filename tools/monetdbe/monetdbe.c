/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0.  If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright 1997 - July 2008 CWI, August 2008 - 2020 MonetDB B.V.
 */

#include "monetdb_config.h"

#include "monetdbe.h"
#include "gdk.h"
#include "mal.h"
#include "mal_client.h"
#include "mal_embedded.h"
#include "mal_backend.h"
#include "mal_builder.h"
#include "opt_prelude.h"
#include "mtime.h"
#include "blob.h"
#include "sql_mvc.h"
#include "sql_catalog.h"
#include "sql_gencode.h"
#include "sql_semantic.h"
#include "sql_scenario.h"
#include "sql_optimizer.h"
#include "rel_exp.h"
#include "rel_rel.h"
#include "rel_updates.h"
#include "monet_options.h"
#include "mapi.h"
#include "monetdbe_mapi.h"
#include "remote.h"
#include "sql.h"
#include "sql_result.h"

#define UNUSED(x) (void)(x)

static int
monetdbe_type(monetdbe_types t) {
	switch(t) {
	case monetdbe_bool: return TYPE_bit;
	case monetdbe_int8_t: return TYPE_bte;
	case monetdbe_int16_t: return TYPE_sht;
	case monetdbe_int32_t: return TYPE_int;
	case monetdbe_int64_t: return TYPE_lng;
#ifdef HAVE_HGE
	case monetdbe_int128_t: return TYPE_hge;
#endif
	case monetdbe_size_t: return TYPE_oid;
	case monetdbe_float: return TYPE_flt;
	case monetdbe_double: return TYPE_dbl;
	case monetdbe_str: return TYPE_str;
	case monetdbe_blob: return TYPE_blob;
	case monetdbe_date: return TYPE_date;
	case monetdbe_time: return TYPE_daytime;
	case monetdbe_timestamp: return TYPE_timestamp;
	default:
		return -1;
	}
}

static monetdbe_types
embedded_type(int t) {
	switch(t) {
	case TYPE_bit: return monetdbe_bool;
	case TYPE_bte: return monetdbe_int8_t;
	case TYPE_sht: return monetdbe_int16_t;
	case TYPE_int: return monetdbe_int32_t;
	case TYPE_lng: return monetdbe_int64_t;
#ifdef HAVE_HGE
	case TYPE_hge: return monetdbe_int128_t;
#endif
	case TYPE_oid: return monetdbe_size_t;
	case TYPE_flt: return monetdbe_float;
	case TYPE_dbl: return monetdbe_double;
	case TYPE_str: return monetdbe_str;
	case TYPE_date: return monetdbe_date;
	case TYPE_daytime: return monetdbe_time;
	case TYPE_timestamp: return monetdbe_timestamp;
	default:
	  	if (t==TYPE_blob)
			return monetdbe_blob;
		return monetdbe_type_unknown;
	}
}

typedef struct {
	Client c;
	char *msg;
	monetdbe_data_blob blob_null;
	monetdbe_data_date date_null;
	monetdbe_data_time time_null;
	monetdbe_data_timestamp timestamp_null;
	str mid;
} monetdbe_database_internal;

typedef struct {
	monetdbe_result res;
	int type;
	res_table *monetdbe_resultset;
	monetdbe_column **converted_columns;
	monetdbe_database_internal *mdbe;
} monetdbe_result_internal;

typedef struct {
	monetdbe_statement res;
	ValRecord *data;
	ValPtr *args;	/* only used during calls */
	int retc;
	monetdbe_database_internal *mdbe;
	cq *q;
} monetdbe_stmt_internal;

static MT_Lock embedded_lock = MT_LOCK_INITIALIZER("embedded_lock");
static bool monetdbe_embedded_initialized = false;
static char *monetdbe_embedded_url = NULL;
static int open_dbs = 0;

static void data_from_date(date d, monetdbe_data_date *ptr);
static void data_from_time(daytime d, monetdbe_data_time *ptr);
static void data_from_timestamp(timestamp d, monetdbe_data_timestamp *ptr);
static timestamp timestamp_from_data(monetdbe_data_timestamp *ptr);
static date date_from_data(monetdbe_data_date *ptr);
static daytime time_from_data(monetdbe_data_time *ptr);

static char* monetdbe_cleanup_result_internal(monetdbe_database_internal *mdbe, monetdbe_result_internal* res);

static void
clear_error( monetdbe_database_internal *mdbe)
{
	if (mdbe->msg)
		freeException(mdbe->msg);
	mdbe->msg = NULL;
}

static char*
set_error( monetdbe_database_internal *mdbe, char *err)
{
	if (!err)
		return err;
	if (mdbe->msg) /* keep first error */
		freeException(err);
	else
		mdbe->msg = err;
	return mdbe->msg;
}

static char*
commit_action(mvc* m, monetdbe_database_internal *mdbe, monetdbe_result **result, monetdbe_result_internal *res_internal)
{
	/* handle autocommit */
    char *commit_msg = SQLautocommit(m);

	if ((mdbe->msg != MAL_SUCCEED || commit_msg != MAL_SUCCEED)) {
		if (res_internal) {
			char* other = monetdbe_cleanup_result_internal(mdbe, res_internal);
			if (other)
				freeException(other);
		}
		if (result)
			*result = NULL;
		(void)set_error(mdbe, commit_msg);
	}
	return mdbe->msg;
}

static int
validate_database_handle_noerror(monetdbe_database_internal *mdbe)
{
	if (!monetdbe_embedded_initialized || !MCvalid(mdbe->c))
		return 0;
	clear_error(mdbe);
	return 1;
}

// Call this function always inside the embedded_lock
static char*
validate_database_handle(monetdbe_database_internal *mdbe, const char* call)
{
	if (!monetdbe_embedded_initialized)
		return createException(MAL, call, "MonetDBe has not yet started");
	if (!MCvalid(mdbe->c))
		return createException(MAL, call, "Invalid database handle");
	clear_error(mdbe);
	return MAL_SUCCEED;
}

static void
monetdbe_destroy_column(monetdbe_column* column)
{
	size_t j;

	if (!column)
		return;

	if (column->type == monetdbe_str) {
		// FIXME: clean up individual strings
		char** data = (char**)column->data;
		for(j = 0; j < column->count; j++) {
			if (data[j])
				GDKfree(data[j]);
		}
	} else if (column->type == monetdbe_blob) {
		monetdbe_data_blob* data = (monetdbe_data_blob*)column->data;
		for(j = 0; j < column->count; j++) {
			if (data[j].data)
				GDKfree(data[j].data);
		}
	}
	GDKfree(column->data);
	GDKfree(column);
}

static char*
monetdbe_cleanup_result_internal(monetdbe_database_internal *mdbe, monetdbe_result_internal* result)
{
	mvc *m = NULL;

	assert(!result || !result->mdbe || result->mdbe == mdbe);
	if ((mdbe->msg = validate_database_handle(mdbe, "monetdbe.monetdbe_cleanup_result_internal")) != MAL_SUCCEED)
		return mdbe->msg;
	if ((mdbe->msg = getSQLContext(mdbe->c, NULL, &m, NULL)) != MAL_SUCCEED)
		goto cleanup;

	if (result->monetdbe_resultset)
		res_tables_destroy(result->monetdbe_resultset);

	if (result->converted_columns) {
		for (size_t i = 0; i < result->res.ncols; i++)
			monetdbe_destroy_column(result->converted_columns[i]);
		GDKfree(result->converted_columns);
	}
	GDKfree(result);
cleanup:
	return commit_action(m, mdbe, NULL, NULL);
}

static char*
monetdbe_get_results(monetdbe_result** result, monetdbe_database_internal *mdbe) {

	backend *be = NULL;
	if ((mdbe->msg = getBackendContext(mdbe->c, &be)) != NULL)
		return mdbe->msg;

    mvc *m = be->mvc;

	monetdbe_result_internal* res_internal;

    if (!(res_internal = GDKzalloc(sizeof(monetdbe_result_internal)))) {
        mdbe->msg = createException(MAL, "monetdbe.monetdbe_get_results", MAL_MALLOC_FAIL);
        return mdbe->msg;
    }
    // TODO: set type of result outside.
    res_internal->res.last_id = be->last_id;
    res_internal->mdbe = mdbe;
    *result = (monetdbe_result*) res_internal;
    m->reply_size = -2; /* do not clean up result tables */

    if (be->results) {
        res_internal->res.ncols = (size_t) be->results->nr_cols;
        res_internal->monetdbe_resultset = be->results;
        if (be->results->nr_cols > 0)
            res_internal->res.nrows = be->results->nr_rows;
        be->results = NULL;
        res_internal->converted_columns = GDKzalloc(sizeof(monetdbe_column*) * res_internal->res.ncols);
        if (!res_internal->converted_columns) {
            mdbe->msg = createException(MAL, "monetdbe.monetdbe_get_results", MAL_MALLOC_FAIL);
            return mdbe->msg;
        }
    }

	return MAL_SUCCEED;
}

static char*
monetdbe_query_internal(monetdbe_database_internal *mdbe, char* query, monetdbe_result** result, monetdbe_cnt* affected_rows, int *prepare_id, char language)
{
	char *nq = NULL;
	Client c = mdbe->c;
	mvc* m = NULL;
	backend *b;
	size_t query_len, input_query_len, prep_len = 0;
	buffer query_buf;
	stream *query_stream;
	bstream *old_bstream = NULL;
	stream *fdout = c->fdout;

	if (result)
		*result = NULL;

	if ((mdbe->msg = validate_database_handle(mdbe, "monetdbe.monetdbe_query_internal")) != MAL_SUCCEED)
		return mdbe->msg;

	old_bstream = c->fdin;
	if ((mdbe->msg = getSQLContext(c, NULL, &m, NULL)) != MAL_SUCCEED)
		goto cleanup;
	b = (backend *) c->sqlcontext;

	if (!query) {
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_query_internal", "Query missing");
		goto cleanup;
	}
	if (!(query_stream = buffer_rastream(&query_buf, "sqlstatement"))) {
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_query_internal", "Could not setup query stream");
		goto cleanup;
	}
	input_query_len = strlen(query);
	query_len = input_query_len + 3;
	if (prepare_id) {
		prep_len = sizeof("PREPARE ")-1;
		query_len += prep_len;
	}
	if (!(nq = GDKmalloc(query_len))) {
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_query_internal", "Could not setup query stream");
		goto cleanup;
	}
	if (prepare_id)
		strcpy(nq, "PREPARE ");
	strcpy(nq + prep_len, query);
	strcpy(nq + prep_len + input_query_len, "\n;");

	query_buf.pos = 0;
	query_buf.len = query_len;
	query_buf.buf = nq;

	if (!(c->fdin = bstream_create(query_stream, query_len))) {
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_query_internal", "Could not setup query stream");
		goto cleanup;
	}
	if (bstream_next(c->fdin) < 0) {
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_query_internal", "Internal error while starting the query");
		goto cleanup;
	}

	assert(language);
	b->language = language;
	b->output_format = OFMT_NONE;
	b->no_mitosis = 0;
	m->user_id = m->role_id = USER_MONETDB;
	m->errstr[0] = '\0';
	m->params = NULL;
	m->sym = NULL;
	m->label = 0;
	if (m->sa)
		m->sa = sa_reset(m->sa);
	m->scanner.mode = LINE_N;
	m->scanner.rs = c->fdin;
	scanner_query_processed(&(m->scanner));

	if ((mdbe->msg = MSinitClientPrg(c, "user", "main")) != MAL_SUCCEED)
		goto cleanup;
	if (prepare_id)
		m->emode = m_prepare;
	if ((mdbe->msg = SQLparser(c)) != MAL_SUCCEED)
		goto cleanup;
	if (m->emode == m_prepare)
		*prepare_id = b->q->id;
	c->fdout = NULL;
	if ((mdbe->msg = SQLengine(c)) != MAL_SUCCEED)
		goto cleanup;
	if (!b->results && b->rowcnt >= 0 && affected_rows)
		*affected_rows = b->rowcnt;

	if (result) {
		if ((mdbe->msg = monetdbe_get_results(result, mdbe)) != MAL_SUCCEED) {
			goto cleanup;
		}

		if (m->emode & m_prepare)
			((monetdbe_result_internal*) *result)->type = Q_PREPARE;
		else
			((monetdbe_result_internal*) *result)->type = (b->results) ? b->results->query_type : m->type;
	}

cleanup:
	c->fdout = fdout;
	if (nq)
		GDKfree(nq);
	MSresetInstructions(c->curprg->def, 1);
	if (old_bstream) { //c->fdin was set
		bstream_destroy(c->fdin);
		c->fdin = old_bstream;
	}

	char* msg = commit_action(m, mdbe, result, result?(monetdbe_result_internal*) result:NULL);

	return msg;
}

static int
monetdbe_close_internal(monetdbe_database_internal *mdbe)
{
	if (!mdbe)
		return 0;

	if (validate_database_handle_noerror(mdbe)) {
		open_dbs--;
		SQLexitClient(mdbe->c);
		MCcloseClient(mdbe->c);
	}
	GDKfree(mdbe);
	return 0;
}

static int
monetdbe_open_internal(monetdbe_database_internal *mdbe)
{
	mvc *m;

	if (!mdbe)
		return -1;
	if (!monetdbe_embedded_initialized) {
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_open_internal", "Embedded MonetDB is not started");
		goto cleanup;
	}
	mdbe->c = MCinitClient((oid) 0, 0, 0);
	if (!MCvalid(mdbe->c)) {
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_open_internal", "Failed to initialize client");
		goto cleanup;
	}
	mdbe->c->curmodule = mdbe->c->usermodule = userModule();
	if (mdbe->c->usermodule == NULL) {
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_open_internal", "Failed to initialize client MAL module");
		goto cleanup;
	}
	if ((mdbe->msg = SQLinitClient(mdbe->c)) != MAL_SUCCEED ||
	    (mdbe->msg = getSQLContext(mdbe->c, NULL, &m, NULL)) != MAL_SUCCEED)
		goto cleanup;
	m->session->auto_commit = 1;
	if (!m->pa)
		m->pa = sa_create(NULL);
	if (!m->sa)
		m->sa = sa_create(m->pa);
	if (!m->ta)
		m->ta = sa_create(m->pa);
	if (!m->pa || !m->sa || !m->ta) {
		mdbe->msg = createException(SQL, "monetdbe.monetdbe_open_internal", MAL_MALLOC_FAIL);
		goto cleanup;
	}
cleanup:
	if (mdbe->msg)
		return -2;
	mdbe->blob_null.data = NULL;
	data_from_date(date_nil, &mdbe->date_null);
	data_from_time(daytime_nil, &mdbe->time_null);
	data_from_timestamp(timestamp_nil, &mdbe->timestamp_null);
	open_dbs++;
	return 0;
}

static void
monetdbe_shutdown_internal(void) // Call this function always inside the embedded_lock
{
	if (monetdbe_embedded_initialized && (open_dbs == 0)) {
            malEmbeddedReset();
		monetdbe_embedded_initialized = false;
		if (monetdbe_embedded_url)
			GDKfree(monetdbe_embedded_url);
		monetdbe_embedded_url = NULL;
	}
}

static bool
monetdbe_is_remote(char* dbname) {

	// TODO copied from mal_client.c
	return dbname != NULL && (strncmp(dbname, "mapi:monetdb://", 15) == 0);
}



static void
monetdbe_startup(monetdbe_database_internal *mdbe, char* dbdir, monetdbe_options *opts)
{
	// Only call monetdbe_startup when there is no monetdb internal yet initialized.
	assert(!monetdbe_embedded_initialized);

	const char* mbedded = "MBEDDED";
	opt *set = NULL;
	int setlen;
	int workers, memory, querytimeout, sessiontimeout;
	gdk_return gdk_res;

	GDKfataljumpenable = 1;

	bool with_mapi_server = false;

	if(setjmp(GDKfataljump) != 0) {
		assert(0);
		mdbe->msg = GDKfatalmsg;
		// we will get here if GDKfatal was called.
		if (mdbe->msg == NULL)
			mdbe->msg = createException(MAL, "monetdbe.monetdbe_startup", "GDKfatal() with unspecified error");
		goto cleanup;
	}

	if (monetdbe_embedded_initialized) {
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_startup", "MonetDBe is already initialized");
		return;
	}

	if ((setlen = mo_builtin_settings(&set)) == 0) {
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_startup", MAL_MALLOC_FAIL);
		goto cleanup;
	}
	if (dbdir && (setlen = mo_add_option(&set, setlen, opt_cmdline, "gdk_dbpath", dbdir)) == 0) {
		mo_free_options(set, setlen);
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_startup", MAL_MALLOC_FAIL);
		goto cleanup;
	}
	if (opts && opts->nr_threads == 1)
		setlen = mo_add_option(&set, setlen, opt_cmdline, "sql_optimizer", "sequential_pipe");
	else
		setlen = mo_add_option(&set, setlen, opt_cmdline, "sql_optimizer", "default_pipe");

	if (setlen == 0) {
		mo_free_options(set, setlen);
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_startup", MAL_MALLOC_FAIL);
		goto cleanup;
	}

	if (opts && opts->mapi_server) {
		/*This monetdbe instance wants to listen to external mapi client connections.*/
		with_mapi_server = true;
		if (opts->mapi_server->port) {
			int psetlen = setlen;
			setlen = mo_add_option(&set, setlen, opt_cmdline, "mapi_port", opts->mapi_server->port);
			if (setlen == psetlen) {
				mo_free_options(set, setlen);
				mdbe->msg = createException(MAL, "monetdbe.monetdbe_startup", MAL_MALLOC_FAIL);
				goto cleanup;
			}
		}
		if (opts->mapi_server->usock) {
			int psetlen = setlen;
			setlen = mo_add_option(&set, setlen, opt_cmdline, "mapi_usock", opts->mapi_server->usock);
			if (setlen == psetlen) {
				mo_free_options(set, setlen);
				mdbe->msg = createException(MAL, "monetdbe.monetdbe_startup", MAL_MALLOC_FAIL);
				goto cleanup;
			}
		}
	}

	GDKtracer_set_adapter(mbedded); /* set the output of GDKtracer logs */

	workers = 0;
	memory = 0;
	querytimeout = 0;
	sessiontimeout = 0;

	if (opts && opts->nr_threads) {
		if( opts->nr_threads < 0){
			mdbe->msg = createException(MAL, "monetdbe.monetdbe_startup", "Nr_threads should be positive");
			goto cleanup;
		}
		workers = GDKnr_threads = opts->nr_threads;
	}
	if (opts && opts->memorylimit) {
		if( opts->memorylimit < 0){
			mdbe->msg = createException(MAL, "monetdbe.monetdbe_startup", "Memorylimit should be positive");
			goto cleanup;
		}
		// Memory limit is session specific
		memory = (size_t) opts->memorylimit;
		GDK_vm_maxsize = (size_t) memory << 20; /* convert from MiB to bytes */
	}
	if (opts && opts->querytimeout) {
		if( opts->querytimeout < 0){
			mdbe->msg = createException(MAL, "monetdbe.monetdbe_startup", "Query timeout should be positive (in sec)");
			goto cleanup;
		}
		// Query time is session specific
		querytimeout = opts->querytimeout;
	}
	if (opts && opts->sessiontimeout) {
		if( opts->sessiontimeout < 0){
			mdbe->msg = createException(MAL, "monetdbe.monetdbe_startup", "Session timeout should be positive (in sec)");
			goto cleanup;
		}
		// Query time is session specific
		sessiontimeout = opts->sessiontimeout;
	}

	if (!dbdir) { /* in-memory */
		if (BBPaddfarm(NULL, (1U << PERSISTENT) | (1U << TRANSIENT), false) != GDK_SUCCEED) {
			mo_free_options(set, setlen);
			mdbe->msg = createException(MAL, "monetdbe.monetdbe_startup", "Cannot add in-memory farm");
			goto cleanup;
		}
	} else {
		if (BBPaddfarm(dbdir, 1U << PERSISTENT, false) != GDK_SUCCEED ||
			BBPaddfarm(/*dbextra ? dbextra : */dbdir, 1U << TRANSIENT, false) != GDK_SUCCEED) {
			mo_free_options(set, setlen);
			mdbe->msg = createException(MAL, "monetdbe.monetdbe_startup", "Cannot add farm %s", dbdir);
			goto cleanup;
		}
		if (GDKcreatedir(dbdir) != GDK_SUCCEED) {
			mo_free_options(set, setlen);
			mdbe->msg = createException(MAL, "monetdbe.monetdbe_startup", "Cannot create directory %s", dbdir);
			goto cleanup;
		}
	}
	gdk_res = GDKinit(set, setlen, true);
	mo_free_options(set, setlen);
	if (gdk_res == GDK_FAIL) {
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_startup", "GDKinit() failed");
		goto cleanup;
	}

	if ((mdbe->msg = malEmbeddedBoot(workers, memory, querytimeout, sessiontimeout, with_mapi_server)) != MAL_SUCCEED)
		goto cleanup;

	monetdbe_embedded_initialized = true;
	monetdbe_embedded_url = dbdir?GDKstrdup(dbdir):NULL;
	GDKfataljumpenable = 0;
cleanup:
	if (mdbe->msg)
		monetdbe_shutdown_internal();
}

static bool urls_matches(const char* l, const char* r) {
	return (l && r && (strcmp(l, r) == 0)) || (l == NULL && r == NULL);
}

static int
monetdbe_open_remote(monetdbe_database_internal *mdbe, char *url, monetdbe_options *opts) {

	monetdbe_remote* remote = opts->remote;
	if (!remote) {
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_open_remote", "Missing user credential for monetdbe remote proxy set up");
		return -1;
	}

	Client c = mdbe->c;

	assert(!c->curprg);

	const char *mod = "user";
	char nme[16];
	const char *name = number2name(nme, sizeof(nme), ++((backend*)  c->sqlcontext)->remote);
	c->curprg = newFunction(putName(mod), putName(name), FUNCTIONsymbol);

	if (c->curprg == NULL) {
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_open_remote", MAL_MALLOC_FAIL);
		return -2;
	}

	MalBlkPtr mb = c->curprg->def;

	InstrPtr q = getInstrPtr(mb, 0);
	q->argc = q->retc = 0;
	q = pushReturn(mb, q, newTmpVariable(mb, TYPE_str));

	InstrPtr p = newFcnCall(mb, remoteRef, connectRef);
	p = pushStr(mb, p, url);
	p = pushStr(mb, p, remote->username);
	p = pushStr(mb, p, remote->password);
	p = pushStr(mb, p, "msql");
	p = pushBit(mb, p, 1);

	q = newInstruction(mb, NULL, NULL);
	q->barrier= RETURNsymbol;
	q = pushReturn(mb, q, getArg(p, 0));

	pushInstruction(mb, q);

	if (p == NULL) {
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_open_remote", MAL_MALLOC_FAIL);
		return -2;
	}
	if ( (mdbe->msg = chkProgram(c->usermodule, mb)) != MAL_SUCCEED ) {
		return -2;
	}
	MalStkPtr stk = prepareMALstack(mb, mb->vsize); // TODO: clean up after usage
	stk->keepAlive = TRUE;
	if ( (mdbe->msg = runMAL(c, mb, 0, stk)) != MAL_SUCCEED ) {
		return -2;
	}
	mdbe->mid = strdup(*getArgReference_str(stk, p, 0));

	garbageCollector(c, mb, stk, TRUE);
	freeStack(stk);

	return 0;
}

int
monetdbe_open(monetdbe_database *dbhdl, char *url, monetdbe_options *opts)
{
	int res = 0;

	if (!dbhdl)
		return -1;
	if (url && strcmp(url, ":memory:") == 0)
		url = NULL;
	MT_lock_set(&embedded_lock);
	monetdbe_database_internal *mdbe = (monetdbe_database_internal*)GDKzalloc(sizeof(monetdbe_database_internal));
	if (!mdbe) {
		MT_lock_unset(&embedded_lock);
		return -1;
	}
	*dbhdl = (monetdbe_database)mdbe;
	mdbe->msg = NULL;
	mdbe->c = NULL;

	bool is_remote = monetdbe_is_remote(url);
	if (!monetdbe_embedded_initialized) {
		/* When used as a remote mapi proxy,
		 * it is still necessary to have an initialized monetdbe. E.g. for BAT life cycle management.
		 * Use an ephemeral/anonymous dbfarm when there is no initialized monetdbe yet.
		 */
		char* _url = is_remote?NULL:url;
		monetdbe_startup(mdbe, _url, opts);
	}
	else if (!is_remote && !urls_matches(monetdbe_embedded_url, url)) {
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_open", "monetdbe_open currently only one active database is supported");
	}
	if (!mdbe->msg)
		res = monetdbe_open_internal(mdbe);

	if (res == 0 && is_remote)
		res = monetdbe_open_remote(mdbe, url, opts);

	MT_lock_unset(&embedded_lock);
	if (mdbe->msg)
		return -2;
	return res;
}

int
monetdbe_close(monetdbe_database dbhdl)
{
	if (!dbhdl)
		return 0;

	monetdbe_database_internal *mdbe = (monetdbe_database_internal*)dbhdl;
	MT_lock_set(&embedded_lock);

	// TODO it is a bit unclear how to handle the error in a faulty disconnect.
	char* msg = NULL;
	if (mdbe->mid) {
		msg = RMTdisconnect(NULL, &(mdbe->mid));
	}

	int err = monetdbe_close_internal(mdbe);
	if (!open_dbs)
		monetdbe_shutdown_internal();
	MT_lock_unset(&embedded_lock);

	if (!err && msg) {
		return -2;
	}

	return err;
}

char *
monetdbe_error(monetdbe_database dbhdl)
{
	if (!dbhdl)
		return NULL;

	monetdbe_database_internal *mdbe = (monetdbe_database_internal*)dbhdl;
	return mdbe->msg;
}

char*
monetdbe_dump_database(monetdbe_database dbhdl, const char *filename)
{
	if (!dbhdl)
		return NULL;

	monetdbe_database_internal *mdbe = (monetdbe_database_internal*)dbhdl;

	if ((mdbe->msg = validate_database_handle(mdbe, "embedded.monetdbe_dump_database")) != MAL_SUCCEED) {

		return mdbe->msg;
	}

	mdbe->msg = monetdbe_mapi_dump_database(dbhdl, filename);

	return mdbe->msg;
}

char*
monetdbe_dump_table(monetdbe_database dbhdl, const char *sname, const char *tname, const char *filename)
{
	if (!dbhdl)
		return NULL;

	monetdbe_database_internal *mdbe = (monetdbe_database_internal*)dbhdl;

	if ((mdbe->msg = validate_database_handle(mdbe, "embedded.monetdbe_dump_table")) != MAL_SUCCEED) {

		return mdbe->msg;
	}

	mdbe->msg = monetdbe_mapi_dump_table(dbhdl, sname, tname, filename);

	return mdbe->msg;
}

char*
monetdbe_get_autocommit(monetdbe_database dbhdl, int* result)
{
	if (!dbhdl)
		return NULL;

	monetdbe_database_internal *mdbe = (monetdbe_database_internal*)dbhdl;

	if ((mdbe->msg = validate_database_handle(mdbe, "monetdbe.monetdbe_get_autocommit")) != MAL_SUCCEED) {

		return mdbe->msg;
	}

	if (!result) {
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_get_autocommit", "Parameter result is NULL");
	} else {
		mvc *m = ((backend *) mdbe->c->sqlcontext)->mvc;
		*result = m->session->auto_commit;
	}

	return mdbe->msg;
}

char*
monetdbe_set_autocommit(monetdbe_database dbhdl, int value)
{
	if (!dbhdl)
		return NULL;

	monetdbe_database_internal *mdbe = (monetdbe_database_internal*)dbhdl;

	if (!validate_database_handle_noerror(mdbe)) {

		return 0;
	}

	mvc *m = ((backend *) mdbe->c->sqlcontext)->mvc;
	int commit = !m->session->auto_commit && value;

	m->session->auto_commit = value;
	m->session->ac_on_commit = m->session->auto_commit;
	if (m->session->tr->active) {
		if (commit) {
			mdbe->msg = mvc_commit(m, 0, NULL, true);
		} else {
			mdbe->msg = mvc_rollback(m, 0, NULL, true);
		}
	}

	return mdbe->msg;
}

int
monetdbe_in_transaction(monetdbe_database dbhdl)
{
	if (!dbhdl)
		return 0;
	monetdbe_database_internal *mdbe = (monetdbe_database_internal*)dbhdl;

	if (!validate_database_handle_noerror(mdbe)) {

		return 0;
	}

	mvc *m = ((backend *) mdbe->c->sqlcontext)->mvc;
	int result = 0;

	if (m->session->tr)
		result = m->session->tr->active;

	return result;
}

struct callback_context {
	monetdbe_database_internal *mdbe;
};

static str
monetdbe_result_cb(void* context, char* tblname, columnar_result* results, size_t nr_results) {
	monetdbe_database_internal *mdbe = ((struct callback_context*) context)->mdbe;

	if (nr_results == 0)
		return MAL_SUCCEED; // No work to do.

	backend *be = NULL;
	if ((mdbe->msg = getBackendContext(mdbe->c, &be)) != NULL)
		return mdbe->msg;

	BAT* order = BATdescriptor(results[0].id);

	mvc_result_table(be, 0, (int) nr_results, Q_TABLE, order);

	for (unsigned  i = 0; i < nr_results; i++) {
		BAT *b = NULL;
		if (i > 0) {
			b = BATdescriptor(results[i].id);
		}
		else
			b = order; // We already fetched this first column

		char* colname	= results[i].colname;
		char* tpename	= results[i].tpename;
		int digits		= results[i].digits;
		int scale		= results[i].scale;

		if ( b == NULL)
			mdbe->msg= createException(MAL,"monetdbe.monetdbe_result_cb",SQLSTATE(HY005) "Cannot access column descriptor ");
		else if (mvc_result_column(be, tblname, colname, tpename, digits, scale, b))
			mdbe->msg = createException(SQL, "monetdbe.monetdbe_result_cb", SQLSTATE(42000) "Cannot access column descriptor %s.%s",tblname,colname);
		if( b)
			BBPkeepref(b->batCacheid);
	}

	return MAL_SUCCEED;
}

struct prepare_callback_context {
	int* prepare_id;
	monetdbe_database_internal *mdbe;
};

static str
monetdbe_prepare_cb(void* context, char* tblname, columnar_result* results, size_t nr_results) {
	(void) tblname;
	monetdbe_database_internal *mdbe	= ((struct prepare_callback_context*) context)->mdbe;
	int *prepare_id						= ((struct prepare_callback_context*) context)->prepare_id;

	if (nr_results != 6) // 1) btype 2) bdigits 3) bscale 4) bschema 5) btable 6) bcolumn
		return createException(SQL, "monetdbe.monetdbe_prepare_cb", SQLSTATE(42000) "result table for prepared statement is wrong.");

	backend *be = NULL;
	if ((mdbe->msg = getBackendContext(mdbe->c, &be)) != NULL)
		return mdbe->msg;

	BAT* btype = NULL;
	BAT* bdigits = NULL;
	BAT* bscale = NULL;
	BAT* bschema = NULL;
	BAT* btable = NULL;
	BAT* bcolumn = NULL;

	size_t nparams = 0;
	BATiter bcolumn_iter = {0};
	BATiter btable_iter = {0};
	char* function = NULL;
	Symbol prg = NULL;
	MalBlkPtr mb = NULL;
	InstrPtr o = NULL, e = NULL, r = NULL;
	sql_rel* rel = NULL;
	list *args = NULL, *rets = NULL;
	sql_allocator* sa = NULL;
	ValRecord v = {0};
	ptr vp = NULL;
	struct callback_context* ccontext= NULL;
	columnar_result_callback* rcb = NULL;

	str msg = MAL_SUCCEED;
	if (!(btype		= BATdescriptor(results[0].id))	||
		!(bdigits	= BATdescriptor(results[1].id))	||
		!(bscale	= BATdescriptor(results[2].id))	||
		!(bschema	= BATdescriptor(results[3].id))	||
		!(btable	= BATdescriptor(results[4].id))	||
		!(bcolumn	= BATdescriptor(results[5].id)))
	{
		msg = createException(SQL, "monetdbe.monetdbe_prepare_cb", SQLSTATE(42000) "Cannot access prepare result");
		goto cleanup;
	}

	nparams = BATcount(btype);

	if (nparams 	!= BATcount(bdigits) ||
		nparams 	!= BATcount(bscale) ||
		nparams 	!= BATcount(bschema) ||
		nparams + 1 != BATcount(btable) ||
		nparams 	!= BATcount(bcolumn))
	{
		msg = createException(SQL, "monetdbe.monetdbe_prepare_cb", SQLSTATE(42000) "prepare results are incorrect.");
		goto cleanup;
	}

	bcolumn_iter	= bat_iterator(bcolumn);
	btable_iter		= bat_iterator(btable);
	function		=  BUNtvar(btable_iter, BATcount(btable)-1);

	prg				= newFunction(userRef, putName("temp"), FUNCTIONsymbol);

	resizeMalBlk(prg->def, (int) nparams + 3 /*function declaration + remote.exec + return statement*/);
	mb = prg->def;

	o = getInstrPtr(mb, 0);
	o->retc = o->argc = 0;

	e = newInstruction(mb, remoteRef, execRef);
	setDestVar(e, newTmpVariable(mb, TYPE_any));
	e = pushStr(mb, e, mdbe->mid);
	e = pushStr(mb, e, userRef);
	e = pushStr(mb, e, function);

	rcb = GDKmalloc(sizeof(columnar_result_callback));
	if (rcb == NULL) {
		msg = createException(MAL, "monetdbe.monetdbe_prepare_cb", MAL_MALLOC_FAIL);
		goto cleanup;
	}

	ccontext = GDKzalloc(sizeof(struct callback_context));
	if (ccontext == NULL) {
		msg = createException(MAL, "monetdbe.monetdbe_prepare_cb", MAL_MALLOC_FAIL);
		goto cleanup;
	}

	ccontext->mdbe = mdbe;

	rcb->context = ccontext;
	rcb->call = monetdbe_result_cb;

	vp = (ptr) rcb;

	VALset(&v, TYPE_ptr, &vp);
	e = pushValue(mb, e, &v);

	r = newInstruction(mb, NULL, NULL);
	r->barrier= RETURNsymbol;
	r->argc= r->retc=0;

	sa = be->mvc->sa;

	args = new_exp_list(sa);
	rets = new_exp_list(sa);

	for (size_t i = 0; i < nparams; i++) {

		char* table =  BUNtvar(btable_iter, i);

		if (strNil(table)) {
			// input argument
			sql_type *t = SA_ZNEW(sa, sql_type);
			t->localtype = *(int*) Tloc(btype, i);

			sql_subtype *st = SA_ZNEW(sa, sql_subtype);
			sql_init_subtype(st, t, (unsigned) *(int*) Tloc(bdigits, i), (unsigned) *(int*) Tloc(bscale, i));

			sql_arg *a = SA_ZNEW(sa, sql_arg);
			a->type = *st;
			append(args, a);

			int idx = newVariable(mb, NULL, 0, t->localtype);
			o = pushArgument(mb, o, idx);

			InstrPtr p = newFcnCall(mb, remoteRef, putRef);
			setArgType(mb, p, 0, TYPE_str);
			p = pushStr(mb, p, mdbe->mid);
			p = pushArgument(mb, p, idx);

			e = pushArgument(mb, e, getArg(p, 0));
		}
		else {
			// output argument
			sql_type *t = SA_ZNEW(sa, sql_type);
			int type = *(int*) Tloc(btype, i);
			t->localtype = type;

			char* column = BUNtvar(bcolumn_iter, i);
			sql_subtype *st = SA_ZNEW(sa, sql_subtype);
			sql_init_subtype(st, t, (unsigned) *(int*) Tloc(bdigits, i), (unsigned) *(int*) Tloc(bscale, i));

			sql_exp * c = exp_column(sa, table, column, st, CARD_MULTI, true, false);
			append(rets, c);
		}
	}
	pushInstruction(mb, e);
	pushInstruction(mb, r);

	if ( (mdbe->msg = chkProgram(mdbe->c->usermodule, mb)) != MAL_SUCCEED ) {
		msg = mdbe->msg;
		goto cleanup;
	}

	rel = rel_project(sa, NULL, rets);
	be->q = qc_insert(be->mvc->qc, sa, rel, NULL, args, be->mvc->type, NULL, be->no_mitosis);
	*prepare_id = be->q->id;

	/*
	 * HACK: we need to rename the Symbol aka MAL function to the query cache name.
	 * Basically we keep the MALblock but we destroy the containing old Symbol
	 * and create a new one with the correct name and set its MAL block pointer to 
	 * point to the mal block we have created in this function.
	 */
	prg->def = NULL;
	freeSymbol(prg);
	if ((prg = newFunction(userRef, putName(be->q->name), FUNCTIONsymbol)) == NULL) {
		msg = createException(MAL, "monetdbe.monetdbe_prepare_cb", MAL_MALLOC_FAIL);
		goto cleanup;
	}
	prg->def = mb;
	setFunctionId(getSignature(prg), be->q->name);

	// finally add this beautiful new function to the local user module.
	insertSymbol(mdbe->c->usermodule, prg);

cleanup:
	// clean these up
	if (btype)		BBPunfix(btype->batCacheid);
	if (bdigits)	BBPunfix(bdigits->batCacheid);
	if (bscale)		BBPunfix(bscale->batCacheid);
	if (bschema)	BBPunfix(bschema->batCacheid);
	if (btable)		BBPunfix(btable->batCacheid);
	if (bcolumn)	BBPunfix(bcolumn->batCacheid);

	if (msg && rcb) GDKfree(rcb);
	if (msg && ccontext) GDKfree(ccontext);

	return msg;
}

static char*
monetdbe_query_remote(monetdbe_database_internal *mdbe, char* query, monetdbe_result** result, monetdbe_cnt* affected_rows, int *prepare_id)
{
	// TODO: do something with affected_rows
	(void) affected_rows;

	const char *mod = "user";
	char nme[16];

	Client c = mdbe->c;

	const char *name = number2name(nme, sizeof(nme), ++((backend*)  c->sqlcontext)->remote);
	Symbol prg = newFunction(putName(mod), putName(name), FUNCTIONsymbol);

	if (prg == NULL) {
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_query_remote", MAL_MALLOC_FAIL);
		return mdbe->msg;
	}

	MalBlkPtr mb = prg->def;

	InstrPtr f = getInstrPtr(mb, 0);
	f->retc = f->argc = 0;

	InstrPtr o = newStmt(mb, remoteRef, putRef);
	o = pushStr(mb, o, mdbe->mid);
	o = pushBit(mb, o, TRUE);


	if (prepare_id) {
		size_t query_len, input_query_len, prep_len = 0;
		input_query_len = strlen(query);
		query_len = input_query_len + 3;
		const char PREPARE[] = "PREPARE ";
		prep_len = sizeof(PREPARE)-1;
		query_len += prep_len;
		char *nq = NULL;
		if (!(nq = GDKmalloc(query_len))) {
			mdbe->msg = createException(MAL, "monetdbe.monetdbe_query_remote", "Could not setup query stream");
			return mdbe->msg;
		}
		strcpy(nq, PREPARE);
		strcpy(nq + prep_len, query);
		strcpy(nq + prep_len + input_query_len, "\n;");
		query = nq;
	}

	InstrPtr p = newStmt(mb, remoteRef, putRef);
	p = pushStr(mb, p, mdbe->mid);
	p = pushStr(mb, p, query);


	InstrPtr e = newInstruction(mb, remoteRef, execRef);
	setDestVar(e, newTmpVariable(mb, TYPE_any));
	e = pushStr(mb, e, mdbe->mid);
	e = pushStr(mb, e, sqlRef);
	e = pushStr(mb, e, evalRef);

	/*
	 * prepare the call back routine and its context
	 * and pass it over as a pointer to remote.exec.
	 */
	columnar_result_callback* rcb = GDKzalloc(sizeof(columnar_result_callback));
	if (!prepare_id) {
		struct callback_context* ccontext;
		ccontext		= GDKzalloc(sizeof(struct callback_context));
		ccontext->mdbe	= mdbe;
		rcb->context	= ccontext;
		rcb->call		= monetdbe_result_cb;
	}
	else {
		struct prepare_callback_context* ccontext;
		ccontext				= GDKzalloc(sizeof(struct prepare_callback_context));
		ccontext->mdbe			= mdbe;
		ccontext->prepare_id	= prepare_id;
		rcb->context			= ccontext;
		rcb->call				= monetdbe_prepare_cb;
	}

	ValRecord v;
	ptr vp = (ptr) rcb;
	
	VALset(&v, TYPE_ptr, &vp);	
	e = pushValue(mb, e, &v);

	e = pushArgument(mb, e, getArg(p, 0));
	e = pushArgument(mb, e, getArg(o, 0));

	pushInstruction(mb, e);

	InstrPtr r = newInstruction(mb, NULL, NULL);
	r->barrier= RETURNsymbol;
	r->argc= r->retc=0;
	pushInstruction(mb, r);

	if ( (mdbe->msg = chkProgram(c->usermodule, mb)) != MAL_SUCCEED ) {
		return mdbe->msg;
	}

	MalStkPtr stk = prepareMALstack(mb, mb->vsize); // TODO: clean up after usage
	stk->keepAlive = TRUE;
	if ( (mdbe->msg = runMAL(c, mb, 0, stk)) != MAL_SUCCEED )
		return mdbe->msg;

	if (result) {
		if ((mdbe->msg = monetdbe_get_results(result, mdbe)) != MAL_SUCCEED) {
			return mdbe->msg;
		}

		mvc* m = NULL;
		backend * be = NULL;
		if ((mdbe->msg = getSQLContext(c, NULL, &m, &be)) != MAL_SUCCEED)
			return mdbe->msg;

		if (m->emode & m_prepare)
			((monetdbe_result_internal*) *result)->type = Q_PREPARE;
		else
			((monetdbe_result_internal*) *result)->type = (be->results) ? be->results->query_type : m->type;
	}

	return mdbe->msg;
}

char*
monetdbe_query(monetdbe_database dbhdl, char* query, monetdbe_result** result, monetdbe_cnt* affected_rows)
{
	if (!dbhdl)
		return NULL;
	monetdbe_database_internal *mdbe = (monetdbe_database_internal*)dbhdl;

	if (mdbe->mid) {
		mdbe->msg = monetdbe_query_remote(mdbe, query, result, affected_rows, NULL);
	}
	else {
		mdbe->msg = monetdbe_query_internal(mdbe, query, result, affected_rows, NULL, 'S');
	}

	return mdbe->msg;
}

char*
monetdbe_prepare(monetdbe_database dbhdl, char* query, monetdbe_statement **stmt)
{
	if (!dbhdl)
		return NULL;
	monetdbe_database_internal *mdbe = (monetdbe_database_internal*)dbhdl;

	int prepare_id = 0;

	if (!stmt)
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_prepare", "Parameter stmt is NULL");
	else if (mdbe->mid) {
		mdbe->msg = monetdbe_query_remote(mdbe, query, NULL, NULL, &prepare_id);
	}
	else {
		mdbe->msg = monetdbe_query_internal(mdbe, query, NULL, NULL, &prepare_id, 'S');
	}
	if (mdbe->msg == MAL_SUCCEED) {
		mvc *m = ((backend *) mdbe->c->sqlcontext)->mvc;
		monetdbe_stmt_internal *stmt_internal = (monetdbe_stmt_internal*)GDKmalloc(sizeof(monetdbe_stmt_internal));
		cq *q = qc_find(m->qc, prepare_id);

		if (q && stmt_internal) {
			Symbol s = findSymbolInModule(mdbe->c->usermodule, q->f->imp);
			InstrPtr p = s->def->stmt[0];
			stmt_internal->mdbe = mdbe;
			stmt_internal->q = q;
			stmt_internal->retc = p->retc;
			stmt_internal->res.nparam = list_length(q->f->ops);
			stmt_internal->data = (ValRecord*)GDKzalloc(sizeof(ValRecord) * stmt_internal->res.nparam);
			stmt_internal->args = (ValPtr*)GDKmalloc(sizeof(ValPtr) * (list_length(q->f->ops) + stmt_internal->retc));
			stmt_internal->res.type = (monetdbe_types*)GDKmalloc(sizeof(monetdbe_types)* stmt_internal->res.nparam);
			if (!stmt_internal->res.type || !stmt_internal->data || !stmt_internal->args) {
				if (stmt_internal->data)
					GDKfree(stmt_internal->data);
				mdbe->msg = createException(MAL, "monetdbe.monetdbe_prepare", "Could not setup prepared statement");
			} else {
				int i = 0;
				for (node *n = q->f->ops->h; n; n = n->next, i++) {
					sql_arg *a = n->data;
					sql_subtype *t = &a->type;
					stmt_internal->res.type[i] = embedded_type(t->type->localtype);
					stmt_internal->args[i+stmt_internal->retc] = &stmt_internal->data[i];
				}
			}
		}
		if (mdbe->msg == MAL_SUCCEED)
			*stmt = (monetdbe_statement*)stmt_internal;
		else if (stmt_internal)
			GDKfree(stmt_internal);
	}

	return mdbe->msg;
}

char*
monetdbe_bind(monetdbe_statement *stmt, void *data, size_t i)
{
	monetdbe_stmt_internal *stmt_internal = (monetdbe_stmt_internal*)stmt;

	/* TODO !data treat as NULL value (add nil mask) ? */
	if (i > stmt->nparam)
		return createException(MAL, "monetdbe.monetdbe_bind", "Parameter %zu not bound to a value", i);
	sql_arg *a = (sql_arg*)list_fetch(stmt_internal->q->f->ops, (int) i);
	assert(a);
	stmt_internal->data[i].vtype = a->type.type->localtype;
	/* TODO handle conversion from NULL and special types */
	VALset(&stmt_internal->data[i], a->type.type->localtype, data);
	return MAL_SUCCEED;
}

char*
monetdbe_execute(monetdbe_statement *stmt, monetdbe_result **result, monetdbe_cnt *affected_rows)
{
	monetdbe_result_internal *res_internal = NULL;
	monetdbe_stmt_internal *stmt_internal = (monetdbe_stmt_internal*)stmt;
	backend *b = (backend *) stmt_internal->mdbe->c->sqlcontext;
	mvc *m = b->mvc;
	monetdbe_database_internal *mdbe = stmt_internal->mdbe;

	if ((mdbe->msg = SQLtrans(m)) != MAL_SUCCEED)
		return mdbe->msg;

	/* check if all inputs are bound */
	for(int i = 0; i< list_length(stmt_internal->q->f->ops); i++){
		if (!stmt_internal->data[i].vtype)
			return createException(MAL, "monetdbe.monetdbe_execute", "Parameter %d not bound to a value", i);
	}

	cq* q = stmt_internal->q;

	MalStkPtr glb = (MalStkPtr) (NULL);
	Symbol s = findSymbolInModule(mdbe->c->usermodule, q->f->imp);

	mdbe->msg = callMAL(mdbe->c, s->def, &glb, stmt_internal->args, 0);

	if (!b->results && b->rowcnt >= 0 && affected_rows)
		*affected_rows = b->rowcnt;


	if (result) {
		if ((mdbe->msg = monetdbe_get_results(result, mdbe)) != MAL_SUCCEED) {
			goto cleanup;
		}

		((monetdbe_result_internal*) *result)->type = (b->results) ? Q_TABLE : Q_UPDATE;
	}
cleanup:
	return commit_action(m, stmt_internal->mdbe, result, res_internal);
}

char*
monetdbe_cleanup_statement(monetdbe_database dbhdl, monetdbe_statement *stmt)
{
	monetdbe_stmt_internal *stmt_internal = (monetdbe_stmt_internal*)stmt;
	monetdbe_database_internal *mdbe = (monetdbe_database_internal*)dbhdl;
	mvc *m = ((backend *) mdbe->c->sqlcontext)->mvc;
	cq *q = stmt_internal->q;

	assert(!stmt_internal->mdbe || mdbe == stmt_internal->mdbe);
	GDKfree(stmt_internal->data);
	GDKfree(stmt_internal->args);
	GDKfree(stmt_internal->res.type);
	GDKfree(stmt_internal);

	if (q)
		qc_delete(m->qc, q);
	return MAL_SUCCEED;
}

char*
monetdbe_cleanup_result(monetdbe_database dbhdl, monetdbe_result* result)
{
	monetdbe_database_internal *mdbe = (monetdbe_database_internal*)dbhdl;
	monetdbe_result_internal* res = (monetdbe_result_internal *) result;


	if (!result) {
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_cleanup_result_internal", "Parameter result is NULL");
	} else {
		mdbe->msg = monetdbe_cleanup_result_internal(mdbe, res);
	}

	return mdbe->msg;
}

char*
monetdbe_get_columns(monetdbe_database dbhdl, const char* schema_name, const char *table_name, size_t *column_count,
					char ***column_names, int **column_types)
{
	monetdbe_database_internal *mdbe = (monetdbe_database_internal*)dbhdl;
	mvc *m;
	sql_schema *s;
	sql_table *t;
	int columns;
	node *n;


	if ((mdbe->msg = validate_database_handle(mdbe, "monetdbe.monetdbe_get_columns")) != MAL_SUCCEED) {

		return mdbe->msg;
	}

	if ((mdbe->msg = getSQLContext(mdbe->c, NULL, &m, NULL)) != MAL_SUCCEED)
		goto cleanup;
	if ((mdbe->msg = SQLtrans(m)) != MAL_SUCCEED)
		goto cleanup;
	if (!column_count) {
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_get_columns", "Parameter column_count is NULL");
		goto cleanup;
	}
	if (!column_names) {
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_get_columns", "Parameter column_names is NULL");
		goto cleanup;
	}
	if (!column_types) {
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_get_columns", "Parameter column_types is NULL");
		goto cleanup;
	}
	if (!table_name) {
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_get_columns", "Parameter table_name is NULL");
		goto cleanup;
	}
	if (schema_name) {
		if (!(s = mvc_bind_schema(m, schema_name))) {
			mdbe->msg = createException(MAL, "monetdbe.monetdbe_get_columns", "Could not find schema %s", schema_name);
			goto cleanup;
		}
	} else {
		s = cur_schema(m);
	}
	if (!(t = mvc_bind_table(m, s, table_name))) {
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_get_columns", "Could not find table %s", table_name);
		goto cleanup;
	}

	columns = t->columns.set->cnt;
	*column_count = columns;
	*column_names = GDKzalloc(sizeof(char*) * columns);
	*column_types = GDKzalloc(sizeof(int) * columns);
	if (*column_names == NULL || *column_types == NULL) {
		if (*column_names) {
			GDKfree(*column_names);
			*column_names = NULL;
		}
		if (*column_types) {
			GDKfree(*column_types);
			*column_types = NULL;
		}
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_get_columns", MAL_MALLOC_FAIL);
		goto cleanup;
	}

	for (n = t->columns.set->h; n; n = n->next) {
		sql_column *col = n->data;
		(*column_names)[col->colnr] = col->base.name;
		(*column_types)[col->colnr] = embedded_type(col->type.type->localtype);
	}

cleanup:
	mdbe->msg = commit_action(m, mdbe, NULL, NULL);

	return mdbe->msg;
}

#define GENERATE_BASE_HEADERS(type, tpename) \
	static int tpename##_is_null(type *value)

#define GENERATE_BASE_FUNCTIONS(tpe, tpename, mname) \
	GENERATE_BASE_HEADERS(tpe, tpename); \
	static int tpename##_is_null(tpe *value) { return *value == mname##_nil; }

#ifdef bool
#undef bool
#endif

GENERATE_BASE_FUNCTIONS(int8_t, bool, bit)
GENERATE_BASE_FUNCTIONS(int8_t, int8_t, bte)
GENERATE_BASE_FUNCTIONS(int16_t, int16_t, sht)
GENERATE_BASE_FUNCTIONS(int32_t, int32_t, int)
GENERATE_BASE_FUNCTIONS(int64_t, int64_t, lng)
#ifdef HAVE_HGE
GENERATE_BASE_FUNCTIONS(__int128, int128_t, hge)
#endif
GENERATE_BASE_FUNCTIONS(size_t, size_t, oid)

GENERATE_BASE_FUNCTIONS(float, float, flt)
GENERATE_BASE_FUNCTIONS(double, double, dbl)

GENERATE_BASE_HEADERS(char*, str);
GENERATE_BASE_HEADERS(monetdbe_data_blob, blob);

GENERATE_BASE_HEADERS(monetdbe_data_date, date);
GENERATE_BASE_HEADERS(monetdbe_data_time, time);
GENERATE_BASE_HEADERS(monetdbe_data_timestamp, timestamp);

#define GENERATE_BAT_INPUT_BASE(tpe)									\
	monetdbe_column_##tpe *bat_data = GDKzalloc(sizeof(monetdbe_column_##tpe));	\
	if (!bat_data) {													\
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_result_fetch", MAL_MALLOC_FAIL); \
		goto cleanup;													\
	}																	\
	bat_data->type = monetdbe_##tpe;									\
	bat_data->is_null = tpe##_is_null;									\
	if (sqltpe->type->radix == 10) bat_data->scale = pow(10, sqltpe->scale); \
	column_result = (monetdbe_column*) bat_data;

#define GENERATE_BAT_INPUT(b, tpe, tpe_name, mtype)						\
	{																	\
		GENERATE_BAT_INPUT_BASE(tpe_name);								\
		bat_data->count = (size_t) mres->nrows;							\
		bat_data->null_value = mtype##_nil;								\
		if (bat_data->count) {											\
			bat_data->data = GDKzalloc(bat_data->count * sizeof(bat_data->null_value)); \
			if (!bat_data->data) {										\
				mdbe->msg = createException(MAL, "monetdbe.monetdbe_result_fetch", MAL_MALLOC_FAIL); \
				goto cleanup;											\
			}															\
		}																\
		size_t it = 0;													\
		mtype* val = (mtype*)Tloc(b, 0);								\
		/* bat is dense, materialize it */								\
		for (it = 0; it < bat_data->count; it++, val++)					\
			bat_data->data[it] = (tpe) *val;							\
	}

char*
monetdbe_append(monetdbe_database dbhdl, const char* schema, const char* table, monetdbe_column **input, size_t column_count)
{
	monetdbe_database_internal *mdbe = (monetdbe_database_internal*)dbhdl;
	mvc *m = NULL;
	sql_schema *s = NULL;
	sql_table *t = NULL;
	size_t i, cnt;
	node *n;


	if ((mdbe->msg = validate_database_handle(mdbe, "monetdbe.monetdbe_append")) != MAL_SUCCEED) {
		return mdbe->msg;
	}

	if ((mdbe->msg = getSQLContext(mdbe->c, NULL, &m, NULL)) != MAL_SUCCEED)
		goto cleanup;
	if ((mdbe->msg = SQLtrans(m)) != MAL_SUCCEED)
		goto cleanup;

	if (schema == NULL) {
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_append", "schema parameter is NULL");
		goto cleanup;
	}
	if (table == NULL) {
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_append", "table parameter is NULL");
		goto cleanup;
	}
	if (input == NULL) {
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_append", "input parameter is NULL");
		goto cleanup;
	}
	if (column_count < 1) {
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_append", "column_count must be higher than 0");
		goto cleanup;
	}

	if (schema) {
		if (!(s = mvc_bind_schema(m, schema))) {
			mdbe->msg = createException(MAL, "monetdbe.monetdbe_append", "Schema missing %s", schema);
			goto cleanup;
		}
	} else {
		s = cur_schema(m);
	}
	if (!(t = mvc_bind_table(m, s, table))) {
		mdbe->msg = createException(SQL, "monetdbe.monetdbe_append", "Table missing %s.%s", schema, table);
		goto cleanup;
	}

	/* for now no default values, ie user should supply all columns */
	if (column_count != (size_t)list_length(t->columns.set)) {
		mdbe->msg = createException(SQL, "monetdbe.monetdbe_append", "Incorrect number of columns");
		goto cleanup;
	}

	cnt = input[0]->count;

	for (i = 0, n = t->columns.set->h; i < column_count && n; i++, n = n->next) {
		sql_column *c = n->data;
		int mtype = monetdbe_type(input[i]->type);
		const void* nil = (mtype>=0)?ATOMnilptr(mtype):NULL;
		char *v = input[i]->data;

		if (mtype < 0) {
			mdbe->msg = createException(SQL, "monetdbe.monetdbe_append", "Cannot find type for column %zu", i);
			goto cleanup;
		}
		if (mtype >= TYPE_bit && mtype <=
#ifdef HAVE_HGE
	TYPE_hge
#else
	TYPE_lng
#endif
		) {
			//-------------------------------------
			BAT *bn = NULL;

			if (mtype != c->type.type->localtype) {
				mdbe->msg = createException(SQL, "monetdbe.monetdbe_append", "Cannot append %d into column '%s'", input[i]->type, c->base.name);
				goto cleanup;
			}

			if ((bn = COLnew(0, mtype, 0, TRANSIENT)) == NULL) {
				mdbe->msg = createException(SQL, "monetdbe.monetdbe_append", "Cannot create append column");
				goto cleanup;
			}

			//save prev heap pointer
			char *prev_base;
			size_t prev_size;
			prev_base = bn->theap.base;
			prev_size = bn->theap.size;

			//BAT heap base to input[i]->data
			bn->theap.base = input[i]->data;
			bn->theap.size = tailsize(bn, cnt);

			//BATsetdims(bn); called in COLnew
			BATsetcapacity(bn, cnt);
			BATsetcount(bn, cnt);

			//set default flags
			BATsettrivprop(bn);

			if (store_funcs.append_col(m->session->tr, c, bn, TYPE_bat) != 0) {
				bn->theap.base = prev_base;
				bn->theap.size = prev_size;
				BBPreclaim(bn);
				mdbe->msg = createException(SQL, "monetdbe.monetdbe_append", "Cannot append BAT");
				goto cleanup;
			}

			bn->theap.base = prev_base;
			bn->theap.size = prev_size;
			BBPreclaim(bn);
		} else if (mtype == TYPE_str) {
			char **d = (char**)v;

			for (size_t j=0; j<cnt; j++){
				char *s = d[j];
				if (!s)
					s = (char*) nil;

				if (store_funcs.append_col(m->session->tr, c, s, mtype) != 0) {
					mdbe->msg = createException(SQL, "monetdbe.monetdbe_append", "Cannot append values");
					goto cleanup;
				}
			}
		} else if (mtype == TYPE_timestamp) {
			monetdbe_data_timestamp* ts = (monetdbe_data_timestamp*)v;

			for (size_t j=0; j<cnt; j++){
				timestamp t = *(timestamp*) nil;
				if(!timestamp_is_null(ts+j))
					t = timestamp_from_data(&ts[j]);

				if (store_funcs.append_col(m->session->tr, c, &t, mtype) != 0) {
					mdbe->msg = createException(SQL, "monetdbe.monetdbe_append", "Cannot append values");
					goto cleanup;
				}
			}
		} else if (mtype == TYPE_date) {
			monetdbe_data_date* de = (monetdbe_data_date*)v;

			for (size_t j=0; j<cnt; j++){
				date d = *(date*) nil;
				if(!date_is_null(de+j))
					d = date_from_data(&de[j]);

				if (store_funcs.append_col(m->session->tr, c, &d, mtype) != 0) {
					mdbe->msg = createException(SQL, "monetdbe.monetdbe_append", "Cannot append values");
					goto cleanup;
				}
			}
		} else if (mtype == TYPE_daytime) {
			monetdbe_data_time* t = (monetdbe_data_time*)v;

			for (size_t j=0; j<cnt; j++){
				daytime dt = *(daytime*) nil;
				if(!time_is_null(t+j))
					dt = time_from_data(&t[j]);

				if (store_funcs.append_col(m->session->tr, c, &dt, mtype) != 0) {
					mdbe->msg = createException(SQL, "monetdbe.monetdbe_append", "Cannot append values");
					goto cleanup;
				}
			}
		} else if (mtype == TYPE_blob) {
			monetdbe_data_blob* be = (monetdbe_data_blob*)v;

			for (size_t j=0; j<cnt; j++){
				blob* b = (blob*) nil;
				int res;
				if (!blob_is_null(be+j)) {
					size_t len = be[j].size;
					b = (blob*) GDKmalloc(blobsize(len));
					if (b == NULL)
						mdbe->msg = createException(MAL, "monetdbe.monetdbe_append", MAL_MALLOC_FAIL);

					b->nitems = len;
					memcpy(b->data, be[j].data, len);
				}

				res = store_funcs.append_col(m->session->tr, c, b, mtype);
				if (b && b != (blob*)nil)
					GDKfree(b);
				if (res != 0) {
					mdbe->msg = createException(SQL, "monetdbe.monetdbe_append", "Cannot append values");
					goto cleanup;
				}
			}
		}
	}
cleanup:
	mdbe->msg = commit_action(m, mdbe, NULL, NULL);

	return mdbe->msg;
}

const void *
monetdbe_null(monetdbe_database dbhdl, monetdbe_types t)
{
	monetdbe_database_internal *mdbe = (monetdbe_database_internal*)dbhdl;
	int mtype = monetdbe_type(t);

	if (mtype < 0)
		return NULL;

	if ((mtype >= TYPE_bit && mtype <=
#ifdef HAVE_HGE
	TYPE_hge
#else
	TYPE_lng
#endif
			))
		return ATOMnilptr(mtype);
	else if (mtype == TYPE_str)
		return NULL;
	else if (mtype == TYPE_blob)
		return &mdbe->blob_null;
	else if (mtype == TYPE_date)
		return &mdbe->date_null;
	else if (mtype == TYPE_daytime)
		return &mdbe->time_null;
	else if (mtype == TYPE_timestamp)
		return &mdbe->timestamp_null;
	return NULL;
}

char*
monetdbe_result_fetch(monetdbe_result* mres, monetdbe_column** res, size_t column_index)
{
	BAT* b = NULL;
	int bat_type;
	mvc* m;
	monetdbe_result_internal* result = (monetdbe_result_internal*) mres;
	sql_subtype* sqltpe = NULL;
	monetdbe_column* column_result = NULL;
	size_t j = 0;
	monetdbe_database_internal *mdbe = result->mdbe;
	Client c = mdbe->c;


	if ((mdbe->msg = validate_database_handle(mdbe, "monetdbe.monetdbe_result_fetch")) != MAL_SUCCEED) {

		return mdbe->msg;
	}

	if ((mdbe->msg = getSQLContext(c, NULL, &m, NULL)) != MAL_SUCCEED)
		goto cleanup;
	if (!res) {
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_result_fetch", "Parameter res is NULL");
		goto cleanup;
	}
	if (column_index >= mres->ncols) {
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_result_fetch", "Index out of range");
		goto cleanup;
	}
	// check if we have the column converted already
	if (result->converted_columns[column_index]) {
		*res = result->converted_columns[column_index];

		return MAL_SUCCEED;
	}

	// otherwise we have to convert the column
	b = BATdescriptor(result->monetdbe_resultset->cols[column_index].b);
	if (!b) {
		mdbe->msg = createException(MAL, "monetdbe.monetdbe_result_fetch", RUNTIME_OBJECT_MISSING);
		goto cleanup;
	}
	bat_type = b->ttype;
	sqltpe = &result->monetdbe_resultset->cols[column_index].type;

	if (bat_type == TYPE_bit) {
		GENERATE_BAT_INPUT(b, int8_t, bool, bit);
	} else if (bat_type == TYPE_bte) {
		GENERATE_BAT_INPUT(b, int8_t, int8_t, bte);
	} else if (bat_type == TYPE_sht) {
		GENERATE_BAT_INPUT(b, int16_t, int16_t, sht);
	} else if (bat_type == TYPE_int) {
		GENERATE_BAT_INPUT(b, int32_t, int32_t, int);
	} else if (bat_type == TYPE_oid) {
		GENERATE_BAT_INPUT(b, size_t, size_t, oid);
	} else if (bat_type == TYPE_lng) {
		GENERATE_BAT_INPUT(b, int64_t, int64_t, lng);
#ifdef HAVE_HGE
	} else if (bat_type == TYPE_hge) {
		GENERATE_BAT_INPUT(b, __int128, int128_t, hge);
#endif
	} else if (bat_type == TYPE_flt) {
		GENERATE_BAT_INPUT(b, float, float, flt);
	} else if (bat_type == TYPE_dbl) {
		GENERATE_BAT_INPUT(b, double, double, dbl);
	} else if (bat_type == TYPE_str) {
		BATiter li;
		BUN p = 0, q = 0;
		GENERATE_BAT_INPUT_BASE(str);
		bat_data->count = (size_t) mres->nrows;
		if (bat_data->count) {
			bat_data->data = GDKzalloc(sizeof(char *) * bat_data->count);
			bat_data->null_value = NULL;
			if (!bat_data->data) {
				mdbe->msg = createException(MAL, "monetdbe.monetdbe_result_fetch", MAL_MALLOC_FAIL);
				goto cleanup;
			}
		}

		j = 0;
		li = bat_iterator(b);
		BATloop(b, p, q)
		{
			char *t = (char *)BUNtail(li, p);
			if (strcmp(t, str_nil) == 0) {
				bat_data->data[j] = NULL;
			} else {
				bat_data->data[j] = GDKstrdup(t);
				if (!bat_data->data[j]) {
					goto cleanup;
				}
			}
			j++;
		}
	} else if (bat_type == TYPE_date) {
		date *baseptr;
		GENERATE_BAT_INPUT_BASE(date);
		bat_data->count = (size_t) mres->nrows;
		if (bat_data->count) {
			bat_data->data = GDKmalloc(sizeof(bat_data->null_value) * bat_data->count);
			if (!bat_data->data) {
				mdbe->msg = createException(MAL, "monetdbe.monetdbe_result_fetch", MAL_MALLOC_FAIL);
				goto cleanup;
			}
		}

		baseptr = (date *)Tloc(b, 0);
		for (j = 0; j < bat_data->count; j++)
			data_from_date(baseptr[j], bat_data->data + j);
		memcpy(&bat_data->null_value, &mdbe->date_null, sizeof(monetdbe_data_date));
	} else if (bat_type == TYPE_daytime) {
		daytime *baseptr;
		GENERATE_BAT_INPUT_BASE(time);
		bat_data->count = (size_t) mres->nrows;
		if (bat_data->count) {
			bat_data->data = GDKmalloc(sizeof(bat_data->null_value) * bat_data->count);
			if (!bat_data->data) {
				mdbe->msg = createException(MAL, "monetdbe.monetdbe_result_fetch", MAL_MALLOC_FAIL);
				goto cleanup;
			}
		}

		baseptr = (daytime *)Tloc(b, 0);
		for (j = 0; j < bat_data->count; j++)
			data_from_time(baseptr[j], bat_data->data + j);
		memcpy(&bat_data->null_value, &mdbe->time_null, sizeof(monetdbe_data_time));
	} else if (bat_type == TYPE_timestamp) {
		timestamp *baseptr;
		GENERATE_BAT_INPUT_BASE(timestamp);
		bat_data->count = (size_t) mres->nrows;
		if (bat_data->count) {
			bat_data->data = GDKmalloc(sizeof(bat_data->null_value) * bat_data->count);
			if (!bat_data->data) {
				mdbe->msg = createException(MAL, "monetdbe.monetdbe_result_fetch", MAL_MALLOC_FAIL);
				goto cleanup;
			}
		}

		baseptr = (timestamp *)Tloc(b, 0);
		for (j = 0; j < bat_data->count; j++)
			data_from_timestamp(baseptr[j], bat_data->data + j);
		memcpy(&bat_data->null_value, &mdbe->timestamp_null, sizeof(monetdbe_data_timestamp));
	} else if (bat_type == TYPE_blob) {
		BATiter li;
		BUN p = 0, q = 0;
		GENERATE_BAT_INPUT_BASE(blob);
		bat_data->count = (size_t) mres->nrows;
		if (bat_data->count) {
			bat_data->data = GDKmalloc(sizeof(monetdbe_data_blob) * bat_data->count);
			if (!bat_data->data) {
				mdbe->msg = createException(MAL, "monetdbe.monetdbe_result_fetch", MAL_MALLOC_FAIL);
				goto cleanup;
			}
		}
		j = 0;

		li = bat_iterator(b);
		BATloop(b, p, q)
		{
			blob *t = (blob *)BUNtail(li, p);
			if (t->nitems == ~(size_t)0) {
				bat_data->data[j].size = 0;
				bat_data->data[j].data = NULL;
			} else {
				bat_data->data[j].size = t->nitems;
				bat_data->data[j].data = GDKmalloc(t->nitems);
				if (!bat_data->data[j].data) {
					mdbe->msg = createException(MAL, "monetdbe.monetdbe_result_fetch", MAL_MALLOC_FAIL);
					goto cleanup;
				}
				memcpy(bat_data->data[j].data, t->data, t->nitems);
			}
			j++;
		}
		bat_data->null_value.size = 0;
		bat_data->null_value.data = NULL;
	} else {
		// unsupported type: convert to string
		BATiter li;
		BUN p = 0, q = 0;
		GENERATE_BAT_INPUT_BASE(str);
		bat_data->count = (size_t) mres->nrows;
		if (bat_data->count) {
			bat_data->null_value = NULL;
			bat_data->data = GDKzalloc(sizeof(char *) * bat_data->count);
			if (!bat_data->data) {
				mdbe->msg = createException(MAL, "monetdbe.monetdbe_result_fetch", MAL_MALLOC_FAIL);
				goto cleanup;
			}
		}
		j = 0;

		li = bat_iterator(b);
		BATloop(b, p, q)
		{
			void *t = BUNtail(li, p);
			if (BATatoms[bat_type].atomCmp(t, BATatoms[bat_type].atomNull) == 0) {
				bat_data->data[j] = NULL;
			} else {
				char *sresult = NULL;
				size_t length = 0;
				if (BATatoms[bat_type].atomToStr(&sresult, &length, t, true) == 0) {
					mdbe->msg = createException(MAL, "monetdbe.monetdbe_result_fetch", "Failed to convert element to string");
					goto cleanup;
				}
				bat_data->data[j] = sresult;
			}
			j++;
		}
	}
	if (column_result)
		column_result->name = result->monetdbe_resultset->cols[column_index].name;
cleanup:
	if (b)
		BBPunfix(b->batCacheid);
	if (mdbe->msg) {
		*res = NULL;
		monetdbe_destroy_column(column_result);
	} else {
		result->converted_columns[column_index] = column_result;
		*res = result->converted_columns[column_index];
	}
	mdbe->msg = commit_action(m, mdbe, NULL, NULL);

	return mdbe->msg;
}

static void
data_from_date(date d, monetdbe_data_date *ptr)
{
	ptr->day = date_day(d);
	ptr->month = date_month(d);
	ptr->year = date_year(d);
}

static date
date_from_data(monetdbe_data_date *ptr)
{
	return date_create(ptr->year, ptr->month, ptr->day);
}

static void
data_from_time(daytime d, monetdbe_data_time *ptr)
{
	ptr->hours = daytime_hour(d);
	ptr->minutes = daytime_min(d);
	ptr->seconds = daytime_sec(d);
	ptr->ms = daytime_usec(d) / 1000;
}

static daytime
time_from_data(monetdbe_data_time *ptr)
{
	return daytime_create(ptr->hours, ptr->minutes, ptr->seconds, ptr->ms * 1000);
}

static void
data_from_timestamp(timestamp d, monetdbe_data_timestamp *ptr)
{
	daytime tm = timestamp_daytime(d);
	date dt = timestamp_date(d);

	ptr->date.day = date_day(dt);
	ptr->date.month = date_month(dt);
	ptr->date.year = date_year(dt);
	ptr->time.hours = daytime_hour(tm);
	ptr->time.minutes = daytime_min(tm);
	ptr->time.seconds = daytime_sec(tm);
	ptr->time.ms = daytime_usec(tm) / 1000;
}

static timestamp
timestamp_from_data(monetdbe_data_timestamp *ptr)
{
	return timestamp_create(
		date_create(ptr->date.year, ptr->date.month, ptr->date.day),
		daytime_create(ptr->time.hours, ptr->time.minutes, ptr->time.seconds, ptr->time.ms * 1000));
}

static int
date_is_null(monetdbe_data_date *value)
{
	monetdbe_data_date null_value;
	data_from_date(date_nil, &null_value);
	return value->year == null_value.year && value->month == null_value.month &&
		   value->day == null_value.day;
}

static int
time_is_null(monetdbe_data_time *value)
{
	monetdbe_data_time null_value;
	data_from_time(daytime_nil, &null_value);
	return value->hours == null_value.hours &&
		   value->minutes == null_value.minutes &&
		   value->seconds == null_value.seconds && value->ms == null_value.ms;
}

static int
timestamp_is_null(monetdbe_data_timestamp *value)
{
	return is_timestamp_nil(timestamp_from_data(value));
}

static int
str_is_null(char **value)
{
	return !value || *value == NULL;
}

static int
blob_is_null(monetdbe_data_blob *value)
{
	return !value || value->data == NULL;
}
