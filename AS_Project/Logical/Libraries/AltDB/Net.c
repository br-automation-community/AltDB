/********************************************************************
 * Library: AltDB
 * File: Net.c
 ********************************************************************
 * TCP transport (driving AsTCP's TcpOpen/TcpClient/TcpSend/TcpRecv/
 * TcpClose asynchronous function blocks from C), TDS packet framing,
 * PRELOGIN and LOGIN7 message construction.
 *
 * Every outgoing message (PRELOGIN, LOGIN7, SQL batch) is built as a
 * single TDS packet (8-byte header + payload) in SendBuf; see
 * Common.h for the resulting, documented limit on SQL statement
 * length (SQL_MAX_LEN).
 ********************************************************************/
#include <bur/plctypes.h>
#include <runtime.h>
#include <AsTCP.h>
#include <AltDB.h>
#include "Common.h"
#include <string.h>

#define NETSTEP_SEND 0
#define NETSTEP_RECV 1

/* ---------------- TCP driving ---------------- */

static INT PumpTcpOpen(UDINT slot)
{
	TcpOpen_typ *p = &gConn[slot].tcpOpen;

	p->enable = TRUE;
	p->pIfAddr = 0;
	p->port = 0;
	p->options = 0;
	TcpOpen(p);
	if (p->status == ERR_FUB_BUSY) return NET_IN_PROGRESS;
	if (p->status == ERR_OK) {
		gConn[slot].TcpIdent = p->ident;
		return NET_DONE;
	}
	AltDB_DiagPushClient(slot, "08001", "unable to open local TCP socket");
	return NET_ERROR;
}

static INT PumpTcpClient(UDINT slot)
{
	TcpClient_typ *p = &gConn[slot].tcpClient;

	p->enable = TRUE;
	p->ident = gConn[slot].TcpIdent;
	p->pServer = (UDINT)(UDINT)&gConn[slot].HostBuf[0];
	p->portserv = gConn[slot].Port;
	TcpClient(p);
	if (p->status == ERR_FUB_BUSY) return NET_IN_PROGRESS;
	if (p->status == ERR_OK) return NET_DONE;
	AltDB_DiagPushClient(slot, "08001", "TCP connect to server failed");
	return NET_ERROR;
}

INT AltDB_NetSendStep(UDINT slot)
{
	TcpSend_typ *p = &gConn[slot].tcpSend;
	ConnSlot *c = &gConn[slot];

	if (c->SendPos >= c->SendLen) return NET_DONE;

	p->enable = TRUE;
	p->ident = c->TcpIdent;
	p->pData = (UDINT)&c->SendBuf[c->SendPos];
	p->datalen = c->SendLen - c->SendPos;
	p->flags = 0;
	TcpSend(p);
	if (p->status == ERR_FUB_BUSY) return NET_IN_PROGRESS;
	if (p->status == ERR_OK) {
		c->SendPos += p->sentlen;
		return (c->SendPos >= c->SendLen) ? NET_DONE : NET_IN_PROGRESS;
	}
	AltDB_DiagPushClient(slot, "08S01", "TCP send failed, connection lost");
	return NET_ERROR;
}

/* Reads any bytes currently available, strips TDS packet headers and
 * appends the logical payload bytes to TokBuf. Returns the number of
 * new logical bytes appended (0 = nothing available right now, a
 * normal/expected result, not an error), or -1 on a hard socket
 * error. */
