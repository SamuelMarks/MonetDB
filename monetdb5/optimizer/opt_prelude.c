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

/*
 * opt_prelude
 * M. Kersten
 * These definitions are handy to have around in the optimizer
 */
#include "monetdb_config.h"
#include "opt_prelude.h"
#include "optimizer_private.h"

/* ! please keep this list sorted for easier maintenance ! */
const char *affectedRowsRef;
const char *aggrRef;
const char *alarmRef;
const char *algebraRef;
const char *alter_add_range_partitionRef;
const char *alter_add_tableRef;
const char *alter_add_value_partitionRef;
const char *alter_del_tableRef;
const char *alter_seqRef;
const char *alter_set_tableRef;
const char *alter_tableRef;
const char *alter_userRef;
const char *appendBulkRef;
const char *appendRef;
const char *assertRef;
const char *avgRef;
const char *bandjoinRef;
const char *batalgebraRef;
const char *batcalcRef;
const char *batcapiRef;
const char *batmalRef;
const char *batmkeyRef;
const char *batmmathRef;
const char *batmtimeRef;
const char *batpyapi3Ref;
const char *batrapiRef;
const char *batRef;
const char *batsqlRef;
const char *batstrRef;
const char *bbpRef;
const char *betweenRef;
const char *binddbatRef;
const char *bindidxRef;
const char *bindRef;
const char *blockRef;
const char *bstreamRef;
const char *calcRef;
const char *capiRef;
const char *claimRef;
const char *clear_tableRef;
const char *columnBindRef;
const char *comment_onRef;
const char *compressRef;
const char *connectRef;
const char *containsRef;
const char *copy_fromRef;
const char *corrRef;
const char *count_no_nilRef;
const char *countRef;
const char *create_functionRef;
const char *create_roleRef;
const char *create_schemaRef;
const char *create_seqRef;
const char *create_tableRef;
const char *create_triggerRef;
const char *create_typeRef;
const char *create_userRef;
const char *create_viewRef;
const char *crossRef;
const char *cume_distRef;
const char *dataflowRef;
const char *dblRef;
const char *decompressRef;
const char *defineRef;
const char *deleteRef;
const char *deltaRef;
const char *dense_rankRef;
const char *dependRef;
const char *deregisterRef;
const char *dictRef;
const char *diffcandRef;
const char *differenceRef;
const char *disconnectRef;
const char *divRef;
const char *drop_constraintRef;
const char *drop_functionRef;
const char *drop_indexRef;
const char *drop_roleRef;
const char *drop_schemaRef;
const char *drop_seqRef;
const char *drop_tableRef;
const char *drop_triggerRef;
const char *drop_typeRef;
const char *drop_userRef;
const char *drop_viewRef;
const char *emptybindidxRef;
const char *emptybindRef;
const char *endswithjoinRef;
const char *eqRef;
const char *evalRef;
const char *execRef;
const char *export_bin_columnRef;
const char *exportOperationRef;
const char *export_tableRef;
const char *fetchRef;
const char *findRef;
const char *firstnRef;
const char *first_valueRef;
const char *forRef;
const char *generatorRef;
const char *getRef;
const char *getTraceRef;
const char *getVariableRef;
const char *grant_functionRef;
const char *grantRef;
const char *grant_rolesRef;
const char *groupbyRef;
const char *groupdoneRef;
const char *groupRef;
const char *growRef;
const char *hgeRef;
const char *identityRef;
const char *ifthenelseRef;
const char *importColumnRef;
const char *intersectcandRef;
const char *intersectRef;
const char *intRef;
const char *ioRef;
const char *iteratorRef;
const char *joinRef;
const char *jsonRef;
const char *lagRef;
const char *languageRef;
const char *last_valueRef;
const char *leadRef;
const char *leftjoinRef;
const char *likejoinRef;
const char *likeRef;
const char *likeselectRef;
const char *lngRef;
const char *lockRef;
const char *lookupRef;
const char *malRef;
const char *manifoldRef;
const char *mapiRef;
const char *markjoinRef;
const char *markselectRef;
const char *maskRef;
const char *matRef;
const char *maxlevenshteinRef;
const char *maxRef;
const char *mdbRef;
const char *mergecandRef;
const char *mergepackRef;
const char *mergetableRef;
const char *minjarowinklerRef;
const char *minRef;
const char *minusRef;
const char *mirrorRef;
const char *mitosisRef;
const char *mmathRef;
const char *modRef;
const char *mtimeRef;
const char *mulRef;
const char *multiplexRef;
const char *mvcRef;
const char *newRef;
const char *nextRef;
const char *not_likeRef;
const char *notRef;
const char *not_uniqueRef;
const char *nth_valueRef;
const char *ntileRef;
const char *outercrossRef;
const char *outerjoinRef;
const char *outerselectRef;
const char *packIncrementRef;
const char *packRef;
const char *parametersRef;
const char *passRef;
const char *percent_rankRef;
const char *plusRef;
const char *predicateRef;
const char *printRef;
const char *prodRef;
const char *profilerRef;
const char *projectdeltaRef;
const char *projectionpathRef;
const char *projectionRef;
const char *projectRef;
const char *putRef;
const char *pyapi3Ref;
const char *querylogRef;
const char *raiseRef;
const char *rangejoinRef;
const char *rankRef;
const char *rapiRef;
const char *reconnectRef;
const char *registerRef;
const char *register_supervisorRef;
const char *remapRef;
const char *remoteRef;
const char *rename_columnRef;
const char *rename_schemaRef;
const char *rename_tableRef;
const char *rename_userRef;
const char *renumberRef;
const char *replaceRef;
const char *resultSetRef;
const char *revoke_functionRef;
const char *revokeRef;
const char *revoke_rolesRef;
const char *row_numberRef;
const char *rpcRef;
const char *rsColumnRef;
const char *rtreeRef;
const char *sampleRef;
const char *selectNotNilRef;
const char *selectRef;
const char *semaRef;
const char *semijoinRef;
const char *seriesRef;
const char *setAccessRef;
const char *set_protocolRef;
const char *setVariableRef;
const char *singleRef;
const char *sliceRef;
const char *sortRef;
const char *sqlcatalogRef;
const char *sqlRef;
const char *startswithjoinRef;
const char *stoptraceRef;
const char *streamsRef;
const char *strimpsRef;
const char *strRef;
const char *subavgRef;
const char *subcountRef;
const char *subdeltaRef;
const char *subeval_aggrRef;
const char *subgroupdoneRef;
const char *subgroupRef;
const char *submaxRef;
const char *subminRef;
const char *subprodRef;
const char *subsliceRef;
const char *subsumRef;
const char *subuniformRef;
const char *sumRef;
const char *takeRef;
const char *thetajoinRef;
const char *thetaselectRef;
const char *tidRef;
const char *transaction_abortRef;
const char *transaction_beginRef;
const char *transaction_commitRef;
const char *transactionRef;
const char *transaction_releaseRef;
const char *transaction_rollbackRef;
const char *umaskRef;
const char *uniqueRef;
const char *unlockRef;
const char *updateRef;
const char *userRef;
const char *window_boundRef;
const char *zero_or_oneRef;
/* ! please keep this list sorted for easier maintenance ! */

