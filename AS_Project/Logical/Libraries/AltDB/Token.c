/********************************************************************
 * Library: AltDB
 * File: Token.c
 ********************************************************************
 * Incremental TDS response token stream parser (Section 10.7). Reads
 * from gConn[slot].TokBuf[TokPos..TokLen) which already has the
 * 8-byte TDS packet headers stripped (Net.c). Never blocks: if a
 * token is not yet fully buffered, parsing stops without consuming
 * it and is resumed transparently on the next call once more bytes
 * have arrived - the parser carries no state beyond (TokBuf, TokPos).
 ********************************************************************/
#include <bur/plctypes.h>
#include <runtime.h>
#include <AsTCP.h>
#include <AltDB.h>
#include "Common.h"
#include <string.h>

/* TDS type ids (Section 10.9 / Table 21) */
#define TY_NULL        0x1F
#define TY_INT1        0x30
#define TY_BIT         0x32
#define TY_INT2        0x34
#define TY_INT4        0x38
#define TY_DATETIM4    0x3A
#define TY_FLT4        0x3B
#define TY_MONEY       0x3C
#define TY_DATETIME    0x3D
#define TY_FLT8        0x3E
#define TY_MONEY4      0x7A
#define TY_INT8        0x7F
#define TY_INTN        0x26
#define TY_BITN        0x68
#define TY_FLTN        0x6D
#define TY_MONEYN      0x6E
#define TY_DATETIMN    0x6F
#define TY_GUID        0x24
#define TY_DECIMALN    0x6A
#define TY_NUMERICN    0x6C
#define TY_DATEN       0x28
#define TY_TIMEN       0x29
#define TY_DATETIME2N  0x2A
#define TY_DATETIMEOFFSETN 0x2B
#define TY_BIGVARCHR   0xA7
#define TY_BIGCHAR     0xAF
#define TY_NVARCHAR    0xE7
#define TY_NCHAR       0xEF
#define TY_BIGVARBIN   0xA5
#define TY_BIGBINARY   0xAD
#define TY_CHAR_LEGACY     0x2F
#define TY_VARCHAR_LEGACY  0x27
#define TY_BINARY_LEGACY   0x2D
#define TY_VARBINARY_LEGACY 0x25
#define TY_TEXT        0x23
#define TY_NTEXT       0x63
#define TY_IMAGE       0x22
#define TY_VARIANT     0x62
#define TY_XML         0xF1
#define TY_UDT         0xF0

/* ---------------- TYPE_INFO parsing (COLMETADATA) ---------------- */

/* Returns bytes of TYPE_INFO consumed (>=1), 0 if more bytes are
 * needed, -1 if the type byte is not a recognised TDS data type
 * (unrecoverable: we cannot safely determine its size). */