INT AltDB_NetRecvPump(UDINT slot)
{
	ConnSlot *c = &gConn[slot];
	TcpRecv_typ *p = &gConn[slot].tcpRecv;
	UDINT produced = 0;
	UDINT si;
	UDINT avail;

	/* compact TokBuf if the parser has already consumed a large prefix */
	if (c->TokPos > 0 && (c->TokPos == c->TokLen || c->TokPos > (UDINT)sizeof(c->TokBuf) / 2)) {
		if (c->TokPos < c->TokLen) {
			memmove(&c->TokBuf[0], &c->TokBuf[c->TokPos], c->TokLen - c->TokPos);
		}
		c->TokLen -= c->TokPos;
		c->TokPos = 0;
	}

	if (c->TokLen >= sizeof(c->TokBuf)) {
		/* parser is not keeping up with a bound buffer; stall receiving */
		return 0;
	}

	p->enable = TRUE;
	p->ident = c->TcpIdent;
	p->pData = (UDINT)&c->RecvScratch[0];
	p->datamax = sizeof(c->RecvScratch);
	p->flags = 0;
	TcpRecv(p);

	if (p->status == ERR_FUB_BUSY) return 0;
	if (p->status != ERR_OK) {
		if (p->status == tcpERR_NO_DATA || p->status == tcpERR_WOULDBLOCK) return 0;
		AltDB_DiagPushClient(slot, "08S01", "TCP receive failed, connection lost");
		return -1;
	}
	avail = p->recvlen;
	if (avail == 0) return 0;

	si = 0;
	while (si < avail) {
		if (c->HeaderGot < 8) {
			UDINT need = 8 - c->HeaderGot;
			UDINT take = (avail - si < need) ? (avail - si) : need;
			memcpy(&c->HeaderBuf[c->HeaderGot], &c->RecvScratch[si], take);
			c->HeaderGot += (USINT)take;
			si += take;
			if (c->HeaderGot < 8) break; /* header itself split across reads */
			c->FrameRemaining = AltDB_ReadBE16(&c->HeaderBuf[2]) - 8;
		}
		{
			UDINT take = avail - si;
			if (take > c->FrameRemaining) take = c->FrameRemaining;
			if (c->TokLen + take > sizeof(c->TokBuf)) {
				take = sizeof(c->TokBuf) - c->TokLen;
			}
			if (take > 0) {
				memcpy(&c->TokBuf[c->TokLen], &c->RecvScratch[si], take);
				c->TokLen += take;
				produced += take;
				si += take;
				c->FrameRemaining -= take;
			}
			if (c->FrameRemaining == 0) {
				c->HeaderGot = 0; /* ready for next physical packet header */
			} else {
				/* buffer pressure: stop for this call, resume next pump */
				break;
			}
		}
	}
	return (INT)produced;
}

INT AltDB_NetDisconnectStep(UDINT slot)
{
	ConnSlot *c = &gConn[slot];
	TcpClose_typ *p = &gConn[slot].tcpClose;

	if (c->TcpIdent == 0) return NET_DONE;

	p->enable = TRUE;
	p->ident = c->TcpIdent;
	p->how = 0;
	TcpClose(p);
	if (p->status == ERR_FUB_BUSY) return NET_IN_PROGRESS;
	c->TcpIdent = 0;
	return NET_DONE;
}

/* ---------------- PRELOGIN ---------------- */

/* PRELOGIN option table: VERSION(6), ENCRYPTION(1), INSTOPT(1),
 * THREADID(4), MARS(1), terminator. Identical layout for request and
 * response (Section 10.3). */
void AltDB_BuildPrelogin(UDINT slot)
{
	ConnSlot *c = &gConn[slot];
	unsigned char *b = c->SendBuf;
	UDINT payloadLen = 26 + 6 + 1 + 1 + 4 + 1; /* 39 */
	UDINT total = 8 + payloadLen;

	memset(b, 0, 8 + payloadLen);
	b[0] = 0x12; b[1] = 0x01; /* type PRELOGIN, status EOM */
	AltDB_WriteBE16(&b[2], (UINT)total);
	b[4] = 0; b[5] = 0; b[6] = 1; b[7] = 0;

	{
		unsigned char *p = &b[8];
		UINT off = 26;

		p[0] = 0x00; AltDB_WriteBE16(&p[1], off); AltDB_WriteBE16(&p[3], 6); off += 6;
		p[5] = 0x01; AltDB_WriteBE16(&p[6], off); AltDB_WriteBE16(&p[8], 1); off += 1;
		p[10] = 0x02; AltDB_WriteBE16(&p[11], off); AltDB_WriteBE16(&p[13], 1); off += 1;
		p[15] = 0x03; AltDB_WriteBE16(&p[16], off); AltDB_WriteBE16(&p[18], 4); off += 4;
		p[20] = 0x04; AltDB_WriteBE16(&p[21], off); AltDB_WriteBE16(&p[23], 1); off += 1;
		p[25] = 0xFF;

		p[26] = 0x0F; p[27] = 0x00; p[28] = 0x00; p[29] = 0x00; p[30] = 0x00; p[31] = 0x00; /* VERSION */
		p[32] = 0x02; /* ENCRYPTION = NOT_SUPPORTED (no TLS in this library) */
		p[33] = 0x00; /* INSTOPT = none */
		p[34] = 0x00; p[35] = 0x00; p[36] = 0x00; p[37] = 0x00; /* THREADID */
		p[38] = 0x00; /* MARS = off */
	}

	c->SendLen = total;
	c->SendPos = 0;
}

