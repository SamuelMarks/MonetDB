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
 * SQLDriverConnect()
 * CLI Compliance: ODBC (Microsoft)
 *
 * Author: Martin van Dinther
 * Date  : 30 aug 2002
 *
 **********************************************************************/

#include "ODBCGlobal.h"
#include "ODBCDbc.h"
#include "ODBCUtil.h"
#ifdef HAVE_STRINGS_H
#include <strings.h>		/* for strcasecmp */
#else
#include <string.h>
#endif

static int
get_key_attr(SQLCHAR **conn, SQLSMALLINT *nconn, char **key, char **attr)
{
	SQLCHAR *p;
	size_t len;

	*key = *attr = NULL;

	p = *conn;
	if (!**conn)
		return 0;
	while (*nconn > 0 && **conn && **conn != '=' && **conn != ';') {
		(*conn)++;
		(*nconn)--;
	}
	if (*nconn == 0 || !**conn || **conn == ';')
		return 0;
	len = *conn - p;
	*key = malloc(len + 1);
	strncpy(*key, (char *) p, len);
	(*key)[len] = 0;
	(*conn)++;
	(*nconn)--;
	p = *conn;

	if (*nconn > 0 && **conn == '{' && strcasecmp(*key, "DRIVER") == 0) {
		(*conn)++;
		(*nconn)--;
		p++;
		while (*nconn > 0 && **conn && **conn != '}') {
			(*conn)++;
			(*nconn)--;
		}
		len = *conn - p;
		*attr = malloc(len + 1);
		strncpy(*attr, (char *) p, len);
		(*attr)[len] = 0;
		(*conn)++;
		(*nconn)--;
		/* should check that *nconn == 0 || **conn == ';' */
	} else {
		while (*nconn > 0 && **conn && **conn != ';') {
			(*conn)++;
			(*nconn)--;
		}
		len = *conn - p;
		*attr = malloc(len + 1);
		strncpy(*attr, (char *) p, len);
		(*attr)[len] = 0;
	}
	if (*nconn > 0 && **conn) {
		(*conn)++;
		(*nconn)--;
	}
	return 1;
}

static SQLRETURN
SQLDriverConnect_(ODBCDbc *dbc, SQLHWND hWnd, SQLCHAR *szConnStrIn,
		  SQLSMALLINT nConnStrIn, SQLCHAR *szConnStrOut,
		  SQLSMALLINT cbConnStrOutMax, SQLSMALLINT *pnConnStrOut,
		  SQLUSMALLINT nDriverCompletion)
{
	char *key, *attr;
	char *dsn = 0, *uid = 0, *pwd = 0;
	SQLRETURN rc;

	(void) hWnd;		/* Stefan: unused!? */

	/* check connection state, should not be connected */
	if (dbc->Connected) {
		/* 08002 = Connection already in use */
		addDbcError(dbc, "08002", NULL, 0);
		return SQL_ERROR;
	}
	assert(!dbc->Connected);

	fixODBCstring(szConnStrIn, nConnStrIn, addDbcError, dbc);

#ifdef ODBCDEBUG
	ODBCLOG("\"%.*s\" %d\n", nConnStrIn, szConnStrIn, nDriverCompletion);
#endif

	/* check input arguments */
	switch (nDriverCompletion) {
	case SQL_DRIVER_PROMPT:
	case SQL_DRIVER_COMPLETE:
	case SQL_DRIVER_COMPLETE_REQUIRED:
	case SQL_DRIVER_NOPROMPT:
		break;
	default:
		/* HY092 = Invalid attribute/option identifier */
		addDbcError(dbc, "HY092", NULL, 0);
		return SQL_ERROR;
	}

	while (get_key_attr(&szConnStrIn, &nConnStrIn, &key, &attr)) {
		if (strcasecmp(key, "DSN") == 0 && dsn == NULL)
			dsn = attr;
		else if (strcasecmp(key, "UID") == 0 && uid == NULL)
			uid = attr;
		else if (strcasecmp(key, "PWD") == 0 && pwd == NULL)
			pwd = attr;
		else
			free(attr);
		free(key);
	}

	if (dsn && strlen(dsn) > SQL_MAX_DSN_LENGTH) {
		/* IM010 = Data source name too long */
		addDbcError(dbc, "IM010", NULL, 0);
		rc = SQL_ERROR;
	} else {
		rc = SQLConnect_(dbc, (SQLCHAR *) dsn, SQL_NTS,
				 (SQLCHAR *) uid, SQL_NTS,
				 (SQLCHAR *) pwd, SQL_NTS);
	}

	if (SQL_SUCCEEDED(rc)) {
		int n;

		if (szConnStrOut == NULL)
			cbConnStrOutMax = -1;
		if (cbConnStrOutMax > 0) {
			n = snprintf((char *) szConnStrOut, 
				cbConnStrOutMax, "DSN=%s;",
				dsn ? dsn : "DEFAULT");
			/* some snprintf's return -1 if buffer too small */
			if (n < 0)
				n = cbConnStrOutMax + 1; /* make sure it becomes < 0 */
			cbConnStrOutMax -= n;
			szConnStrOut += n;
		} else {
			cbConnStrOutMax = -1;
		}
		if (uid) {
			if (cbConnStrOutMax > 0) {
				n = snprintf((char *) szConnStrOut, 
					     cbConnStrOutMax,
					     "UID=%s;", uid);
				if (n < 0)
					n = cbConnStrOutMax + 1;
				cbConnStrOutMax -= n;
				szConnStrOut += n;
			} else {
				cbConnStrOutMax = -1;
			}
		}
		if (pwd) {
			if (cbConnStrOutMax > 0) {
				n = snprintf((char *) szConnStrOut, 
					     cbConnStrOutMax,
					     "PWD=%s;", pwd);
				if (n < 0)
					n = cbConnStrOutMax + 1;
				cbConnStrOutMax -= n;
				szConnStrOut += n;
			} else {
				cbConnStrOutMax = -1;
			}
		}

		/* calculate how much space was needed */
		if (pnConnStrOut)
			*pnConnStrOut = strlen(dsn ? dsn : "DEFAULT") 
				+ 5 +
				(uid ? strlen(uid) + 5 : 0) +
				(pwd ? strlen(pwd) + 5 : 0);

		/* if it didn't fit, say so */
		if (cbConnStrOutMax < 0) {
			addDbcError(dbc, "01004", NULL, 0);
			rc = SQL_SUCCESS_WITH_INFO;
		}
	}
	if (dsn)
		free(dsn);
	if (uid)
		free(uid);
	if (pwd)
		free(pwd);
	return rc;
}