static INT ParseTypeInfo(const unsigned char *b, UDINT avail, ColumnInfo *col)
{
	UDINT pos = 1;
	unsigned char t;

	if (avail < 1) return 0;
	t = b[0];
	col->DataType = t;
	col->IsUnicode = FALSE;
	col->IsPlp = FALSE;
	col->IsLongLen = FALSE;
	col->Supported = TRUE;
	col->Precision = 0;
	col->Scale = 0;
	col->MaxLength = 0;

	switch (t) {
	case TY_NULL:
		col->MaxLength = 0;
		break;
	case TY_INT1: case TY_BIT:
		col->MaxLength = 1;
		break;
	case TY_INT2:
		col->MaxLength = 2;
		break;
	case TY_INT4: case TY_DATETIM4: case TY_FLT4: case TY_MONEY4:
		col->MaxLength = 4;
		break;
	case TY_MONEY: case TY_DATETIME: case TY_FLT8: case TY_INT8:
		col->MaxLength = 8;
		break;

	case TY_INTN: case TY_BITN: case TY_FLTN: case TY_MONEYN: case TY_DATETIMN: case TY_GUID:
		if (avail < pos + 1) return 0;
		col->MaxLength = b[pos]; pos += 1;
		break;

	case TY_DECIMALN: case TY_NUMERICN:
		if (avail < pos + 3) return 0;
		col->MaxLength = b[pos]; col->Precision = b[pos + 1]; col->Scale = b[pos + 2];
		pos += 3;
		break;

	case TY_DATEN:
		break; /* no extra TYPE_INFO byte */
	case TY_TIMEN: case TY_DATETIME2N: case TY_DATETIMEOFFSETN:
		if (avail < pos + 1) return 0;
		col->Scale = b[pos]; pos += 1;
		break;

	case TY_BIGVARCHR: case TY_BIGCHAR: case TY_NVARCHAR: case TY_NCHAR:
	case TY_BIGVARBIN: case TY_BIGBINARY: {
		UINT maxLen;
		if (avail < pos + 2) return 0;
		maxLen = (UINT)AltDB_ReadLE16(&b[pos]); pos += 2;
		col->MaxLength = maxLen;
		col->IsPlp = (maxLen == 0xFFFF);
		col->IsUnicode = (t == TY_NVARCHAR || t == TY_NCHAR);
		if (t == TY_BIGVARCHR || t == TY_BIGCHAR || t == TY_NVARCHAR || t == TY_NCHAR) {
			if (avail < pos + 5) return 0;
			pos += 5; /* collation, not required for the supported conversions */
		}
		break;
	}

	case TY_CHAR_LEGACY: case TY_VARCHAR_LEGACY: case TY_BINARY_LEGACY: case TY_VARBINARY_LEGACY:
		if (avail < pos + 1) return 0;
		col->MaxLength = b[pos]; pos += 1;
		break;

	case TY_TEXT: case TY_NTEXT: case TY_IMAGE: {
		UDINT i;
		unsigned char numParts;
		if (avail < pos + 4) return 0;
		col->MaxLength = AltDB_ReadLE32(&b[pos]); pos += 4;
		col->IsLongLen = TRUE;
		col->IsUnicode = (t == TY_NTEXT);
		if (t != TY_IMAGE) {
			if (avail < pos + 5) return 0;
			pos += 5; /* collation */
		}
		if (avail < pos + 1) return 0;
		numParts = b[pos]; pos += 1;
		for (i = 0; i < numParts; i++) {
			UINT n;
			if (avail < pos + 2) return 0;
			n = (UINT)AltDB_ReadLE16(&b[pos]);
			if (avail < pos + 2 + (UDINT)n * 2) return 0;
			pos += 2 + (UDINT)n * 2;
		}
		break;
	}

	case TY_VARIANT:
		if (avail < pos + 4) return 0;
		col->MaxLength = AltDB_ReadLE32(&b[pos]); pos += 4;
		col->Supported = FALSE;
		break;

	case TY_XML: {
		unsigned char schemaPresent;
		if (avail < pos + 1) return 0;
		schemaPresent = b[pos]; pos += 1;
		col->IsPlp = TRUE;
		col->IsUnicode = TRUE;
		col->Supported = FALSE;
		if (schemaPresent) {
			UDINT i;
			for (i = 0; i < 3; i++) {
				UDINT n;
				if (i < 2) {
					if (avail < pos + 1) return 0;
					n = b[pos]; pos += 1;
				} else {
					if (avail < pos + 2) return 0;
					n = AltDB_ReadLE16(&b[pos]); pos += 2;
				}
				if (avail < pos + n * 2) return 0;
				pos += n * 2;
			}
		}
		break;
	}

	case TY_UDT: {
		UDINT i;
		if (avail < pos + 2) return 0;
		pos += 2; /* MAX_BYTE_SIZE */
		col->IsPlp = TRUE;
		col->Supported = FALSE;
		for (i = 0; i < 4; i++) {
			UDINT n;
			if (i < 3) { /* DB_NAME, SCHEMA_NAME, TYPE_NAME: B_VARCHAR */
				if (avail < pos + 1) return 0;
				n = b[pos]; pos += 1;
			} else { /* ASSEMBLY_QUALIFIED_NAME: US_VARCHAR */
				if (avail < pos + 2) return 0;
				n = AltDB_ReadLE16(&b[pos]); pos += 2;
			}
			if (avail < pos + n * 2) return 0;
			pos += n * 2;
		}
		break;
	}

	default:
		return -1;
	}

	return (INT)pos;
}

/* TRUE for byte-length-class types whose value carries a 1-byte
 * length prefix with 0 meaning NULL (as opposed to legal zero-length
 * non-NULL content). */
static BOOL IsByteLenNullable(unsigned char t)
{
	switch (t) {
	case TY_INTN: case TY_BITN: case TY_FLTN: case TY_MONEYN: case TY_DATETIMN: case TY_GUID:
	case TY_DECIMALN: case TY_NUMERICN:
	case TY_DATEN: case TY_TIMEN: case TY_DATETIME2N: case TY_DATETIMEOFFSETN:
		return TRUE;
	default:
		return FALSE;
	}
}

