/*
 * SPDX-License-Identifier: MPL-2.0
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0.  If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright 1997 - July 2008 CWI, August 2008 - 2023 MonetDB B.V.
 */

#ifndef SQL_TOKENS_H
#define SQL_TOKENS_H

typedef enum tokens {
	// Please keep this list sorted for ease of maintenance
	SQL_AGGR,
	SQL_ALTER_SEQ,
	SQL_ALTER_TABLE,
	SQL_ALTER_USER,
	SQL_ANALYZE,
	SQL_AND,
	SQL_ASSIGN,
	SQL_ATOM,
	SQL_BETWEEN,
	SQL_BINCOPYFROM,
	SQL_BINCOPYINTO,
	SQL_BINOP,
	SQL_CACHE,
	SQL_CALL,
	SQL_CASE,
	SQL_CAST,
	SQL_CHARSET,
	SQL_CHECK,
	SQL_COALESCE,
	SQL_COLUMN,
	SQL_COLUMN_GROUP,
	SQL_COLUMN_OPTIONS,
	SQL_COMMENT,
	SQL_COMPARE,
	SQL_CONSTRAINT,
	SQL_COPYFROM,
	SQL_COPYLOADER,
	SQL_COPYINTO,
	SQL_CREATE_FUNC,
	SQL_CREATE_INDEX,
	SQL_CREATE_ROLE,
	SQL_CREATE_SCHEMA,
	SQL_CREATE_SEQ,
	SQL_CREATE_TABLE,
	SQL_CREATE_TABLE_LOADER,
	SQL_CREATE_TRIGGER,
	SQL_CREATE_TYPE,
	SQL_CREATE_USER,
	SQL_CREATE_VIEW,
	SQL_CUBE,
	SQL_CURRENT_ROW,
	SQL_CYCLE,
	SQL_DECLARE,
	SQL_DECLARE_TABLE,
	SQL_DEFAULT,
	SQL_DELETE,
	SQL_DROP_COLUMN,
	SQL_DROP_CONSTRAINT,
	SQL_DROP_DEFAULT,
	SQL_DROP_FUNC,
	SQL_DROP_INDEX,
	SQL_DROP_ROLE,
	SQL_DROP_SCHEMA,
	SQL_DROP_SEQ,
	SQL_DROP_TABLE,
	SQL_DROP_TRIGGER,
	SQL_DROP_TYPE,
	SQL_DROP_USER,
	SQL_DROP_VIEW,
	SQL_ELSE,
	SQL_ESCAPE,
	SQL_EXCEPT,
	SQL_EXECUTE,
	SQL_EXISTS,
	SQL_FILTER,
	SQL_FOLLOWING,
	SQL_FOREIGN_KEY,
	SQL_FRAME,
	SQL_FROM,
	SQL_FUNC,
	SQL_GRANT,
	SQL_GRANT_ROLES,
	SQL_GROUPBY,
	SQL_GROUPING_SETS,
	SQL_IDENT,
	SQL_IF,
	SQL_IN,
	SQL_INC,
	SQL_INDEX,
	SQL_INSERT,
	SQL_INTERSECT,
	SQL_IS_NOT_NULL,
	SQL_IS_NULL,
	SQL_JOIN,
	SQL_LIKE,
	SQL_MAXVALUE,
	SQL_MERGE,
	SQL_MERGE_MATCH,
	SQL_MERGE_NO_MATCH,
	SQL_MERGE_PARTITION,
	SQL_MINVALUE,
	SQL_MULSTMT,
	SQL_NAME,
	SQL_NEXT,
	SQL_NOP,
	SQL_NOT,
	SQL_NOT_BETWEEN,
	SQL_NOT_EXISTS,
	SQL_NOT_IN,
	SQL_NOT_LIKE,
	SQL_NOT_NULL,
	SQL_NULL,
	SQL_NULLIF,
	SQL_OP,
	SQL_OR,
	SQL_ORDERBY,
	SQL_PARAMETER,
	SQL_PARTITION_COLUMN,
	SQL_PARTITION_EXPRESSION,
	SQL_PARTITION_LIST,
	SQL_PARTITION_RANGE,
	SQL_PATH,
	SQL_PRECEDING,
	SQL_PREP,
	SQL_PRIMARY_KEY,
	SQL_PW_ENCRYPTED,
	SQL_PW_UNENCRYPTED,
	SQL_RANK,
	SQL_RENAME_COLUMN,
	SQL_RENAME_SCHEMA,
	SQL_RENAME_TABLE,
	SQL_RENAME_USER,
	SQL_RETURN,
	SQL_REVOKE,
	SQL_REVOKE_ROLES,
	SQL_ROLLUP,
	SQL_ROUTINE,
	SQL_SCHEMA,
	SQL_SELECT,
	SQL_SEQUENCE,
	SQL_SET,
	SQL_SET_TABLE_SCHEMA,
	SQL_START,
	SQL_STORAGE,
	SQL_TABLE,
	SQL_TRUNCATE,
	SQL_TYPE,
	SQL_UNION,
	SQL_UNIQUE,
	SQL_UNOP,
	SQL_UPDATE,
	SQL_USING,
	SQL_VALUES,
	SQL_VIEW,
	SQL_WHEN,
	SQL_WHILE,
	SQL_WINDOW,
	SQL_WITH,
	SQL_XMLATTRIBUTE,
	SQL_XMLCOMMENT,
	SQL_XMLCONCAT,
	SQL_XMLDOCUMENT,
	SQL_XMLELEMENT,
	SQL_XMLFOREST,
	SQL_XMLPARSE,
	SQL_XMLPI,
	SQL_XMLTEXT,
	TR_COMMIT,
	TR_MODE,
	TR_RELEASE,
	TR_ROLLBACK,
	TR_SAVEPOINT,
	TR_START
	// Please keep this list sorted for ease of maintenance
} tokens;

typedef enum jt {
	jt_inner = 0,
	jt_left = 1,
	jt_right = 2,
	jt_full = 3,
	jt_cross = 4
} jt;

typedef enum {
	endian_big = 2,
	endian_little = 1,
	endian_native = 3,
} endianness;

#ifdef WORDS_BIGENDIAN
#define OUR_ENDIANNESS endian_big
#else
#define OUR_ENDIANNESS endian_little
#endif


#endif
