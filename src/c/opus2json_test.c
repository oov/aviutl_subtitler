#include <ovtest.h>

#include <ovarray.h>

#include "opus2json.c"

static void test_parse_time_code(void) {
  char buf[13];
  char const zero9[] = "00:00.000";
  for (size_t i = 0; i < sizeof(zero9) - 1; i++) {
    strcpy(buf, zero9);
    buf[i] = 'a';
    TEST_CHECK(parse_time_code(buf) == -1);
  }
  char const zero12[] = "00:00:00.000";
  for (size_t i = 0; i < sizeof(zero12) - 1; i++) {
    strcpy(buf, zero12);
    buf[i] = 'a';
    TEST_CHECK(parse_time_code(buf) == -1);
  }
  TEST_CHECK(parse_time_code("00:00.000") == 0);
  TEST_CHECK(parse_time_code("00:00.001") == 1);
  TEST_CHECK(parse_time_code("00:00.010") == 10);
  TEST_CHECK(parse_time_code("00:00.100") == 100);
  TEST_CHECK(parse_time_code("00:01.000") == 1000);
  TEST_CHECK(parse_time_code("00:10.000") == 10000);
  TEST_CHECK(parse_time_code("01:00.000") == 60000);
  TEST_CHECK(parse_time_code("10:00.000") == 600000);
  TEST_CHECK(parse_time_code("00:00:00.000") == 0);
  TEST_CHECK(parse_time_code("00:00:00.001") == 1);
  TEST_CHECK(parse_time_code("00:00:00.010") == 10);
  TEST_CHECK(parse_time_code("00:00:00.100") == 100);
  TEST_CHECK(parse_time_code("00:00:01.000") == 1000);
  TEST_CHECK(parse_time_code("00:00:10.000") == 10000);
  TEST_CHECK(parse_time_code("00:01:00.000") == 60000);
  TEST_CHECK(parse_time_code("00:10:00.000") == 600000);
  TEST_CHECK(parse_time_code("01:00:00.000") == 3600000);
  TEST_CHECK(parse_time_code("10:00:00.000") == 36000000);
}

TEST_LIST = {
    {"test_parse_time_code", test_parse_time_code},
    {NULL, NULL},
};
