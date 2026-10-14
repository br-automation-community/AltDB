TYPE
	(*Extension type (not part of AsDb): internal diagnostic snapshot of
	a connection, returned by the extension FB dbGetConnState. Exposes
	the state machine of Section 9.2 plus buffering/parsing counters so
	a stuck connection (e.g. permanently ERR_FUB_BUSY) can be diagnosed
	without reading this library's C sources.*)
	AltDBConnStateType : STRUCT
		state				: UINT;		(*CONNSTATE_*, see Priv.var - OPENING/CONNECTING/PRELOGIN/LOGIN/IDLE/EXECUTING/IN_RESULT/BROKEN/FAILED*)
		netStep				: UINT;		(*sub-state of the current network operation (0=send,1=recv)*)
		jobKind				: UINT;		(*JOB_* - which FB currently owns this connection's job, JOB_NONE if idle*)
		locked				: BOOL;		(*TRUE while a job is in progress on this connection*)
		tcpIdent			: UDINT;	(*AsTCP socket identifier, 0 = not opened yet*)
		encoding			: UINT;		(*DBENC_ISO_8859_1 (0) / DBENC_UTF_8 (1)*)
		packetSize			: UINT;		(*negotiated TDS packet size*)
		sendLen				: UDINT;	(*total bytes of the outgoing message currently being sent*)
		sendPos				: UDINT;	(*bytes already handed to TcpSend*)
		tokLen				: UDINT;	(*bytes currently buffered in the de-framed token stream*)
		tokPos				: UDINT;	(*parser read cursor into the token stream*)
		statementActive		: BOOL;		(*TRUE once dbExecuteSql has completed at least once*)
		columnCount			: INT;		(*column count of the current result set, 0 if none*)
		hasResultSet		: BOOL;		(*current statement produced a result set*)
		resultSetOpen		: BOOL;		(*result set still has rows pending / not yet exhausted*)
		rowReady			: BOOL;		(*a row is currently buffered and ready for dbGetData*)
		noMoreRows			: BOOL;		(*end of the current result set has been reached*)
		affectedRows		: UDINT;
		affectedRowsKnown	: BOOL;
		diagCount			: UINT;		(*number of diagnostic records currently in the ring*)
		diagReadIdx			: UINT;		(*index of the next unread diagnostic record*)
		serverVersion		: STRING[31];	(*"mm.mm.bbbb" once LOGINACK has been received, else empty*)
	END_STRUCT;
END_TYPE
