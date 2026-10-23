/********************************************************************
 * Library: AltDB
 * File: Convert.c
 ********************************************************************
 * dbGetData data conversion (Section 11). Converts the raw column
 * value bytes stored by Token.c (gConn[slot].ColValue[columnIdx])
 * into one of the DB_SQL_* target layouts of Table 22.
 *
 * Scope: the full conversion matrix (Table 23) is implemented for
 * the practically important source/target combinations. Combinations
 * marked '-' in Table 23, and sources this library cannot decode at
 * all (SQL_VARIANT, XML, UDT), are rejected with dbERR_DATABASE /
 * SQLSTATE 07006, matching the "not allowed" semantics of the
 * specification. DATETIMEOFFSET is read as if it were DATETIME2 (the
 * trailing minute-offset field is ignored, i.e. treated as UTC/local
 * time) - a documented simplification, see Common.h.
 ********************************************************************/
#include <bur/plctypes.h>
#include <runtime.h>
#include <AsTCP.h>
#include <AltDB.h>
#include "Common.h"
#include <string.h>

enum {
	FAM_INT, FAM_FLOAT, FAM_DECIMAL, FAM_MONEY, FAM_CHAR, FAM_BINARY,
	FAM_DATE, FAM_TIME, FAM_DATETIME, FAM_DATETIME2, FAM_GUID, FAM_UNSUPPORTED
};

static int Family(unsigned char t)
{
	switch (t) {
	case 0x30: case 0x32: case 0x34: case 0x38: case 0x7F:
	case 0x26: case 0x68:
		return FAM_INT;
	case 0x3B: case 0x3E: case 0x6D:
		return FAM_FLOAT;
	case 0x6A: case 0x6C:
		return FAM_DECIMAL;
	case 0x3C: case 0x7A: case 0x6E:
		return FAM_MONEY;
	case 0xA7: case 0xAF: case 0xE7: case 0xEF:
	case 0x2F: case 0x27:
	case 0x23: case 0x63:
		return FAM_CHAR;
	case 0xA5: case 0xAD: case 0x2D: case 0x25: case 0x22:
		return FAM_BINARY;
	case 0x28:
		return FAM_DATE;
	case 0x29:
		return FAM_TIME;
	case 0x3A: case 0x3D: case 0x6F:
		return FAM_DATETIME;
	case 0x2A: case 0x2B:
		return FAM_DATETIME2;
	case 0x24:
		return FAM_GUID;
	default:
		return FAM_UNSUPPORTED;
	}
}

static long long ReadIntValue(const unsigned char *p, UDINT len)
{
	unsigned long long u = 0;
	UDINT i;
	for (i = 0; i < len && i < 8; i++) u |= ((unsigned long long)p[i]) << (8 * i);
	if (len == 1) return (long long)u;        /* tinyint/bit: unsigned */
	if (len == 2) return (long long)(short)(unsigned short)u;
	if (len == 4) return (long long)(DINT)(UDINT)u;
	return (long long)u;
}

static long long ReadMoneyValue(const unsigned char *p, UDINT len)
{
	if (len == 4) return (long long)(DINT)AltDB_ReadLE32(p);
	{
		DINT hi = (DINT)AltDB_ReadLE32(p);
		UDINT lo = AltDB_ReadLE32(p + 4);
		return ((long long)hi << 32) | (long long)lo;
	}
}

static double ReadFloatValue(const unsigned char *p, UDINT len)
{
	if (len == 4) {
		union { UDINT u; float f; } v;
		v.u = AltDB_ReadLE32(p);
		return (double)v.f;
	} else {
		union { unsigned long long u; double d; } v;
		v.u = AltDB_ReadLE64(p);
		return v.d;
	}
}

/* value = integer / 10^scale, formatted as a decimal string */
static UDINT FormatScaledI64(long long v, USINT scale, char *dst, UDINT dstCap)
{
	unsigned char mag[8];
	BOOL positive = (v >= 0);
	unsigned long long u = positive ? (unsigned long long)v : (unsigned long long)(-(v + 1)) + 1ULL;
	UDINT i;
	for (i = 0; i < 8; i++) { mag[i] = (unsigned char)(u & 0xFF); u >>= 8; }
	return AltDB_FormatDecimal(mag, 8, positive, scale, dst, dstCap);
}

