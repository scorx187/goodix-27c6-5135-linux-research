/*
 * SIGFM parser/resource hardening tests.
 *
 * No biometric input.
 * No hardware.
 * No exact match scores printed.
 */

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

#include "../libfprint/sigfm/binary.hpp"
#include "../libfprint/sigfm/img-info.hpp"
#include "../libfprint/sigfm/sigfm.hpp"


static SigfmImgInfo
make_valid_info (std::size_t count)
{
  SigfmImgInfo info;

  info.keypoints.reserve (
    count);

  for (std::size_t i = 0;
       i < count;
       i++)
    {
      info.keypoints.emplace_back (
        cv::Point2f (
          static_cast<float> (i + 1),
          static_cast<float> (i + 2)),
        1.5f,
        45.0f,
        0.5f,
        0,
        static_cast<int> (i));
    }

  info.descriptors =
    cv::Mat::zeros (
      static_cast<int> (count),
      128,
      CV_32FC1);

  for (int row = 0;
       row < info.descriptors.rows;
       row++)
    {
      float *ptr =
        info.descriptors.ptr<float> (
          row);

      for (int col = 0;
           col < info.descriptors.cols;
           col++)
        ptr[col] =
          static_cast<float> (
            (row + 1) * (col + 3)) /
          1000.0f;
    }

  return info;
}


static int
test_stream_bounds ()
{
  const std::array<unsigned char, 2> source = {{
    0x11,
    0x22
  }};

  std::array<unsigned char, 3> destination = {{}};

  try
    {
      bin::stream stream (
        source.begin (),
        source.end ());

      stream.read (
        destination.begin (),
        destination.size ());

      return 1;
    }
  catch (const std::runtime_error&)
    {
    }

  return 0;
}


static int
test_null_api ()
{
  int len = 123;

  if (sigfm_copy_info (nullptr) != nullptr)
    return 10;

  if (sigfm_keypoints_count (nullptr) != 0)
    return 11;

  if (sigfm_serialize_binary (
        nullptr,
        &len) != nullptr)
    return 12;

  if (len != 0)
    return 13;

  if (sigfm_deserialize_binary (
        nullptr,
        1) != nullptr)
    return 14;

  if (sigfm_match_score (
        nullptr,
        nullptr) >= 0)
    return 15;

  return 0;
}


static int
test_valid_roundtrip ()
{
  SigfmImgInfo info =
    make_valid_info (6);

  int len = 0;

  unsigned char *blob =
    sigfm_serialize_binary (
      &info,
      &len);

  if (blob == nullptr ||
      len <= 0)
    return 20;

  SigfmImgInfo *restored =
    sigfm_deserialize_binary (
      blob,
      len);

  if (restored == nullptr)
    {
      std::free (blob);
      return 21;
    }

  if (sigfm_keypoints_count (
        restored) != 6)
    {
      sigfm_free_info (
        restored);

      std::free (blob);

      return 22;
    }

  SigfmImgInfo *copy =
    sigfm_copy_info (
      restored);

  if (copy == nullptr)
    {
      sigfm_free_info (
        restored);

      std::free (blob);

      return 23;
    }

  const int score =
    sigfm_match_score (
      restored,
      copy);

  if (score < 0)
    {
      sigfm_free_info (
        copy);

      sigfm_free_info (
        restored);

      std::free (blob);

      return 24;
    }

  sigfm_free_info (
    copy);

  sigfm_free_info (
    restored);

  std::free (blob);

  return 0;
}


static int
test_truncation_and_trailing ()
{
  SigfmImgInfo info =
    make_valid_info (6);

  int len = 0;

  unsigned char *raw =
    sigfm_serialize_binary (
      &info,
      &len);

  if (raw == nullptr ||
      len <= 1)
    return 30;

  /*
   * Every strict prefix must fail safely.
   */
  for (int cut = 1;
       cut < len;
       cut++)
    {
      SigfmImgInfo *candidate =
        sigfm_deserialize_binary (
          raw,
          cut);

      if (candidate != nullptr)
        {
          sigfm_free_info (
            candidate);

          std::free (raw);

          return 31;
        }
    }

  std::vector<unsigned char> trailing (
    raw,
    raw + len);

  trailing.push_back (
    0xa5);

  if (sigfm_deserialize_binary (
        trailing.data (),
        static_cast<int> (
          trailing.size ())) != nullptr)
    {
      std::free (raw);
      return 32;
    }

  if (sigfm_deserialize_binary (
        raw,
        0) != nullptr)
    {
      std::free (raw);
      return 33;
    }

  if (sigfm_deserialize_binary (
        raw,
        -1) != nullptr)
    {
      std::free (raw);
      return 34;
    }

  /*
   * Must reject before reading past the actual buffer.
   */
  if (sigfm_deserialize_binary (
        raw,
        static_cast<int> (
          bin::max_serialized_bytes + 1U))
      != nullptr)
    {
      std::free (raw);
      return 35;
    }

  std::free (raw);

  return 0;
}


