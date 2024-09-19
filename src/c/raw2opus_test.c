#include <ovtest.h>

#include <ovarray.h>

#include "aviutl.h"
#include "path.h"
#include "raw2opus.c"

struct mock_edit {
  int start_frame;
  int end_frame;
  FILE_INFO fi;
};

static struct mock_edit state1 = {
    .start_frame = -1,
    .end_frame = -1,
    .fi =
        {
            .audio_rate = 48000,
            .video_scale = 1,
            .video_rate = 60,
            .audio_ch = 2,
            .frame_n = 2,
        },
};

static struct mock_edit state2 = {
    .start_frame = 0,
    .end_frame = 1,
    .fi =
        {
            .audio_rate = 48000,
            .video_scale = 1,
            .video_rate = 30,
            .audio_ch = 2,
            .frame_n = 4,
        },
};

static BOOL mock_get_select_frame(void *editp, int *s, int *e) {
  struct mock_edit *ep = editp;
  if (ep->start_frame == -1 || ep->end_frame == -1) {
    return FALSE;
  }
  *s = ep->start_frame;
  *e = ep->end_frame;
  return TRUE;
}

static int mock_get_frame_n(void *editp) {
  struct mock_edit *ep = editp;
  return ep->fi.frame_n;
}

static BOOL mock_get_file_info(void *editp, FILE_INFO *fip) {
  struct mock_edit *ep = editp;
  *fip = ep->fi;
  return TRUE;
}

static int mock_get_audio_filtered(void *editp, int n, void *buf) {
  (void)n;
  struct mock_edit *ep = editp;
  int samples = ep->fi.audio_rate * ep->fi.video_scale / ep->fi.video_rate;
  memset(buf, 0, (size_t)(samples * ep->fi.audio_ch) * sizeof(int16_t));
  int16_t *p = buf;
  p[0] = 123;
  p[1] = -123;
  p[samples * ep->fi.audio_ch - 2] = 234;
  p[samples * ep->fi.audio_ch - 1] = -234;
  return samples;
}

static FILTER mock_fp = {
    .exfunc =
        &(EXFUNC){
            .get_select_frame = mock_get_select_frame,
            .get_frame_n = mock_get_frame_n,
            .get_file_info = mock_get_file_info,
            .get_audio_filtered = mock_get_audio_filtered,
        },
};

static void test_raw2opus_invalid_params(void) {
  struct raw2opus_params params = {
      .editp = NULL,
      .fp = &mock_fp,
  };
  struct raw2opus_info info;
  error err = raw2opus(&params, &info);
  TEST_EISG_F(err, err_invalid_arugment);
}

struct ctx {
  FILE_INFO fi;
  wchar_t *opus_path;
  struct raw2opus_info info;
  int frames;
  int processed;
  int prev_progress;
  bool aborted;
};

static bool on_progress(void *const userdata, int const progress) {
  struct ctx *ctx = userdata;
  TEST_CHECK(ctx->prev_progress <= progress);
  ctx->prev_progress = progress;
  ++ctx->processed;
  if (ctx->aborted) {
    return false;
  }
  return true;
}

static void on_finish(struct ctx *ctx, error err) {
  if (ctx->aborted) {
    TEST_CHECK(ctx->processed == 1);
    TEST_EISG_F(err, err_abort);
    TEST_CHECK(GetFileAttributesW(ctx->opus_path) == INVALID_FILE_ATTRIBUTES);
  } else {
    TEST_CHECK(ctx->processed == ctx->frames);
    TEST_SUCCEEDED_F(err);
    TEST_CHECK(GetFileAttributesW(ctx->opus_path) != INVALID_FILE_ATTRIBUTES);
  }
  DeleteFileW(ctx->opus_path);
  OV_ARRAY_DESTROY(&ctx->opus_path);
}

static void ctx_init(struct ctx *ctx, struct mock_edit const *const e) {
  *ctx = (struct ctx){
      .fi = e->fi,
      .frames = e->start_frame == -1 || e->end_frame == -1 ? e->fi.frame_n : e->end_frame - e->start_frame + 1,
  };
  error err = path_get_temp_file(&ctx->opus_path, L"raw2opus_test.opus");
  TEST_SUCCEEDED_F(err);
}

static void test_raw2opus_all_frame(void) {
  struct ctx ctx = {0};
  ctx_init(&ctx, &state1);
  on_finish(&ctx,
            raw2opus(
                &(struct raw2opus_params){
                    .editp = &state1,
                    .fp = &mock_fp,
                    .opus_path = ctx.opus_path,
                    .userdata = &ctx,
                    .on_progress = on_progress,
                },
                &ctx.info));
}

static void test_raw2opus_range(void) {
  struct ctx ctx = {0};
  ctx_init(&ctx, &state2);
  on_finish(&ctx,
            raw2opus(
                &(struct raw2opus_params){
                    .editp = &state2,
                    .fp = &mock_fp,
                    .opus_path = ctx.opus_path,
                    .userdata = &ctx,
                    .on_progress = on_progress,
                },
                &ctx.info));
}

static void test_raw2opus_abort(void) {
  struct ctx ctx = {0};
  ctx_init(&ctx, &state2);
  ctx.aborted = true;
  on_finish(&ctx,
            raw2opus(
                &(struct raw2opus_params){
                    .editp = &state2,
                    .fp = &mock_fp,
                    .opus_path = ctx.opus_path,
                    .userdata = &ctx,
                    .on_progress = on_progress,
                },
                &ctx.info));
}

static void test_bytes_to_human_readable(void) {
  static struct test_data {
    uint64_t bytes;
    char const *golden;
    size_t suffix;
  } const tests[] = {
      {0, "0", 0},
      {1, "1", 0},
      {10, "10", 0},
      {100, "100", 0},
      {1000, "0.98", 1},
      {1024, "1.00", 1},
      {1029, "1.00", 1},
      {1030, "1.01", 1},
      {1034, "1.01", 1},
      {10240, "10.0", 1},
      {10291, "10.0", 1},
      {10292, "10.1", 1},
      {10342, "10.1", 1},
      {102400, "100", 1},
      {102911, "100", 1},
      {102912, "101", 1},
      {103424, "101", 1},
  };
  char buf[8];
  for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
    TEST_CASE_("#%zu", i);
    size_t const suffix = bytes_to_human_readable(buf, tests[i].bytes, '.');
    TEST_CHECK(strcmp(buf, tests[i].golden) == 0);
    TEST_MSG("want: %s, got: %s", tests[i].golden, buf);
    TEST_CHECK(suffix == tests[i].suffix);
    TEST_MSG("want: %zu, got: %zu", tests[i].suffix, suffix);
  }
}

TEST_LIST = {
    {"test_raw2opus_invalid_params", test_raw2opus_invalid_params},
    {"test_raw2opus_all_frame", test_raw2opus_all_frame},
    {"test_raw2opus_range", test_raw2opus_range},
    {"test_raw2opus_abort", test_raw2opus_abort},
    {"test_bytes_to_human_readable", test_bytes_to_human_readable},
    {NULL, NULL},
};