/* Parses one column value from the ROW/NBCROW wire format into dst
 * (clamped to dstCap; *outValLen reports the true, unclamped length
 * so overflow can be detected by the caller). Returns bytes consumed
 * from the wire (>0), 0 if more bytes are needed, -1 on a malformed
 * value that cannot be safely skipped. */
static INT ParseColumnValue(const unsigned char *b, UDINT avail, const ColumnInfo *col,
                             unsigned char *dst, UDINT dstCap, UDINT *outValLen, BOOL *outIsNull)
{
	unsigned char t = col->DataType;
	UDINT fixedLen = 0;

	*outValLen = 0;
	*outIsNull = FALSE;

	switch (t) {
	case TY_NULL: *outIsNull = TRUE; return 0;
	case TY_INT1: case TY_BIT: fixedLen = 1; break;
	case TY_INT2: fixedLen = 2; break;
	case TY_INT4: case TY_DATETIM4: case TY_FLT4: case TY_MONEY4: fixedLen = 4; break;
	case TY_MONEY: case TY_DATETIME: case TY_FLT8: case TY_INT8: fixedLen = 8; break;
	default: fixedLen = 0; break;
	}

	if (fixedLen > 0) {
		if (avail < fixedLen) return 0;
		*outValLen = fixedLen;
		memcpy(dst, b, (fixedLen < dstCap) ? fixedLen : dstCap);
		return (INT)fixedLen;
	}

	if (col->IsPlp) {
		/* PLP (Section 10.9): 8-byte total length, then chunks of
		 * (4-byte length, data), terminated by a 4-byte zero length.
		 * 0xFFFFFFFFFFFFFFFF total length = NULL. */
		unsigned long long total;
		UDINT pos = 8;
		UDINT copied = 0;

		if (avail < 8) return 0;
		total = AltDB_ReadLE64(b);
		if (total == 0xFFFFFFFFFFFFFFFFULL) { *outIsNull = TRUE; return 8; }

		for (;;) {
			UDINT chunkLen;
			if (avail < pos + 4) return 0;
			chunkLen = AltDB_ReadLE32(&b[pos]); pos += 4;
			if (chunkLen == 0) break;
			if (avail < pos + chunkLen) return 0;
			if (copied < dstCap) {
				UDINT n = chunkLen;
				if (copied + n > dstCap) n = dstCap - copied;
				memcpy(&dst[copied], &b[pos], n);
			}
			copied += chunkLen;
			pos += chunkLen;
		}
		*outValLen = copied;
		return (INT)pos;
	}

	if (col->IsLongLen) {
		/* Legacy TEXT/NTEXT/IMAGE: 1-byte TEXTPTR length (0 = NULL),
		 * TEXTPTR bytes, 8-byte timestamp, 4-byte data length, data. */
		UDINT pos = 0;
		unsigned char ptrLen;

		if (avail < 1) return 0;
		ptrLen = b[0]; pos = 1;
		if (ptrLen == 0) { *outIsNull = TRUE; return (INT)pos; }
		if (avail < pos + ptrLen + 8 + 4) return 0;
		pos += ptrLen + 8;
		{
			UDINT dataLen = AltDB_ReadLE32(&b[pos]); pos += 4;
			if (avail < pos + dataLen) return 0;
			*outValLen = dataLen;
			memcpy(dst, &b[pos], (dataLen < dstCap) ? dataLen : dstCap);
			pos += dataLen;
			return (INT)pos;
		}
	}

	if (t == TY_VARIANT) {
		UDINT total;
		if (avail < 4) return 0;
		total = AltDB_ReadLE32(b);
		if (total == 0) { *outIsNull = TRUE; return 4; }
		if (avail < 4 + total) return 0;
		/* BaseType + PropBytes + Value; copy raw for completeness, not
		 * decoded further (col->Supported is FALSE for SQL_VARIANT). */
		*outValLen = total;
		memcpy(dst, &b[4], (total < dstCap) ? total : dstCap);
		return (INT)(4 + total);
	}

	if (IsByteLenNullable(t)) {
		UDINT len;
		if (avail < 1) return 0;
		len = b[0];
		if (len == 0) { *outIsNull = TRUE; return 1; }
		if (avail < 1 + len) return 0;
		*outValLen = len;
		memcpy(dst, &b[1], (len < dstCap) ? len : dstCap);
		return (INT)(1 + len);
	}

	switch (t) {
	case TY_BIGVARCHR: case TY_BIGCHAR: case TY_NVARCHAR: case TY_NCHAR:
	case TY_BIGVARBIN: case TY_BIGBINARY: {
		UDINT len;
		if (avail < 2) return 0;
		len = AltDB_ReadLE16(b);
		if (len == 0xFFFF) { *outIsNull = TRUE; return 2; }
		if (avail < 2 + len) return 0;
		*outValLen = len;
		memcpy(dst, &b[2], (len < dstCap) ? len : dstCap);
		return (INT)(2 + len);
	}
	case TY_CHAR_LEGACY: case TY_VARCHAR_LEGACY: case TY_BINARY_LEGACY: case TY_VARBINARY_LEGACY: {
		UDINT len;
		if (avail < 1) return 0;
		len = b[0];
		if (avail < 1 + len) return 0;
		*outValLen = len; /* legacy types have no dedicated NULL sentinel here */
		memcpy(dst, &b[1], (len < dstCap) ? len : dstCap);
		return (INT)(1 + len);
	}
	default:
		return -1;
	}
}

