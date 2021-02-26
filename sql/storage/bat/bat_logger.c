/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0.  If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright 1997 - July 2008 CWI, August 2008 - 2021 MonetDB B.V.
 */

#include "monetdb_config.h"
#include "bat_logger.h"
#include "bat_utils.h"
#include "sql_types.h" /* EC_POS */
#include "wlc.h"
#include "gdk_logger_internals.h"
#include "mutils.h"

#define CATALOG_JUN2020 52204	/* first in Jun2020 */
#define CATALOG_OCT2020 52205	/* first in Oct2020 */

/* Note, CATALOG version 52300 is the first one where the basic system
 * tables (the ones created in store.c) have fixed and unchangeable
 * ids. */

/* return GDK_SUCCEED if we can handle the upgrade from oldversion to
 * newversion */
static gdk_return
bl_preversion(sqlstore *store, int oldversion, int newversion)
{
	(void)newversion;

/* disable upgrades for now */
	if (oldversion < 52300)
		return GDK_FAIL;

#ifdef CATALOG_JUN2020
	if (oldversion == CATALOG_JUN2020) {
		/* upgrade to default releases */
		store->catalog_version = oldversion;
		return GDK_SUCCEED;
	}
#endif

#ifdef CATALOG_OCT2020
	if (oldversion == CATALOG_OCT2020) {
		/* upgrade to default releases */
		store->catalog_version = oldversion;
		return GDK_SUCCEED;
	}
#endif

	return GDK_FAIL;
}

#define N(schema, table, column)	schema "_" table "_" column

#define D(schema, table)	"D_" schema "_" table

#if defined CATALOG_JUN2020 || defined CATALOG_OCT2020
static gdk_return
tabins(logger *lg, int tt, int nid, ...)
{
	va_list va;
	int cid;
	const void *cval;
	gdk_return rc;
	BAT *b;

	va_start(va, nid);
	while ((cid = va_arg(va, int)) != 0) {
		cval = va_arg(va, void *);
		if ((b = temp_descriptor(logger_find_bat(lg, cid))) == NULL) {
			va_end(va);
			return GDK_FAIL;
		}
		rc = BUNappend(b, cval, true);
		bat_destroy(b);
		if (rc != GDK_SUCCEED) {
			va_end(va);
			return rc;
		}
	}
	va_end(va);

	if (tt >= 0) {
		if ((b = COLnew(0, tt, 0, PERSISTENT)) == NULL)
			return GDK_FAIL;
		rc = log_bat_persists(lg, b, nid);
		bat_destroy(b);
		if (rc != GDK_SUCCEED)
			return rc;
	}
	return GDK_SUCCEED;
}
#endif