void
optimizerInit(void)
{
/* ! please keep this list sorted for easier maintenance ! */
	affectedRowsRef = putName("affectedRows");
	aggrRef = putName("aggr");
	alarmRef = putName("alarm");
	algebraRef = putName("algebra");
	alter_add_range_partitionRef = putName("alter_add_range_partition");
	alter_add_tableRef = putName("alter_add_table");
	alter_add_value_partitionRef = putName("alter_add_value_partition");
	alter_del_tableRef = putName("alter_del_table");
	alter_seqRef = putName("alter_seq");
	alter_set_tableRef = putName("alter_set_table");
	alter_tableRef = putName("alter_table");
	alter_userRef = putName("alter_user");
	appendBulkRef = putName("appendBulk");
	appendRef = putName("append");
	assertRef = putName("assert");
	avgRef = putName("avg");
	bandjoinRef = putName("bandjoin");
	batalgebraRef = putName("batalgebra");
	batcalcRef = putName("batcalc");
	batcapiRef = putName("batcapi");
	batmalRef = putName("batmal");
	batmkeyRef = putName("batmkey");
	batmmathRef = putName("batmmath");
	batmtimeRef = putName("batmtime");
	batpyapi3Ref = putName("batpyapi3");
	batrapiRef = putName("batrapi");
	batRef = putName("bat");
	batsqlRef = putName("batsql");
	batstrRef = putName("batstr");
	bbpRef = putName("bbp");
	betweenRef = putName("between");
	binddbatRef = putName("bind_dbat");
	bindidxRef = putName("bind_idxbat");
	bindRef = putName("bind");
	blockRef = putName("block");
	bstreamRef = putName("bstream");
	calcRef = putName("calc");
	capiRef = putName("capi");
	claimRef = putName("claim");
	clear_tableRef = putName("clear_table");
	columnBindRef = putName("columnBind");
	comment_onRef = putName("comment_on");
	compressRef = putName("compress");
	connectRef = putName("connect");
	containsRef = putName("contains");
	copy_fromRef = putName("copy_from");
	corrRef = putName("corr");
	count_no_nilRef = putName("count_no_nil");
	countRef = putName("count");
	create_functionRef = putName("create_function");
	create_roleRef = putName("create_role");
	create_schemaRef = putName("create_schema");
	create_seqRef = putName("create_seq");
	create_tableRef = putName("create_table");
	create_triggerRef = putName("create_trigger");
	create_typeRef = putName("create_type");
	create_userRef = putName("create_user");
	create_viewRef = putName("create_view");
	crossRef = putName("crossproduct");
	cume_distRef = putName("cume_dist");
	dataflowRef = putName("dataflow");
	dblRef = putName("dbl");
	decompressRef = putName("decompress");
	defineRef = putName("define");
	deleteRef = putName("delete");
	deltaRef = putName("delta");
	dense_rankRef = putName("dense_rank");
	dependRef = putName("depend");
	deregisterRef = putName("deregister");
	dictRef = putName("dict");
	diffcandRef = putName("diffcand");
	differenceRef = putName("difference");
	disconnectRef = putName("disconnect");
	divRef = putName("/");
	drop_constraintRef = putName("drop_constraint");
	drop_functionRef = putName("drop_function");
	drop_indexRef = putName("drop_index");
	drop_roleRef = putName("drop_role");
	drop_schemaRef = putName("drop_schema");
	drop_seqRef = putName("drop_seq");
	drop_tableRef = putName("drop_table");
	drop_triggerRef = putName("drop_trigger");
	drop_typeRef = putName("drop_type");
	drop_userRef = putName("drop_user");
	drop_viewRef = putName("drop_view");
	emptybindidxRef = putName("emptybindidx");
	emptybindRef = putName("emptybind");
	endswithjoinRef = putName("endswithjoin");
	eqRef = putName("==");
	evalRef = putName("eval");
	execRef = putName("exec");
	export_bin_columnRef = "export_bin_column";
	exportOperationRef = putName("exportOperation");
	export_tableRef = putName("export_table");
	fetchRef = putName("fetch");
	findRef = putName("find");
	firstnRef = putName("firstn");
	first_valueRef = putName("first_value");
	forRef = putName("for");
	generatorRef = putName("generator");
	getRef = putName("get");
	getTraceRef = putName("getTrace");
	getVariableRef = putName("getVariable");
	grant_functionRef = putName("grant_function");
	grantRef = putName("grant");
	grant_rolesRef = putName("grant_roles");
	groupbyRef = putName("groupby");
	groupdoneRef = putName("groupdone");
	groupRef = putName("group");
	growRef = putName("grow");
	hgeRef = putName("hge");
	identityRef = putName("identity");
	ifthenelseRef = putName("ifthenelse");
	importColumnRef = putName("importColumn");
	intersectcandRef = putName("intersectcand");
	intersectRef = putName("intersect");
	intRef = putName("int");
	ioRef = putName("io");
	iteratorRef = putName("iterator");
	joinRef = putName("join");
	jsonRef = putName("json");
	lagRef = putName("lag");
	languageRef = putName("language");
	last_valueRef = putName("last_value");
	leadRef = putName("lead");
	leftjoinRef = putName("leftjoin");
	likejoinRef = putName("likejoin");
	likeRef = putName("like");
	likeselectRef = putName("likeselect");
	lngRef = putName("lng");
	lockRef = putName("lock");
	lookupRef = putName("lookup");
	malRef = putName("mal");
	manifoldRef = putName("manifold");
	mapiRef = putName("mapi");
	markjoinRef = putName("markjoin");
	markselectRef = putName("markselect");
	maskRef = putName("mask");
	matRef = putName("mat");
	maxlevenshteinRef = putName("maxlevenshtein");
	maxRef = putName("max");
	mdbRef = putName("mdb");
	mergecandRef = putName("mergecand");
	mergepackRef = putName("mergepack");
	mergetableRef = putName("mergetable");
	minjarowinklerRef = putName("minjarowinkler");
	minRef = putName("min");
	minusRef = putName("-");
	mirrorRef = putName("mirror");
	mitosisRef = putName("mitosis");
	mmathRef = putName("mmath");
	modRef = putName("%");
	mtimeRef = putName("mtime");
	mulRef = putName("*");
	multiplexRef = putName("multiplex");
	mvcRef = putName("mvc");
	newRef = putName("new");
	nextRef = putName("next");
	not_likeRef = putName("not_like");
	notRef = putName("not");
	not_uniqueRef = putName("not_unique");
	nth_valueRef = putName("nth_value");
	ntileRef = putName("ntile");
	outercrossRef = putName("outercrossproduct");
	outerjoinRef = putName("outerjoin");
	outerselectRef = putName("outerselect");
	packIncrementRef = putName("packIncrement");
	packRef = putName("pack");
	parametersRef = putName("parameters");
	passRef = putName("pass");
	percent_rankRef = putName("percent_rank");
	plusRef = putName("+");
	predicateRef = putName("predicate");
	printRef = putName("print");
	prodRef = putName("prod");
	profilerRef = putName("profiler");
	projectdeltaRef = putName("projectdelta");
	projectionpathRef = putName("projectionpath");
	projectionRef = putName("projection");
	projectRef = putName("project");
	putRef = putName("put");
	pyapi3Ref = putName("pyapi3");
	querylogRef = putName("querylog");
	raiseRef = putName("raise");
	rangejoinRef = putName("rangejoin");
	rankRef = putName("rank");
	rapiRef = putName("rapi");
	reconnectRef = putName("reconnect");
	registerRef = putName("register");
	register_supervisorRef = putName("register_supervisor");
	remapRef = putName("remap");
	remoteRef = putName("remote");
	rename_columnRef = putName("rename_column");
	rename_schemaRef = putName("rename_schema");
	rename_tableRef = putName("rename_table");
	rename_userRef = putName("rename_user");
	renumberRef = putName("renumber");
	replaceRef = putName("replace");
	resultSetRef = putName("resultSet");
	revoke_functionRef = putName("revoke_function");
	revokeRef = putName("revoke");
	revoke_rolesRef = putName("revoke_roles");
	row_numberRef = putName("row_number");
	rpcRef = putName("rpc");
	rsColumnRef = putName("rsColumn");
	rtreeRef = putName("rtree");
	sampleRef = putName("sample");
	selectNotNilRef = putName("selectNotNil");
	selectRef = putName("select");
	semaRef = putName("sema");
	semijoinRef = putName("semijoin");
	seriesRef = putName("series");
	setAccessRef = putName("setAccess");
	set_protocolRef = putName("set_protocol");
	setVariableRef = putName("setVariable");
	singleRef = putName("single");
	sliceRef = putName("slice");
	sortRef = putName("sort");
	sqlcatalogRef = putName("sqlcatalog");
	sqlRef = putName("sql");
	startswithjoinRef = putName("startswithjoin");
	stoptraceRef = putName("stoptrace");
	streamsRef = putName("streams");
	strimpsRef = putName("strimps");
	strRef = putName("str");
	subavgRef = putName("subavg");
	subcountRef = putName("subcount");
	subdeltaRef = putName("subdelta");
	subeval_aggrRef = putName("subeval_aggr");
	subgroupdoneRef = putName("subgroupdone");
	subgroupRef = putName("subgroup");
	submaxRef = putName("submax");
	subminRef = putName("submin");
	subprodRef = putName("subprod");
	subsliceRef = putName("subslice");
	subsumRef = putName("subsum");
	subuniformRef = putName("subuniform");
	sumRef = putName("sum");
	takeRef = putName("take");
	thetajoinRef = putName("thetajoin");
	thetaselectRef = putName("thetaselect");
	tidRef = putName("tid");
	transaction_abortRef = putName("transaction_abort");
	transaction_beginRef = putName("transaction_begin");
	transaction_commitRef = putName("transaction_commit");
	transactionRef = putName("transaction");
	transaction_releaseRef = putName("transaction_release");
	transaction_rollbackRef = putName("transaction_rollback");
	umaskRef = putName("umask");
	uniqueRef = putName("unique");
	unlockRef = putName("unlock");
	updateRef = putName("update");
	userRef = putName("user");
	window_boundRef = putName("window_bound");
	zero_or_oneRef = putName("zero_or_one");
/* ! please keep this list sorted for easier maintenance ! */
}
