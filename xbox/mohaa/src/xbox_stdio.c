/*
 * xbox_stdio.c -- floating point text conversions for the Xbox build.
 *
 * nxdk's pdclib parses %f/%e/%g in printf and scanf but does nothing with
 * them, and xboxrt's strtod/strtof/strtold are assert(0) stubs. OpenMoHAA
 * converts floats to and from text everywhere (cvars, scripts, shaders, TIKI
 * files), so:
 *   - printf family: eyalroz/printf (third_party/printf, MIT), plus the
 *     FILE* variants below built on top of its vsnprintf_
 *   - strtod/strtof/strtold/atof and sscanf/vsscanf: implemented here
 * compat/xbox_compat.h redirects the standard names to these functions.
 */
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int _strnicmp(const char *s1, const char *s2, size_t n);

/*
==============================================================
STRING TO FLOATING POINT
==============================================================
*/

// Powers of ten that are exact in a double.
static const double s_pow10[] = {
    1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22,
};

static double scale_pow10(double v, int exp10)
{
    if (v == 0.0)
        return v;
    while (exp10 > 22) {
        v *= 1e22;
        exp10 -= 22;
        if (isinf(v))
            return v;
    }
    while (exp10 < -22) {
        v /= 1e22;
        exp10 += 22;
        if (v == 0.0)
            return v;
    }
    return exp10 >= 0 ? v * s_pow10[exp10] : v / s_pow10[-exp10];
}

static int hex_digit(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    c = tolower(c);
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

static double parse_hex(const char *p, const char **endp)
{
    uint64_t mant = 0;
    int      exp2 = 0, any = 0;

    for (; hex_digit(*p) >= 0; p++, any = 1) {
        if (mant >> 60)
            exp2 += 4;
        else
            mant = mant * 16 + hex_digit(*p);
    }
    if (*p == '.') {
        for (p++; hex_digit(*p) >= 0; p++, any = 1) {
            if (!(mant >> 60)) {
                mant = mant * 16 + hex_digit(*p);
                exp2 -= 4;
            }
        }
    }
    if (any && (*p == 'p' || *p == 'P')) {
        const char *q = p + 1;
        int         neg = 0, e = 0;
        if (*q == '+' || *q == '-')
            neg = *q++ == '-';
        if (isdigit((unsigned char)*q)) {
            while (isdigit((unsigned char)*q) && e < 100000)
                e = e * 10 + (*q++ - '0');
            while (isdigit((unsigned char)*q))
                q++;
            exp2 += neg ? -e : e;
            p = q;
        }
    }
    *endp = any ? p : NULL;
    return ldexp((double)mant, exp2);
}

double xbox_strtod(const char *nptr, char **endptr)
{
    const char *p = nptr;
    uint64_t    mant = 0;
    int         digits = 0, exp10 = 0, any = 0, neg = 0;
    double      v;

    while (isspace((unsigned char)*p))
        p++;
    if (*p == '+' || *p == '-')
        neg = *p++ == '-';

    if (!_strnicmp(p, "inf", 3)) {
        p += 3;
        if (!_strnicmp(p, "inity", 5))
            p += 5;
        if (endptr)
            *endptr = (char *)p;
        return neg ? -INFINITY : INFINITY;
    }
    if (!_strnicmp(p, "nan", 3)) {
        p += 3;
        if (*p == '(') {
            const char *q = p + 1;
            while (isalnum((unsigned char)*q) || *q == '_')
                q++;
            if (*q == ')')
                p = q + 1;
        }
        if (endptr)
            *endptr = (char *)p;
        return neg ? -NAN : NAN;
    }

    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        const char *end;
        v = parse_hex(p + 2, &end);
        if (end) {
            if (endptr)
                *endptr = (char *)end;
            return neg ? -v : v;
        }
        // "0x" without hex digits: just the leading "0"
    }

    // Keep up to 19 significant digits; the rest only move the exponent.
    for (; isdigit((unsigned char)*p); p++, any = 1) {
        if (digits < 19) {
            mant = mant * 10 + (*p - '0');
            if (mant)
                digits++;
        } else {
            exp10++;
        }
    }
    if (*p == '.') {
        for (p++; isdigit((unsigned char)*p); p++, any = 1) {
            if (digits < 19) {
                mant = mant * 10 + (*p - '0');
                if (mant)
                    digits++;
                exp10--;
            }
        }
    }
    if (!any) {
        if (endptr)
            *endptr = (char *)nptr;
        return 0.0;
    }
    if (*p == 'e' || *p == 'E') {
        const char *q = p + 1;
        int         eneg = 0, e = 0;
        if (*q == '+' || *q == '-')
            eneg = *q++ == '-';
        if (isdigit((unsigned char)*q)) {
            while (isdigit((unsigned char)*q) && e < 100000)
                e = e * 10 + (*q++ - '0');
            while (isdigit((unsigned char)*q))
                q++;
            exp10 += eneg ? -e : e;
            p = q;
        }
    }

    v = scale_pow10((double)mant, exp10);
    if (isinf(v) || (v == 0.0 && mant != 0))
        errno = ERANGE;
    if (endptr)
        *endptr = (char *)p;
    return neg ? -v : v;
}

