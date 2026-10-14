/********************************************************************
 * Library: AltDB
 * File: Info.c
 ********************************************************************
 * dbGetInfo, dbGetErrorMessage (Section 7.11).
 *
 * dbGetInfo implements the small set of string info types that can
 * be answered from session data already held by the connection
 * (DBMS name/version, driver name/version, database name). All
 * other info types - including every numeric SQL_* capability flag
 * of Appendix A - are reported as dbERR_NOT_AVAILABLE, which Section
 * 7.11 documents as the correct response for an unsupported info
 * type, so this is a spec-compliant (if intentionally minimal)
 * implementation rather than a stub.
 ********************************************************************/
#include <bur/plctypes.h>
#include <runtime.h>
#include <AsTCP.h>
#include <AltDB.h>
#include "Common.h"
#include <string.h>

void dbGetInfo(struct dbGetInfo *inst)
{
	UDINT instAddr = (UDINT)inst;
	UDINT slot;
	UINT generation;
	const char *text;

	if (!inst->enable) { inst->status = ERR_FUB_ENABLE_FALSE; inst->dbError = 0; return; }
	if (!AltDB_SlotFromHandle(inst->connectionIdent, &slot, &generation)) {
		inst->status = dbERR_INVALID_HANDLE; inst->dbError = DB_SQL_INVALID_HANDLE; return;
	}
	if (gConn[slot].Locked && gConn[slot].JobOwner != instAddr) { inst->status = ERR_FUB_BUSY; return; }

	switch (inst->infoType) {
	case DB_IT_SQL_DBMS_NAME:    text = "Microsoft SQL Server"; break;
	case DB_IT_SQL_DBMS_VER:     text = gConn[slot].ServerVersion; break;
	case DB_IT_SQL_DRIVER_NAME:  text = "AltDB"; break;
	case DB_IT_SQL_DRIVER_VER:   text = "1.0.0000"; break;
	case DB_IT_SQL_DATABASE_NAME: text = gConn[slot].LoginDb; break;
	default: text = 0; break;
	}

	if (text == 0) {
		inst->status = dbERR_NOT_AVAILABLE; inst->dbError = DB_SQL_ERROR;
		return;
	}
	{
		UDINT n = (UDINT)strlen(text);
		UDINT need = n + 1;

		if (inst->serverInfoSize < need) {
			if (inst->pServerInfo != 0 && inst->serverInfoSize > 0) {
				UDINT copyLen = inst->serverInfoSize - 1;
				memcpy((void *)inst->pServerInfo, text, copyLen);
				((char *)inst->pServerInfo)[copyLen] = 0;
			}
			inst->status = dbERR_BUFFER_SIZE; inst->dbError = DB_SQL_ERROR;
			inst->actServerInfoSize = (UINT)need;
			return;
		}
		if (inst->pServerInfo != 0) memcpy((void *)inst->pServerInfo, text, need);
		inst->status = ERR_OK; inst->dbError = 0;
		inst->actServerInfoSize = (UINT)need;
	}
}

void dbGetErrorMessage(struct dbGetErrorMessage *inst)
{
	UDINT instAddr = (UDINT)inst;
	UDINT slot;
	UINT generation;
	DiagRec *rec;
	UDINT need;

	if (!inst->enable) { inst->status = ERR_FUB_ENABLE_FALSE; inst->dbError = 0; return; }
	if (!AltDB_SlotFromHandle(inst->connectionIdent, &slot, &generation)) {
		inst->status = dbERR_INVALID_HANDLE; inst->dbError = DB_SQL_INVALID_HANDLE; return;
	}
	/* available even while the connection is locked by another job or
	 * BROKEN/FAILED, mirroring AsDb's always-on diagnostics access */

	if (gConn[slot].DiagReadIdx >= gConn[slot].DiagCount) {
		memset(inst->sqlState, 0, sizeof(inst->sqlState));
		inst->nativeError = 0;
		inst->actErrorMessageSize = 0;
		inst->status = ERR_OK; inst->dbError = DB_SQL_NO_DATA;
		return;
	}

	rec = &gConn[slot].Diag[gConn[slot].DiagReadIdx];
	need = (UDINT)rec->MessageLen + 1;

	if (inst->errorMessageSize < need) {
		if (inst->pErrorMessage != 0 && inst->errorMessageSize > 0) {
			UDINT copyLen = inst->errorMessageSize - 1;
			memcpy((void *)inst->pErrorMessage, rec->Message, copyLen);
			((char *)inst->pErrorMessage)[copyLen] = 0;
		}
		memcpy(inst->sqlState, rec->SqlState, 6);
		inst->nativeError = rec->NativeError;
		inst->actErrorMessageSize = (INT)need;
		inst->status = dbERR_BUFFER_SIZE; inst->dbError = DB_SQL_ERROR;
		return; /* record remains unread, same contract as dbGetData */
	}

	if (inst->pErrorMessage != 0) memcpy((void *)inst->pErrorMessage, rec->Message, need);
	memcpy(inst->sqlState, rec->SqlState, 6);
	inst->nativeError = rec->NativeError;
	inst->actErrorMessageSize = (INT)need;
	inst->status = ERR_OK;
	inst->dbError = rec->IsError ? -1 : 1;
	gConn[slot].DiagReadIdx++;
}
