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
 * This code was created by Peter Harvey (mostly during Christmas 98/99).
 * This code is LGPL. Please ensure that this message remains in future
 * distributions and uses of this code (thats about all I get out of it).
 * - Peter Harvey pharvey@codebydesign.com
 *
 * This file has been modified for the MonetDB project.  See the file
 * Copyright in this directory for more information.
 */

/**********************************************************************
 * SQLSetConnectAttr()
 * CLI Compliance: ISO 92
 *
 * Author: Martin van Dinther, Sjoerd Mullender
 * Date  : 30 Aug 2002
 *
 **********************************************************************/

#include "ODBCGlobal.h"
#include "ODBCDbc.h"
#include "ODBCUtil.h"


SQLRETURN
MNDBSetConnectAttr(ODBCDbc *dbc,
		   SQLINTEGER Attribute,
		   SQLPOINTER ValuePtr,
		   SQLINTEGER StringLength)
{
	(void) StringLength;	/* Stefan: unused!? */

	bool autocommit = false;
	switch (Attribute) {
	case SQL_ATTR_AUTOCOMMIT:		/* SQLUINTEGER */
		switch ((SQLUINTEGER) (uintptr_t) ValuePtr) {
		case SQL_AUTOCOMMIT_ON:
		case SQL_AUTOCOMMIT_OFF:
			autocommit = (bool) (SQLUINTEGER) (uintptr_t) ValuePtr;
#ifdef ODBCDEBUG
			ODBCLOG("SQLSetConnectAttr set autocommit %s\n", autocommit ? "on" : "off");
#endif
			if (dbc->mid)
				mapi_setAutocommit(dbc->mid, autocommit);
			break;
		default:
			/* Invalid attribute value */
			addDbcError(dbc, "HY024", NULL, 0);
			return SQL_ERROR;
		}
		return SQL_SUCCESS;
	case SQL_ATTR_CURRENT_CATALOG:		/* SQLCHAR* */
		/* Driver does not support this function */
		addDbcError(dbc, "IM001", NULL, 0);
		return SQL_ERROR;
	case SQL_ATTR_CONNECTION_TIMEOUT: {	/* SQLUINTEGER */
		long timeout = 1000 * (long)(uintptr_t) ValuePtr;
		msetting_set_long(dbc->settings, MP_CONNECT_TIMEOUT, timeout);
		if (dbc->mid)
			mapi_timeout(dbc->mid, timeout);
	}	 break;
	case SQL_ATTR_LOGIN_TIMEOUT:		/* SQLUINTEGER */
		/* currently only value 0 (no timeout) is accepted
		 * as mapilib currently does not provide a way to set login timeout */
		if ((SQLUINTEGER) (uintptr_t) ValuePtr > 0) {
			/* add Warning: Option value changed */
			addDbcError(dbc, "01S02", NULL, 0);
			return SQL_SUCCESS_WITH_INFO;
		}
		break;
	case SQL_ATTR_METADATA_ID:		/* SQLUINTEGER */
		switch ((SQLUINTEGER) (uintptr_t) ValuePtr) {
		case SQL_TRUE:
		case SQL_FALSE:
			dbc->sql_attr_metadata_id = (SQLUINTEGER) (uintptr_t) ValuePtr;
#ifdef ODBCDEBUG
			ODBCLOG("SQLSetConnectAttr set metadata_id %s\n",
				dbc->sql_attr_metadata_id == SQL_TRUE ? "true" : "false");
#endif
			break;
		default:
			/* Invalid attribute value */
			addDbcError(dbc, "HY024", NULL, 0);
			return SQL_ERROR;
		}
		return SQL_SUCCESS;
	case SQL_ATTR_TXN_ISOLATION:		/* SQLUINTEGER */
		/* nothing to change, we only do the highest level */
		break;

		/* TODO: implement connection attribute behavior */
	case SQL_ATTR_ACCESS_MODE:		/* SQLUINTEGER */
#ifdef SQL_ATTR_ASYNC_DBC_EVENT
	case SQL_ATTR_ASYNC_DBC_EVENT:		/* SQLPOINTER */
#endif
#ifdef SQL_ATTR_ASYNC_DBC_FUNCTIONS_ENABLE
	case SQL_ATTR_ASYNC_DBC_FUNCTIONS_ENABLE: /* SQLUINTEGER */
#endif
#ifdef SQL_ATTR_ASYNC_DBC_PCALLBACK
	case SQL_ATTR_ASYNC_DBC_PCALLBACK:	/* SQLPOINTER */
#endif
#ifdef SQL_ATTR_ASYNC_DBC_PCONTEXT
	case SQL_ATTR_ASYNC_DBC_PCONTEXT:	/* SQLPOINTER */
#endif
	case SQL_ATTR_ASYNC_ENABLE:		/* SQLULEN */
#ifdef SQL_ATTR_DBC_INFO_TOKEN
	case SQL_ATTR_DBC_INFO_TOKEN:		/* SQLPOINTER */
#endif
	case SQL_ATTR_ENLIST_IN_DTC:		/* SQLPOINTER */
	case SQL_ATTR_ODBC_CURSORS:		/* SQLULEN */
	case SQL_ATTR_PACKET_SIZE:		/* SQLUINTEGER */
	case SQL_ATTR_QUIET_MODE:		/* HWND (SQLPOINTER) */
	case SQL_ATTR_TRACE:			/* SQLUINTEGER */
	case SQL_ATTR_TRACEFILE:		/* SQLCHAR* */
	case SQL_ATTR_TRANSLATE_LIB:		/* SQLCHAR* */
	case SQL_ATTR_TRANSLATE_OPTION:		/* SQLUINTEGER */
		/* Optional feature not implemented */
		addDbcError(dbc, "HYC00", NULL, 0);
		return SQL_ERROR;

	case SQL_ATTR_AUTO_IPD:			/* SQLUINTEGER */
	case SQL_ATTR_CONNECTION_DEAD:		/* SQLUINTEGER */
		/* read-only attribute */
	default:
		/* Invalid attribute/option identifier */
		addDbcError(dbc, "HY092", NULL, 0);
		return SQL_ERROR;
	}

	return SQL_SUCCESS;
}