/* ---------------- COLMETADATA (0x81) ---------------- */

static INT ParseColMetadata(UDINT slot)
{
	ConnSlot *c = &gConn[slot];
	const unsigned char *b = &c->TokBuf[c->TokPos];
	UDINT avail = c->TokLen - c->TokPos;
	UDINT pos = 1;
	UINT count;
	UINT colIdx;
	ColumnInfo overflowCol;

	if (avail < 3) return 0;
	count = (UINT)AltDB_ReadLE16(&b[1]);
	pos = 3;
	if (count == 0xFFFF) count = 0;

	for (colIdx = 0; colIdx < count; colIdx++) {
		ColumnInfo *dst = (colIdx < ND_MAX_COLUMNS) ? &c->Columns[colIdx] : &overflowCol;
		INT tiLen;
		UINT nameLen;

		if (avail < pos + 6) return 0; /* UserType(4) + Flags(2) */
		pos += 6;

		tiLen = ParseTypeInfo(&b[pos], avail - pos, dst);
		if (tiLen == 0) return 0;
		if (tiLen < 0) { AltDB_DiagPushClient(slot, "HY000", "unrecognised TDS column type"); return -1; }
		pos += (UDINT)tiLen;

		if (avail < pos + 1) return 0;
		nameLen = b[pos];
		if (avail < pos + 1 + (UDINT)nameLen * 2) return 0;
		pos += 1 + (UDINT)nameLen * 2;
	}

	c->ColumnCount = (count > 0x7FFF) ? 0x7FFF : (INT)count;
	memset(c->ColAlreadyRead, 0, sizeof(c->ColAlreadyRead));
	memset(c->ColIsNull, 0, sizeof(c->ColIsNull));
	c->RowReady = FALSE;
	c->RowOverflow = FALSE;
	c->HasResultSet = TRUE;
	c->ResultSetOpen = TRUE;
	c->TokPos += pos;
	return 1;
}

/* ---------------- ROW (0xD1) / NBCROW (0xD2) ---------------- */