struct table {
	const char *schema;
	const char *table;
	const char *column;
	const char *fullname;
	int newid;
	bool hasids;
} tables[] = {
	{
		.schema = "sys",
		.newid = 2000,
	},
	{
		.schema = "sys",
		.table = "schemas",
		.fullname = "D_sys_schemas",
		.newid = 2001,
	},
	{
		.schema = "sys",
		.table = "schemas",
		.column = "id",
		.fullname = "sys_schemas_id",
		.newid = 2002,
		.hasids = true,
	},
	{
		.schema = "sys",
		.table = "schemas",
		.column = "name",
		.fullname = "sys_schemas_name",
		.newid = 2003,
	},
	{
		.schema = "sys",
		.table = "schemas",
		.column = "authorization",
		.fullname = "sys_schemas_authorization",
		.newid = 2004,
	},
	{
		.schema = "sys",
		.table = "schemas",
		.column = "owner",
		.fullname = "sys_schemas_owner",
		.newid = 2005,
	},
	{
		.schema = "sys",
		.table = "schemas",
		.column = "system",
		.fullname = "sys_schemas_system",
		.newid = 2006,
	},
	{
		.schema = "sys",
		.table = "types",
		.fullname = "D_sys_types",
		.newid = 2007,
	},
	{
		.schema = "sys",
		.table = "types",
		.column = "id",
		.fullname = "sys_types_id",
		.newid = 2008,
	},
	{
		.schema = "sys",
		.table = "types",
		.column = "systemname",
		.fullname = "sys_types_systemname",
		.newid = 2009,
	},
	{
		.schema = "sys",
		.table = "types",
		.column = "sqlname",
		.fullname = "sys_types_sqlname",
		.newid = 2010,
	},
	{
		.schema = "sys",
		.table = "types",
		.column = "digits",
		.fullname = "sys_types_digits",
		.newid = 2011,
	},
	{
		.schema = "sys",
		.table = "types",
		.column = "scale",
		.fullname = "sys_types_scale",
		.newid = 2012,
	},
	{
		.schema = "sys",
		.table = "types",
		.column = "radix",
		.fullname = "sys_types_radix",
		.newid = 2013,
	},
	{
		.schema = "sys",
		.table = "types",
		.column = "eclass",
		.fullname = "sys_types_eclass",
		.newid = 2014,
	},
	{
		.schema = "sys",
		.table = "types",
		.column = "schema_id",
		.fullname = "sys_types_schema_id",
		.newid = 2015,
		.hasids = true,
	},
	{
		.schema = "sys",
		.table = "functions",
		.fullname = "D_sys_functions",
		.newid = 2016,
	},
	{
		.schema = "sys",
		.table = "functions",
		.column = "id",
		.fullname = "sys_functions_id",
		.newid = 2017,
	},
	{
		.schema = "sys",
		.table = "functions",
		.column = "name",
		.fullname = "sys_functions_name",
		.newid = 2018,
	},
	{
		.schema = "sys",
		.table = "functions",
		.column = "func",
		.fullname = "sys_functions_func",
		.newid = 2019,
	},
	{
		.schema = "sys",
		.table = "functions",
		.column = "mod",
		.fullname = "sys_functions_mod",
		.newid = 2020,
	},
	{
		.schema = "sys",
		.table = "functions",
		.column = "language",
		.fullname = "sys_functions_language",
		.newid = 2021,
	},
	{
		.schema = "sys",
		.table = "functions",
		.column = "type",
		.fullname = "sys_functions_type",
		.newid = 2022,
	},
	{
		.schema = "sys",
		.table = "functions",
		.column = "side_effect",
		.fullname = "sys_functions_side_effect",
		.newid = 2023,
	},
	{
		.schema = "sys",
		.table = "functions",
		.column = "varres",
		.fullname = "sys_functions_varres",
		.newid = 2024,
	},
	{
		.schema = "sys",
		.table = "functions",
		.column = "vararg",
		.fullname = "sys_functions_vararg",
		.newid = 2025,
	},
	{
		.schema = "sys",
		.table = "functions",
		.column = "schema_id",
		.fullname = "sys_functions_schema_id",
		.newid = 2026,
		.hasids = true,
	},
	{
		.schema = "sys",
		.table = "functions",
		.column = "system",
		.fullname = "sys_functions_system",
		.newid = 2027,
	},
	{
		.schema = "sys",
		.table = "functions",
		.column = "semantics",
		.fullname = "sys_functions_semantics",
		.newid = 2162,
	},
	{
		.schema = "sys",
		.table = "args",
		.fullname = "D_sys_args",
		.newid = 2028,
	},
	{
		.schema = "sys",
		.table = "args",
		.column = "id",
		.fullname = "sys_args_id",
		.newid = 2029,
	},
	{
		.schema = "sys",
		.table = "args",
		.column = "func_id",
		.fullname = "sys_args_func_id",
		.newid = 2030,
	},
	{
		.schema = "sys",
		.table = "args",
		.column = "name",
		.fullname = "sys_args_name",
		.newid = 2031,
	},
	{
		.schema = "sys",
		.table = "args",
		.column = "type",
		.fullname = "sys_args_type",
		.newid = 2032,
	},
	{
		.schema = "sys",
		.table = "args",
		.column = "type_digits",
		.fullname = "sys_args_type_digits",
		.newid = 2033,
	},
	{
		.schema = "sys",
		.table = "args",
		.column = "type_scale",
		.fullname = "sys_args_type_scale",
		.newid = 2034,
	},
	{
		.schema = "sys",
		.table = "args",
		.column = "inout",
		.fullname = "sys_args_inout",
		.newid = 2035,
	},
	{
		.schema = "sys",
		.table = "args",
		.column = "number",
		.fullname = "sys_args_number",
		.newid = 2036,
	},
	{
		.schema = "sys",
		.table = "sequences",
		.fullname = "D_sys_sequences",
		.newid = 2037,
	},
	{
		.schema = "sys",
		.table = "sequences",
		.column = "id",
		.fullname = "sys_sequences_id",
		.newid = 2038,
	},
	{
		.schema = "sys",
		.table = "sequences",
		.column = "schema_id",
		.fullname = "sys_sequences_schema_id",
		.newid = 2039,
		.hasids = true,
	},
	{
		.schema = "sys",
		.table = "sequences",
		.column = "name",
		.fullname = "sys_sequences_name",
		.newid = 2040,
	},
	{
		.schema = "sys",
		.table = "sequences",
		.column = "start",
		.fullname = "sys_sequences_start",
		.newid = 2041,
	},
	{
		.schema = "sys",
		.table = "sequences",
		.column = "minvalue",
		.fullname = "sys_sequences_minvalue",
		.newid = 2042,
	},
	{
		.schema = "sys",
		.table = "sequences",
		.column = "maxvalue",
		.fullname = "sys_sequences_maxvalue",
		.newid = 2043,
	},
	{
		.schema = "sys",
		.table = "sequences",
		.column = "increment",
		.fullname = "sys_sequences_increment",
		.newid = 2044,
	},
	{
		.schema = "sys",
		.table = "sequences",
		.column = "cacheinc",
		.fullname = "sys_sequences_cacheinc",
		.newid = 2045,
	},
	{
		.schema = "sys",
		.table = "sequences",
		.column = "cycle",
		.fullname = "sys_sequences_cycle",
		.newid = 2046,
	},
	{
		.schema = "sys",
		.table = "table_partitions",
		.fullname = "D_sys_table_partitions",
		.newid = 2047,
	},
	{
		.schema = "sys",
		.table = "table_partitions",
		.column = "id",
		.fullname = "sys_table_partitions_id",
		.newid = 2048,
	},
	{
		.schema = "sys",
		.table = "table_partitions",
		.column = "table_id",
		.fullname = "sys_table_partitions_table_id",
		.newid = 2049,
		.hasids = true,
	},
	{
		.schema = "sys",
		.table = "table_partitions",
		.column = "column_id",
		.fullname = "sys_table_partitions_column_id",
		.newid = 2050,
		.hasids = true,
	},
	{
		.schema = "sys",
		.table = "table_partitions",
		.column = "expression",
		.fullname = "sys_table_partitions_expression",
		.newid = 2051,
	},
	{
		.schema = "sys",
		.table = "table_partitions",
		.column = "type",
		.fullname = "sys_table_partitions_type",
		.newid = 2052,
	},
	{
		.schema = "sys",
		.table = "range_partitions",
		.fullname = "D_sys_range_partitions",
		.newid = 2053,
	},
	{
		.schema = "sys",
		.table = "range_partitions",
		.column = "table_id",
		.fullname = "sys_range_partitions_table_id",
		.newid = 2054,
		.hasids = true,
	},
	{
		.schema = "sys",
		.table = "range_partitions",
		.column = "partition_id",
		.fullname = "sys_range_partitions_partition_id",
		.newid = 2055,
	},
	{
		.schema = "sys",
		.table = "range_partitions",
		.column = "minimum",
		.fullname = "sys_range_partitions_minimum",
		.newid = 2056,
	},
	{
		.schema = "sys",
		.table = "range_partitions",
		.column = "maximum",
		.fullname = "sys_range_partitions_maximum",
		.newid = 2057,
	},
	{
		.schema = "sys",
		.table = "range_partitions",
		.column = "with_nulls",
		.fullname = "sys_range_partitions_with_nulls",
		.newid = 2058,
	},
	{
		.schema = "sys",
		.table = "value_partitions",
		.fullname = "D_sys_value_partitions",
		.newid = 2059,
	},
	{
		.schema = "sys",
		.table = "value_partitions",
		.column = "table_id",
		.fullname = "sys_value_partitions_table_id",
		.newid = 2060,
		.hasids = true,
	},
	{
		.schema = "sys",
		.table = "value_partitions",
		.column = "partition_id",
		.fullname = "sys_value_partitions_partition_id",
		.newid = 2061,
	},
	{
		.schema = "sys",
		.table = "value_partitions",
		.column = "value",
		.fullname = "sys_value_partitions_value",
		.newid = 2062,
	},
	{
		.schema = "sys",
		.table = "dependencies",
		.fullname = "D_sys_dependencies",
		.newid = 2063,
	},
	{
		.schema = "sys",
		.table = "dependencies",
		.column = "id",
		.fullname = "sys_dependencies_id",
		.newid = 2064,
		.hasids = true,
	},
	{
		.schema = "sys",
		.table = "dependencies",
		.column = "depend_id",
		.fullname = "sys_dependencies_depend_id",
		.newid = 2065,
		.hasids = true,
	},
	{
		.schema = "sys",
		.table = "dependencies",
		.column = "depend_type",
		.fullname = "sys_dependencies_depend_type",
		.newid = 2066,
	},
	{
		.schema = "sys",
		.table = "_tables",
		.fullname = "D_sys__tables",
		.newid = 2067,
	},
	{
		.schema = "sys",
		.table = "_tables",
		.column = "id",
		.fullname = "sys__tables_id",
		.newid = 2068,
		.hasids = true,
	},
	{
		.schema = "sys",
		.table = "_tables",
		.column = "name",
		.fullname = "sys__tables_name",
		.newid = 2069,
	},
	{
		.schema = "sys",
		.table = "_tables",
		.column = "schema_id",
		.fullname = "sys__tables_schema_id",
		.newid = 2070,
		.hasids = true,
	},
	{
		.schema = "sys",
		.table = "_tables",
		.column = "query",
		.fullname = "sys__tables_query",
		.newid = 2071,
	},
	{
		.schema = "sys",
		.table = "_tables",
		.column = "type",
		.fullname = "sys__tables_type",
		.newid = 2072,
	},
	{
		.schema = "sys",
		.table = "_tables",
		.column = "system",
		.fullname = "sys__tables_system",
		.newid = 2073,
	},
	{
		.schema = "sys",
		.table = "_tables",
		.column = "commit_action",
		.fullname = "sys__tables_commit_action",
		.newid = 2074,
	},
	{
		.schema = "sys",
		.table = "_tables",
		.column = "access",
		.fullname = "sys__tables_access",
		.newid = 2075,
	},
	{
		.schema = "sys",
		.table = "_columns",
		.fullname = "D_sys__columns",
		.newid = 2076,
	},
	{
		.schema = "sys",
		.table = "_columns",
		.column = "id",
		.fullname = "sys__columns_id",
		.newid = 2077,
		.hasids = true,
	},
	{
		.schema = "sys",
		.table = "_columns",
		.column = "name",
		.fullname = "sys__columns_name",
		.newid = 2078,
	},
	{
		.schema = "sys",
		.table = "_columns",
		.column = "type",
		.fullname = "sys__columns_type",
		.newid = 2079,
	},
	{
		.schema = "sys",
		.table = "_columns",
		.column = "type_digits",
		.fullname = "sys__columns_type_digits",
		.newid = 2080,
	},
	{
		.schema = "sys",
		.table = "_columns",
		.column = "type_scale",
		.fullname = "sys__columns_type_scale",
		.newid = 2081,
	},
	{
		.schema = "sys",
		.table = "_columns",
		.column = "table_id",
		.fullname = "sys__columns_table_id",
		.newid = 2082,
		.hasids = true,
	},
	{
		.schema = "sys",
		.table = "_columns",
		.column = "default",
		.fullname = "sys__columns_default",
		.newid = 2083,
	},
	{
		.schema = "sys",
		.table = "_columns",
		.column = "null",
		.fullname = "sys__columns_null",
		.newid = 2084,
	},
	{
		.schema = "sys",
		.table = "_columns",
		.column = "number",
		.fullname = "sys__columns_number",
		.newid = 2085,
	},
	{
		.schema = "sys",
		.table = "_columns",
		.column = "storage",
		.fullname = "sys__columns_storage",
		.newid = 2086,
	},
	{
		.schema = "sys",
		.table = "keys",
		.fullname = "D_sys_keys",
		.newid = 2087,
	},
	{
		.schema = "sys",
		.table = "keys",
		.column = "id",
		.fullname = "sys_keys_id",
		.newid = 2088,
	},
	{
		.schema = "sys",
		.table = "keys",
		.column = "table_id",
		.fullname = "sys_keys_table_id",
		.newid = 2089,
		.hasids = true,
	},
	{
		.schema = "sys",
		.table = "keys",
		.column = "type",
		.fullname = "sys_keys_type",
		.newid = 2090,
	},
	{
		.schema = "sys",
		.table = "keys",
		.column = "name",
		.fullname = "sys_keys_name",
		.newid = 2091,
	},
	{
		.schema = "sys",
		.table = "keys",
		.column = "rkey",
		.fullname = "sys_keys_rkey",
		.newid = 2092,
	},
	{
		.schema = "sys",
		.table = "keys",
		.column = "action",
		.fullname = "sys_keys_action",
		.newid = 2093,
	},
	{
		.schema = "sys",
		.table = "idxs",
		.fullname = "D_sys_idxs",
		.newid = 2094,
	},
	{
		.schema = "sys",
		.table = "idxs",
		.column = "id",
		.fullname = "sys_idxs_id",
		.newid = 2095,
	},
	{
		.schema = "sys",
		.table = "idxs",
		.column = "table_id",
		.fullname = "sys_idxs_table_id",
		.newid = 2096,
		.hasids = true,
	},
	{
		.schema = "sys",
		.table = "idxs",
		.column = "type",
		.fullname = "sys_idxs_type",
		.newid = 2097,
	},
	{
		.schema = "sys",
		.table = "idxs",
		.column = "name",
		.fullname = "sys_idxs_name",
		.newid = 2098,
	},
	{
		.schema = "sys",
		.table = "triggers",
		.fullname = "D_sys_triggers",
		.newid = 2099,
	},
	{
		.schema = "sys",
		.table = "triggers",
		.column = "id",
		.fullname = "sys_triggers_id",
		.newid = 2100,
	},
	{
		.schema = "sys",
		.table = "triggers",
		.column = "name",
		.fullname = "sys_triggers_name",
		.newid = 2101,
	},
	{
		.schema = "sys",
		.table = "triggers",
		.column = "table_id",
		.fullname = "sys_triggers_table_id",
		.newid = 2102,
		.hasids = true,
	},
	{
		.schema = "sys",
		.table = "triggers",
		.column = "time",
		.fullname = "sys_triggers_time",
		.newid = 2103,
	},
	{
		.schema = "sys",
		.table = "triggers",
		.column = "orientation",
		.fullname = "sys_triggers_orientation",
		.newid = 2104,
	},
	{
		.schema = "sys",
		.table = "triggers",
		.column = "event",
		.fullname = "sys_triggers_event",
		.newid = 2105,
	},
	{
		.schema = "sys",
		.table = "triggers",
		.column = "old_name",
		.fullname = "sys_triggers_old_name",
		.newid = 2106,
	},
	{
		.schema = "sys",
		.table = "triggers",
		.column = "new_name",
		.fullname = "sys_triggers_new_name",
		.newid = 2107,
	},
	{
		.schema = "sys",
		.table = "triggers",
		.column = "condition",
		.fullname = "sys_triggers_condition",
		.newid = 2108,
	},
	{
		.schema = "sys",
		.table = "triggers",
		.column = "statement",
		.fullname = "sys_triggers_statement",
		.newid = 2109,
	},
	{
		.schema = "sys",
		.table = "objects",
		.fullname = "D_sys_objects",
		.newid = 2110,
	},
	{
		.schema = "sys",
		.table = "objects",
		.column = "id",
		.fullname = "sys_objects_id",
		.newid = 2111,
	},
	{
		.schema = "sys",
		.table = "objects",
		.column = "name",
		.fullname = "sys_objects_name",
		.newid = 2112,
	},
	{
		.schema = "sys",
		.table = "objects",
		.column = "nr",
		.fullname = "sys_objects_nr",
		.newid = 2113,
	},
	{
		.schema = "sys",
		.table = "objects",
		.column = "sub",
		.fullname = "sys_objects_sub",
		.newid = 2163,
	},
	{
		.schema = "tmp",
		.newid = 2114,
	},
	{
		.schema = "tmp",
		.table = "_tables",
		.fullname = "D_tmp__tables",
		.newid = 2115,
	},
	{
		.schema = "tmp",
		.table = "_tables",
		.column = "id",
		.fullname = "tmp__tables_id",
		.newid = 2116,
	},
	{
		.schema = "tmp",
		.table = "_tables",
		.column = "name",
		.fullname = "tmp__tables_name",
		.newid = 2117,
	},
	{
		.schema = "tmp",
		.table = "_tables",
		.column = "schema_id",
		.fullname = "tmp__tables_schema_id",
		.newid = 2118,
	},
	{
		.schema = "tmp",
		.table = "_tables",
		.column = "query",
		.fullname = "tmp__tables_query",
		.newid = 2119,
	},
	{
		.schema = "tmp",
		.table = "_tables",
		.column = "type",
		.fullname = "tmp__tables_type",
		.newid = 2120,
	},
	{
		.schema = "tmp",
		.table = "_tables",
		.column = "system",
		.fullname = "tmp__tables_system",
		.newid = 2121,
	},
	{
		.schema = "tmp",
		.table = "_tables",
		.column = "commit_action",
		.fullname = "tmp__tables_commit_action",
		.newid = 2122,
	},
	{
		.schema = "tmp",
		.table = "_tables",
		.column = "access",
		.fullname = "tmp__tables_access",
		.newid = 2123,
	},
	{
		.schema = "tmp",
		.table = "_columns",
		.fullname = "D_tmp__columns",
		.newid = 2124,
	},
	{
		.schema = "tmp",
		.table = "_columns",
		.column = "id",
		.fullname = "tmp__columns_id",
		.newid = 2125,
	},
	{
		.schema = "tmp",
		.table = "_columns",
		.column = "name",
		.fullname = "tmp__columns_name",
		.newid = 2126,
	},
	{
		.schema = "tmp",
		.table = "_columns",
		.column = "type",
		.fullname = "tmp__columns_type",
		.newid = 2127,
	},
	{
		.schema = "tmp",
		.table = "_columns",
		.column = "type_digits",
		.fullname = "tmp__columns_type_digits",
		.newid = 2128,
	},
	{
		.schema = "tmp",
		.table = "_columns",
		.column = "type_scale",
		.fullname = "tmp__columns_type_scale",
		.newid = 2129,
	},
	{
		.schema = "tmp",
		.table = "_columns",
		.column = "table_id",
		.fullname = "tmp__columns_table_id",
		.newid = 2130,
	},
	{
		.schema = "tmp",
		.table = "_columns",
		.column = "default",
		.fullname = "tmp__columns_default",
		.newid = 2131,
	},
	{
		.schema = "tmp",
		.table = "_columns",
		.column = "null",
		.fullname = "tmp__columns_null",
		.newid = 2132,
	},
	{
		.schema = "tmp",
		.table = "_columns",
		.column = "number",
		.fullname = "tmp__columns_number",
		.newid = 2133,
	},
	{
		.schema = "tmp",
		.table = "_columns",
		.column = "storage",
		.fullname = "tmp__columns_storage",
		.newid = 2134,
	},
	{
		.schema = "tmp",
		.table = "keys",
		.fullname = "D_tmp_keys",
		.newid = 2135,
	},
	{
		.schema = "tmp",
		.table = "keys",
		.column = "id",
		.fullname = "tmp_keys_id",
		.newid = 2136,
	},
	{
		.schema = "tmp",
		.table = "keys",
		.column = "table_id",
		.fullname = "tmp_keys_table_id",
		.newid = 2137,
	},
	{
		.schema = "tmp",
		.table = "keys",
		.column = "type",
		.fullname = "tmp_keys_type",
		.newid = 2138,
	},
	{
		.schema = "tmp",
		.table = "keys",
		.column = "name",
		.fullname = "tmp_keys_name",
		.newid = 2139,
	},
	{
		.schema = "tmp",
		.table = "keys",
		.column = "rkey",
		.fullname = "tmp_keys_rkey",
		.newid = 2140,
	},
	{
		.schema = "tmp",
		.table = "keys",
		.column = "action",
		.fullname = "tmp_keys_action",
		.newid = 2141,
	},
	{
		.schema = "tmp",
		.table = "idxs",
		.fullname = "D_tmp_idxs",
		.newid = 2142,
	},
	{
		.schema = "tmp",
		.table = "idxs",
		.column = "id",
		.fullname = "tmp_idxs_id",
		.newid = 2143,
	},
	{
		.schema = "tmp",
		.table = "idxs",
		.column = "table_id",
		.fullname = "tmp_idxs_table_id",
		.newid = 2144,
	},
	{
		.schema = "tmp",
		.table = "idxs",
		.column = "type",
		.fullname = "tmp_idxs_type",
		.newid = 2145,
	},
	{
		.schema = "tmp",
		.table = "idxs",
		.column = "name",
		.fullname = "tmp_idxs_name",
		.newid = 2146,
	},
	{
		.schema = "tmp",
		.table = "triggers",
		.fullname = "D_tmp_triggers",
		.newid = 2147,
	},
	{
		.schema = "tmp",
		.table = "triggers",
		.column = "id",
		.fullname = "tmp_triggers_id",
		.newid = 2148,
	},
	{
		.schema = "tmp",
		.table = "triggers",
		.column = "name",
		.fullname = "tmp_triggers_name",
		.newid = 2149,
	},
	{
		.schema = "tmp",
		.table = "triggers",
		.column = "table_id",
		.fullname = "tmp_triggers_table_id",
		.newid = 2150,
	},
	{
		.schema = "tmp",
		.table = "triggers",
		.column = "time",
		.fullname = "tmp_triggers_time",
		.newid = 2151,
	},
	{
		.schema = "tmp",
		.table = "triggers",
		.column = "orientation",
		.fullname = "tmp_triggers_orientation",
		.newid = 2152,
	},
	{
		.schema = "tmp",
		.table = "triggers",
		.column = "event",
		.fullname = "tmp_triggers_event",
		.newid = 2153,
	},
	{
		.schema = "tmp",
		.table = "triggers",
		.column = "old_name",
		.fullname = "tmp_triggers_old_name",
		.newid = 2154,
	},
	{
		.schema = "tmp",
		.table = "triggers",
		.column = "new_name",
		.fullname = "tmp_triggers_new_name",
		.newid = 2155,
	},
	{
		.schema = "tmp",
		.table = "triggers",
		.column = "condition",
		.fullname = "tmp_triggers_condition",
		.newid = 2156,
	},
	{
		.schema = "tmp",
		.table = "triggers",
		.column = "statement",
		.fullname = "tmp_triggers_statement",
		.newid = 2157,
	},
	{
		.schema = "tmp",
		.table = "objects",
		.fullname = "D_tmp_objects",
		.newid = 2158,
	},
	{
		.schema = "tmp",
		.table = "objects",
		.column = "id",
		.fullname = "tmp_objects_id",
		.newid = 2159,
	},
	{
		.schema = "tmp",
		.table = "objects",
		.column = "name",
		.fullname = "tmp_objects_name",
		.newid = 2160,
	},
	{
		.schema = "tmp",
		.table = "objects",
		.column = "nr",
		.fullname = "tmp_objects_nr",
		.newid = 2161,
	},
	{
		.schema = "tmp",
		.table = "objects",
		.column = "sub",
		.fullname = "tmp_objects_sub",
		.newid = 2164,
	},
	{0}
};