/* Parses a buffered PRELOGIN response from TokBuf[TokPos..TokLen).
 * Returns 1 if parsed (TokPos advanced past it), 0 if more bytes are
 * needed, -1 on an unsupported/invalid response (diagnostic pushed). */
static INT ParsePreloginResponse(UDINT slot)
{
	ConnSlot *c = &gConn[slot];
	const unsigned char *b = &c->TokBuf[c->TokPos];
	UDINT avail = c->TokLen - c->TokPos;
	UDINT i;
	UDINT tableLen = 0;
	UDINT dataEnd = 0;
	BOOL haveEncryption = FALSE;
	USINT encryptionVal = 0;

	for (i = 0; ; i += 5) {
		if (i + 1 > avail) return 0;
		if (b[i] == 0xFF) { tableLen = i + 1; break; }
		if (i + 5 > avail) return 0;
		{
			UINT tokOff = (UINT)AltDB_ReadBE16(&b[i + 1]);
			UINT tokLen = (UINT)AltDB_ReadBE16(&b[i + 3]);
			UDINT end = (UDINT)tokOff + (UDINT)tokLen;

			if (end > dataEnd) dataEnd = end;
			if (b[i] == 0x01 && tokLen >= 1) {
				if ((UDINT)tokOff + 1 > avail) return 0; /* ENCRYPTION byte not buffered yet */
				haveEncryption = TRUE;
				encryptionVal = b[tokOff];
			}
		}
		if (i > 200) { AltDB_DiagPushClient(slot, "08001", "malformed PRELOGIN response"); return -1; }
	}

	/* Wait for every byte referenced by the option table (not a fixed
	 * padding guess - SQL Server's PRELOGIN response is tightly sized
	 * with no slack, so an inexact bound here would stall forever). */
	if (avail < tableLen || avail < dataEnd) return 0;

	if (haveEncryption && (encryptionVal == 0x01 || encryptionVal == 0x03)) {
		/* ENCRYPT_ON / ENCRYPT_REQ: server requires TLS, which this
		 * library does not implement (see Common.h). */
		AltDB_DiagPushClient(slot, "08001", "server requires TLS encryption, not supported by this library");
		return -1;
	}

	c->TokPos += (tableLen > dataEnd) ? tableLen : dataEnd; /* consume exactly the parsed message */
	return 1;
}

/* ---------------- LOGIN7 ---------------- */

static UINT PutVarField(unsigned char *fixed, UINT pairOffset, unsigned char *var, UINT *varOff, UINT cap,
                         UINT encoding, const unsigned char *text, UDINT textLen)
{
	UINT chars;
	UDINT written;

	AltDB_WriteLE16(&fixed[pairOffset], (UINT)(94 + *varOff));
	written = AltDB_ConnToUtf16le(encoding, text, textLen, &var[*varOff], (cap > *varOff) ? (cap - *varOff) : 0);
	chars = (UINT)(written / 2);
	AltDB_WriteLE16(&fixed[pairOffset + 2], chars);
	*varOff += (UINT)written;
	return chars;
}

