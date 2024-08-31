#pragma once

#include <ovbase.h>
#include <ovutil/win32.h>

struct process;

struct process_options {
  wchar_t const *cmdline;
  wchar_t const *envvar_name;
  wchar_t const *envvar_value;
  void *userdata;
  void (*on_receive_stdout)(void *userdata, void const *const ptr, size_t const len);
  void (*on_receive_stderr)(void *userdata, void const *const ptr, size_t const len);
  void (*on_close_stdout)(void *userdata, error err);
  void (*on_close_stderr)(void *userdata, error err);
};

NODISCARD error process_create(struct process **prp, struct process_options const *const options);
void process_destroy(struct process **const prp);
void process_close_stdin(struct process *const pr);
void process_close_stdout(struct process *const pr);
void process_close_stderr(struct process *const pr);
NODISCARD error process_write(struct process *const pr, void const *const buf, size_t const len);
bool process_isrunning(struct process const *const pr);
void process_abort(struct process *const pr);
NODISCARD error process_send_ctrl_break(struct process *const pr);

struct process_line_buffer_context {
  char buf[1024];
  size_t written;
  void *userdata;
  void (*on_line)(void *const userdata, char const *const message);
};

void process_line_buffer(struct process_line_buffer_context *ctx, void const *const ptr, size_t const len);
