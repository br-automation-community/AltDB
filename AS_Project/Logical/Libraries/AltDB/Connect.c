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

/* Parsed/validated dbConnect/dbConnectEnc inputs, independent of any
 * connection slot (no slot is needed just to validate). */
typedef struct ParsedConnectInputs
{
	char   hostBuf[128];	/* NUL-terminated, port and named-instance suffix stripped */
	UDINT  hostLen;
	UINT   port;
	UINT   encoding;		/* DBENC_ISO_8859_1 / DBENC_UTF_8 */
	const char *user;
	UDINT  userLen;
	const char *pass;
	UDINT  passLen;
	const char *db;
	UDINT  dbLen;
} ParsedConnectInputs;

/* Validates dbConnect/dbConnectEnc inputs and fills *out. Performs no
 * slot allocation or lookup, so the caller can validate (cheap) before
 * committing to a slot allocation or lock (preserves AsDb's original
 * error precedence, e.g. an unsupported databaseSystem is reported
 * even when the connection pool happens to be full). On failure,
 * status and dbError are set and *out is left in an undefined state. */
static BOOL ValidateAndParseConnectInputs(UDINT pServerName, UDINT pUserName, UDINT pPassword,
	UDINT pDatabaseName, UDINT databaseSystem, UDINT pEncoding,
	ParsedConnectInputs *out, UINT *status, INT *dbError)
{
	const char *server = (const char *)pServerName;
	const char *colon;
	const char *backslash;
	char hostPortBuf[128];
	UDINT hostPortLen;

	if (databaseSystem != DB_SYSTEM_MS_SQL) {
		*status = dbERR_UNSUPPORTED_DATABASE; *dbError = DB_SQL_ERROR; return FALSE;
	}
	if (pServerName == 0 || pUserName == 0 || pPassword == 0 || pDatabaseName == 0) {
		*status = dbERR_NULLPOINTER; *dbError = DB_SQL_ERROR; return FALSE;
	}
	out->user = (const char *)pUserName;
	out->pass = (const char *)pPassword;
	out->db = (const char *)pDatabaseName;
	out->userLen = (UDINT)strlen(out->user);
	out->passLen = (UDINT)strlen(out->pass);
	out->dbLen = (UDINT)strlen(out->db);

	if (out->userLen > 128) { *status = dbERR_INVALID_USERNAME; *dbError = DB_SQL_ERROR; return FALSE; }
	if (out->passLen > 128) { *status = dbERR_INVALID_PASSWORD; *dbError = DB_SQL_ERROR; return FALSE; }
	if (out->dbLen < 1 || out->dbLen > 128) {
		*status = dbERR_INVALID_DATABASE_NAME; *dbError = DB_SQL_ERROR; return FALSE;
	}

	/* Strip an optional backslash-prefixed instance-name suffix (SQL
	 * Server named-instance syntax, e.g. "192.168.66.99:1433" followed
	 * by "SQLEXPRESS"). Named-instance resolution via the SQL Server
	 * Browser service (UDP 1434) is out of scope for this library,
	 * exactly as for the original AsDb (a fixed TCP port is required
	 * either way), so the instance name is accepted for drop-in
	 * compatibility but otherwise ignored; only the host[:port] part
	 * before the backslash is parsed and used to connect. */
	backslash = strchr(server, '\\');
	hostPortLen = backslash ? (UDINT)(backslash - server) : (UDINT)strlen(server);
	if (hostPortLen == 0 || hostPortLen > sizeof(hostPortBuf) - 1) {
		*status = dbERR_INVALID_SERVERNAME; *dbError = DB_SQL_ERROR; return FALSE;
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
		out->hostLen = (UDINT)(colon - hostPortBuf);
		if (!okDigits || p < 1 || p > 65535 || out->hostLen == 0) {
			*status = dbERR_INVALID_SERVERNAME; *dbError = DB_SQL_ERROR; return FALSE;
		}
		out->port = (UINT)p;
	} else {
		out->hostLen = (UDINT)strlen(hostPortBuf);
		if (out->hostLen == 0) {
			*status = dbERR_INVALID_SERVERNAME; *dbError = DB_SQL_ERROR; return FALSE;
		}
		out->port = 1433; /* default MS SQL port, [V-03] */
	}
	memcpy(out->hostBuf, hostPortBuf, out->hostLen); out->hostBuf[out->hostLen] = 0;

	out->encoding = DBENC_ISO_8859_1;
	if (pEncoding != 0) {
		const char *enc = (const char *)pEncoding;
		if (EqualsIgnoreCase(enc, "UTF-8")) out->encoding = DBENC_UTF_8;
		else if (EqualsIgnoreCase(enc, "ISO-8859-1")) out->encoding = DBENC_ISO_8859_1;
		else {
			*status = dbERR_UNSUPPORTED_ENCODING; *dbError = DB_SQL_ERROR; return FALSE;
		}
	}

	return TRUE;
}