SQLRETURN SQL_API
SQLDriverConnect(SQLHDBC hDbc, SQLHWND hWnd, SQLCHAR *szConnStrIn,
		 SQLSMALLINT nConnStrIn, SQLCHAR *szConnStrOut,
		 SQLSMALLINT cbConnStrOutMax, SQLSMALLINT *pnConnStrOut,
		 SQLUSMALLINT nDriverCompletion)
{
	ODBCDbc *dbc = (ODBCDbc *) hDbc;

#ifdef ODBCDEBUG
	ODBCLOG("SQLDriverConnect ");
#endif

	if (!isValidDbc(dbc))
		return SQL_INVALID_HANDLE;

	clearDbcErrors(dbc);

	return SQLDriverConnect_(dbc, hWnd, szConnStrIn, nConnStrIn,
				 szConnStrOut, cbConnStrOutMax, pnConnStrOut,
				 nDriverCompletion);
}

SQLRETURN SQL_API
SQLDriverConnectW(SQLHDBC hDbc, SQLHWND hWnd, SQLWCHAR *szConnStrIn,
		  SQLSMALLINT nConnStrIn, SQLWCHAR *szConnStrOut,
		  SQLSMALLINT cbConnStrOutMax, SQLSMALLINT *pnConnStrOut,
		  SQLUSMALLINT nDriverCompletion)
{
	ODBCDbc *dbc = (ODBCDbc *) hDbc;
	SQLCHAR *in = NULL, *out;
	SQLSMALLINT n;
	SQLRETURN rc;

#ifdef ODBCDEBUG
	ODBCLOG("SQLDriverConnectW ");
#endif

	if (!isValidDbc(dbc))
		return SQL_INVALID_HANDLE;

	clearDbcErrors(dbc);

	fixWcharIn(szConnStrIn, nConnStrIn, in, addDbcError, dbc, return SQL_ERROR);
	prepWcharOut(out, cbConnStrOutMax);

	rc = SQLDriverConnect_(dbc, hWnd, in, SQL_NTS, out,
			       cbConnStrOutMax * 4, &n, nDriverCompletion);

	fixWcharOut(rc, out, n, szConnStrOut, cbConnStrOutMax, pnConnStrOut, addDbcError, dbc);
	if (out)
		free(out);
	if (in)
		free(in);
	return rc;
}