SQLRETURN SQL_API
SQLSetConnectAttr(SQLHDBC ConnectionHandle,
		  SQLINTEGER Attribute,
		  SQLPOINTER ValuePtr,
		  SQLINTEGER StringLength)
{
#ifdef ODBCDEBUG
	ODBCLOG("SQLSetConnectAttr %p %s %p %d\n",
		ConnectionHandle,
		translateConnectAttribute(Attribute),
		ValuePtr, (int) StringLength);
#endif

	if (!isValidDbc((ODBCDbc *) ConnectionHandle))
		return SQL_INVALID_HANDLE;

	clearDbcErrors((ODBCDbc *) ConnectionHandle);

	return MNDBSetConnectAttr((ODBCDbc *) ConnectionHandle,
				  Attribute,
				  ValuePtr,
				  StringLength);
}

SQLRETURN SQL_API
SQLSetConnectAttrA(SQLHDBC ConnectionHandle,
		   SQLINTEGER Attribute,
		   SQLPOINTER ValuePtr,
		   SQLINTEGER StringLength)
{
	return SQLSetConnectAttr(ConnectionHandle,
				 Attribute,
				 ValuePtr,
				 StringLength);
}

SQLRETURN SQL_API
SQLSetConnectAttrW(SQLHDBC ConnectionHandle,
		   SQLINTEGER Attribute,
		   SQLPOINTER ValuePtr,
		   SQLINTEGER StringLength)
{
	ODBCDbc *dbc = (ODBCDbc *) ConnectionHandle;
	SQLPOINTER ptr;
	SQLINTEGER n;
	SQLRETURN rc;

#ifdef ODBCDEBUG
	ODBCLOG("SQLSetConnectAttrW %p %s %p %d\n",
		ConnectionHandle,
		translateConnectAttribute(Attribute),
		ValuePtr, (int) StringLength);
#endif

	if (!isValidDbc(dbc))
		return SQL_INVALID_HANDLE;

	clearDbcErrors(dbc);

	switch (Attribute) {
	case SQL_ATTR_CURRENT_CATALOG:
	case SQL_ATTR_TRACEFILE:
	case SQL_ATTR_TRANSLATE_LIB:
		if (StringLength > 0)	/* convert from bytes to characters */
			StringLength /= 2;
		fixWcharIn(ValuePtr, StringLength, SQLCHAR, ptr,
			   addDbcError, dbc, return SQL_ERROR);
		n = SQL_NTS;
		break;
	default:
		ptr = ValuePtr;
		n = StringLength;
		break;
	}

	rc = MNDBSetConnectAttr(dbc, Attribute, ptr, n);

	if (ptr && ptr != ValuePtr)
		free(ptr);

	return rc;
}
