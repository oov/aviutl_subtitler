#include <ovtest.h>

#include <ovarray.h>

#include "path.h"

static void test_path_get_temp_file(void) {
  wchar_t *path = NULL;
  error err = path_get_temp_file(NULL, L"test");
  TEST_EISG_F(err, err_invalid_arugment);
  err = path_get_temp_file(&path, L"");
  if (TEST_SUCCEEDED_F(err)) {
    TEST_CHECK(path != NULL);
    wchar_t golden[MAX_PATH * 2];
    GetTempPathW(MAX_PATH + 1, golden);
    TEST_CHECK(wcscmp(path, golden) == 0);
    OV_ARRAY_DESTROY(&path);
  }
  err = path_get_temp_file(&path, L"hello.txt");
  if (TEST_SUCCEEDED_F(err)) {
    TEST_CHECK(path != NULL);
    wchar_t golden[MAX_PATH * 2];
    GetTempPathW(MAX_PATH + 1, golden);
    wcscat(golden, L"hello.txt");
    TEST_CHECK(wcscmp(path, golden) == 0);
    OV_ARRAY_DESTROY(&path);
  }
}

static void test_path_extract_file_name(void) {
  wchar_t const path1[] = L"C:\\Users\\test\\hello_world.txt";
  TEST_CHECK(path_extract_file_name(path1) == wcsrchr(path1, L'\\') + 1);
}

TEST_LIST = {
    {"test_path_get_temp_file", test_path_get_temp_file},
    {"test_path_extract_file_name", test_path_extract_file_name},
    {NULL, NULL},
};
