// Host-side test for src/xbox_stdio.c and third_party/printf, against glibc.
// Run: tests/run-stdio-test.sh
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

int _strnicmp(const char *a, const char *b, size_t n) { return strncasecmp(a, b, n); }

double xbox_strtod(const char *nptr, char **endptr);
int xbox_sscanf(const char *str, const char *fmt, ...);
int snprintf_(char *s, size_t count, const char *format, ...);

static int failures;

static void check_strtod(const char *s)
{
    char *e1, *e2;
    double a = xbox_strtod(s, &e1), b = strtod(s, &e2);
    int same = (isnan(a) && isnan(b)) || a == b || fabs(a - b) <= fabs(b) * 4e-16;
    if (!same || e1 - s != e2 - s) {
        printf("strtod(\"%s\"): got %.17g (+%d), want %.17g (+%d)\n", s, a, (int)(e1 - s), b, (int)(e2 - s));
        failures++;
    }
}

static void check_printf(const char *fmt, double v)
{
    char a[128], b[128];
    snprintf_(a, sizeof(a), fmt, v);
    snprintf(b, sizeof(b), fmt, v);
    if (strcmp(a, b)) {
        printf("printf(\"%s\", %.17g): got \"%s\", want \"%s\"\n", fmt, v, a, b);
        failures++;
    }
}

int main(void)
{
    const char *nums[] = {"0", "1", "-1", "3.14159", "  42.5abc", "1e10", "1.5E-7", "-0.0", ".5", "5.",
                          "1e308", "1e-300", "123456789012345678901234567890", "0x1A", "0x1.8p1", "inf",
                          "-Infinity", "nan", "abc", "+", "1e", "1e+", "0.1", "0.2", "0.30000000000000004",
                          "2.2250738585072014e-308", "4.9e-324", "1.7976931348623157e308", "640.000000"};
    for (size_t i = 0; i < sizeof(nums) / sizeof(nums[0]); i++)
        check_strtod(nums[i]);

    const char *fmts[] = {"%f", "%.2f", "%g", "%e", "%10.3f", "%-8.1f|", "%.0f", "%G", "%.15g"};
    // Known eyalroz/printf limits (accepted, the game never prints such values):
    //   %f of values >= 1e18 falls back to exponential notation, and %.15g of
    //   large values carries a ~1e-13 relative error.
    const double vals[] = {0, 1, -1.5, 3.14159265358979, 1e-5, 123456.789, 1e9, 0.1, 640, 0.5, 2.5};
    for (size_t i = 0; i < sizeof(fmts) / sizeof(fmts[0]); i++)
        for (size_t j = 0; j < sizeof(vals) / sizeof(vals[0]); j++)
            check_printf(fmts[i], vals[j]);

    // sscanf: the patterns OpenMoHAA uses (vectors, mixed ints/strings, scansets)
    float x = 0, y = 0, z = 0;
    int n = xbox_sscanf("( 1.5 -2 3e1 )", "( %f %f %f )", &x, &y, &z);
    if (n != 3 || x != 1.5f || y != -2.0f || z != 30.0f) { printf("sscanf vec: %d %g %g %g\n", n, x, y, z); failures++; }

    int a = 0, b = 0; char s[32] = "", rest[32] = "";
    n = xbox_sscanf("map m1l1 12 0x1f tail", "map %31s %d %i %[^\n]", s, &a, &b, rest);
    if (n != 4 || strcmp(s, "m1l1") || a != 12 || b != 31 || strcmp(rest, "tail")) {
        printf("sscanf mix: %d '%s' %d %d '%s'\n", n, s, a, b, rest); failures++;
    }
    double d = 0; int pos = 0;
    n = xbox_sscanf("  7.25xyz", "%lf%n", &d, &pos);
    if (n != 1 || d != 7.25 || pos != 6) { printf("sscanf lf/n: %d %g %d\n", n, d, pos); failures++; }
    n = xbox_sscanf("", "%d", &a);
    if (n != EOF) { printf("sscanf empty: %d\n", n); failures++; }
    n = xbox_sscanf("abc", "%d", &a);
    if (n != 0) { printf("sscanf nomatch: %d\n", n); failures++; }

    printf(failures ? "FAILED: %d\n" : "all tests passed\n", failures);
    return failures != 0;
}
