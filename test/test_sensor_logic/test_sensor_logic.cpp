#include <unity.h>

#include "sensor_logic.h"

void setUp(void) {}
void tearDown(void) {}

void test_parse_valid_line(void)
{
  int id = 0, distance = 0;
  TEST_ASSERT_TRUE(parseSensorLine("2,455", id, distance));
  TEST_ASSERT_EQUAL(2, id);
  TEST_ASSERT_EQUAL(455, distance);
}

void test_parse_rejects_missing_comma(void)
{
  int id = 0, distance = 0;
  TEST_ASSERT_FALSE(parseSensorLine("1230", id, distance));
}

void test_parse_rejects_leading_comma(void)
{
  int id = 0, distance = 0;
  TEST_ASSERT_FALSE(parseSensorLine(",450", id, distance));
}

void test_parse_rejects_empty_line(void)
{
  int id = 0, distance = 0;
  TEST_ASSERT_FALSE(parseSensorLine("", id, distance));
}

void test_parse_rejects_empty_distance_field(void)
{
  // 4.5: "1," has a comma but no digits after it — strtol() alone would silently
  // return 0 here; must be rejected instead of accepted as distance=0.
  int id = 0, distance = 0;
  TEST_ASSERT_FALSE(parseSensorLine("1,", id, distance));
}

void test_parse_rejects_non_numeric_distance_err(void)
{
  int id = 0, distance = 0;
  TEST_ASSERT_FALSE(parseSensorLine("1,ERR", id, distance));
}

void test_parse_rejects_non_numeric_distance_nan(void)
{
  int id = 0, distance = 0;
  TEST_ASSERT_FALSE(parseSensorLine("1,NaN", id, distance));
}

void test_parse_rejects_non_numeric_distance_with_trailing_digits(void)
{
  // The distance field itself must start with digits; a later comma-separated
  // digit group ("250") does not rescue a garbage distance field ("OK").
  int id = 0, distance = 0;
  TEST_ASSERT_FALSE(parseSensorLine("1,OK,250", id, distance));
}

void test_range_inclusive_bounds(void)
{
  TEST_ASSERT_TRUE(isDistanceInRange(200, 200, 800));
  TEST_ASSERT_TRUE(isDistanceInRange(800, 200, 800));
  TEST_ASSERT_FALSE(isDistanceInRange(199, 200, 800));
  TEST_ASSERT_FALSE(isDistanceInRange(801, 200, 800));
}

void test_valid_device_id(void)
{
  TEST_ASSERT_TRUE(isValidDeviceId(1, 3));
  TEST_ASSERT_TRUE(isValidDeviceId(3, 3));
  TEST_ASSERT_FALSE(isValidDeviceId(0, 3));
  TEST_ASSERT_FALSE(isValidDeviceId(4, 3));
}

int main(int argc, char** argv)
{
  UNITY_BEGIN();
  RUN_TEST(test_parse_valid_line);
  RUN_TEST(test_parse_rejects_missing_comma);
  RUN_TEST(test_parse_rejects_leading_comma);
  RUN_TEST(test_parse_rejects_empty_line);
  RUN_TEST(test_parse_rejects_empty_distance_field);
  RUN_TEST(test_parse_rejects_non_numeric_distance_err);
  RUN_TEST(test_parse_rejects_non_numeric_distance_nan);
  RUN_TEST(test_parse_rejects_non_numeric_distance_with_trailing_digits);
  RUN_TEST(test_range_inclusive_bounds);
  RUN_TEST(test_valid_device_id);
  return UNITY_END();
}
