/********************************************************************
 * Library: AltDB
 * File: ConnState.c
 ********************************************************************
 * dbGetConnState (Section 7.12, extension FB - not part of AsDb).
 * Synchronous: returns a snapshot of the internal connection state
 * machine so a connection that appears stuck (e.g. a dbConnect/
 * dbExecuteSql/dbFetchNextRow call that keeps returning
 * ERR_FUB_BUSY) can be diagnosed from the calling IEC/C program
 * without instrumenting this library's C sources.
 ********************************************************************/
#include <bur/plctypes.h>
#include <runtime.h>
#include <AsTCP.h>
#include <AltDB.h>
#include "Common.h"
#include <string.h>

void dbGetConnState(struct dbGetConnState *inst)
{
	UDINT slot;
	UINT generation;
	ConnSlot *c;
	UDINT n;

	if (!inst->enable) { inst->status = ERR_FUB_ENABLE_FALSE; inst->dbError = 0; return; }
	if (!AltDB_SlotFromHandle(inst->connectionIdent, &slot, &generation)) {
		inst->status = dbERR_INVALID_HANDLE; inst->dbError = DB_SQL_INVALID_HANDLE; return;
	}

	c = &gConn[slot];
	memset(&inst->connState, 0, sizeof(inst->connState));

	inst->connState.state = c->State;
	inst->connState.netStep = c->NetStep;
	inst->connState.jobKind = c->JobKind;
	inst->connState.locked = c->Locked;
	inst->connState.tcpIdent = c->TcpIdent;
	inst->connState.encoding = c->Encoding;
	inst->connState.packetSize = c->PacketSize;
	inst->connState.sendLen = c->SendLen;
	inst->connState.sendPos = c->SendPos;
	inst->connState.tokLen = c->TokLen;
	inst->connState.tokPos = c->TokPos;
	inst->connState.statementActive = c->StatementActive;
	inst->connState.columnCount = c->ColumnCount;
	inst->connState.hasResultSet = c->HasResultSet;
	inst->connState.resultSetOpen = c->ResultSetOpen;
	inst->connState.rowReady = c->RowReady;
	inst->connState.noMoreRows = c->NoMoreRows;
	inst->connState.affectedRows = c->AffectedRows;
	inst->connState.affectedRowsKnown = c->AffectedRowsKnown;
	inst->connState.diagCount = c->DiagCount;
	inst->connState.diagReadIdx = c->DiagReadIdx;

	/* IEC STRING[31] is generated as a plain NUL-terminated char[32]
	 * (confirmed against AsTCP.h's AsTcpMcsType.name), not a
	 * length-prefixed Pascal string. */
	n = (UDINT)strlen(c->ServerVersion);
	if (n > 31) n = 31;
	memcpy((char *)&inst->connState.serverVersion, c->ServerVersion, n);
	((char *)&inst->connState.serverVersion)[n] = 0;

	inst->status = ERR_OK;
	inst->dbError = 0;
}