/* Copies already-validated inputs into the slot's login parameters.
 * Shared by (a) the first allocation of a new connection and (b)
 * re-capturing possibly-corrected inputs after a connect job was
 * abandoned via enable := FALSE while still in progress (Section 9.3
 * job cancellation; see CustomerChangeAnalysis.md item 1). */
static void CommitConnectInputsToSlot(UDINT slot, const ParsedConnectInputs *in)
{
	gConn[slot].Encoding = in->encoding;
	memcpy(gConn[slot].HostBuf, in->hostBuf, in->hostLen + 1);
	gConn[slot].Port = in->port;
	gConn[slot].LoginUserLen = in->userLen;
	memcpy(gConn[slot].LoginUser, in->user, in->userLen + 1);
	gConn[slot].LoginPassLen = in->passLen;
	memcpy(gConn[slot].LoginPass, in->pass, in->passLen + 1);
	gConn[slot].LoginDbLen = in->dbLen;
	memcpy(gConn[slot].LoginDb, in->db, in->dbLen + 1);
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
			/* Tear down the in-flight connection instead of leaving a
			 * half-finished handshake (and a leaked TCP socket) behind.
			 * Best-effort and non-blocking: if the close needs more
			 * cycles than this single call, it is completed either by
			 * a later enable := FALSE call on this same instance or by
			 * the Abandoned-recapture path below on the next
			 * dbConnect/dbConnectEnc call for this handle. */
			AltDB_NetDisconnectStep(slot);
			gConn[slot].Abandoned = TRUE;
			AltDB_Unlock(slot);
		}
		*status = ERR_FUB_ENABLE_FALSE; *dbError = 0;
		return;
	}

	if (*connectionIdent == 0) {
		ParsedConnectInputs parsed;

		if (!ValidateAndParseConnectInputs(pServerName, pUserName, pPassword, pDatabaseName,
		databaseSystem, pEncoding, &parsed, status, dbError)) {
			return; /* no slot allocated yet, nothing to release */
		}
		if (!AltDB_SlotAlloc(&slot)) { *status = dbERR_MEMORY; *dbError = DB_SQL_ERROR; return; }
		generation = gConn[slot].Generation;

		CommitConnectInputsToSlot(slot, &parsed);
		gConn[slot].JobKind = JOB_CONNECT;
		gConn[slot].State = CONNSTATE_OPENING;
		AltDB_DiagClear(slot);

		*connectionIdent = AltDB_MakeHandle(slot, generation);
	} else {
		if (!AltDB_SlotFromHandle(*connectionIdent, &slot, &generation)) {
			*status = dbERR_DATABASE; *dbError = DB_SQL_ERROR; return;
		}

		if (gConn[slot].Abandoned) {
			/* Previous connect job on this handle was cancelled
			 * mid-flight (enable := FALSE while BUSY). Re-validate and
			 * re-capture the current inputs (the application may have
			 * corrected them, e.g. the password) before touching the
			 * lock, then finish closing any socket left over from the
			 * abandoned attempt and restart the handshake from
			 * CONNSTATE_OPENING rather than blindly resuming stale
			 * state. */
			ParsedConnectInputs parsed;

			if (!ValidateAndParseConnectInputs(pServerName, pUserName, pPassword, pDatabaseName,
			databaseSystem, pEncoding, &parsed, status, dbError)) {
				return; /* still Abandoned, lock untouched; caller may retry */
			}
			if (!AltDB_TryLock(slot, instAddr)) { *status = ERR_FUB_BUSY; return; }
			if (AltDB_NetDisconnectStep(slot) == NET_IN_PROGRESS) {
				*status = ERR_FUB_BUSY; return; /* lock held; finish closing on the next call */
			}
			CommitConnectInputsToSlot(slot, &parsed);
			gConn[slot].Abandoned = FALSE;
			gConn[slot].JobKind = JOB_CONNECT;
			gConn[slot].State = CONNSTATE_OPENING;
			AltDB_DiagClear(slot);
		} else if (gConn[slot].JobKind != JOB_CONNECT) {
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