static int
test_resource_bombs ()
{
  constexpr std::size_t count = 6;

  SigfmImgInfo info =
    make_valid_info (count);

  int len = 0;

  unsigned char *raw =
    sigfm_serialize_binary (
      &info,
      &len);

  if (raw == nullptr ||
      len <= 0)
    return 40;

  std::vector<unsigned char> blob (
    raw,
    raw + len);

  std::free (raw);

  /*
   * First field is vector<size_t> count.
   * Simulate attacker requesting enormous reserve().
   */
  {
    auto mutated = blob;

    const std::size_t huge =
      std::numeric_limits<std::size_t>::max ();

    std::memcpy (
      mutated.data (),
      &huge,
      sizeof (huge));

    if (sigfm_deserialize_binary (
          mutated.data (),
          static_cast<int> (
            mutated.size ())) != nullptr)
      return 41;
  }

  /*
   * Serialized KeyPoint:
   * int class_id
   * float angle
   * int octave
   * float response
   * float size
   * float x
   * float y
   */
  constexpr std::size_t keypoint_bytes =
    sizeof (int) * 2U +
    sizeof (float) * 5U;

  const std::size_t mat_offset =
    sizeof (std::size_t) +
    count * keypoint_bytes;

  if (mat_offset +
      3U * sizeof (int) >
      blob.size ())
    return 42;

  /*
   * Matrix header is:
   * type, rows, cols
   */
  {
    auto mutated = blob;

    const int huge_rows =
      std::numeric_limits<int>::max ();

    std::memcpy (
      mutated.data () +
        mat_offset +
        sizeof (int),
      &huge_rows,
      sizeof (huge_rows));

    if (sigfm_deserialize_binary (
          mutated.data (),
          static_cast<int> (
            mutated.size ())) != nullptr)
      return 43;
  }

  {
    auto mutated = blob;

    const int bad_type =
      CV_8UC1;

    std::memcpy (
      mutated.data () +
        mat_offset,
      &bad_type,
      sizeof (bad_type));

    if (sigfm_deserialize_binary (
          mutated.data (),
          static_cast<int> (
            mutated.size ())) != nullptr)
      return 44;
  }

  {
    auto mutated = blob;

    const int bad_cols =
      64;

    std::memcpy (
      mutated.data () +
        mat_offset +
        2U * sizeof (int),
      &bad_cols,
      sizeof (bad_cols));

    if (sigfm_deserialize_binary (
          mutated.data (),
          static_cast<int> (
            mutated.size ())) != nullptr)
      return 45;
  }

  return 0;
}


static int
test_invalid_logical_objects ()
{
  int len = 99;

  {
    SigfmImgInfo bad =
      make_valid_info (6);

    bad.descriptors =
      cv::Mat::zeros (
        6,
        64,
        CV_32FC1);

    if (sigfm_serialize_binary (
          &bad,
          &len) != nullptr ||
        len != 0)
      return 50;
  }

  {
    SigfmImgInfo bad =
      make_valid_info (6);

    bad.descriptors =
      cv::Mat::zeros (
        5,
        128,
        CV_32FC1);

    if (sigfm_serialize_binary (
          &bad,
          &len) != nullptr ||
        len != 0)
      return 51;
  }

  {
    SigfmImgInfo bad =
      make_valid_info (6);

    bad.keypoints[0].pt.x =
      std::numeric_limits<float>::quiet_NaN ();

    if (sigfm_serialize_binary (
          &bad,
          &len) != nullptr ||
        len != 0)
      return 52;
  }

  {
    SigfmImgInfo bad =
      make_valid_info (6);

    bad.descriptors.at<float> (
      0,
      0) =
        std::numeric_limits<float>::quiet_NaN ();

    if (sigfm_serialize_binary (
          &bad,
          &len) != nullptr ||
        len != 0)
      return 53;
  }

  {
    SigfmImgInfo bad;

    bad.keypoints.resize (
      bin::max_container_elements + 1U);

    if (sigfm_serialize_binary (
          &bad,
          &len) != nullptr ||
        len != 0)
      return 54;
  }

  return 0;
}


static int
test_deterministic_mutation_sweep ()
{
  SigfmImgInfo info =
    make_valid_info (6);

  int len = 0;

  unsigned char *raw =
    sigfm_serialize_binary (
      &info,
      &len);

  if (raw == nullptr ||
      len <= 0)
    return 60;

  std::vector<unsigned char> original (
    raw,
    raw + len);

  std::free (raw);

  for (std::size_t i = 0;
       i < original.size ();
       i++)
    {
      auto mutated =
        original;

      mutated[i] ^=
        0xffU;

      SigfmImgInfo *candidate =
        sigfm_deserialize_binary (
          mutated.data (),
          static_cast<int> (
            mutated.size ()));

      if (candidate != nullptr)
        sigfm_free_info (
          candidate);
    }

  return 0;
}


int
main ()
{
  int rc;

  rc = test_stream_bounds ();
  if (rc != 0)
    return rc;

  rc = test_null_api ();
  if (rc != 0)
    return rc;

  rc = test_valid_roundtrip ();
  if (rc != 0)
    return rc;

  rc = test_truncation_and_trailing ();
  if (rc != 0)
    return rc;

  rc = test_resource_bombs ();
  if (rc != 0)
    return rc;

  rc = test_invalid_logical_objects ();
  if (rc != 0)
    return rc;

  rc = test_deterministic_mutation_sweep ();
  if (rc != 0)
    return rc;

  std::puts (
    "SIGFM_ROBUSTNESS=PASS");

  return 0;
}