/* Decodes a DECIMALN/NUMERICN value (1 sign byte + little-endian
 * magnitude) into a plain double; sufficient precision for REAL /
 * DOUBLE targets (exact string output uses AltDB_FormatDecimal
 * directly for the CHAR target). */
static double DecimalToDouble(const unsigned char *raw, UDINT len, USINT scale)
{
	double v = 0.0;
	UDINT magLen = (len > 0) ? len - 1 : 0;
	BOOL positive = (len > 0) ? (raw[0] != 0) : TRUE;
	double divisor = 1.0;
	UDINT i;

	for (i = magLen; i > 0; i--) {
		v = v * 256.0 + (double)raw[i];
	}
	if (!positive) v = -v;
	for (i = 0; i < scale; i++) divisor *= 10.0;
	return v / divisor;
}

static void WriteGuidString(const unsigned char *g, char *dst)
{
	static const char *hex = "0123456789ABCDEF";
	static const int order[16] = {3,2,1,0, 5,4, 7,6, 8,9, 10,11,12,13,14,15};
	int i, p = 0;
	for (i = 0; i < 16; i++) {
		unsigned char b = g[order[i]];
		dst[p++] = hex[(b >> 4) & 0xF];
		dst[p++] = hex[b & 0xF];
		if (i == 3 || i == 5 || i == 7 || i == 9) dst[p++] = '-';
	}
	dst[p] = 0;
}

/* ---------------- date / time decoding (Section 10.9) ---------------- */

/* Howard Hinnant's civil_from_days algorithm (public domain formula),
 * valid over the whole proleptic Gregorian calendar. z = days since
 * 1970-01-01. */
static void CivilFromDays(long long z, INT *y, UINT *m, UINT *d)
{
	long long era;
	unsigned doe, yoe, doy, mp;

	z += 719468;
	era = (z >= 0 ? z : z - 146096) / 146097;
	doe = (unsigned)(z - era * 146097);
	yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	mp = (5 * doy + 2) / 153;
	*d = (UINT)(doy - (153 * mp + 2) / 5 + 1);
	*m = (UINT)(mp + (mp < 10 ? 3 : -9));
	*y = (INT)((long long)yoe + era * 400 + (*m <= 2 ? 1 : 0));
}

typedef struct { INT y; UINT mo; UINT d; UINT h; UINT mi; UINT s; UDINT frac; USINT fracDigits; } DecodedDT;

/* days-since-0001-01-01 -> civil date */
static void DateFromDbDays(UDINT dbDays, INT *y, UINT *m, UINT *d)
{
	/* days_from_civil(1,1,1) = -719162 (date 0001-01-01 relative to 1970-01-01) */
	CivilFromDays((long long)dbDays - 719162LL, y, m, d);
}