static INT ParseRow(UDINT slot, BOOL isNbc)
{
	ConnSlot *c = &gConn[slot];
	const unsigned char *b = &c->TokBuf[c->TokPos];
	UDINT avail = c->TokLen - c->TokPos;
	UDINT pos = 1;
	INT colCount = (c->ColumnCount > 0) ? c->ColumnCount : 0;
	UDINT nullBitmapBytes = 0;
	const unsigned char *nullBitmap = NULL;
	INT i;
	BOOL overflow = FALSE;

	if (isNbc) {
		nullBitmapBytes = ((UDINT)colCount + 7) / 8;
		if (avail < pos + nullBitmapBytes) return 0;
		nullBitmap = &b[pos];
		pos += nullBitmapBytes;
	}

	for (i = 0; i < colCount; i++) {
		BOOL bitmapNull = FALSE;
		BOOL isNull = FALSE;
		UDINT valLen = 0;
		INT consumed;
		ColumnInfo dummyCol;
		ColumnInfo *col;
		unsigned char dummyDst[1];
		unsigned char *dst;
		UDINT dstCap;

		if (isNbc && (nullBitmap[i / 8] & (1 << (i % 8))) != 0) {
			bitmapNull = TRUE;
		}

		if (i < ND_MAX_COLUMNS) {
			col = &c->Columns[i];
			dst = c->ColValue[i];
			dstCap = ND_MAX_COL_VALUE_LEN;
		} else {
			/* Columns beyond ND_MAX_COLUMNS: metadata was not retained
			 * (ParseColMetadata), so the value cannot be decoded. This
			 * is only reachable for results with more columns than
			 * this library supports and is reported as dbERR_MEMORY
			 * by the caller via RowOverflow. */
			memset(&dummyCol, 0, sizeof(dummyCol));
			dummyCol.DataType = TY_NULL;
			col = &dummyCol;
			dst = dummyDst;
			dstCap = 0;
			overflow = TRUE;
		}

		if (bitmapNull) {
			isNull = TRUE;
			consumed = 0;
		} else {
			consumed = ParseColumnValue(&b[pos], avail - pos, col, dst, dstCap, &valLen, &isNull);
			if (consumed == 0) return 0;
			if (consumed < 0) { AltDB_DiagPushClient(slot, "HY000", "malformed row value"); return -1; }
		}
		pos += (UDINT)consumed;

		if (i < ND_MAX_COLUMNS) {
			c->ColIsNull[i] = isNull;
			c->ColAlreadyRead[i] = FALSE;
			if (!isNull) {
				c->ColValueLen[i] = valLen;
				if (valLen > ND_MAX_COL_VALUE_LEN) overflow = TRUE;
			} else {
				c->ColValueLen[i] = 0;
			}
		}
	}

	c->TokPos += pos;
	c->RowReady = TRUE;
	c->RowOverflow = overflow;
	return 1;
}

/* ---------------- ERROR (0xAA) / INFO (0xAB) ---------------- */

static void MapSqlState(DINT nativeError, unsigned char class_, BOOL isError, char out[6])
{
	const char *s;

	if (!isError) { s = "01000"; }
	else switch (nativeError) {
		case 18456: s = "28000"; break;
		case 4060:  s = "42000"; break;
		case 102: case 156: s = "42000"; break;
		case 208: s = "42S02"; break;
		case 207: s = "42S22"; break;
		case 2627: case 2601: case 547: case 515: s = "23000"; break;
		case 8152: case 2628: s = "22001"; break;
		case 245: case 8114: s = "22018"; break;
		case 8115: s = "22003"; break;
		case 1205: s = "40001"; break;
		default: s = (class_ >= 17) ? "HY000" : "42000"; break;
	}
	memcpy(out, s, 5);
	out[5] = 0;
}

static INT ParseErrorOrInfo(UDINT slot, BOOL isError)
{
	ConnSlot *c = &gConn[slot];
	const unsigned char *b = &c->TokBuf[c->TokPos];
	UDINT avail = c->TokLen - c->TokPos;
	UINT length;
	UDINT total;
	DINT number;
	unsigned char class_;
	UINT msgChars;
	UDINT msgBytes;
	char sqlState[6];
	unsigned char connMsg[256];
	UDINT connMsgLen;

	if (avail < 3) return 0;
	length = (UINT)AltDB_ReadLE16(&b[1]);
	total = 3 + length;
	if (avail < total) return 0;

	number = (DINT)AltDB_ReadLE32(&b[3]);
	/* b[7] = State (1 byte), b[8] = Class (1 byte) */
	class_ = b[8];
	msgChars = (UINT)AltDB_ReadLE16(&b[9]);
	msgBytes = (UDINT)msgChars * 2;

	MapSqlState(number, class_, isError && class_ >= 11, sqlState);
	connMsgLen = AltDB_WireToConn(c->Encoding, &b[11], msgBytes, connMsg, sizeof(connMsg));
	AltDB_DiagPushServer(slot, isError && class_ >= 11, number, sqlState, connMsg, connMsgLen);

	c->TokPos += total;
	return 1;
}

/* ---------------- LOGINACK (0xAD) ---------------- */