float xbox_strtof(const char *nptr, char **endptr)
{
    return (float)xbox_strtod(nptr, endptr);
}

long double xbox_strtold(const char *nptr, char **endptr)
{
    return xbox_strtod(nptr, endptr);
}

double xbox_atof(const char *nptr)
{
    return xbox_strtod(nptr, NULL);
}

/*
==============================================================
SSCANF
==============================================================
*/

enum { LEN_NONE, LEN_HH, LEN_H, LEN_L, LEN_LL, LEN_BIG_L, LEN_SIZE };

static void store_int(void *dst, int len, unsigned long long v)
{
    switch (len) {
    case LEN_HH:   *(signed char *)dst = (signed char)v; break;
    case LEN_H:    *(short *)dst = (short)v; break;
    case LEN_L:    *(long *)dst = (long)v; break;
    case LEN_LL:   *(long long *)dst = (long long)v; break;
    case LEN_SIZE: *(size_t *)dst = (size_t)v; break;
    default:       *(int *)dst = (int)v; break;
    }
}

// Copies at most `width` characters (0 = unlimited) of the next token into buf.
static const char *copy_token(const char *s, int width, char *buf, size_t size)
{
    size_t n = 0, max = width > 0 && (size_t)width < size - 1 ? (size_t)width : size - 1;
    while (s[n] && !isspace((unsigned char)s[n]) && n < max) {
        buf[n] = s[n];
        n++;
    }
    buf[n] = 0;
    return s + n;
}