static BOOL DecodeDateTime(int fam, unsigned char dataType, const unsigned char *raw, UDINT rawLen, USINT scale, DecodedDT *out)
{
	memset(out, 0, sizeof(*out));

	if (fam == FAM_DATE) {
		UDINT days = raw[0] | ((UDINT)raw[1] << 8) | ((UDINT)raw[2] << 16);
		DateFromDbDays(days, &out->y, &out->mo, &out->d);
		return TRUE;
	}
	if (fam == FAM_TIME || fam == FAM_DATETIME2) {
		UDINT dateBytes = (fam == FAM_DATETIME2) ? 3 : 0;
		UDINT offsetBytes = (dataType == 0x2B) ? 2 : 0;
		UDINT timeLen = (rawLen > dateBytes + offsetBytes) ? rawLen - dateBytes - offsetBytes : rawLen;
		UDINT i;
		unsigned long long ticks = 0;
		unsigned long long divisor = 1;
		UDINT secTotal;

		for (i = 0; i < timeLen && i < 8; i++) ticks |= ((unsigned long long)raw[i]) << (8 * i);
		for (i = 0; i < scale; i++) divisor *= 10ULL;
		secTotal = (UDINT)(ticks / divisor);
		out->frac = (UDINT)(ticks % divisor);
		out->fracDigits = scale;
		out->h = secTotal / 3600; out->mi = (secTotal / 60) % 60; out->s = secTotal % 60;

		if (fam == FAM_DATETIME2) {
			UDINT days = raw[timeLen] | ((UDINT)raw[timeLen + 1] << 8) | ((UDINT)raw[timeLen + 2] << 16);
			DateFromDbDays(days, &out->y, &out->mo, &out->d);
		}
		return TRUE;
	}
	if (fam == FAM_DATETIME) {
		if (rawLen == 4) { /* SMALLDATETIME: days(2,u16) + minutes(2,u16) since 1900-01-01 */
			UINT days = (UINT)AltDB_ReadLE16(raw);
			UINT minutes = (UINT)AltDB_ReadLE16(&raw[2]);
			CivilFromDays((long long)days - 25567LL, &out->y, &out->mo, &out->d); /* 1900-01-01 vs 1970-01-01 */
			out->h = minutes / 60; out->mi = minutes % 60; out->s = 0;
		} else { /* DATETIME: days(4,signed) + ticks(4,u32) of 1/300s since midnight */
			DINT days = (DINT)AltDB_ReadLE32(raw);
			UDINT ticks = AltDB_ReadLE32(&raw[4]);
			UDINT secTotal = ticks / 300;
			CivilFromDays((long long)days - 25567LL, &out->y, &out->mo, &out->d);
			out->h = secTotal / 3600; out->mi = (secTotal / 60) % 60; out->s = secTotal % 60;
		}
		return TRUE;
	}
	return FALSE;
}

static UDINT FormatDateTimeChar(int fam, const DecodedDT *dt, char *dst, UDINT cap)
{
	UDINT n = 0;
	char tmp[16];
	UDINT tn;

#define APPEND_NUM(v, width) do { \
		UDINT k; tn = (UDINT)AltDB_FormatI64((v), tmp, sizeof(tmp)); \
		for (k = tn; k < (UDINT)(width); k++) { if (n < cap) dst[n++] = '0'; } \
		for (k = 0; k < tn; k++) { if (n < cap) dst[n++] = tmp[k]; } \
	} while (0)

	if (fam == FAM_DATE || fam == FAM_DATETIME || fam == FAM_DATETIME2) {
		APPEND_NUM(dt->y, 4);
		if (n < cap) dst[n++] = '-';
		APPEND_NUM((long long)dt->mo, 2);
		if (n < cap) dst[n++] = '-';
		APPEND_NUM((long long)dt->d, 2);
		if (fam == FAM_DATE) { if (n < cap) dst[n] = 0; return n; }
		if (n < cap) dst[n++] = ' ';
	}

	APPEND_NUM((long long)dt->h, 2);
	if (n < cap) dst[n++] = ':';
	APPEND_NUM((long long)dt->mi, 2);
	if (n < cap) dst[n++] = ':';
	APPEND_NUM((long long)dt->s, 2);
	if (dt->fracDigits > 0) {
		if (n < cap) dst[n++] = '.';
		APPEND_NUM((long long)dt->frac, dt->fracDigits);
	}
	if (n < cap) dst[n] = 0;
	return n;
#undef APPEND_NUM
}

static void PutCharResult(const char *text, UDINT textLen, UDINT pData, UDINT dataSize,
                           UINT *status, INT *dbError, UDINT *actDataSize)
{
	UDINT required = textLen + 1;
	if (dataSize < required) {
		*status = dbERR_BUFFER_SIZE; *dbError = DB_SQL_ERROR; *actDataSize = required;
		return;
	}
	memcpy((void *)pData, text, textLen);
	((char *)pData)[textLen] = 0;
	*status = ERR_OK; *dbError = 0; *actDataSize = required;
}