static INT ParseLoginAck(UDINT slot)
{
	ConnSlot *c = &gConn[slot];
	const unsigned char *b = &c->TokBuf[c->TokPos];
	UDINT avail = c->TokLen - c->TokPos;
	UINT length;
	UDINT total;

	if (avail < 3) return 0;
	length = (UINT)AltDB_ReadLE16(&b[1]);
	total = 3 + length;
	if (avail < total) return 0;

	if (length >= 10) {
		/* payload (offset 3 = start of token data): Interface(1) at 3,
		 * TDSVersion(4) at 4..7, ProgName B_VARCHAR length byte at 8,
		 * ProgName chars at 9.., ProgVersion(4) right after. */
		UDINT progNameLenPos = 8;
		UINT progNameChars = b[progNameLenPos];
		UDINT progVerPos = progNameLenPos + 1 + (UDINT)progNameChars * 2;

		if (avail >= progVerPos + 4) {
			unsigned char major = b[progVerPos];
			unsigned char minor = b[progVerPos + 1];
			UINT build = (UINT)((b[progVerPos + 2] << 8) | b[progVerPos + 3]);
			char tmp[32];
			UDINT n = 0;

			n += AltDB_FormatI64(major, &tmp[n], sizeof(tmp) - n);
			tmp[n++] = '.';
			n += AltDB_FormatI64(minor, &tmp[n], sizeof(tmp) - n);
			tmp[n++] = '.';
			n += AltDB_FormatI64(build, &tmp[n], sizeof(tmp) - n);
			if (n >= sizeof(c->ServerVersion)) n = sizeof(c->ServerVersion) - 1;
			memcpy(c->ServerVersion, tmp, n);
			c->ServerVersion[n] = 0;
		}
	}

	c->TokPos += total;
	return 1;
}

/* ---------------- ENVCHANGE (0xE3) ---------------- */

static INT ParseEnvChange(UDINT slot)
{
	ConnSlot *c = &gConn[slot];
	const unsigned char *b = &c->TokBuf[c->TokPos];
	UDINT avail = c->TokLen - c->TokPos;
	UINT length;
	UDINT total;
	unsigned char envType;

	if (avail < 3) return 0;
	length = (UINT)AltDB_ReadLE16(&b[1]);
	total = 3 + length;
	if (avail < total) return 0;

	envType = (length >= 1) ? b[3] : 0;

	if (envType == 4 && length >= 2) {
		/* packet size: NewValue is a B_VARCHAR of decimal digit characters */
		UINT nChars = b[4];
		UDINT i;
		UDINT v = 0;
		for (i = 0; i < nChars && (5 + i * 2 + 1) < total; i++) {
			unsigned char ch = b[5 + i * 2];
			if (ch >= '0' && ch <= '9') v = v * 10 + (UDINT)(ch - '0');
		}
		if (v > 0 && v <= 32768) c->PacketSize = (UINT)v;
	} else if ((envType == 8 || envType == 9 || envType == 10 || envType == 17) && length >= 2) {
		unsigned char n = b[4];
		if (n == 8 && total >= 5 + 8) {
			memcpy(c->TransactionDescr, &b[5], 8);
		}
	}

	c->TokPos += total;
	return 1;
}

/* ---------------- generic Length-prefixed skip ---------------- */

static INT SkipLengthPrefixed16(UDINT slot)
{
	ConnSlot *c = &gConn[slot];
	const unsigned char *b = &c->TokBuf[c->TokPos];
	UDINT avail = c->TokLen - c->TokPos;
	UINT length;

	if (avail < 3) return 0;
	length = (UINT)AltDB_ReadLE16(&b[1]);
	if (avail < (UDINT)(3 + length)) return 0;
	c->TokPos += 3 + length;
	return 1;
}

static INT SkipSessionState(UDINT slot)
{
	ConnSlot *c = &gConn[slot];
	const unsigned char *b = &c->TokBuf[c->TokPos];
	UDINT avail = c->TokLen - c->TokPos;
	UDINT length;

	if (avail < 5) return 0;
	length = AltDB_ReadLE32(&b[1]);
	if (avail < 5 + length) return 0;
	c->TokPos += 5 + length;
	return 1;
}

/* ---------------- DONE / DONEPROC / DONEINPROC (0xFD/0xFE/0xFF) ---------------- */

#define DONE_MORE     0x0001
#define DONE_ERROR    0x0002
#define DONE_COUNT    0x0010
#define DONE_SRVERROR 0x0100

