#pragma once

#include <ovbase.h>

struct export_audio;

struct export_audio_options {
  void *editp;
  void *fp;
  void *userdata;

  void (*on_read)(void *const userdata, void *const p, size_t const samples, int const progress);
  void (*on_finish)(void *const userdata, error err);
};

NODISCARD error export_audio_create(struct export_audio **const eap, struct export_audio_options const *const options);
void export_audio_destroy(struct export_audio **const eap);
void export_audio_abort(struct export_audio *const ea);