void AltDB_GetData(UDINT slot, UDINT columnIdx, INT dataType, UDINT pData, UDINT dataSize,
                      UINT *status, INT *dbError, UDINT *actDataSize)
{
	ConnSlot *c = &gConn[slot];
	UDINT idx;
	ColumnInfo *col;
	int fam;
	const unsigned char *raw;
	UDINT rawLen;

	*actDataSize = 0;

	if (!c->RowReady) { *status = dbERR_SQL_EXECUTE_REQUIRED; *dbError = DB_SQL_ERROR; return; }
	if (columnIdx == 0 || columnIdx > (UDINT)c->ColumnCount || columnIdx > ND_MAX_COLUMNS) {
		*status = dbERR_INVALID_COLUMN; *dbError = DB_SQL_ERROR; return;
	}
	if (pData == 0) { *status = dbERR_NULLPOINTER; *dbError = DB_SQL_ERROR; return; }

	idx = columnIdx - 1;
	if (c->ColAlreadyRead[idx]) { *status = dbERR_DATABASE; *dbError = DB_SQL_NO_DATA; return; }

	if (c->ColIsNull[idx]) {
		*status = ERR_OK; *dbError = 0; *actDataSize = 0xFFFFFFFFUL;
		c->ColAlreadyRead[idx] = TRUE;
		return;
	}

	col = &c->Columns[idx];
	raw = c->ColValue[idx];
	rawLen = c->ColValueLen[idx];
	if (rawLen > ND_MAX_COL_VALUE_LEN) rawLen = ND_MAX_COL_VALUE_LEN;

	if (!col->Supported) {
		AltDB_DiagPushClient(slot, "07006", "source column type is not convertible by this library");
		*status = dbERR_DATABASE; *dbError = DB_SQL_ERROR; return;
	}

	fam = Family(col->DataType);

	switch (dataType) {

	case 1: { /* DB_SQL_CHAR */
		char text[600];
		UDINT n = 0;

		switch (fam) {
		case FAM_CHAR:
			if (col->IsUnicode) n = AltDB_WireToConn(c->Encoding, raw, rawLen, (unsigned char *)text, sizeof(text) - 1);
			else { n = (rawLen < sizeof(text) - 1) ? rawLen : sizeof(text) - 1; memcpy(text, raw, n); }
			break;
		case FAM_BINARY: {
			static const char *hex = "0123456789ABCDEF";
			UDINT i;
			for (i = 0; i < rawLen && n + 2 < sizeof(text); i++) {
				text[n++] = hex[(raw[i] >> 4) & 0xF];
				text[n++] = hex[raw[i] & 0xF];
			}
			break;
		}
		case FAM_INT:
			n = (UDINT)AltDB_FormatI64(ReadIntValue(raw, rawLen), text, sizeof(text));
			break;
		case FAM_MONEY:
			n = FormatScaledI64(ReadMoneyValue(raw, rawLen), 4, text, sizeof(text));
			break;
		case FAM_DECIMAL:
			n = AltDB_FormatDecimal(&raw[1], (rawLen > 0) ? rawLen - 1 : 0, raw[0] != 0, col->Scale, text, sizeof(text));
			break;
		case FAM_FLOAT:
			n = AltDB_FormatDouble(ReadFloatValue(raw, rawLen), (rawLen == 4) ? 7 : 15, text, sizeof(text));
			break;
		case FAM_GUID:
			if (rawLen >= 16) { WriteGuidString(raw, text); n = (UDINT)strlen(text); }
			break;
		case FAM_DATE: case FAM_TIME: case FAM_DATETIME: case FAM_DATETIME2: {
			DecodedDT dt;
			if (DecodeDateTime(fam, col->DataType, raw, rawLen, col->Scale, &dt)) {
				n = FormatDateTimeChar(fam, &dt, text, sizeof(text));
			}
			break;
		}
		default:
			AltDB_DiagPushClient(slot, "07006", "source column type is not convertible to DB_SQL_CHAR");
			*status = dbERR_DATABASE; *dbError = DB_SQL_ERROR; return;
		}
		PutCharResult(text, n, pData, dataSize, status, dbError, actDataSize);
		if (*status == ERR_OK) c->ColAlreadyRead[idx] = TRUE;
		return;
	}

	case -6: case 5: case 4: case -25: { /* TINYINT / SMALLINT / INTEGER / BIGINT */
		long long v;
		UDINT needed = (dataType == -6) ? 1 : (dataType == 5) ? 2 : (dataType == 4) ? 4 : 8;

		if (fam == FAM_INT) v = ReadIntValue(raw, rawLen);
		else if (fam == FAM_MONEY) v = ReadMoneyValue(raw, rawLen) / 10000;
		else if (fam == FAM_DECIMAL) v = (long long)DecimalToDouble(raw, rawLen, col->Scale);
		else if (fam == FAM_FLOAT) v = (long long)ReadFloatValue(raw, rawLen);
		else {
			AltDB_DiagPushClient(slot, "07006", "source column type is not convertible to an integer type");
			*status = dbERR_DATABASE; *dbError = DB_SQL_ERROR; return;
		}
				/* AsDB compatibility: allow BIGINT -> UDINT */

		if (dataType == -25 && dataSize == 4)
		{
			UDINT tmp = (UDINT)v;
			memcpy((void *)pData, &tmp, 4);
			*status = ERR_OK;
			*dbError = 0;
			*actDataSize = 4;
			c->ColAlreadyRead[idx] = TRUE;
			return;
		}		
		if (dataSize < needed) { *status = dbERR_BUFFER_SIZE; *dbError = DB_SQL_ERROR; *actDataSize = needed; return; }
		{
			unsigned char tmp[8];
			UDINT i;
			for (i = 0; i < needed; i++) tmp[i] = (unsigned char)((unsigned long long)v >> (8 * i));
			memcpy((void *)pData, tmp, needed);
		}
		*status = ERR_OK; *dbError = 0; *actDataSize = needed;
		c->ColAlreadyRead[idx] = TRUE;
		return;
	}

	case 7: case 8: { /* DB_SQL_REAL / DB_SQL_DOUBLE */
		double v;
		UDINT needed = (dataType == 7) ? 4 : 8;

		if (fam == FAM_INT) v = (double)ReadIntValue(raw, rawLen);
		else if (fam == FAM_MONEY) v = (double)ReadMoneyValue(raw, rawLen) / 10000.0;
		else if (fam == FAM_DECIMAL) v = DecimalToDouble(raw, rawLen, col->Scale);
		else if (fam == FAM_FLOAT) v = ReadFloatValue(raw, rawLen);
		else {
			AltDB_DiagPushClient(slot, "07006", "source column type is not convertible to a floating point type");
			*status = dbERR_DATABASE; *dbError = DB_SQL_ERROR; return;
		}
		if (dataSize < needed) { *status = dbERR_BUFFER_SIZE; *dbError = DB_SQL_ERROR; *actDataSize = needed; return; }
		if (needed == 4) {
			union { UDINT u; float f; } u; u.f = (float)v;
			memcpy((void *)pData, &u.u, 4);
		} else {
			union { unsigned long long u; double d; } u; u.d = v;
			memcpy((void *)pData, &u.u, 8);
		}
		*status = ERR_OK; *dbError = 0; *actDataSize = needed;
		c->ColAlreadyRead[idx] = TRUE;
		return;
	}

	case 9: case 10: case 11: { /* DB_SQL_DATE / DB_SQL_TIME / DB_SQL_TIMESTAMP */
		DecodedDT dt;
		UDINT needed = (dataType == 9 || dataType == 10) ? 6 : 12;
		UINT vals[6];

		if (fam != FAM_DATE && fam != FAM_TIME && fam != FAM_DATETIME && fam != FAM_DATETIME2) {
			AltDB_DiagPushClient(slot, "07006", "source column type is not convertible to a date/time type");
			*status = dbERR_DATABASE; *dbError = DB_SQL_ERROR; return;
		}
		if (dataType == 9 && fam == FAM_TIME) {
			AltDB_DiagPushClient(slot, "07006", "TIME source cannot convert to DB_SQL_DATE");
			*status = dbERR_DATABASE; *dbError = DB_SQL_ERROR; return;
		}
		if (!DecodeDateTime(fam, col->DataType, raw, rawLen, col->Scale, &dt)) {
			*status = dbERR_DATABASE; *dbError = DB_SQL_ERROR; return;
		}
		if (dataSize < needed) { *status = dbERR_BUFFER_SIZE; *dbError = DB_SQL_ERROR; *actDataSize = needed; return; }

		if (dataType == 9) { vals[0] = (UINT)dt.y; vals[1] = dt.mo; vals[2] = dt.d; }
		else if (dataType == 10) { vals[0] = dt.h; vals[1] = dt.mi; vals[2] = dt.s; }
		else { vals[0] = (UINT)dt.y; vals[1] = dt.mo; vals[2] = dt.d; vals[3] = dt.h; vals[4] = dt.mi; vals[5] = dt.s; }
		{
			UDINT n = (dataType == 11) ? 6 : 3;
			UDINT i;
			for (i = 0; i < n; i++) AltDB_WriteLE16((unsigned char *)pData + i * 2, vals[i]);
		}
		*status = ERR_OK; *dbError = 0; *actDataSize = needed;
		c->ColAlreadyRead[idx] = TRUE;
		return;
	}

	case -11: { /* DB_SQL_GUID */
		if (fam != FAM_GUID && !(fam == FAM_BINARY && rawLen == 16)) {
			AltDB_DiagPushClient(slot, "07006", "source column type is not convertible to DB_SQL_GUID");
			*status = dbERR_DATABASE; *dbError = DB_SQL_ERROR; return;
		}
		if (dataSize < 16) { *status = dbERR_BUFFER_SIZE; *dbError = DB_SQL_ERROR; *actDataSize = 16; return; }
		memcpy((void *)pData, raw, 16);
		*status = ERR_OK; *dbError = 0; *actDataSize = 16;
		c->ColAlreadyRead[idx] = TRUE;
		return;
	}

	case 2: case 3: { /* DB_SQL_NUMERIC / DB_SQL_DECIMAL: ODBC SQL_NUMERIC_STRUCT (19 bytes) */
		unsigned char prec, scale, sign;
		unsigned char val[16];

		memset(val, 0, sizeof(val));
		if (fam == FAM_DECIMAL) {
			UDINT magLen = (rawLen > 0) ? rawLen - 1 : 0;
			if (magLen > 16) magLen = 16;
			memcpy(val, &raw[1], magLen);
			sign = (raw[0] != 0) ? 1 : 0;
			prec = col->Precision; scale = col->Scale;
		} else if (fam == FAM_INT || fam == FAM_MONEY) {
			long long v = (fam == FAM_INT) ? ReadIntValue(raw, rawLen) : ReadMoneyValue(raw, rawLen);
			unsigned long long u = (v >= 0) ? (unsigned long long)v : (unsigned long long)(-(v + 1)) + 1ULL;
			UDINT i;
			for (i = 0; i < 8; i++) { val[i] = (unsigned char)(u & 0xFF); u >>= 8; }
			sign = (v >= 0) ? 1 : 0;
			scale = (fam == FAM_MONEY) ? 4 : 0;
			prec = 19;
		} else {
			AltDB_DiagPushClient(slot, "07006", "source column type is not convertible to DB_SQL_NUMERIC/DECIMAL");
			*status = dbERR_DATABASE; *dbError = DB_SQL_ERROR; return;
		}
		if (dataSize < 19) { *status = dbERR_BUFFER_SIZE; *dbError = DB_SQL_ERROR; *actDataSize = 19; return; }
		{
			unsigned char *out = (unsigned char *)pData;
			out[0] = prec; out[1] = (unsigned char)scale; out[2] = sign;
			memcpy(&out[3], val, 16);
		}
		*status = ERR_OK; *dbError = 0; *actDataSize = 19;
		c->ColAlreadyRead[idx] = TRUE;
		return;
	}

	default:
		AltDB_DiagPushClient(slot, "HY004", "unsupported dataType requested");
		*status = dbERR_INVALID_DATATYPE; *dbError = DB_SQL_ERROR;
		return;
	}
}
