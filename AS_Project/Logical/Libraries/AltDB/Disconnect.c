/********************************************************************
 * Library: AltDB
 * File: Disconnect.c
 ********************************************************************
 * dbDisconnect (Section 7.10). Works regardless of connection state
 * (including BROKEN/FAILED) and takes the slot over from whichever
 * instance currently holds it, since tearing a connection down must
 * not be blockable by a stuck job on another instance.
 ********************************************************************/
#include <bur/plctypes.h>
#include <runtime.h>
#include <AsTCP.h>
#include <AltDB.h>
#include "Common.h"

void dbDisconnect(struct dbDisconnect *inst)
{
	UDINT instAddr = (UDINT)inst;
	UDINT slot;
	UINT generation;
	BOOL continuing;
	INT r;

	if (!inst->enable) {
		if (AltDB_SlotFromHandle(inst->connectionIdent, &slot, &generation) &&
		    gConn[slot].JobKind == JOB_DISCONNECT && gConn[slot].JobOwner == instAddr) {
			AltDB_Unlock(slot);
		}
		inst->status = ERR_FUB_ENABLE_FALSE; inst->dbError = 0;
		return;
	}

	if (!AltDB_SlotFromHandle(inst->connectionIdent, &slot, &generation)) {
		inst->status = dbERR_INVALID_HANDLE; inst->dbError = DB_SQL_INVALID_HANDLE; return;
	}

	continuing = gConn[slot].Locked && gConn[slot].JobKind == JOB_DISCONNECT && gConn[slot].JobOwner == instAddr;
	if (!continuing) {
		gConn[slot].Locked = TRUE;
		gConn[slot].JobOwner = instAddr;
		gConn[slot].JobKind = JOB_DISCONNECT;
	}

	r = AltDB_NetDisconnectStep(slot);
	if (r == NET_IN_PROGRESS) { inst->status = ERR_FUB_BUSY; return; }

	/* invalidate the handle (bump generation) before releasing the slot */
	gConn[slot].Generation = (UINT)((gConn[slot].Generation + 1) & 0x0FFF);
	AltDB_SlotReset(slot);

	inst->status = ERR_OK; inst->dbError = 0;
}
