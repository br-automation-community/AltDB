/********************************************************************
 * Library: AltDB
 * File: Util.c
 ********************************************************************
 * Shared helpers: connection handle encode/decode, slot pool
 * management, diagnostics ring, byte order helpers, character set
 * transcoding and number formatting for DB_SQL_CHAR output.
 ********************************************************************/
#include <bur/plctypes.h>
#include <runtime.h>
#include <AsTCP.h>
#include <AltDB.h>
#include "Common.h"
#include <string.h>

UDINT AltDB_MakeHandle(UDINT slot, UINT generation)
{
	return HANDLE_TAG | (((UDINT)generation << HANDLE_GEN_SHIFT) & 0xFFFFF000UL) | ((slot + 1) & HANDLE_SLOT_MASK);
}

BOOL AltDB_SlotFromHandle(UDINT handle, UDINT *pSlot, UINT *pGeneration)
{
	UDINT slot;
	UINT generation;

	if ((handle & 0xFF000000UL) != HANDLE_TAG) {
		return FALSE;
	}
	slot = (handle & HANDLE_SLOT_MASK);
	if (slot == 0 || slot > MAX_CONN) {
		return FALSE;
	}
	slot = slot - 1;
	generation = (UINT)((handle >> HANDLE_GEN_SHIFT) & 0x0FFF);
	if (gConn[slot].State == CONNSTATE_FREE || gConn[slot].Generation != generation) {
		return FALSE;
	}
	*pSlot = slot;
	*pGeneration = generation;
	return TRUE;
}

void AltDB_SlotReset(UDINT slot)
{
	UINT keepGeneration = gConn[slot].Generation;

	memset(&gConn[slot], 0, sizeof(gConn[slot]));
	gConn[slot].Generation = keepGeneration;
	gConn[slot].State = CONNSTATE_FREE;
	gConn[slot].PacketSize = TDS_DEFAULT_PACKET_SIZE;
}

BOOL AltDB_SlotAlloc(UDINT *pSlot)
{
	UDINT i;

	for (i = 0; i < MAX_CONN; i++) {
		if (gConn[i].State == CONNSTATE_FREE) {
			AltDB_SlotReset(i);
			*pSlot = i;
			return TRUE;
		}
	}
	return FALSE;
}

BOOL AltDB_TryLock(UDINT slot, UDINT owner)
{
	if (gConn[slot].Locked && gConn[slot].JobOwner != owner) {
		return FALSE;
	}
	gConn[slot].Locked = TRUE;
	gConn[slot].JobOwner = owner;
	return TRUE;
}

void AltDB_Unlock(UDINT slot)
{
	gConn[slot].Locked = FALSE;
	gConn[slot].JobOwner = 0;
	gConn[slot].JobKind = JOB_NONE;
	gConn[slot].JobTicks = 0;
}

BOOL AltDB_IsOwner(UDINT slot, UDINT owner)
{
	return (gConn[slot].Locked && gConn[slot].JobOwner == owner);
}

/* ---------------- diagnostics ring ---------------- */

void AltDB_DiagClear(UDINT slot)
{
	gConn[slot].DiagCount = 0;
	gConn[slot].DiagReadIdx = 0;
	memset(gConn[slot].Diag, 0, sizeof(gConn[slot].Diag));
}

static DiagRec *DiagNextSlot(UDINT slot)
{
	UINT idx;

	if (gConn[slot].DiagCount >= MAX_DIAG) {
		/* ring full: drop the oldest unread record to make room */
		memmove(&gConn[slot].Diag[0], &gConn[slot].Diag[1], sizeof(DiagRec) * (MAX_DIAG - 1));
		idx = (UINT)(MAX_DIAG - 1);
		if (gConn[slot].DiagReadIdx > 0) {
			gConn[slot].DiagReadIdx--;
		}
	} else {
		idx = gConn[slot].DiagCount;
		gConn[slot].DiagCount++;
	}
	return &gConn[slot].Diag[idx];
}