int xbox_vsscanf(const char *str, const char *fmt, va_list ap)
{
    const char *s = str;
    int         assigned = 0;

    while (*fmt) {
        if (isspace((unsigned char)*fmt)) {
            while (isspace((unsigned char)*s))
                s++;
            fmt++;
            continue;
        }
        if (*fmt != '%' || fmt[1] == '%') {
            if (*fmt == '%') {
                fmt++;
                while (isspace((unsigned char)*s))
                    s++;
            }
            if (*s != *fmt)
                goto input_end;
            s++;
            fmt++;
            continue;
        }

        fmt++;
        int suppress = 0, width = 0, len = LEN_NONE;
        if (*fmt == '*') {
            suppress = 1;
            fmt++;
        }
        while (isdigit((unsigned char)*fmt))
            width = width * 10 + (*fmt++ - '0');
        if (*fmt == 'h') {
            len = fmt[1] == 'h' ? (fmt++, LEN_HH) : LEN_H;
            fmt++;
        } else if (*fmt == 'l') {
            len = fmt[1] == 'l' ? (fmt++, LEN_LL) : LEN_L;
            fmt++;
        } else if (*fmt == 'L') {
            len = LEN_BIG_L;
            fmt++;
        } else if (*fmt == 'z' || *fmt == 'j' || *fmt == 't') {
            len = *fmt == 'j' ? LEN_LL : LEN_SIZE;
            fmt++;
        }

        char conv = *fmt++;
        switch (conv) {
        case 'n':
            if (!suppress)
                store_int(va_arg(ap, void *), len, (unsigned long long)(s - str));
            continue;

        case 'c': {
            int   w = width ? width : 1;
            char *out = suppress ? NULL : va_arg(ap, char *);
            if (!*s)
                goto input_end;
            for (int i = 0; i < w && *s; i++, s++) {
                if (out)
                    out[i] = *s;
            }
            if (!suppress)
                assigned++;
            break;
        }

        case 's': {
            char *out = suppress ? NULL : va_arg(ap, char *);
            while (isspace((unsigned char)*s))
                s++;
            if (!*s)
                goto input_end;
            int n = 0;
            while (*s && !isspace((unsigned char)*s) && (!width || n < width)) {
                if (out)
                    out[n] = *s;
                n++;
                s++;
            }
            if (out) {
                out[n] = 0;
                assigned++;
            }
            break;
        }

        case '[': {
            char set[256] = {0};
            int  invert = 0;
            if (*fmt == '^') {
                invert = 1;
                fmt++;
            }
            if (*fmt == ']')
                set[(unsigned char)*fmt++] = 1;
            while (*fmt && *fmt != ']') {
                if (fmt[1] == '-' && fmt[2] && fmt[2] != ']') {
                    for (int c = (unsigned char)fmt[0]; c <= (unsigned char)fmt[2]; c++)
                        set[c] = 1;
                    fmt += 3;
                } else {
                    set[(unsigned char)*fmt++] = 1;
                }
            }
            if (*fmt == ']')
                fmt++;
            char *out = suppress ? NULL : va_arg(ap, char *);
            int   n = 0;
            while (*s && (set[(unsigned char)*s] != 0) != invert && (!width || n < width)) {
                if (out)
                    out[n] = *s;
                n++;
                s++;
            }
            if (!n)
                goto match_fail;
            if (out) {
                out[n] = 0;
                assigned++;
            }
            break;
        }

        case 'd': case 'i': case 'u': case 'o': case 'x': case 'X': case 'p': {
            char  buf[64], *end;
            int   base = conv == 'd' || conv == 'u' ? 10 : conv == 'i' ? 0 : conv == 'o' ? 8 : 16;
            while (isspace((unsigned char)*s))
                s++;
            if (!*s)
                goto input_end;
            copy_token(s, width, buf, sizeof(buf));
            unsigned long long v = conv == 'd' || conv == 'i' ? (unsigned long long)strtoll(buf, &end, base)
                                                              : strtoull(buf, &end, base);
            if (end == buf)
                goto match_fail;
            s += end - buf;
            if (!suppress) {
                if (conv == 'p')
                    *va_arg(ap, void **) = (void *)(uintptr_t)v;
                else
                    store_int(va_arg(ap, void *), len, v);
                assigned++;
            }
            break;
        }

        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A': {
            char buf[128], *end;
            while (isspace((unsigned char)*s))
                s++;
            if (!*s)
                goto input_end;
            copy_token(s, width, buf, sizeof(buf));
            double v = xbox_strtod(buf, &end);
            if (end == buf)
                goto match_fail;
            s += end - buf;
            if (!suppress) {
                if (len == LEN_L)
                    *va_arg(ap, double *) = v;
                else if (len == LEN_BIG_L)
                    *va_arg(ap, long double *) = v;
                else
                    *va_arg(ap, float *) = (float)v;
                assigned++;
            }
            break;
        }

        default:
            goto match_fail;
        }
    }
    return assigned;

input_end:
    // Input ran out before the first conversion: EOF, as the standard says.
    return assigned ? assigned : EOF;
match_fail:
    return assigned;
}

int xbox_sscanf(const char *str, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = xbox_vsscanf(str, fmt, ap);
    va_end(ap);
    return n;
}

/*
==============================================================
FILE* PRINTF VARIANTS
==============================================================
*/

int vsnprintf_(char *s, size_t count, const char *format, va_list arg);

int xbox_vfprintf(FILE *stream, const char *fmt, va_list ap)
{
    char    local[1024];
    va_list copy;

    va_copy(copy, ap);
    int n = vsnprintf_(local, sizeof(local), fmt, ap);
    if (n < 0) {
        va_end(copy);
        return n;
    }
    if ((size_t)n < sizeof(local)) {
        va_end(copy);
        return (int)fwrite(local, 1, n, stream);
    }

    char *big = malloc(n + 1);
    if (!big) {
        va_end(copy);
        return -1;
    }
    vsnprintf_(big, n + 1, fmt, copy);
    va_end(copy);
    n = (int)fwrite(big, 1, n, stream);
    free(big);
    return n;
}

int xbox_fprintf(FILE *stream, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = xbox_vfprintf(stream, fmt, ap);
    va_end(ap);
    return n;
}

int xbox_vprintf(const char *fmt, va_list ap)
{
    return xbox_vfprintf(stdout, fmt, ap);
}

int xbox_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = xbox_vfprintf(stdout, fmt, ap);
    va_end(ap);
    return n;
}

// eyalroz/printf requires this hook for printf_(); it is not used.
void putchar_(char c)
{
    (void)c;
}
