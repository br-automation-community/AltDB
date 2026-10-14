/********************************************************************
 * Library: AltDB
 * File: Common.h
 ********************************************************************
 * Shared declarations for the implementation (*.c) files of this
 * library. Not part of the public interface.
 *
 * Implementation scope / known deviations from Spec.md (documented
 * here once, referenced from the relevant modules):
 *  - No TLS/TDS 8.0 support. PRELOGIN always advertises
 *    ENCRYPTION = NOT_SUPPORTED. Servers that require encryption
 *    (ENCRYPT=ON/REQUIRED) will be rejected with dbERR_DATABASE.
 *  - Host name resolution is delegated to AsTCP's TcpClient, which
 *    accepts both dotted-decimal addresses and host names.
 *  - ATTENTION (query cancellation) is not implemented. A result set
 *    that is not fully read before the next dbExecuteSql is drained
 *    by continuing to read and discard tokens (not by cancelling the
 *    batch on the wire). enable = FALSE on a busy FB abandons local
 *    job ownership but does not cancel the in-flight server request.
 *  - TDS/row buffers are bounded (see Priv.var): results with more
 *    than MAX_COLUMNS columns or column values larger than
 *    MAX_COL_VALUE_LEN are rejected with dbERR_MEMORY/garbled data
 *    is never produced, but the row is not usable.
 *  - TEXT/NTEXT/IMAGE/SQL_VARIANT/UDT and DATETIMEOFFSET columns are
 *    parsed structurally (to keep the token stream in sync) but are
 *    only partially convertible; see Convert.c.
 ********************************************************************/

#ifndef _ALTDB_COMMON_H_
#define _ALTDB_COMMON_H_

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#include "State.h"

/* ---- token parser result / modes (Token.c) ---- */
#define TOK_NEED_MORE          0
#define TOK_GOT_COLMETADATA    1
#define TOK_GOT_DONE_FINAL     2	/* batch finished, no (more) result sets */
#define TOK_GOT_ROW            3
#define TOK_GOT_EOF            4	/* current result set ended, no more rows */
#define TOK_ERROR              5	/* server error class>=11 or protocol error */
#define TOK_ERROR_MEMORY       6	/* row/column too large for internal buffers (dbERR_MEMORY) */

#define TOKMODE_LOGIN   0	/* awaiting LOGINACK + terminal DONE */
#define TOKMODE_EXEC    1	/* awaiting first COLMETADATA or terminal DONE */
#define TOKMODE_FETCH   2	/* awaiting next ROW or end of this result set */
#define TOKMODE_DRAIN   3	/* discard everything up to terminal DONE */

/* ---- network step results (Net.c) ---- */
#define NET_IN_PROGRESS 0
#define NET_DONE        1
#define NET_ERROR       2

/* Util.c */
UDINT  AltDB_MakeHandle(UDINT slot, UINT generation);
BOOL   AltDB_SlotFromHandle(UDINT handle, UDINT *pSlot, UINT *pGeneration);
void   AltDB_SlotReset(UDINT slot);
BOOL   AltDB_SlotAlloc(UDINT *pSlot);
BOOL   AltDB_TryLock(UDINT slot, UDINT owner);
void   AltDB_Unlock(UDINT slot);
BOOL   AltDB_IsOwner(UDINT slot, UDINT owner);

void   AltDB_DiagClear(UDINT slot);
void   AltDB_DiagPushServer(UDINT slot, BOOL isError, DINT nativeError,
                               const char *sqlState, const unsigned char *text, UDINT textLen);
void   AltDB_DiagPushClient(UDINT slot, const char *sqlState, const char *text);

UINT   AltDB_ReadLE16(const unsigned char *p);
UDINT  AltDB_ReadLE32(const unsigned char *p);
unsigned long long AltDB_ReadLE64(const unsigned char *p);
UDINT  AltDB_ReadBE16(const unsigned char *p);
void   AltDB_WriteLE16(unsigned char *p, UINT v);
void   AltDB_WriteLE32(unsigned char *p, UDINT v);
void   AltDB_WriteBE16(unsigned char *p, UINT v);

UDINT  AltDB_EncodeToWire(UINT encoding, const char *src, UDINT srcLen, unsigned char *dst, UDINT dstCap);
UDINT  AltDB_WireToConn(UINT encoding, const unsigned char *src, UDINT srcBytes, unsigned char *dst, UDINT dstCap);
UDINT  AltDB_Utf16leToConn(UINT encoding, const unsigned char *src, UDINT srcBytes, unsigned char *dst, UDINT dstCap);
UDINT  AltDB_ConnToUtf16le(UINT encoding, const unsigned char *src, UDINT srcLen, unsigned char *dst, UDINT dstCap);

UDINT  AltDB_FormatI64(long long v, char *dst, UDINT dstCap);
UDINT  AltDB_FormatDouble(double v, int sigDigits, char *dst, UDINT dstCap);
UDINT  AltDB_FormatDecimal(const unsigned char *magnitude, UDINT magLen, BOOL positive, USINT scale, char *dst, UDINT dstCap);

/* Net.c */
INT    AltDB_NetConnectStep(UDINT slot);
INT    AltDB_NetSendStep(UDINT slot);
INT    AltDB_NetRecvPump(UDINT slot);
INT    AltDB_NetDisconnectStep(UDINT slot);
void   AltDB_BuildPrelogin(UDINT slot);
BOOL   AltDB_BuildLogin7(UDINT slot, const char *user, const char *pass, const char *db, const char *server);
BOOL   AltDB_BuildSqlBatch(UDINT slot, const char *sql, UDINT sqlLen);

/* Token.c */
INT    AltDB_TokenStep(UDINT slot, UINT mode);

/* Convert.c */
void   AltDB_GetData(UDINT slot, UDINT columnIdx, INT dataType, UDINT pData, UDINT dataSize,
                        UINT *status, INT *dbError, UDINT *actDataSize);

#endif /* _ALTDB_COMMON_H_ */