static INT ParseDone(UDINT slot, BOOL *outMore, BOOL *outError)
{
	ConnSlot *c = &gConn[slot];
	const unsigned char *b = &c->TokBuf[c->TokPos];
	UDINT avail = c->TokLen - c->TokPos;
	UINT status;
	unsigned long long rowCount;

	if (avail < 13) return 0;
	status = (UINT)AltDB_ReadLE16(&b[1]);
	rowCount = AltDB_ReadLE64(&b[5]);

	*outMore = (status & DONE_MORE) != 0;
	*outError = (status & (DONE_ERROR | DONE_SRVERROR)) != 0;

	if ((status & DONE_COUNT) != 0 && !c->AffectedRowsKnown) {
		c->AffectedRows = (rowCount > 0xFFFFFFFFULL) ? 0xFFFFFFFFUL : (UDINT)rowCount;
		c->AffectedRowsKnown = TRUE;
	}

	c->TokPos += 13;
	return 1;
}

/* ---------------- top level dispatcher ---------------- */

INT AltDB_TokenStep(UDINT slot, UINT mode)
{
	ConnSlot *c = &gConn[slot];
	BOOL sawError = FALSE;

	for (;;) {
		unsigned char tokType;
		INT r;

		if (c->TokPos >= c->TokLen) return TOK_NEED_MORE;
		tokType = c->TokBuf[c->TokPos];

		switch (tokType) {
		case 0x81: /* COLMETADATA */
			r = ParseColMetadata(slot);
			if (r == 0) return TOK_NEED_MORE;
			if (r < 0) return TOK_ERROR;
			if (mode == TOKMODE_EXEC) return TOK_GOT_COLMETADATA;
			/* FETCH/DRAIN: a later statement's columns; keep consuming */
			break;

		case 0xD1: /* ROW */
		case 0xD2: /* NBCROW */
			r = ParseRow(slot, (tokType == 0xD2));
			if (r == 0) return TOK_NEED_MORE;
			if (r < 0) return TOK_ERROR;
			if (c->RowOverflow) return TOK_ERROR_MEMORY;
			if (mode == TOKMODE_FETCH) return TOK_GOT_ROW;
			/* EXEC should not normally see rows before COLMETADATA is
			 * returned to the caller; DRAIN discards them. */
			break;

		case 0xFD: case 0xFE: case 0xFF: { /* DONE / DONEPROC / DONEINPROC */
			BOOL more, errFlag;
			r = ParseDone(slot, &more, &errFlag);
			if (r == 0) return TOK_NEED_MORE;

			if (mode == TOKMODE_FETCH) {
				c->ResultSetOpen = FALSE;
				c->NoMoreRows = TRUE;
				return (errFlag || sawError) ? TOK_ERROR : TOK_GOT_EOF;
			}
			if (errFlag || sawError) return TOK_ERROR;
			if (more) break; /* more statements follow in this batch */
			if (mode == TOKMODE_DRAIN) return TOK_GOT_DONE_FINAL;
			if (mode == TOKMODE_LOGIN) return TOK_GOT_DONE_FINAL;
			/* TOKMODE_EXEC: batch ended without ever producing a result set */
			c->HasResultSet = FALSE;
			c->ResultSetOpen = FALSE;
			return TOK_GOT_DONE_FINAL;
		}

		case 0xAA: /* ERROR */
			r = ParseErrorOrInfo(slot, TRUE);
			if (r == 0) return TOK_NEED_MORE;
			sawError = TRUE;
			break;

		case 0xAB: /* INFO */
			r = ParseErrorOrInfo(slot, FALSE);
			if (r == 0) return TOK_NEED_MORE;
			break;

		case 0xAD: /* LOGINACK */
			r = ParseLoginAck(slot);
			if (r == 0) return TOK_NEED_MORE;
			break;

		case 0xE3: /* ENVCHANGE */
			r = ParseEnvChange(slot);
			if (r == 0) return TOK_NEED_MORE;
			break;

		case 0x79: /* RETURNSTATUS: fixed 4-byte value, ignored */
			if (c->TokLen - c->TokPos < 5) return TOK_NEED_MORE;
			c->TokPos += 5;
			break;

		case 0xA9: /* ORDER */
		case 0xA4: /* TABNAME */
		case 0xA5: /* COLINFO */
			r = SkipLengthPrefixed16(slot);
			if (r == 0) return TOK_NEED_MORE;
			break;

		case 0xE4: /* SESSIONSTATE */
			r = SkipSessionState(slot);
			if (r == 0) return TOK_NEED_MORE;
			break;

		default:
			AltDB_DiagPushClient(slot, "08S01", "unexpected/unsupported TDS token, protocol out of sync");
			return TOK_ERROR;
		}
	}
}