BOOL AltDB_BuildLogin7(UDINT slot, const char *user, const char *pass, const char *db, const char *server)
{
	ConnSlot *c = &gConn[slot];
	unsigned char *msg = c->SendBuf;
	unsigned char *fixed = &msg[8];
	unsigned char *var = &msg[8 + 94];
	UINT varCap = (UINT)(sizeof(c->SendBuf) - 8 - 94);
	UINT varOff = 0;
	UDINT total;
	UINT pwOff, pwChars, i;

	memset(msg, 0, sizeof(c->SendBuf));

	AltDB_WriteLE32(&fixed[4], 0x74000004UL);			/* TDSVersion 7.4 */
	AltDB_WriteLE32(&fixed[8], TDS_DEFAULT_PACKET_SIZE);	/* PacketSize */
	AltDB_WriteLE32(&fixed[12], 0x00010000UL);			/* ClientProgVer */
	AltDB_WriteLE32(&fixed[16], 0);						/* ClientPID */
	AltDB_WriteLE32(&fixed[20], 0);						/* ConnectionID */
	fixed[24] = 0xE0;										/* OptionFlags1 */
	fixed[25] = 0x03;										/* OptionFlags2 (fODBC=1) */
	fixed[26] = 0x00;										/* TypeFlags */
	fixed[27] = 0x00;										/* OptionFlags3 */
	AltDB_WriteLE32(&fixed[28], 0);						/* ClientTimeZone */
	AltDB_WriteLE32(&fixed[32], 0x00000409UL);			/* ClientLCID */

	PutVarField(fixed, 36, var, &varOff, varCap, c->Encoding, (const unsigned char *)"AltDB", 7); /* HostName */
	PutVarField(fixed, 40, var, &varOff, varCap, c->Encoding, (const unsigned char *)user, (UDINT)strlen(user)); /* UserName */

	pwOff = (UINT)(94 + varOff);
	pwChars = (UINT)(AltDB_ConnToUtf16le(c->Encoding, (const unsigned char *)pass, (UDINT)strlen(pass),
	                                        &var[varOff], varCap - varOff) / 2);
	for (i = 0; i < pwChars; i++) {
		unsigned char b0 = var[varOff + i * 2];
		unsigned char b1 = var[varOff + i * 2 + 1];
		b0 = (unsigned char)(((b0 << 4) | (b0 >> 4)) ^ 0xA5);
		b1 = (unsigned char)(((b1 << 4) | (b1 >> 4)) ^ 0xA5);
		var[varOff + i * 2] = b0;
		var[varOff + i * 2 + 1] = b1;
	}
	AltDB_WriteLE16(&fixed[44], pwOff);
	AltDB_WriteLE16(&fixed[46], pwChars);
	varOff += pwChars * 2;

	PutVarField(fixed, 48, var, &varOff, varCap, c->Encoding, (const unsigned char *)"AltDB", 7); /* AppName */
	PutVarField(fixed, 52, var, &varOff, varCap, c->Encoding, (const unsigned char *)server, (UDINT)strlen(server)); /* ServerName */
	AltDB_WriteLE16(&fixed[56], (UINT)(94 + varOff)); AltDB_WriteLE16(&fixed[58], 0); /* Extension: none */
	PutVarField(fixed, 60, var, &varOff, varCap, c->Encoding, (const unsigned char *)"AltDB", 7); /* CltIntName */
	AltDB_WriteLE16(&fixed[64], (UINT)(94 + varOff)); AltDB_WriteLE16(&fixed[66], 0); /* Language: none */
	PutVarField(fixed, 68, var, &varOff, varCap, c->Encoding, (const unsigned char *)db, (UDINT)strlen(db)); /* Database */

	memset(&fixed[72], 0, 6); /* ClientID */
	AltDB_WriteLE16(&fixed[78], (UINT)(94 + varOff)); AltDB_WriteLE16(&fixed[80], 0); /* SSPI: none */
	AltDB_WriteLE16(&fixed[82], (UINT)(94 + varOff)); AltDB_WriteLE16(&fixed[84], 0); /* AtchDBFile: none */
	AltDB_WriteLE16(&fixed[86], (UINT)(94 + varOff)); AltDB_WriteLE16(&fixed[88], 0); /* ChangePassword: none */
	AltDB_WriteLE32(&fixed[90], 0); /* cbSSPILong */

	total = 8 + 94 + varOff;
	AltDB_WriteLE32(&fixed[0], (UDINT)(94 + varOff));
	msg[0] = 0x10; msg[1] = 0x01; /* type LOGIN7, status EOM */
	AltDB_WriteBE16(&msg[2], (UINT)total);
	msg[4] = 0; msg[5] = 0; msg[6] = 1; msg[7] = 0;

	c->SendLen = total;
	c->SendPos = 0;
	return TRUE;
}

/* ---------------- SQL_BATCH (Section 10.6) ---------------- */