static gdk_return
upgrade(old_logger *lg)
{
	gdk_return rc = GDK_FAIL;
	struct bats {
		BAT *nmbat;
		BAT *idbat;
		BAT *parbat;
		BAT *cands;
	} bats[3];
	BAT *mapold = COLnew(0, TYPE_int, 256, TRANSIENT);
	BAT *mapnew = COLnew(0, TYPE_int, 256, TRANSIENT);

	bats[0].nmbat = temp_descriptor(old_logger_find_bat(lg, "sys_schemas_name", 0, 0));
	bats[0].idbat = temp_descriptor(old_logger_find_bat(lg, "sys_schemas_id", 0, 0));
	bats[0].parbat = NULL;
	bats[0].cands = temp_descriptor(old_logger_find_bat(lg, "D_sys_schemas", 0, 0));
	bats[1].nmbat = temp_descriptor(old_logger_find_bat(lg, "sys__tables_name", 0, 0));
	bats[1].idbat = temp_descriptor(old_logger_find_bat(lg, "sys__tables_id", 0, 0));
	bats[1].parbat = temp_descriptor(old_logger_find_bat(lg, "sys__tables_schema_id", 0, 0));
	bats[1].cands = temp_descriptor(old_logger_find_bat(lg, "D_sys__tables", 0, 0));
	bats[2].nmbat = temp_descriptor(old_logger_find_bat(lg, "sys__columns_name", 0, 0));
	bats[2].idbat = temp_descriptor(old_logger_find_bat(lg, "sys__columns_id", 0, 0));
	bats[2].parbat = temp_descriptor(old_logger_find_bat(lg, "sys__columns_table_id", 0, 0));
	bats[2].cands = temp_descriptor(old_logger_find_bat(lg, "D_sys__columns", 0, 0));
	if (mapold == NULL || mapnew == NULL)
		goto bailout;
	for (int i = 0; i < 3; i++) {
		if (bats[i].nmbat == NULL || bats[i].idbat == NULL || bats[i].cands == NULL)
			goto bailout;
		if (i > 0 && bats[i].parbat == NULL)
			goto bailout;
		/* create a candidate list from the deleted rows bat */
		if (BATcount(bats[i].cands) == 0) {
			/* no deleted rows -> no candidate list */
			bat_destroy(bats[i].cands);
			bats[i].cands = NULL;
		} else {
			BAT *b;
			if (BATsort(&b, NULL, NULL, bats[i].cands, NULL, NULL, false, false, false) != GDK_SUCCEED)
				goto bailout;
			bat_destroy(bats[i].cands);
			bats[i].cands = BATnegcands(BATcount(bats[i].nmbat), b);
			bat_destroy(b);
			if (bats[i].cands == NULL)
				goto bailout;
		}
	}

	/* figure out mapping from old IDs to new stable IDs, result in two
	 * aligned BATs, mapold and mapnew */
	int schid, tabid, parid;
	schid = tabid = parid = 0;	/* restrict search to parent object */
	for (int i = 0; tables[i].schema != NULL; i++) {
		int lookup;				/* which system table to look the name up in */
		const char *name;		/* the name to look up */
		if (tables[i].table == NULL) {
			/* it's a schema */
			name = tables[i].schema;
			lookup = 0;
			parid = 0;			/* no parent object */
		} else if (tables[i].column == NULL) {
			/* it's a table */
			name = tables[i].table;
			lookup = 1;
			parid = schid;		/* parent object is last schema */
		} else {
			/* it's a column */
			name = tables[i].column;
			lookup = 2;
			parid = tabid;		/* parent object is last table */
		}
		BAT *cand = bats[lookup].cands;
		if (bats[lookup].parbat != NULL) {
			/* restrict search to parent object */
			cand = BATselect(bats[lookup].parbat, cand, &parid, NULL, true, true, false);
			if (cand == NULL)
				goto bailout;
		}
		BAT *b = BATselect(bats[lookup].nmbat, cand, name, NULL, true, true, false);
		if (cand != bats[lookup].cands)
			bat_destroy(cand);
		if (b == NULL)
			goto bailout;
		if (BATcount(b) > 0) {
			int oldid = ((int *) bats[lookup].idbat->theap->base)[BUNtoid(b, 0) - bats[lookup].nmbat->hseqbase];
			if (oldid != tables[i].newid &&
				(BUNappend(mapold, &oldid, false) != GDK_SUCCEED ||
				 BUNappend(mapnew, &tables[i].newid, false) != GDK_SUCCEED)) {
				bat_destroy(b);
				goto bailout;
			}
			if (tables[i].table == NULL)
				schid = oldid;
			else if (tables[i].column == NULL)
				tabid = oldid;
		}
		bat_destroy(b);
	}

	if (BATcount(mapold) == 0) {
		/* skip unnecessary work if there is no need for mapping */
		bat_destroy(mapold);
		bat_destroy(mapnew);
		mapold = NULL;
		mapnew = NULL;
	}

	/* do the mapping in the system tables: all tables with the .hasids
	 * flag set may contain IDs that have to be mapped; also add all
	 * system tables to the new catalog bats and add the new ones to the
	 * lg->add bat and the old ones that were replaced to the lg->del bat */
	const char *delname;
	delname = NULL;
	int delidx;
	delidx = -1;
	for (int i = 0; tables[i].schema != NULL; i++) {
		if (tables[i].fullname == NULL) /* schema */
			continue;
		if (tables[i].column == NULL) { /* table */
			delname = tables[i].fullname;
			delidx = i;
			continue;
		}
		BAT *b = temp_descriptor(old_logger_find_bat(lg, tables[i].fullname, 0, 0));
		if (b == NULL)
			continue;
		BAT *orig = NULL;
		if (delidx >= 0) {
			BAT *d = temp_descriptor(old_logger_find_bat(lg, delname, 0, 0));
			BAT *m = BATconstant(0, TYPE_msk, &(msk){0}, BATcount(b), PERSISTENT);
			if (d == NULL || m == NULL) {
				bat_destroy(d);
				bat_destroy(m);
				goto bailout;
			}
			const oid *dels = (const oid *) Tloc(d, 0);
			for (BUN q = BUNlast(d), p = 0; p < q; p++)
				mskSetVal(m, (BUN) dels[p], true);
			if (BUNappend(lg->lg->catalog_bid, &m->batCacheid, false) != GDK_SUCCEED ||
				BUNappend(lg->lg->catalog_id, &tables[delidx].newid, false) != GDK_SUCCEED ||
				BUNappend(lg->del, &d->batCacheid, false) != GDK_SUCCEED) {
				bat_destroy(d);
				bat_destroy(m);
				goto bailout;
			}
			BBPretain(m->batCacheid);
			bat_destroy(d);
			bat_destroy(m);
			delidx = -1;
		}
		if (tables[i].hasids && mapold) {
			BAT *b1, *b2;
			BAT *cands = temp_descriptor(old_logger_find_bat(lg, delname, 0, 0));
			gdk_return rc;
			if (cands) {
				if (BATcount(cands) == 0) {
					bat_destroy(cands);
					cands = NULL;
				} else {
					rc = BATsort(&b1, NULL, NULL, cands, NULL, NULL, false, false, false);
					bat_destroy(cands);
					if (rc != GDK_SUCCEED) {
						bat_destroy(b);
						goto bailout;
					}
					cands = BATnegcands(BATcount(b), b1);
					bat_destroy(b1);
					if (cands == NULL) {
						bat_destroy(b);
						goto bailout;
					}
				}
			}
			rc = BATjoin(&b1, &b2, b, mapold, cands, NULL, false, BATcount(mapold));
			bat_destroy(cands);
			if (rc != GDK_SUCCEED) {
				bat_destroy(b);
				goto bailout;
			}
			if (BATcount(b1) == 0) {
				bat_destroy(b1);
				bat_destroy(b2);
			} else {
				orig = b;
				b = COLcopy(orig, orig->ttype, true, PERSISTENT);
				if (b == NULL) {
					bat_destroy(orig);
					bat_destroy(b1);
					bat_destroy(b2);
					goto bailout;
				}
				BAT *b3;
				b3 = BATproject(b2, mapnew);
				bat_destroy(b2);
				rc = BATreplace(b, b1, b3, false);
				bat_destroy(b1);
				bat_destroy(b3);
				if (rc != GDK_SUCCEED) {
					bat_destroy(orig);
					bat_destroy(b);
					goto bailout;
				}
			}
			/* now b contains the updated values for the column in tables[i] */
		}
		/* here, b is either the original, unchanged bat or the updated one */
		if (BUNappend(lg->lg->catalog_bid, &b->batCacheid, false) != GDK_SUCCEED ||
			BUNappend(lg->lg->catalog_id, &tables[i].newid, false) != GDK_SUCCEED) {
			bat_destroy(orig);	/* may be NULL */
			bat_destroy(b);
			goto bailout;
		}
		if (orig != NULL) {
			if (BUNappend(lg->del, &orig->batCacheid, false) != GDK_SUCCEED) {
				bat_destroy(orig);
				bat_destroy(b);
				goto bailout;
			}
			BBPretain(b->batCacheid);
			bat_destroy(orig);
		}
		bat_destroy(b);
	}

	/* add all extant non-system bats to the new catalog */
	BAT *cands, *b;
	if (BATcount(lg->dcatalog) == 0) {
		cands = NULL;
	} else {
		if (BATsort(&b, NULL, NULL, lg->dcatalog, NULL, NULL, false, false, false) != GDK_SUCCEED)
			goto bailout;
		cands = BATnegcands(BATcount(lg->catalog_oid), b);
		bat_destroy(b);
		if (cands == NULL)
			goto bailout;
	}
	b = BATselect(lg->catalog_oid, cands, &(lng){0}, NULL, true, true, true);
	bat_destroy(cands);
	if (b == NULL)
		goto bailout;
	cands = b;
	b = BATconvert(lg->catalog_oid, cands, TYPE_int, true, 0, 0, 0);
	if (b == NULL) {
		bat_destroy(cands);
		goto bailout;
	}
	if (BATappend(lg->lg->catalog_id, b, NULL, false) != GDK_SUCCEED ||
		BATappend(lg->lg->catalog_bid, lg->catalog_bid, cands, false) != GDK_SUCCEED) {
		bat_destroy(cands);
		bat_destroy(b);
		goto bailout;
	}
	bat_destroy(cands);
	bat_destroy(b);

	rc = GDK_SUCCEED;

  bailout:
	bat_destroy(mapold);
	bat_destroy(mapnew);
	for (int i = 0; i < 3; i++) {
		bat_destroy(bats[i].nmbat);
		bat_destroy(bats[i].idbat);
		bat_destroy(bats[i].parbat);
		bat_destroy(bats[i].cands);
	}
	return rc;
}

