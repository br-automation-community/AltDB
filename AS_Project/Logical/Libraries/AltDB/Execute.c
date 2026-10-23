/********************************************************************
 * Library: AltDB
 * File: Execute.c
 ********************************************************************
 * dbExecuteSql (Section 7.5), dbGetAffectedRows (7.6),
 * dbGetColumnCount (7.7).
 ********************************************************************/
#include <bur/plctypes.h>
#include <runtime.h>
#include <AsTCP.h>
#include <AltDB.h>
#include "Common.h"
#include <string.h>

#define EXECSTEP_DRAIN 0
#define EXECSTEP_SEND  1
#define EXECSTEP_RECV  2

static BOOL IsBrokenState(UINT state)
{
	return (state == CONNSTATE_BROKEN || state == CONNSTATE_FAILED);
}

void dbExecuteSql(struct dbExecuteSql *inst)
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
		if (gConn[slot].JobKind != JOB_EXECUTE) {
			AltDB_DiagPushClient(slot, "HY010", "function sequence error");
			inst->status = dbERR_DATABASE; inst->dbError = DB_SQL_ERROR; return;
		}
	}

	if (fresh) {
		const char *sql;
		UDINT sqlLen;

		if (!AltDB_TryLock(slot, instAddr)) { inst->status = ERR_FUB_BUSY; return; }
		gConn[slot].JobKind = JOB_EXECUTE;

		sql = (const char *)inst->pSqlStatement;
		if (inst->pSqlStatement == 0 || sql[0] == 0) {
			AltDB_Unlock(slot);
			inst->status = dbERR_INVALID_SQL_STATEMENT; inst->dbError = DB_SQL_ERROR; return;
		}
		sqlLen = (UDINT)strlen(sql);
		if (sqlLen > SQL_MAX_LEN) {
			AltDB_Unlock(slot);
			inst->status = dbERR_INVALID_SQL_STATEMENT; inst->dbError = DB_SQL_ERROR; return;
		}

		AltDB_BuildSqlBatch(slot, sql, sqlLen);
		gConn[slot].NetStep = gConn[slot].ResultSetOpen ? EXECSTEP_DRAIN : EXECSTEP_SEND;
	}

	if (gConn[slot].NetStep == EXECSTEP_DRAIN) {
		INT pumped = AltDB_NetRecvPump(slot);
		INT tok;

		if (pumped < 0) {
			AltDB_Unlock(slot); gConn[slot].State = CONNSTATE_BROKEN;
			inst->status = dbERR_DATABASE; inst->dbError = DB_SQL_ERROR; return;
		}
		tok = AltDB_TokenStep(slot, TOKMODE_DRAIN);
		if (tok == TOK_NEED_MORE) { inst->status = ERR_FUB_BUSY; return; }
		/* whether drain ended cleanly or hit an error, the stale result
		 * is considered discarded either way (best-effort, Common.h) */
		gConn[slot].ResultSetOpen = FALSE;
		gConn[slot].NetStep = EXECSTEP_SEND;
		inst->status = ERR_FUB_BUSY;
		return;
	}

	if (gConn[slot].NetStep == EXECSTEP_SEND) {
		INT r = AltDB_NetSendStep(slot);

		if (r == NET_ERROR) {
			AltDB_Unlock(slot); gConn[slot].State = CONNSTATE_BROKEN;
			inst->status = dbERR_DATABASE; inst->dbError = DB_SQL_ERROR; return;
		}
		if (r == NET_DONE) {
			AltDB_DiagClear(slot);
			gConn[slot].ColumnCount = 0;
			gConn[slot].HasResultSet = FALSE;
			gConn[slot].ResultSetOpen = FALSE;
			gConn[slot].AffectedRows = 0;
			gConn[slot].AffectedRowsKnown = FALSE;
			gConn[slot].RowReady = FALSE;
			gConn[slot].NoMoreRows = FALSE;
			gConn[slot].RowOverflow = FALSE;
			gConn[slot].NetStep = EXECSTEP_RECV;
		}
		inst->status = ERR_FUB_BUSY;
		return;
	}

	/* EXECSTEP_RECV */
	{
		INT pumped = AltDB_NetRecvPump(slot);
		INT tok;

		if (pumped < 0) {
			AltDB_Unlock(slot); gConn[slot].State = CONNSTATE_BROKEN;
			inst->status = dbERR_DATABASE; inst->dbError = DB_SQL_ERROR; return;
		}
		tok = AltDB_TokenStep(slot, TOKMODE_EXEC);
		switch (tok) {
		case TOK_NEED_MORE:
			inst->status = ERR_FUB_BUSY;
			return;
		case TOK_GOT_COLMETADATA:
		case TOK_GOT_DONE_FINAL:
			AltDB_Unlock(slot);
			gConn[slot].StatementActive = TRUE;
			inst->status = ERR_OK;
			inst->dbError = (gConn[slot].DiagCount > 0) ? 1 : 0;
			return;
		case TOK_ERROR_MEMORY:
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

void dbGetAffectedRows(struct dbGetAffectedRows *inst)
{
	UDINT instAddr = (UDINT)inst;
	UDINT slot;
	UINT generation;

	if (!inst->enable) { inst->status = ERR_FUB_ENABLE_FALSE; inst->dbError = 0; return; }
	if (!AltDB_SlotFromHandle(inst->connectionIdent, &slot, &generation)) {
		inst->status = dbERR_INVALID_HANDLE; inst->dbError = DB_SQL_INVALID_HANDLE; return;
	}
	if (gConn[slot].Locked && gConn[slot].JobOwner != instAddr) { inst->status = ERR_FUB_BUSY; return; }
	if (!gConn[slot].StatementActive) {
		inst->status = dbERR_SQL_EXECUTE_REQUIRED; inst->dbError = DB_SQL_ERROR; return;
	}
	if (gConn[slot].HasResultSet) {
		 inst->status = ERR_OK; inst->dbError = 0; inst->affectedRows = 0xFFFFFFFF; return;
	}
	inst->status = ERR_OK; inst->dbError = 0;
	inst->affectedRows = gConn[slot].AffectedRowsKnown ? gConn[slot].AffectedRows : 0;
}

void dbGetColumnCount(struct dbGetColumnCount *inst)
{
	UDINT instAddr = (UDINT)inst;
	UDINT slot;
	UINT generation;

	if (!inst->enable) { inst->status = ERR_FUB_ENABLE_FALSE; inst->dbError = 0; return; }
	if (!AltDB_SlotFromHandle(inst->connectionIdent, &slot, &generation)) {
		inst->status = dbERR_INVALID_HANDLE; inst->dbError = DB_SQL_INVALID_HANDLE; return;
	}
	if (gConn[slot].Locked && gConn[slot].JobOwner != instAddr) { inst->status = ERR_FUB_BUSY; return; }
	if (!gConn[slot].StatementActive) {
		inst->status = dbERR_SQL_EXECUTE_REQUIRED; inst->dbError = DB_SQL_ERROR; return;
	}
	inst->status = ERR_OK; inst->dbError = 0;
	inst->columnCount = gConn[slot].HasResultSet ? (INT)gConn[slot].ColumnCount : 0;
}