void AltDB_DiagPushServer(UDINT slot, BOOL isError, DINT nativeError,
                             const char *sqlState, const unsigned char *text, UDINT textLen)
{
	DiagRec *rec = DiagNextSlot(slot);
	const char *prefix = "[AltDB][SQL Server]";
	UDINT prefixLen = (UDINT)strlen(prefix);
	UDINT cap = (UDINT)sizeof(rec->Message) - 1;
	UDINT n;

	rec->Used = TRUE;
	rec->IsError = isError;
	rec->NativeError = nativeError;
	memcpy(rec->SqlState, sqlState, 5);
	rec->SqlState[5] = 0;

	n = (prefixLen < cap) ? prefixLen : cap;
	memcpy(rec->Message, prefix, n);
	if (n < cap && textLen > 0) {
		UDINT remain = cap - n;
		UDINT copyLen = (textLen < remain) ? textLen : remain;
		memcpy(&rec->Message[n], text, copyLen);
		n += copyLen;
	}
	rec->Message[n] = 0;
	rec->MessageLen = (UINT)n;
}

void AltDB_DiagPushClient(UDINT slot, const char *sqlState, const char *text)
{
	DiagRec *rec = DiagNextSlot(slot);
	UDINT cap = (UDINT)sizeof(rec->Message) - 1;
	UDINT n;

	rec->Used = TRUE;
	rec->IsError = TRUE;
	rec->NativeError = 0;
	memcpy(rec->SqlState, sqlState, 5);
	rec->SqlState[5] = 0;

	n = (UDINT)strlen(text);
	if (n > cap) {
		n = cap;
	}
	memcpy(rec->Message, "[AltDB]", 9);
	if (9 + n > cap) {
		n = cap - 9;
	}
	memcpy(&rec->Message[9], text, n);
	rec->Message[9 + n] = 0;
	rec->MessageLen = (UINT)(9 + n);
}

/* ---------------- byte order ---------------- */

UINT AltDB_ReadLE16(const unsigned char *p)
{
	return (UINT)(p[0] | (p[1] << 8));
}

UDINT AltDB_ReadLE32(const unsigned char *p)
{
	return (UDINT)p[0] | ((UDINT)p[1] << 8) | ((UDINT)p[2] << 16) | ((UDINT)p[3] << 24);
}

unsigned long long AltDB_ReadLE64(const unsigned char *p)
{
	unsigned long long lo = AltDB_ReadLE32(p);
	unsigned long long hi = AltDB_ReadLE32(p + 4);
	return lo | (hi << 32);
}

UDINT AltDB_ReadBE16(const unsigned char *p)
{
	return (UDINT)((p[0] << 8) | p[1]);
}

void AltDB_WriteLE16(unsigned char *p, UINT v)
{
	p[0] = (unsigned char)(v & 0xFF);
	p[1] = (unsigned char)((v >> 8) & 0xFF);
}

void AltDB_WriteLE32(unsigned char *p, UDINT v)
{
	p[0] = (unsigned char)(v & 0xFF);
	p[1] = (unsigned char)((v >> 8) & 0xFF);
	p[2] = (unsigned char)((v >> 16) & 0xFF);
	p[3] = (unsigned char)((v >> 24) & 0xFF);
}

void AltDB_WriteBE16(unsigned char *p, UINT v)
{
	p[0] = (unsigned char)((v >> 8) & 0xFF);
	p[1] = (unsigned char)(v & 0xFF);
}

/* ---------------- character set transcoding ---------------- */
/* Connection encoding is either ISO-8859-1 (1 byte per code point,
 * identical to Unicode code points U+0000..U+00FF) or UTF-8. Server
 * strings are exchanged as UTF-16LE. Unmappable characters become
 * '?' (Section 11.3/11.4, [V-18]). */

UDINT AltDB_Utf16leToConn(UINT encoding, const unsigned char *src, UDINT srcBytes, unsigned char *dst, UDINT dstCap)
{
	UDINT si = 0;
	UDINT di = 0;

	while (si + 2 <= srcBytes) {
		UINT cp = AltDB_ReadLE16(&src[si]);
		si += 2;

		if (cp >= 0xD800 && cp <= 0xDBFF && si + 2 <= srcBytes) {
			UINT lo = AltDB_ReadLE16(&src[si]);
			if (lo >= 0xDC00 && lo <= 0xDFFF) {
				si += 2;
				cp = 0xFFFD; /* surrogate pair: not representable in 8-bit encodings, replacement char */
			}
		}

		if (encoding == DBENC_UTF_8) {
			if (cp < 0x80) {
				if (di + 1 > dstCap) break;
				dst[di++] = (unsigned char)cp;
			} else if (cp < 0x800) {
				if (di + 2 > dstCap) break;
				dst[di++] = (unsigned char)(0xC0 | (cp >> 6));
				dst[di++] = (unsigned char)(0x80 | (cp & 0x3F));
			} else {
				if (di + 3 > dstCap) break;
				dst[di++] = (unsigned char)(0xE0 | (cp >> 12));
				dst[di++] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
				dst[di++] = (unsigned char)(0x80 | (cp & 0x3F));
			}
		} else {
			if (di + 1 > dstCap) break;
			dst[di++] = (cp <= 0xFF) ? (unsigned char)cp : (unsigned char)'?';
		}
	}
	return di;
}