static gdk_return
bl_postversion(void *Store, old_logger *old_lg)
{
	sqlstore *store = Store;
	(void)store;
	if (store->catalog_version < 52300 && upgrade(old_lg) != GDK_SUCCEED)
		return GDK_FAIL;
	logger *lg = old_lg->lg;
#ifdef CATALOG_JUN2020
	if (store->catalog_version <= CATALOG_JUN2020) {
		BAT *b;								 /* temp variable */
		{
			/* new BOOLEAN column sys.functions.semantics */
			b = temp_descriptor(logger_find_bat(lg, 2017)); /* sys.functions.id */
			BAT *sem = BATconstant(b->hseqbase, TYPE_bit, &(bit){1}, BATcount(b), PERSISTENT);
			bat_destroy(b);
			if (sem == NULL)
				return GDK_FAIL;
			if (BATsetaccess(sem, BAT_READ) != GDK_SUCCEED ||
				BUNappend(lg->catalog_id, &(int) {2162}, false) != GDK_SUCCEED ||
				BUNappend(lg->catalog_bid, &sem->batCacheid, false) != GDK_SUCCEED) {
				bat_destroy(sem);
				return GDK_FAIL;
			}
			BBPretain(sem->batCacheid);
			bat_destroy(sem);
			if (tabins(lg, -1, 0,
					   2077, &(int) {2162},		/* sys._columns.id */
					   2078, "semantics",		/* sys._columns.name */
					   2079, "boolean",			/* sys._columns.type */
					   2080, &(int) {1},		/* sys._columns.type_digits */
					   2081, &(int) {0},		/* sys._columns.type_scale */
					   2082, &(int) {2016},		/* sys._columns.table_id */
					   2083, str_nil,			/* sys._columns.default */
					   2084, &(bit) {TRUE},		/* sys._columns.null */
					   2085, &(int) {11},		/* sys._columns.number */
					   2086, str_nil,			/* sys._columns.storage */
					   0) != GDK_SUCCEED)
				return GDK_FAIL;
		}

		BAT *func_tid;
		{
			/* move sql.degrees, sql.radians, sql.like and sql.ilike functions
			 * from 09_like.sql and 10_math.sql script to sql_types list */
			BAT *del_funcs = temp_descriptor(logger_find_bat(lg, 2016));
			BAT *func_func = temp_descriptor(logger_find_bat(lg, 2018));
			BAT *func_schem = temp_descriptor(logger_find_bat(lg, 2026));
			BAT *cands;
			if (del_funcs == NULL || func_func == NULL || func_schem == NULL) {
				bat_destroy(del_funcs);
				bat_destroy(func_func);
				bat_destroy(func_schem);
				return GDK_FAIL;
			}
			gdk_return rc = BATsort(&b, NULL, NULL, del_funcs, NULL, NULL, false, false, false);
			if (rc != GDK_SUCCEED) {
				bat_destroy(del_funcs);
				bat_destroy(func_func);
				bat_destroy(func_schem);
				return rc;
			}
			func_tid = BATnegcands(BATcount(func_func), b);
			bat_destroy(b);
			if (func_tid == NULL) {
				bat_destroy(del_funcs);
				bat_destroy(func_func);
				bat_destroy(func_schem);
				return GDK_FAIL;
			}
			b = BATselect(func_schem, func_tid, &(int) {2000}, NULL, true, true, false);
			bat_destroy(func_schem);
			cands = b;
			if (cands == NULL) {
				bat_destroy(del_funcs);
				bat_destroy(func_func);
				bat_destroy(func_tid);
				return GDK_FAIL;
			}

			const char *funcs[] = {
				"degrees",
				"radians",
				"like",
				"ilike",
				NULL,
			};
			for (int i = 0; funcs[i]; i++) {
				if ((b = BATselect(func_func, cands, funcs[i], NULL, true, true, false)) == NULL ||
					BATappend(del_funcs, b, NULL, true) != GDK_SUCCEED) {
					bat_destroy(del_funcs);
					bat_destroy(func_func);
					bat_destroy(func_tid);
					return GDK_FAIL;
				}
				bat_destroy(b);
			}
			bat_destroy(cands);
			bat_destroy(func_func);
			bat_destroy(del_funcs);
		}

		{
			/* Fix SQL aggregation functions defined on the wrong modules:
			 * sql.null, sql.all, sql.zero_or_one and sql.not_unique */
			BAT *func_mod = temp_descriptor(logger_find_bat(lg, 2020));
			if (func_mod == NULL)
				return GDK_FAIL;

			/* find the (undeleted) functions defined on "sql" module */
			BAT *sqlfunc = BATselect(func_mod, func_tid, "sql", NULL, true, true, false);
			bat_destroy(func_tid);
			if (sqlfunc == NULL) {
				bat_destroy(func_mod);
				return GDK_FAIL;
			}
			BAT *func_type = temp_descriptor(logger_find_bat(lg, 2022));
			if (func_type == NULL) {
				bat_destroy(func_mod);
				bat_destroy(sqlfunc);
				return GDK_FAIL;
			}
			/* and are aggregates (3) */
			BAT *sqlaggr_func = BATselect(func_type, sqlfunc, &(int) {3}, NULL, true, true, false);
			bat_destroy(sqlfunc);
			if (sqlaggr_func == NULL) {
				bat_destroy(func_mod);
				return GDK_FAIL;
			}

			BAT *func_func = temp_descriptor(logger_find_bat(lg, 2019));
			if (func_func == NULL) {
				bat_destroy(func_mod);
				bat_destroy(sqlaggr_func);
				return GDK_FAIL;
			}
			const char *aggrs[] = {
				"null",
				"all",
				"zero_or_one",
				"not_unique",
				NULL
			};
			for (int i = 0; aggrs[i] != NULL; i++) {
				BAT *func = BATselect(func_func, sqlaggr_func, aggrs[i], NULL, true, true, false);
				if (func == NULL) {
					bat_destroy(func_mod);
					bat_destroy(sqlaggr_func);
					bat_destroy(func_func);
					return GDK_FAIL;
				}
				BAT *aggr = BATconstant(0, TYPE_str, "aggr", BATcount(func), TRANSIENT);
				if (func == NULL) {
					bat_destroy(func_mod);
					bat_destroy(sqlaggr_func);
					bat_destroy(func_func);
					bat_destroy(func);
					return GDK_FAIL;
				}
				gdk_return rc = BATreplace(func_mod, func, aggr, true);
				bat_destroy(func);
				bat_destroy(aggr);
				if (rc != GDK_SUCCEED) {
					bat_destroy(func_mod);
					bat_destroy(sqlaggr_func);
					bat_destroy(func_func);
					return rc;
				}
			}
		}
	}
#endif

#ifdef CATALOG_OCT2020
	if (store->catalog_version <= CATALOG_OCT2020) {
		/* add sub column to "objects" table. This is required for merge tables */
		if (tabins(lg, -1, 0,
				   2077, &(int) {2163},		/* sys._columns.id */
				   2078, "sub",				/* sys._columns.name */
				   2079, "int",				/* sys._columns.type */
				   2080, &(int) {32},		/* sys._columns.type_digits */
				   2081, &(int) {0},		/* sys._columns.type_scale */
				   2082, &(int) {2110},		/* sys._columns.table_id */
				   2083, str_nil,			/* sys._columns.default */
				   2084, &(bit) {TRUE},		/* sys._columns.null */
				   2085, &(int) {3},		/* sys._columns.number */
				   2086, str_nil,			/* sys._columns.storage */
				   0) != GDK_SUCCEED)
			return GDK_FAIL;
		if (tabins(lg, -1, 0,
				   2077, &(int) {2164},		/* sys._columns.id */
				   2078, "sub",				/* sys._columns.name */
				   2079, "int",				/* sys._columns.type */
				   2080, &(int) {32},		/* sys._columns.type_digits */
				   2081, &(int) {0},		/* sys._columns.type_scale */
				   2082, &(int) {2158},		/* sys._columns.table_id */
				   2083, str_nil,			/* sys._columns.default */
				   2084, &(bit) {TRUE},		/* sys._columns.null */
				   2085, &(int) {3},		/* sys._columns.number */
				   2086, str_nil,			/* sys._columns.storage */
				   0) != GDK_SUCCEED)
			return GDK_FAIL;

		/* alter table sys.objects add column sub integer; */
		BAT *objs_id = temp_descriptor(logger_find_bat(lg, 2111));
		if (objs_id == NULL)
			return GDK_FAIL;

		BAT *objs_sub = BATconstant(objs_id->hseqbase, TYPE_int, &int_nil, BATcount(objs_id), PERSISTENT);
		bat_destroy(objs_id);
		if (objs_sub == NULL) {
			return GDK_FAIL;
		}
		if (BATsetaccess(objs_sub, BAT_READ) != GDK_SUCCEED ||
			BUNappend(lg->catalog_id, &(int) {2163}, false) != GDK_SUCCEED ||
			BUNappend(lg->catalog_bid, &objs_sub->batCacheid, false) != GDK_SUCCEED) {
			bat_destroy(objs_sub);
			return GDK_FAIL;
		}
		BBPretain(objs_sub->batCacheid);
		bat_destroy(objs_sub);
		/* update sys.objects o set sub = (select t.id from sys.dependencies d, sys._tables t where o.nr = d.depend_id and d.id = t.id and o.name = t.name); */

		/* alter table tmp.objects add column sub integer; */
		objs_sub = BATconstant(0, TYPE_int, &int_nil, 0, PERSISTENT);
		if (objs_sub == NULL) {
			return GDK_FAIL;
		}
		if (BATsetaccess(objs_sub, BAT_READ) != GDK_SUCCEED ||
			BUNappend(lg->catalog_id, &(int) {2164}, false) != GDK_SUCCEED ||
			BUNappend(lg->catalog_bid, &objs_sub->batCacheid, false) != GDK_SUCCEED) {
			bat_destroy(objs_sub);
			return GDK_FAIL;
		}
		BBPretain(objs_sub->batCacheid);
		bat_destroy(objs_sub);
	}
#endif

	return GDK_SUCCEED;
}

