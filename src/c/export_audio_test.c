#include <ovtest.h>

#include "aviutl.h"
#include "export_audio.h"

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

static void test_export_audio_invalid_params(void) {
  struct export_audio_params options = {
      .editp = NULL,
      .fp = &mock_fp,
  };
  error err = export_audio(&options);
  TEST_EISG_F(err, err_invalid_arugment);
}

struct ctx {
  FILE_INFO fi;
  int frames;
  int processed;
  bool aborted;
};

static bool on_read(void *const userdata, void *const p, size_t const samples, int const progress) {
  struct ctx *ctx = userdata;
  int16_t *buf = p;
  ++ctx->processed;
  TEST_CHECK(samples == (size_t)(ctx->fi.audio_rate * ctx->fi.video_scale / ctx->fi.video_rate));
  TEST_CHECK(buf[0] == 123 && buf[1] == -123);
  TEST_CHECK(buf[samples * (size_t)ctx->fi.audio_ch - 2] == 234 && buf[samples * (size_t)ctx->fi.audio_ch - 1] == -234);
  int const golden_progress = (ctx->processed * 10000) / ctx->frames;
  TEST_CHECK(golden_progress == progress);
  TEST_MSG("wanted: %d, got: %d", golden_progress, progress);
  if (ctx->aborted) {
    return false;
  }
  return true;
}

static void on_finish(struct ctx *ctx, error err) {
  if (ctx->aborted) {
    TEST_EISG_F(err, err_abort);
    TEST_CHECK(ctx->processed == ctx->frames - 1);
  } else {
    TEST_SUCCEEDED_F(err);
    TEST_CHECK(ctx->processed == ctx->frames);
  }
}

static void ctx_init(struct ctx *ctx, struct mock_edit const *const e) {
  *ctx = (struct ctx){
      .fi = e->fi,
      .frames = e->start_frame == -1 || e->end_frame == -1 ? e->fi.frame_n : e->end_frame - e->start_frame + 1,
  };
}

static void test_export_audio_all_frame(void) {
  struct ctx ctx = {0};
  ctx_init(&ctx, &state1);
  error err = export_audio(&(struct export_audio_params){
      .editp = &state1,
      .fp = &mock_fp,
      .userdata = &ctx,
      .on_read = on_read,
  });
  on_finish(&ctx, err);
}

static void test_export_audio_range(void) {
  struct ctx ctx = {0};
  ctx_init(&ctx, &state2);
  error err = export_audio(&(struct export_audio_params){
      .editp = &state2,
      .fp = &mock_fp,
      .userdata = &ctx,
      .on_read = on_read,
  });
  on_finish(&ctx, err);
}

static void test_export_audio_abort(void) {
  struct ctx ctx = {0};
  ctx_init(&ctx, &state2);
  ctx.aborted = true;
  error err = export_audio(&(struct export_audio_params){
      .editp = &state2,
      .fp = &mock_fp,
      .userdata = &ctx,
      .on_read = on_read,
  });
  on_finish(&ctx, err);
}

TEST_LIST = {
    {"test_export_audio_invalid_params", test_export_audio_invalid_params},
    {"test_export_audio_all_frame", test_export_audio_all_frame},
    {"test_export_audio_range", test_export_audio_range},
    {"test_export_audio_abort", test_export_audio_abort},
    {NULL, NULL},
};