UDINT AltDB_ConnToUtf16le(UINT encoding, const unsigned char *src, UDINT srcLen, unsigned char *dst, UDINT dstCap)
{
	UDINT si = 0;
	UDINT di = 0;

	while (si < srcLen) {
		UINT cp;

		if (encoding == DBENC_UTF_8) {
			unsigned char b0 = src[si];
			if (b0 < 0x80) {
				cp = b0; si += 1;
			} else if ((b0 & 0xE0) == 0xC0 && si + 1 < srcLen) {
				cp = ((b0 & 0x1F) << 6) | (src[si + 1] & 0x3F); si += 2;
			} else if ((b0 & 0xF0) == 0xE0 && si + 2 < srcLen) {
				cp = ((b0 & 0x0F) << 12) | ((src[si + 1] & 0x3F) << 6) | (src[si + 2] & 0x3F); si += 3;
			} else {
				cp = '?'; si += 1;
			}
		} else {
			cp = src[si]; si += 1;
		}

		if (di + 2 > dstCap) break;
		AltDB_WriteLE16(&dst[di], (UINT)cp);
		di += 2;
	}
	return di;
}

UDINT AltDB_EncodeToWire(UINT encoding, const char *src, UDINT srcLen, unsigned char *dst, UDINT dstCap)
{
	return AltDB_ConnToUtf16le(encoding, (const unsigned char *)src, srcLen, dst, dstCap);
}

UDINT AltDB_WireToConn(UINT encoding, const unsigned char *src, UDINT srcBytes, unsigned char *dst, UDINT dstCap)
{
	return AltDB_Utf16leToConn(encoding, src, srcBytes, dst, dstCap);
}

/* ---------------- number formatting (Section 11.3) ---------------- */

UDINT AltDB_FormatI64(long long v, char *dst, UDINT dstCap)
{
	char tmp[24];
	UDINT n = 0;
	BOOL neg = (v < 0);
	unsigned long long u;
	UDINT i;

	u = neg ? (unsigned long long)(-(v + 1)) + 1ULL : (unsigned long long)v;
	do {
		tmp[n++] = (char)('0' + (u % 10ULL));
		u /= 10ULL;
	} while (u != 0 && n < sizeof(tmp));

	i = 0;
	if (neg) {
		if (i >= dstCap) return 0;
		dst[i++] = '-';
	}
	while (n > 0 && i < dstCap) {
		dst[i++] = tmp[--n];
	}
	if (i < dstCap) dst[i] = 0;
	return i;
}

/* Formats an unsigned big integer given as a little-endian byte
 * array (TDS DECIMALN/NUMERICN magnitude) as a decimal string with
 * the given number of fractional digits (scale). */
UDINT AltDB_FormatDecimal(const unsigned char *magnitude, UDINT magLen, BOOL positive, USINT scale, char *dst, UDINT dstCap)
{
	unsigned char num[16];
	char digits[40];
	UDINT nDigits = 0;
	UDINT i;
	UDINT di;
	BOOL allZero;
	BOOL valueIsZero = TRUE;

	if (magLen > sizeof(num)) magLen = sizeof(num);
	memcpy(num, magnitude, magLen);
	for (i = 0; i < magLen; i++) {
		if (num[i] != 0) { valueIsZero = FALSE; break; }
	}

	do {
		UDINT rem = 0;
		allZero = TRUE;
		for (i = magLen; i > 0; i--) {
			UDINT cur = (rem << 8) | num[i - 1];
			num[i - 1] = (unsigned char)(cur / 10);
			if (num[i - 1] != 0) allZero = FALSE;
			rem = cur % 10;
		}
		digits[nDigits++] = (char)('0' + rem);
	} while (!allZero && nDigits < sizeof(digits));

	while (nDigits <= scale && nDigits < sizeof(digits)) {
		digits[nDigits++] = '0';
	}

	di = 0;
	if (!positive && !valueIsZero) {
		if (di < dstCap) dst[di++] = '-';
	}
	for (i = 0; i < nDigits - scale; i++) {
		if (di < dstCap) dst[di++] = digits[nDigits - 1 - i];
	}
	if (scale > 0) {
		if (di < dstCap) dst[di++] = '.';
		for (i = 0; i < scale; i++) {
			if (di < dstCap) dst[di++] = digits[scale - 1 - i];
		}
	}
	if (di < dstCap) dst[di] = 0;
	return di;
}