static int
bl_create(sqlstore *store, int debug, const char *logdir, int cat_version)
{
	if (store->logger)
		return LOG_ERR;
	store->logger = logger_create(debug, "sql", logdir, cat_version, (preversionfix_fptr)&bl_preversion, (postversionfix_fptr)&bl_postversion, store);
	if (store->logger)
		return LOG_OK;
	return LOG_ERR;
}

static void
bl_destroy(sqlstore *store)
{
	logger *l = store->logger;

	store->logger = NULL;
	if (l)
		logger_destroy(l);
}

static int
bl_flush(sqlstore *store, lng save_id)
{
	if (store->logger)
		return logger_flush(store->logger, save_id) == GDK_SUCCEED ? LOG_OK : LOG_ERR;
	return LOG_OK;
}

static int
bl_changes(sqlstore *store)
{
	return (int) MIN(logger_changes(store->logger), GDK_int_max);
}

static int
bl_get_sequence(sqlstore *store, int seq, lng *id)
{
	return logger_sequence(store->logger, seq, id);
}

static int
bl_log_isnew(sqlstore *store)
{
	logger *bat_logger = store->logger;
	if (BATcount(bat_logger->catalog_bid) > 10) {
		return 0;
	}
	return 1;
}

static int
bl_tstart(sqlstore *store, ulng commit_ts)
{
	return log_tstart(store->logger, commit_ts) == GDK_SUCCEED ? LOG_OK : LOG_ERR;
}

