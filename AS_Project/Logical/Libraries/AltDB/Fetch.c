/********************************************************************
 * Library: AltDB
 * File: Fetch.c
 ********************************************************************
 * dbFetchNextRow (Section 7.8), dbGetData (7.9).
 ********************************************************************/
#include <bur/plctypes.h>
#include <runtime.h>
#include <AsTCP.h>
#include <AltDB.h>
#include "Common.h"

static BOOL IsBrokenState(UINT state)
{
	return (state == CONNSTATE_BROKEN || state == CONNSTATE_FAILED);
}

void dbFetchNextRow(struct dbFetchNextRow *inst)
{
	UDINT instAddr = (UDINT)inst;
	UDINT slot;
	UINT generation;
	BOOL fresh;

	if (!inst->enable) {
		if (AltDB_SlotFromHandle(inst->connectionIdent, &slot, &generation) && AltDB_IsOwner(slot, instAddr)) {
			AltDB_Unlock(slot);
		}
		inst->status = ERR_FUB_ENABLE_FALSE; inst->dbError = 0;
		return;
	}

	if (!AltDB_SlotFromHandle(inst->connectionIdent, &slot, &generation)) {
		inst->status = dbERR_INVALID_HANDLE; inst->dbError = DB_SQL_INVALID_HANDLE; return;
	}
	if (IsBrokenState(gConn[slot].State)) {
		AltDB_DiagPushClient(slot, "08S01", "connection is broken, call dbDisconnect");
		inst->status = dbERR_DATABASE; inst->dbError = DB_SQL_ERROR; return;
	}

	fresh = !gConn[slot].Locked;
	if (!fresh) {
		if (gConn[slot].JobOwner != instAddr) { inst->status = ERR_FUB_BUSY; return; }
		if (gConn[slot].JobKind != JOB_FETCH) {
			AltDB_DiagPushClient(slot, "HY010", "function sequence error");
			inst->status = dbERR_DATABASE; inst->dbError = DB_SQL_ERROR; return;
		}
	}

	if (fresh) {
		if (!AltDB_TryLock(slot, instAddr)) { inst->status = ERR_FUB_BUSY; return; }
		gConn[slot].JobKind = JOB_FETCH;
	}

	if (!gConn[slot].ResultSetOpen) {
		/* end of result set, or no SELECT result ever opened - the
		 * default AsDb end-of-data signalling [V-04] */
		AltDB_Unlock(slot);
		inst->status = dbERR_DATABASE; inst->dbError = DB_SQL_NO_DATA;
		return;
	}

	{
		INT pumped = AltDB_NetRecvPump(slot);
		INT tok;

		if (pumped < 0) {
			AltDB_Unlock(slot); gConn[slot].State = CONNSTATE_BROKEN;
			inst->status = dbERR_DATABASE; inst->dbError = DB_SQL_ERROR; return;
		}
		tok = AltDB_TokenStep(slot, TOKMODE_FETCH);
		switch (tok) {
		case TOK_NEED_MORE:
			inst->status = ERR_FUB_BUSY;
			return;
		case TOK_GOT_ROW:
			AltDB_Unlock(slot);
			inst->status = ERR_OK; inst->dbError = 0;
			return;
		case TOK_GOT_EOF:
			AltDB_Unlock(slot);
			inst->status = dbERR_DATABASE; inst->dbError = DB_SQL_NO_DATA;
			return;
		case TOK_ERROR_MEMORY:
			gConn[slot].ResultSetOpen = FALSE;
			gConn[slot].NoMoreRows = TRUE;
			AltDB_Unlock(slot);
			inst->status = dbERR_MEMORY; inst->dbError = DB_SQL_ERROR;
			return;
		default: /* TOK_ERROR */
			AltDB_Unlock(slot);
			inst->status = dbERR_DATABASE; inst->dbError = DB_SQL_ERROR;
			return;
		}
	}
}

void dbGetData(struct dbGetData *inst)
{
	UDINT instAddr = (UDINT)inst;
	UDINT slot;
	UINT generation;

	if (!inst->enable) { inst->status = ERR_FUB_ENABLE_FALSE; inst->dbError = 0; return; }
	if (!AltDB_SlotFromHandle(inst->connectionIdent, &slot, &generation)) {
		inst->status = dbERR_INVALID_HANDLE; inst->dbError = DB_SQL_INVALID_HANDLE; return;
	}
	if (gConn[slot].Locked && gConn[slot].JobOwner != instAddr) {
		AltDB_DiagPushClient(slot, "HY010", "function sequence error");
		inst->status = dbERR_DATABASE; inst->dbError = DB_SQL_ERROR; return;
	}

	AltDB_GetData(slot, inst->columnIdx, inst->dataType, inst->pData, inst->dataSize,
	                &inst->status, &inst->dbError, &inst->actDataSize);
}
