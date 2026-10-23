/********************************************************************
 * Library: AltDB
 * File: Connect.c
 ********************************************************************
 * dbConnect / dbConnectEnc (Section 7.2-7.4).
 ********************************************************************/
#include <bur/plctypes.h>
#include <runtime.h>
#include <AsTCP.h>
#include <AltDB.h>
#include "Common.h"
#include <string.h>

static BOOL EqualsIgnoreCase(const char *a, const char *b)
{
	while (*a && *b) {
		char ca = *a, cb = *b;
		if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 'a' + 'A');
		if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 'a' + 'A');
		if (ca != cb) return FALSE;
		a++; b++;
	}
	return (*a == 0 && *b == 0);
}

static void ConnectCommon(UDINT instAddr, BOOL enable, UDINT pServerName, UDINT pUserName, UDINT pPassword,
                          UDINT pDatabaseName, UDINT databaseSystem, UDINT pEncoding,
                          UINT *status, INT *dbError, UDINT *connectionIdent)
{
	UDINT slot;
	UINT generation;
	INT r;

	if (!enable) {
		if (*connectionIdent != 0 && AltDB_SlotFromHandle(*connectionIdent, &slot, &generation) &&
		    AltDB_IsOwner(slot, instAddr)) {
			AltDB_Unlock(slot);
		}
		*status = ERR_FUB_ENABLE_FALSE; *dbError = 0;
		return;
	}

	if (*connectionIdent == 0) {
		const char *server = (const char *)pServerName;
		const char *user = (const char *)pUserName;
		const char *pass = (const char *)pPassword;
		const char *db = (const char *)pDatabaseName;
		const char *colon;
		const char *backslash;
		char hostPortBuf[128];
		UDINT hostPortLen;
		char hostBuf[128];
		UDINT hostLen;
		UINT port;
		UDINT dbLen;

		if (databaseSystem != DB_SYSTEM_MS_SQL) {
			*status = dbERR_UNSUPPORTED_DATABASE; *dbError = DB_SQL_ERROR; return;
		}
		if (pServerName == 0 || pUserName == 0 || pPassword == 0 || pDatabaseName == 0) {
			*status = dbERR_NULLPOINTER; *dbError = DB_SQL_ERROR; return;
		}
		if (strlen(user) > 128) { *status = dbERR_INVALID_USERNAME; *dbError = DB_SQL_ERROR; return; }
		if (strlen(pass) > 128) { *status = dbERR_INVALID_PASSWORD; *dbError = DB_SQL_ERROR; return; }
		dbLen = (UDINT)strlen(db);
		if (dbLen < 1 || dbLen > 128) { *status = dbERR_INVALID_DATABASE_NAME; *dbError = DB_SQL_ERROR; return; }

		/* Strip an optional "\instanceName" suffix (SQL Server named-
		 * instance syntax, e.g. "192.168.66.99:1433\SQLEXPRESS").
		 * Named-instance resolution via the SQL Server Browser service
		 * (UDP 1434) is out of scope for this library, exactly as for
		 * the original AsDb (a fixed TCP port is required either way),
		 * so the instance name is accepted for drop-in compatibility
		 * but otherwise ignored; only the host[:port] part before the
		 * backslash is parsed and used to connect. */
		backslash = strchr(server, '\\');
		hostPortLen = backslash ? (UDINT)(backslash - server) : (UDINT)strlen(server);
		if (hostPortLen == 0 || hostPortLen > sizeof(hostPortBuf) - 1) {
			*status = dbERR_INVALID_SERVERNAME; *dbError = DB_SQL_ERROR; return;
		}
		memcpy(hostPortBuf, server, hostPortLen); hostPortBuf[hostPortLen] = 0;

		colon = strrchr(hostPortBuf, ':');
		if (colon != NULL) {
			long p = 0;
			UDINT i;
			BOOL okDigits = (colon[1] != 0);
			for (i = 0; colon[1 + i] != 0; i++) {
				char ch = colon[1 + i];
				if (ch < '0' || ch > '9') { okDigits = FALSE; break; }
				p = p * 10 + (ch - '0');
			}
			hostLen = (UDINT)(colon - hostPortBuf);
			if (!okDigits || p < 1 || p > 65535 || hostLen == 0) {
				*status = dbERR_INVALID_SERVERNAME; *dbError = DB_SQL_ERROR; return;
			}
			port = (UINT)p;
		} else {
			hostLen = (UDINT)strlen(hostPortBuf);
			if (hostLen == 0) {
				*status = dbERR_INVALID_SERVERNAME; *dbError = DB_SQL_ERROR; return;
			}
			port = 1433; /* default MS SQL port, [V-03] */
		}
		memcpy(hostBuf, hostPortBuf, hostLen); hostBuf[hostLen] = 0;

		if (!AltDB_SlotAlloc(&slot)) { *status = dbERR_MEMORY; *dbError = DB_SQL_ERROR; return; }
		generation = gConn[slot].Generation;

		gConn[slot].JobKind = JOB_CONNECT;
		gConn[slot].State = CONNSTATE_OPENING;
		gConn[slot].Encoding = DBENC_ISO_8859_1;
		if (pEncoding != 0) {
			const char *enc = (const char *)pEncoding;
			if (EqualsIgnoreCase(enc, "UTF-8")) gConn[slot].Encoding = DBENC_UTF_8;
			else if (EqualsIgnoreCase(enc, "ISO-8859-1")) gConn[slot].Encoding = DBENC_ISO_8859_1;
			else {
				gConn[slot].State = CONNSTATE_FREE; /* release the slot again */
				*status = dbERR_UNSUPPORTED_ENCODING; *dbError = DB_SQL_ERROR; return;
			}
		}
		memcpy(gConn[slot].HostBuf, hostBuf, hostLen + 1);
		gConn[slot].Port = port;
		gConn[slot].LoginUserLen = (UDINT)strlen(user);
		memcpy(gConn[slot].LoginUser, user, gConn[slot].LoginUserLen + 1);
		gConn[slot].LoginPassLen = (UDINT)strlen(pass);
		memcpy(gConn[slot].LoginPass, pass, gConn[slot].LoginPassLen + 1);
		gConn[slot].LoginDbLen = dbLen;
		memcpy(gConn[slot].LoginDb, db, dbLen + 1);
		AltDB_DiagClear(slot);

		*connectionIdent = AltDB_MakeHandle(slot, generation);
	} else {
		if (!AltDB_SlotFromHandle(*connectionIdent, &slot, &generation) || gConn[slot].JobKind != JOB_CONNECT) {
			*status = dbERR_DATABASE; *dbError = DB_SQL_ERROR; return;
		}
	}

	if (!AltDB_TryLock(slot, instAddr)) { *status = ERR_FUB_BUSY; return; }

	r = AltDB_NetConnectStep(slot);
	if (r == NET_IN_PROGRESS) { *status = ERR_FUB_BUSY; return; }

	AltDB_Unlock(slot);
	if (r == NET_DONE) {
		*status = ERR_OK;
		*dbError = 0;
	} else {
		*status = dbERR_DATABASE;
		*dbError = DB_SQL_ERROR;
	}
}

void dbConnect(struct dbConnect *inst)
{
	ConnectCommon((UDINT)inst, inst->enable, inst->pServerName, inst->pUserName, inst->pPassword,
	              inst->pDatabaseName, inst->databaseSystem, 0,
	              &inst->status, &inst->dbError, &inst->connectionIdent);
}

void dbConnectEnc(struct dbConnectEnc *inst)
{
	ConnectCommon((UDINT)inst, inst->enable, inst->pServerName, inst->pUserName, inst->pPassword,
	              inst->pDatabaseName, inst->databaseSystem, inst->pEncoding,
	              &inst->status, &inst->dbError, &inst->connectionIdent);
}
