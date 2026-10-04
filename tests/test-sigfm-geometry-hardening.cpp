/*
 * SIGFM geometry hardening host test.
 *
 * Synthetic/internal math only.
 * No fingerprint image, sensor, USB, PSK or FDT.
 * No matcher scores are printed.
 */

#include <cmath>
#include <cstdio>
#include <limits>

#include "../libfprint/sigfm/sigfm.cpp"


static int
test_comparator ()
{
  const match left (
    cv::Point2i (10, 20),
    cv::Point2i (1, 1));

  const match right (
    cv::Point2i (11, 20),
    cv::Point2i (2, 2));

  const match lower_y (
    cv::Point2i (99, 19),
    cv::Point2i (3, 3));

  if (!(left < right))
    return 1;

  if (right < left)
    return 2;

  if (!(lower_y < left))
    return 3;

  return 0;
}


static int
test_relative_difference ()
{
  if (relative_difference (0.0, 0.0) != 0.0)
    return 10;

  if (relative_difference (1.0, 1.0) != 0.0)
    return 11;

  const double mismatch =
    relative_difference (1.0, 2.0);

  if (!std::isfinite (mismatch) ||
      mismatch <= 0.0)
    return 12;

  if (!std::isinf (
        relative_difference (
          std::numeric_limits<double>::infinity (),
          1.0)))
    return 13;

  return 0;
}


static int
test_clamp ()
{
  if (clamp_unit (1.5) != 1.0)
    return 20;

  if (clamp_unit (-1.5) != -1.0)
    return 21;

  if (clamp_unit (0.25) != 0.25)
    return 22;

  return 0;
}


static int
test_geometry ()
{
  double first = 0.0;
  double second = 0.0;

  const int zero[2] = {
    0,
    0
  };

  const int unit_x[2] = {
    1,
    0
  };

  const int unit_y[2] = {
    0,
    1
  };

  const int double_x[2] = {
    2,
    0
  };

  if (geometry_angles (
        zero,
        zero,
        &first,
        &second))
    return 30;

  if (geometry_angles (
        zero,
        unit_x,
        &first,
        &second))
    return 31;

  if (!geometry_angles (
        unit_x,
        unit_x,
        &first,
        &second))
    return 32;

  if (!std::isfinite (first) ||
      !std::isfinite (second))
    return 33;

  if (!geometry_angles (
        unit_x,
        unit_y,
        &first,
        &second))
    return 34;

  if (!std::isfinite (first) ||
      !std::isfinite (second))
    return 35;

  /*
   * 1 vs 2 length mismatch exceeds the existing
   * SIGFM length tolerance and must be rejected.
   */
  if (geometry_angles (
        unit_x,
        double_x,
        &first,
        &second))
    return 36;

  if (geometry_angles (
        unit_x,
        unit_x,
        nullptr,
        &second))
    return 37;

  return 0;
}


int
main ()
{
  int result;

  result = test_comparator ();

  if (result != 0)
    return result;

  result = test_relative_difference ();

  if (result != 0)
    return result;

  result = test_clamp ();

  if (result != 0)
    return result;

  result = test_geometry ();

  if (result != 0)
    return result;

  std::puts (
    "SIGFM_GEOMETRY_HARDENING=PASS");

  return 0;
}
