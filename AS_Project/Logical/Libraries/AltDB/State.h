/********************************************************************
 * Library: AltDB
 * File: State.h
 ********************************************************************
 * Internal connection pool storage.
 *
 * A static library in Automation Studio cannot declare its own
 * (non-constant) global IEC variable - persistent data belongs to
 * the FB instance of the caller. Since dbExecuteSql/dbFetchNextRow/
 * dbGetData/... must all reach the *same* connection state by
 * connectionIdent regardless of which FB instance (and which task)
 * calls them, and since that state also has to embed AsTCP's own FB
 * instances (TcpOpen/TcpClient/TcpSend/TcpRecv/TcpClose - which, by
 * IEC 61131-3 rules, cannot be nested inside an IEC STRUCT), the
 * connection pool is implemented as a plain hand-written C structure
 * with ordinary (non-static, so it is shared across this library's
 * translation units) file scope storage, defined once in State.c.
 ********************************************************************/
#ifndef _ALTDB_STATE_H_
#define _ALTDB_STATE_H_

#define ND_MAX_CONN            8		/* keep in sync with MAX_CONN in Priv.var */
#define ND_MAX_COLUMNS          32		/* keep in sync with MAX_COLUMNS in Priv.var */
#define ND_MAX_COL_VALUE_LEN   512		/* keep in sync with MAX_COL_VALUE_LEN in Priv.var */
#define ND_MAX_DIAG              8		/* keep in sync with MAX_DIAG in Priv.var */
#define ND_RECV_SCRATCH_SIZE  4096
#define ND_TOK_BUF_SIZE       8192
#define ND_SEND_BUF_SIZE      4352

typedef struct ColumnInfo
{
	unsigned char DataType;		/* raw TDS TYPE_INFO type id */
	UDINT         MaxLength;	/* TYPE_INFO max length, 0xFFFFFFFF = PLP (MAX) type */
	unsigned char Precision;
	unsigned char Scale;
	BOOL          IsUnicode;	/* UTF-16LE source (N-prefixed types) */
	BOOL          IsPlp;		/* partially length prefixed value encoding */
	BOOL          IsLongLen;	/* legacy TEXT/NTEXT/IMAGE value encoding */
	BOOL          Supported;	/* FALSE for types this library cannot decode */
} ColumnInfo;

typedef struct DiagRec
{
	BOOL          Used;
	BOOL          IsError;		/* class >= 11 */
	char          SqlState[6];	/* 5 ASCII chars + NUL */
	DINT          NativeError;
	UINT          MessageLen;
	char          Message[256];	/* NUL-terminated, connection encoding */
} DiagRec;

typedef struct ConnSlot
{
	/* --- lifecycle / handle validation --- */
	UINT   State;			/* CONNSTATE_*, Priv.var */
	UINT   Generation;		/* incremented on free, part of connectionIdent */
	BOOL   Locked;			/* try-lock for FB calls on this connection */
	UINT   JobKind;			/* JOB_*, which operation currently owns the job */
	UDINT  JobOwner;		/* identity of the owning FB instance (its ADR), 0 = none */
	UDINT  JobTicks;		/* watchdog: cycles since job last served */
	BOOL   Abandoned;		/* TRUE if a dbConnect/dbConnectEnc job on this handle was
							   cancelled mid-flight (enable := FALSE while BUSY, Section
							   9.3). The next dbConnect/dbConnectEnc call on this handle
							   re-captures the (possibly corrected) inputs and restarts
							   the handshake from CONNSTATE_OPENING instead of blindly
							   resuming stale state


	/* --- TCP / TDS framing --- */
	UDINT  TcpIdent;		/* 0 = socket not opened */
	UINT   NetStep;			/* sub-state of the current network operation */
	UINT   Encoding;		/* DBENC_ISO_8859_1 / DBENC_UTF_8 */
	UINT   PacketSize;		/* negotiated TDS packet size */

	TcpOpen_typ   tcpOpen;
	TcpClient_typ tcpClient;
	TcpSend_typ   tcpSend;
	TcpRecv_typ   tcpRecv;
	TcpClose_typ  tcpClose;

	/* --- login parameters, copied from the FB inputs at job start --- */
	char   HostBuf[128];		/* NUL-terminated host name / IP, no port */
	UINT   Port;
	char   LoginUser[129];
	UDINT  LoginUserLen;
	char   LoginPass[129];
	UDINT  LoginPassLen;
	char   LoginDb[129];
	UDINT  LoginDbLen;

	unsigned char RecvScratch[ND_RECV_SCRATCH_SIZE];
	unsigned char HeaderBuf[8];
	unsigned char HeaderGot;
	UDINT  FrameRemaining;

	unsigned char TokBuf[ND_TOK_BUF_SIZE];
	UDINT  TokLen;
	UDINT  TokPos;

	unsigned char SendBuf[ND_SEND_BUF_SIZE];
	UDINT  SendLen;
	UDINT  SendPos;

	unsigned char TransactionDescr[8];
	char   ServerVersion[32];	/* NUL-terminated "mm.mm.bbbb" */

	/* --- current statement / result set --- */
	BOOL        StatementActive;	/* dbExecuteSql has completed at least once */
	INT         ColumnCount;
	BOOL        RowOverflow;	/* TRUE if a row exceeded ND_MAX_COL_VALUE_LEN for some column (dbERR_MEMORY) */
	ColumnInfo  Columns[ND_MAX_COLUMNS];
	BOOL        HasResultSet;
	BOOL        ResultSetOpen;
	UDINT       AffectedRows;
	BOOL        AffectedRowsKnown;

	BOOL   RowReady;
	BOOL   NoMoreRows;
	BOOL   ColIsNull[ND_MAX_COLUMNS];
	UDINT  ColValueLen[ND_MAX_COLUMNS];
	unsigned char ColValue[ND_MAX_COLUMNS][ND_MAX_COL_VALUE_LEN];
	BOOL   ColAlreadyRead[ND_MAX_COLUMNS];

	/* --- diagnostics ring (per connection) --- */
	UINT     DiagCount;
	UINT     DiagReadIdx;
	DiagRec  Diag[ND_MAX_DIAG];
} ConnSlot;

extern ConnSlot gConn[ND_MAX_CONN];

#endif /* _ALTDB_STATE_H_ */
