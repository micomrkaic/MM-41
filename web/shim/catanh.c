/* catanh for the browser build.
 *
 * Emscripten's C library (musl) computes catanh(z) as -i catan(iz), which
 * for a real argument beyond 1 lands on the wrong side of the branch cut:
 * catanh(2 + 0i) comes out as 0.549 - 1.571i where C99 (Annex G) and glibc
 * give 0.549 + 1.571i. `2 atanh` should not depend on where the calculator
 * runs, so the real axis is done here by its formula, with the sign of the
 * imaginary part taken from the (signed) zero as the standard says; the
 * rest of the plane uses the definition, (log(1+z) - log(1-z)) / 2.
 */
#include <complex.h>
#include <math.h>

double complex catanh(double complex z) {
  double x = creal(z), y = cimag(z);
  if (y == 0.0 && fabs(x) > 1.0)
    return CMPLX(0.5 * log(fabs((x + 1.0) / (x - 1.0))), copysign(1.57079632679489661923, y));
  if (y == 0.0 && fabs(x) < 1.0)
    return CMPLX(atanh(x), y);
  return 0.5 * (clog(1.0 + z) - clog(1.0 - z));
}