/* Simple significant-digit formatter for REAL/DOUBLE source values.
 * Not a byte-exact match for the original AsDb/ODBC formatting
 * ([V-14], requires golden capture); produces a correct, readable
 * decimal or exponential representation. */
UDINT AltDB_FormatDouble(double v, int sigDigits, char *dst, UDINT dstCap)
{
	UDINT di = 0;
	BOOL neg = FALSE;
	int exp10 = 0;
	double mant;
	unsigned long long scaled;
	char digits[20];
	int i;
	double scale;

	if (v != v) { /* NaN */
		if (dstCap > 3) { dst[0] = 'N'; dst[1] = 'a'; dst[2] = 'N'; dst[3] = 0; return 3; }
		return 0;
	}
	if (v < 0.0) { neg = TRUE; v = -v; }

	if (v == 0.0) {
		if (dstCap > 1) { dst[0] = '0'; dst[1] = 0; return 1; }
		return 0;
	}

	mant = v;
	while (mant >= 10.0 && exp10 < 60) { mant /= 10.0; exp10++; }
	while (mant < 1.0 && exp10 > -60) { mant *= 10.0; exp10--; }

	scale = 1.0;
	for (i = 1; i < sigDigits; i++) scale *= 10.0;
	scaled = (unsigned long long)(mant * scale + 0.5);

	/* rounding may have produced one extra digit (e.g. 9.999->10.00) */
	{
		unsigned long long maxVal = 1;
		for (i = 0; i < sigDigits; i++) maxVal *= 10ULL;
		if (scaled >= maxVal) { scaled /= 10ULL; exp10++; }
	}

	for (i = sigDigits - 1; i >= 0; i--) {
		digits[i] = (char)('0' + (scaled % 10ULL));
		scaled /= 10ULL;
	}

	if (neg && di < dstCap) dst[di++] = '-';

	if (exp10 < -4 || exp10 >= sigDigits) {
		/* exponential form d.ddddE+nn */
		if (di < dstCap) dst[di++] = digits[0];
		if (sigDigits > 1) {
			if (di < dstCap) dst[di++] = '.';
			for (i = 1; i < sigDigits; i++) {
				if (di < dstCap) dst[di++] = digits[i];
			}
		}
		if (di < dstCap) dst[di++] = 'E';
		if (di < dstCap) dst[di++] = (exp10 >= 0) ? '+' : '-';
		{
			int ae = (exp10 >= 0) ? exp10 : -exp10;
			char ebuf[8];
			int en = 0;
			do { ebuf[en++] = (char)('0' + (ae % 10)); ae /= 10; } while (ae != 0);
			while (en > 0) { if (di < dstCap) dst[di++] = ebuf[--en]; }
		}
	} else if (exp10 >= 0) {
		for (i = 0; i <= exp10; i++) {
			if (di < dstCap) dst[di++] = (i < sigDigits) ? digits[i] : '0';
		}
		if (exp10 + 1 < sigDigits) {
			if (di < dstCap) dst[di++] = '.';
			for (i = exp10 + 1; i < sigDigits; i++) {
				if (di < dstCap) dst[di++] = digits[i];
			}
		}
	} else {
		if (di < dstCap) dst[di++] = '0';
		if (di < dstCap) dst[di++] = '.';
		for (i = 0; i < -exp10 - 1; i++) {
			if (di < dstCap) dst[di++] = '0';
		}
		for (i = 0; i < sigDigits; i++) {
			if (di < dstCap) dst[di++] = digits[i];
		}
	}

	if (di < dstCap) dst[di] = 0;
	return di;
}