static int
bl_tend(sqlstore *store)
{
	return log_tend(store->logger) == GDK_SUCCEED ? LOG_OK : LOG_ERR;
}

static int
bl_sequence(sqlstore *store, int seq, lng id)
{
	return log_sequence(store->logger, seq, id) == GDK_SUCCEED ? LOG_OK : LOG_ERR;
}

/* Write a plan entry to copy part of the given file.
 * That part of the file must remain unchanged until the plan is executed.
 */
static void
snapshot_lazy_copy_file(stream *plan, const char *name, uint64_t extent)
{
	mnstr_printf(plan, "c %" PRIu64 " %s\n", extent, name);
}

/* Write a plan entry to write the current contents of the given file.
 * The contents are included in the plan so the source file is allowed to
 * change in the mean time.
 */
static gdk_return
snapshot_immediate_copy_file(stream *plan, const char *path, const char *name)
{
	gdk_return ret = GDK_FAIL;
	const size_t bufsize = 64 * 1024;
	struct stat statbuf;
	char *buf = NULL;
	stream *s = NULL;
	size_t to_copy;

	if (MT_stat(path, &statbuf) < 0) {
		GDKsyserror("stat failed on %s", path);
		goto end;
	}
	to_copy = (size_t) statbuf.st_size;

	s = open_rstream(path);
	if (!s) {
		GDKerror("%s", mnstr_peek_error(NULL));
		goto end;
	}

	buf = GDKmalloc(bufsize);
	if (!buf) {
		GDKerror("GDKmalloc failed");
		goto end;
	}

	mnstr_printf(plan, "w %zu %s\n", to_copy, name);

	while (to_copy > 0) {
		size_t chunk = (to_copy <= bufsize) ? to_copy : bufsize;
		ssize_t bytes_read = mnstr_read(s, buf, 1, chunk);
		if (bytes_read < 0) {
			char *err = mnstr_error(s);
			GDKerror("Reading bytes of component %s failed: %s", path, err);
			free(err);
			goto end;
		} else if (bytes_read < (ssize_t) chunk) {
			char *err = mnstr_error(s);
			GDKerror("Read only %zu/%zu bytes of component %s: %s", (size_t) bytes_read, chunk, path, err);
			free(err);
			goto end;
		}

		ssize_t bytes_written = mnstr_write(plan, buf, 1, chunk);
		if (bytes_written < 0) {
			GDKerror("Writing to plan failed");
			goto end;
		} else if (bytes_written < (ssize_t) chunk) {
			GDKerror("write to plan truncated");
			goto end;
		}
		to_copy -= chunk;
	}

	ret = GDK_SUCCEED;
end:
	GDKfree(buf);
	if (s)
		close_stream(s);
	return ret;
}