BOOL AltDB_BuildSqlBatch(UDINT slot, const char *sql, UDINT sqlLen)
{
	ConnSlot *c = &gConn[slot];
	unsigned char *msg = c->SendBuf;
	UDINT encCap = (UDINT)sizeof(c->SendBuf) - 8 - 22;
	UDINT encLen;
	UDINT total;

	encLen = AltDB_EncodeToWire(c->Encoding, sql, sqlLen, &msg[8 + 22], encCap);

	AltDB_WriteLE32(&msg[8 + 0], 22);		/* ALL_HEADERS TotalLength */
	AltDB_WriteLE32(&msg[8 + 4], 18);		/* HeaderLength */
	AltDB_WriteLE16(&msg[8 + 8], 2);			/* HeaderType: transaction descriptor */
	memcpy(&msg[8 + 10], c->TransactionDescr, 8);
	AltDB_WriteLE32(&msg[8 + 18], 1);		/* OutstandingRequestCount */

	total = 8 + 22 + encLen;
	msg[0] = 0x01; msg[1] = 0x01; /* type SQL_BATCH, status EOM */
	AltDB_WriteBE16(&msg[2], (UINT)total);
	msg[4] = 0; msg[5] = 0; msg[6] = 1; msg[7] = 0;

	c->SendLen = total;
	c->SendPos = 0;
	return TRUE;
}

/* ---------------- top level connect state machine ---------------- */

INT AltDB_NetConnectStep(UDINT slot)
{
	ConnSlot *c = &gConn[slot];
	INT r;

	switch (c->State) {

	case CONNSTATE_OPENING:
		r = PumpTcpOpen(slot);
		if (r == NET_IN_PROGRESS) return NET_IN_PROGRESS;
		if (r == NET_ERROR) { c->State = CONNSTATE_FAILED; return NET_ERROR; }
		c->State = CONNSTATE_CONNECTING;
		return NET_IN_PROGRESS;

	case CONNSTATE_CONNECTING:
		r = PumpTcpClient(slot);
		if (r == NET_IN_PROGRESS) return NET_IN_PROGRESS;
		if (r == NET_ERROR) { c->State = CONNSTATE_FAILED; return NET_ERROR; }
		c->State = CONNSTATE_PRELOGIN;
		c->NetStep = NETSTEP_SEND;
		AltDB_BuildPrelogin(slot);
		return NET_IN_PROGRESS;

	case CONNSTATE_PRELOGIN:
		if (c->NetStep == NETSTEP_SEND) {
			r = AltDB_NetSendStep(slot);
			if (r == NET_ERROR) { c->State = CONNSTATE_FAILED; return NET_ERROR; }
			if (r == NET_DONE) c->NetStep = NETSTEP_RECV;
			return NET_IN_PROGRESS;
		} else {
			r = AltDB_NetRecvPump(slot);
			if (r < 0) { c->State = CONNSTATE_FAILED; return NET_ERROR; }
			r = ParsePreloginResponse(slot);
			if (r < 0) { c->State = CONNSTATE_FAILED; return NET_ERROR; }
			if (r == 0) return NET_IN_PROGRESS;
			/* ParsePreloginResponse already advanced TokPos past exactly
			 * the bytes it consumed; nothing is discarded here. */
			c->State = CONNSTATE_LOGIN;
			c->NetStep = NETSTEP_SEND;
			AltDB_BuildLogin7(slot, (const char *)c->LoginUser, (const char *)c->LoginPass,
			                    (const char *)c->LoginDb, (const char *)c->HostBuf);
			return NET_IN_PROGRESS;
		}

	case CONNSTATE_LOGIN:
		if (c->NetStep == NETSTEP_SEND) {
			r = AltDB_NetSendStep(slot);
			if (r == NET_ERROR) { c->State = CONNSTATE_FAILED; return NET_ERROR; }
			if (r == NET_DONE) c->NetStep = NETSTEP_RECV;
			return NET_IN_PROGRESS;
		} else {
			INT tok;
			r = AltDB_NetRecvPump(slot);
			if (r < 0) { c->State = CONNSTATE_FAILED; return NET_ERROR; }
			tok = AltDB_TokenStep(slot, TOKMODE_LOGIN);
			if (tok == TOK_NEED_MORE) return NET_IN_PROGRESS;
			if (tok == TOK_ERROR) { c->State = CONNSTATE_FAILED; return NET_ERROR; }
			/* TOK_GOT_DONE_FINAL: login succeeded */
			c->State = CONNSTATE_IDLE;
			return NET_DONE;
		}

	default:
		return NET_ERROR;
	}
}