/* Add plan entries for all relevant files in the Write Ahead Log */
static gdk_return
snapshot_wal(logger *bat_logger, stream *plan, const char *db_dir)
{
	char log_file[FILENAME_MAX];
	int len;

	len = snprintf(log_file, sizeof(log_file), "%s/%s%s", db_dir, bat_logger->dir, LOGFILE);
	if (len == -1 || (size_t)len >= sizeof(log_file)) {
		GDKerror("Could not open %s, filename is too large", log_file);
		return GDK_FAIL;
	}
	snapshot_immediate_copy_file(plan, log_file, log_file + strlen(db_dir) + 1);

	for (ulng id = bat_logger->saved_id+1; id <= bat_logger->id; id++) {
		struct stat statbuf;

		len = snprintf(log_file, sizeof(log_file), "%s/%s%s." LLFMT, db_dir, bat_logger->dir, LOGFILE, id);
		if (len == -1 || (size_t)len >= sizeof(log_file)) {
			GDKerror("Could not open %s, filename is too large", log_file);
			return GDK_FAIL;
		}
		if (MT_stat(log_file, &statbuf) == 0) {
			snapshot_lazy_copy_file(plan, log_file + strlen(db_dir) + 1, statbuf.st_size);
		} else {
			GDKerror("Could not open %s", log_file);
			return GDK_FAIL;
		}
	}
	return GDK_SUCCEED;
}

static gdk_return
snapshot_heap(stream *plan, const char *db_dir, uint64_t batid, const char *filename, const char *suffix, uint64_t extent)
{
	char path1[FILENAME_MAX];
	char path2[FILENAME_MAX];
	const size_t offset = strlen(db_dir) + 1;
	struct stat statbuf;
	int len;

	// first check the backup dir
	len = snprintf(path1, FILENAME_MAX, "%s/%s/%" PRIo64 "%s", db_dir, BAKDIR, batid, suffix);
	if (len == -1 || len >= FILENAME_MAX) {
		path1[FILENAME_MAX - 1] = '\0';
		GDKerror("Could not open %s, filename is too large", path1);
		return GDK_FAIL;
	}
	if (MT_stat(path1, &statbuf) == 0) {
		snapshot_lazy_copy_file(plan, path1 + offset, extent);
		return GDK_SUCCEED;
	}
	if (errno != ENOENT) {
		GDKsyserror("Error stat'ing %s", path1);
		return GDK_FAIL;
	}

	// then check the regular location
	len = snprintf(path2, FILENAME_MAX, "%s/%s/%s%s", db_dir, BATDIR, filename, suffix);
	if (len == -1 || len >= FILENAME_MAX) {
		path2[FILENAME_MAX - 1] = '\0';
		GDKerror("Could not open %s, filename is too large", path2);
		return GDK_FAIL;
	}
	if (MT_stat(path2, &statbuf) == 0) {
		snapshot_lazy_copy_file(plan, path2 + offset, extent);
		return GDK_SUCCEED;
	}
	if (errno != ENOENT) {
		GDKsyserror("Error stat'ing %s", path2);
		return GDK_FAIL;
	}

	GDKerror("One of %s and %s must exist", path1, path2);
	return GDK_FAIL;
}

/* Add plan entries for all persistent BATs by looping over the BBP.dir.
 * Also include the BBP.dir itself.
 */
static gdk_return
snapshot_bats(stream *plan, const char *db_dir)
{
	char bbpdir[FILENAME_MAX];
	stream *cat = NULL;
	char line[1024];
	int gdk_version, len;
	gdk_return ret = GDK_FAIL;

	len = snprintf(bbpdir, FILENAME_MAX, "%s/%s/%s", db_dir, BAKDIR, "BBP.dir");
	if (len == -1 || len >= FILENAME_MAX) {
		GDKerror("Could not open %s, filename is too large", bbpdir);
		goto end;
	}
	ret = snapshot_immediate_copy_file(plan, bbpdir, bbpdir + strlen(db_dir) + 1);
	if (ret != GDK_SUCCEED)
		goto end;

	// Open the catalog and parse the header
	cat = open_rastream(bbpdir);
	if (cat == NULL) {
		GDKerror("Could not open %s for reading: %s", bbpdir, mnstr_peek_error(NULL));
		goto end;
	}
	if (mnstr_readline(cat, line, sizeof(line)) < 0) {
		GDKerror("Could not read first line of %s", bbpdir);
		goto end;
	}
	if (sscanf(line, "BBP.dir, GDKversion %d", &gdk_version) != 1) {
		GDKerror("Invalid first line of %s", bbpdir);
		goto end;
	}
	if (gdk_version != 061043U) {
		// If this version number has changed, the structure of BBP.dir
		// may have changed. Update this whole function to take this
		// into account.
		// Note: when startup has completed BBP.dir is guaranteed
		// to the latest format so we don't have to support any older
		// formats in this function.
		GDKerror("GDK version mismatch in snapshot yet");
		goto end;
	}
	if (mnstr_readline(cat, line, sizeof(line)) < 0) {
		GDKerror("Couldn't skip the second line of %s", bbpdir);
		goto end;
	}
	if (mnstr_readline(cat, line, sizeof(line)) < 0) {
		GDKerror("Couldn't skip the third line of %s", bbpdir);
		goto end;
	}

	/* TODO get transaction id and last processed log file id */
	if (mnstr_readline(cat, line, sizeof(line)) < 0) {
		GDKerror("Couldn't skip the 4th line of %s", bbpdir);
		goto end;
	}

	while (mnstr_readline(cat, line, sizeof(line)) > 0) {
		uint64_t batid;
		uint64_t tail_free;
		uint64_t theap_free;
		char filename[sizeof(BBP_physical(0))];
		// The lines in BBP.dir come in various lengths.
		// we try to parse the longest variant then check
		// the return value of sscanf to see which fields
		// were actually present.
		int scanned = sscanf(line,
				// Taken from the sscanf in BBPreadEntries() in gdk_bbp.c.
				// 8 fields, we need field 1 (batid) and field 4 (filename)
				"%" SCNu64 " %*s %*s %19s %*s %*s %*s %*s"

				// Taken from the sscanf in heapinit() in gdk_bbp.c.
				// 14 fields, we need field 10 (free)
				" %*s %*s %*s %*s %*s %*s %*s %*s %*s %" SCNu64 " %*s %*s %*s %*s"

				// Taken from the sscanf in vheapinit() in gdk_bbp.c.
				// 3 fields, we need field 1 (free).
				"%" SCNu64 " %*s ^*s"
				,
				&batid, filename,
				&tail_free,
				&theap_free);

		// The following switch uses fallthroughs to make
		// the larger cases include the work of the smaller cases.
		switch (scanned) {
			default:
				GDKerror("Couldn't parse (%d) %s line: %s", scanned, bbpdir, line);
				goto end;
			case 4:
				// tail and theap
				ret = snapshot_heap(plan, db_dir, batid, filename, ".theap", theap_free);
				if (ret != GDK_SUCCEED)
					goto end;
				/* fallthrough */
			case 3:
				// tail only
				snapshot_heap(plan, db_dir, batid, filename, ".tail", tail_free);
				if (ret != GDK_SUCCEED)
					goto end;
				/* fallthrough */
			case 2:
				// no tail?
				break;
		}
	}

end:
	if (cat) {
		close_stream(cat);
	}
	return ret;
}

/* Add a file to the plan which records the current wlc status, if any.
 * In particular, `wlc_batches`.
 *
 * With this information, a replica initialized from this snapshot can
 * be configured to catch up with its master by replaying later transactions.
 */
static gdk_return
snapshot_wlc(stream *plan, const char *db_dir)
{
	const char name[] = "wlr.config.in";
	char buf[1024];
	int len;

	(void)db_dir;

	if (wlc_state != WLC_RUN)
		return GDK_SUCCEED;

	len = snprintf(buf, sizeof(buf),
		"beat=%d\n"
		"batches=%d\n"
		, wlc_beat, wlc_batches
	);

	mnstr_printf(plan, "w %d %s\n", len, name);
	mnstr_write(plan, buf, 1, len);

	return GDK_SUCCEED;
}

static gdk_return
snapshot_vaultkey(stream *plan, const char *db_dir)
{
	char path[FILENAME_MAX];
	struct stat statbuf;

	int len = snprintf(path, FILENAME_MAX, "%s/.vaultkey", db_dir);
	if (len == -1 || len >= FILENAME_MAX) {
		path[FILENAME_MAX - 1] = '\0';
		GDKerror("Could not open %s, filename is too large", path);
		return GDK_FAIL;
	}
	if (MT_stat(path, &statbuf) == 0) {
		snapshot_lazy_copy_file(plan, ".vaultkey", statbuf.st_size);
		return GDK_SUCCEED;
	}
	if (errno == ENOENT) {
		// No .vaultkey? Fine.
		return GDK_SUCCEED;
	}

	GDKsyserror("Error stat'ing %s", path);
	return GDK_FAIL;
}
static gdk_return
bl_snapshot(sqlstore *store, stream *plan)
{
	logger *bat_logger = store->logger;
	gdk_return ret;
	char *db_dir = NULL;
	size_t db_dir_len;

	// Farm 0 is always the persistent farm.
	db_dir = GDKfilepath(0, NULL, "", NULL);
	db_dir_len = strlen(db_dir);
	if (db_dir[db_dir_len - 1] == DIR_SEP)
		db_dir[db_dir_len - 1] = '\0';

	mnstr_printf(plan, "%s\n", db_dir);

	// Please monetdbd
	mnstr_printf(plan, "w 0 .uplog\n");

	ret = snapshot_vaultkey(plan, db_dir);
	if (ret != GDK_SUCCEED)
		goto end;

	ret = snapshot_bats(plan, db_dir);
	if (ret != GDK_SUCCEED)
		goto end;

	ret = snapshot_wal(bat_logger, plan, db_dir);
	if (ret != GDK_SUCCEED)
		goto end;

	ret = snapshot_wlc(plan, db_dir);
	if (ret != GDK_SUCCEED)
		goto end;

	ret = GDK_SUCCEED;
end:
	if (db_dir)
		GDKfree(db_dir);
	return ret;
}

void
bat_logger_init( logger_functions *lf )
{
	lf->create = bl_create;
	lf->destroy = bl_destroy;
	lf->flush = bl_flush;
	lf->changes = bl_changes;
	lf->get_sequence = bl_get_sequence;
	lf->log_isnew = bl_log_isnew;
	lf->log_tstart = bl_tstart;
	lf->log_tend = bl_tend;
	lf->log_sequence = bl_sequence;
	lf->get_snapshot_files = bl_snapshot;
}
