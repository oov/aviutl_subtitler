#include "process.h"

#include <ovarray.h>
#include <ovprintf.h>
#include <ovthreads.h>

#include "i18n.h"

struct process {
  HANDLE process;
  HANDLE in_w;
  HANDLE out_r;
  HANDLE err_r;

  thrd_t thread_stdout;
  thrd_t thread_stderr;

  void *userdata;
  void (*on_receive_stdout)(void *userdata, void const *const ptr, size_t const len);
  void (*on_receive_stderr)(void *userdata, void const *const ptr, size_t const len);
  void (*on_close_stdout)(void *userdata, error err);
  void (*on_close_stderr)(void *userdata, error err);
};

static NODISCARD error build_environment_strings(wchar_t **const dest,
                                                 wchar_t const *const name,
                                                 wchar_t const *const value) {
  error err = eok();
  wchar_t *const envstr = GetEnvironmentStringsW();
  if (!envstr) {
    err = emsg_i18n(err_type_generic, err_fail, gettext("Unable to get environment strings."));
    goto cleanup;
  }

  size_t name_len = 0;
  size_t value_len = 0;
  size_t len = 0;
  if (name && value) {
    name_len = wcslen(name);
    value_len = wcslen(value);
    len += name_len + 1 + value_len + 1;
  }
  wchar_t const *src = envstr;
  while (*src) {
    size_t const l = wcslen(src) + 1;
    len += l;
    src += l;
  }

  wchar_t *newenv = NULL;
  err = mem(&newenv, len + 1, sizeof(wchar_t));
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }

  wchar_t *p = newenv;
  if (name && value) {
    wcscpy(p, name);
    p += name_len;
    *p++ = L'=';
    len -= name_len + 1;
    wcscpy(p, value);
    p += value_len + 1;
    len -= value_len + 1;
  }
  memcpy(p, envstr, (size_t)(len) * sizeof(wchar_t));
  p[len] = L'\0';
  *dest = newenv;
cleanup:
  FreeEnvironmentStringsW(envstr);
  if (efailed(err)) {
    ereport(mem_free(&newenv));
  }
  return err;
}

static NODISCARD error get_working_directory(wchar_t **const dest, wchar_t const *cmdline) {
  if (!dest || !cmdline) {
    return errg(err_invalid_arugment);
  }
  size_t const exe_pathlen = wcslen(cmdline) + 1;
  wchar_t *path = NULL;
  wchar_t *dir = NULL;
  error err = mem(&path, (size_t)exe_pathlen, sizeof(wchar_t));
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  int pathlen = 0;
  if (*cmdline == L'"') {
    ++cmdline;
    while (*cmdline != L'\0' && *cmdline != L'"') {
      path[pathlen++] = *cmdline++;
    }
  } else {
    while (*cmdline != L'\0' && *cmdline != L' ') {
      path[pathlen++] = *cmdline++;
    }
  }
  path[pathlen] = L'\0';
  size_t dirlen = (size_t)GetFullPathNameW(path, 0, NULL, NULL);
  if (dirlen == 0) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  err = mem(&dir, dirlen, sizeof(wchar_t));
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  wchar_t *fn = NULL;
  if (GetFullPathNameW(path, (DWORD)dirlen, dir, &fn) == 0) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  if (!fn) {
    err = errg(err_fail);
    goto cleanup;
  }
  *fn = '\0';
  *dest = dir;
  dir = NULL;
cleanup:
  ereport(mem_free(&path));
  ereport(mem_free(&dir));
  return err;
}

static void read_worker(HANDLE h,
                        void (*receive)(void *userdata, void const *const ptr, size_t const len),
                        void (*close)(void *userdata, error err),
                        void *userdata) {
  enum {
    buffer_size = 1024,
  };
  uint8_t buf[buffer_size];
  DWORD len;
  error err = eok();
  while (1) {
    if (!ReadFile(h, buf, buffer_size, &len, NULL)) {
      HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
      if (hr == HRESULT_FROM_WIN32(ERROR_BROKEN_PIPE)) {
        break;
      }
      err = errhr(hr);
      goto cleanup;
    }
    if (len > 0) {
      receive(userdata, buf, (size_t)len);
    }
  }
cleanup:
  if (close) {
    close(userdata, err);
  } else {
    ereport(err);
  }
}

#if 0
static void log_thread_id(wchar_t const *const msg) {
  wchar_t buf[128];
  ov_snprintf_wchar(buf, sizeof(buf) / sizeof(buf[0]), NULL, L"[%04X] %ls", (int)(GetCurrentThreadId()), msg);
  OutputDebugStringW(buf);
}
#else
#  define log_thread_id(msg) ((void)0)
#endif

static int read_stdout_worker(void *userdata) {
  struct process *const pr = userdata;
  log_thread_id(L"read_stdout_worker");
  read_worker(pr->out_r, pr->on_receive_stdout, pr->on_close_stdout, pr->userdata);
  return 0;
}

static int read_stderr_worker(void *userdata) {
  struct process *const pr = userdata;
  log_thread_id(L"read_stderr_worker");
  read_worker(pr->err_r, pr->on_receive_stderr, pr->on_close_stderr, pr->userdata);
  return 0;
}

NODISCARD error process_write(struct process *const pr, void const *const buf, size_t const len) {
  const char *b = buf;
  size_t sz = len;
  error err = eok();
  for (DWORD written; sz > 0; b += written, sz -= written) {
    if (!WriteFile(pr->in_w, b, sz, &written, NULL)) {
      err = errhr(HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }
  }
  if (!FlushFileBuffers(pr->in_w)) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
cleanup:
  return err;
}

NODISCARD error process_create(struct process **prp, struct process_options const *const options) {
  if (!prp || *prp || !options || !options->cmdline) {
    return errg(err_invalid_arugment);
  }
  error err = eok();

  struct process *pr = NULL;

  HANDLE in_r = INVALID_HANDLE_VALUE;
  HANDLE in_w = INVALID_HANDLE_VALUE;
  HANDLE in_w_tmp = INVALID_HANDLE_VALUE;

  HANDLE out_r = INVALID_HANDLE_VALUE;
  HANDLE out_w = INVALID_HANDLE_VALUE;
  HANDLE out_r_tmp = INVALID_HANDLE_VALUE;

  HANDLE err_r = INVALID_HANDLE_VALUE;
  HANDLE err_w = INVALID_HANDLE_VALUE;
  HANDLE err_r_tmp = INVALID_HANDLE_VALUE;

  wchar_t *env = NULL;
  wchar_t *cmdline = NULL;
  wchar_t *dir = NULL;

  PROCESS_INFORMATION pi = {INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, 0, 0};
  SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), 0, TRUE};

  if (!CreatePipe(&in_r, &in_w_tmp, &sa, 0)) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  if (!CreatePipe(&out_r_tmp, &out_w, &sa, 0)) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  if (!CreatePipe(&err_r_tmp, &err_w, &sa, 0)) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }

  HANDLE curproc = GetCurrentProcess();
  if (!DuplicateHandle(curproc, in_w_tmp, curproc, &in_w, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  if (!DuplicateHandle(curproc, out_r_tmp, curproc, &out_r, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  if (!DuplicateHandle(curproc, err_r_tmp, curproc, &err_r, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }

  CloseHandle(in_w_tmp);
  in_w_tmp = INVALID_HANDLE_VALUE;
  CloseHandle(out_r_tmp);
  out_r_tmp = INVALID_HANDLE_VALUE;
  CloseHandle(err_r_tmp);
  err_r_tmp = INVALID_HANDLE_VALUE;

  err = build_environment_strings(&env, options->envvar_name, options->envvar_value);
  if (efailed(err)) {
    err = ethru(err);
  }
  if (!env) {
    goto cleanup;
  }

  // have to copy this buffer because CreateProcessW may modify path string.
  size_t const cmdlinelen = wcslen(options->cmdline);
  err = mem(&cmdline, cmdlinelen + 1, sizeof(wchar_t));
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  memcpy(cmdline, options->cmdline, cmdlinelen * sizeof(wchar_t));
  cmdline[cmdlinelen] = L'\0';

  err = get_working_directory(&dir, options->cmdline);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  if (!CreateProcessW(NULL,
                      cmdline,
                      NULL,
                      NULL,
                      TRUE,
                      CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_PROCESS_GROUP,
                      env,
                      dir,
                      &(STARTUPINFOW){
                          .cb = sizeof(STARTUPINFOW),
                          .dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW,
                          .wShowWindow = SW_HIDE,
                          .hStdInput = in_r,
                          .hStdOutput = out_w,
                          .hStdError = err_w,
                      },
                      &pi)) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }

  err = mem(&pr, 1, sizeof(struct process));
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  *pr = (struct process){
      .process = pi.hProcess,
      .in_w = in_w,
      .out_r = out_r,
      .err_r = err_r,
      .userdata = options->userdata,
      .on_receive_stdout = options->on_receive_stdout,
      .on_receive_stderr = options->on_receive_stderr,
      .on_close_stdout = options->on_close_stdout,
      .on_close_stderr = options->on_close_stderr,
  };
  pi.hProcess = INVALID_HANDLE_VALUE;
  in_w = INVALID_HANDLE_VALUE;
  out_r = INVALID_HANDLE_VALUE;
  err_r = INVALID_HANDLE_VALUE;

  if (pr->on_receive_stdout) {
    if (thrd_create(&pr->thread_stdout, read_stdout_worker, pr) != thrd_success) {
      pr->on_receive_stdout = NULL;
      err = emsg_i18n(err_type_generic, err_fail, gettext("Unable to create thread."));
      goto cleanup;
    }
  } else {
    process_close_stdout(pr);
  }

  if (pr->on_receive_stderr) {
    if (thrd_create(&pr->thread_stderr, read_stderr_worker, pr) != thrd_success) {
      pr->on_receive_stderr = NULL;
      err = emsg_i18n(err_type_generic, err_fail, gettext("Unable to create thread."));
      goto cleanup;
    }
  } else {
    process_close_stderr(pr);
  }

  *prp = pr;
  pr = NULL;
cleanup:
  if (pr) {
    process_destroy(&pr);
  }
  if (dir) {
    ereport(mem_free(&dir));
  }
  if (cmdline) {
    ereport(mem_free(&cmdline));
  }
  if (env) {
    ereport(mem_free(&env));
  }

  if (pi.hThread != INVALID_HANDLE_VALUE) {
    CloseHandle(pi.hThread);
    pi.hThread = INVALID_HANDLE_VALUE;
  }
  if (pi.hProcess != INVALID_HANDLE_VALUE) {
    CloseHandle(pi.hProcess);
    pi.hProcess = INVALID_HANDLE_VALUE;
  }

  if (err_r != INVALID_HANDLE_VALUE) {
    CloseHandle(err_r);
    err_r = INVALID_HANDLE_VALUE;
  }
  if (err_w != INVALID_HANDLE_VALUE) {
    CloseHandle(err_w);
    err_w = INVALID_HANDLE_VALUE;
  }
  if (err_r_tmp != INVALID_HANDLE_VALUE) {
    CloseHandle(err_r_tmp);
    err_r_tmp = INVALID_HANDLE_VALUE;
  }
  if (out_r != INVALID_HANDLE_VALUE) {
    CloseHandle(out_r);
    out_r = INVALID_HANDLE_VALUE;
  }
  if (out_w != INVALID_HANDLE_VALUE) {
    CloseHandle(out_w);
    out_w = INVALID_HANDLE_VALUE;
  }
  if (out_r_tmp != INVALID_HANDLE_VALUE) {
    CloseHandle(out_r_tmp);
    out_r_tmp = INVALID_HANDLE_VALUE;
  }
  if (in_r != INVALID_HANDLE_VALUE) {
    CloseHandle(in_r);
    in_r = INVALID_HANDLE_VALUE;
  }
  if (in_w != INVALID_HANDLE_VALUE) {
    CloseHandle(in_w);
    in_w = INVALID_HANDLE_VALUE;
  }
  if (in_w_tmp != INVALID_HANDLE_VALUE) {
    CloseHandle(in_w_tmp);
    in_w_tmp = INVALID_HANDLE_VALUE;
  }
  return err;
}

void process_destroy(struct process **const prp) {
  if (!prp || !*prp) {
    return;
  }
  struct process *const pr = *prp;
  process_close_stdin(pr);
  if (pr->process != INVALID_HANDLE_VALUE) {
    if (process_isrunning(pr)) {
      WaitForSingleObject(pr->process, INFINITE);
    }
    CloseHandle(pr->process);
    pr->process = INVALID_HANDLE_VALUE;
  }
  if (pr->on_receive_stdout) {
    thrd_join(pr->thread_stdout, NULL);
    pr->on_receive_stdout = NULL;
  }
  if (pr->on_receive_stderr) {
    thrd_join(pr->thread_stderr, NULL);
    pr->on_receive_stderr = NULL;
  }
  process_close_stdout(pr);
  process_close_stderr(pr);
  pr->userdata = NULL;
  ereport(mem_free(prp));
}

void process_close_stdin(struct process *const pr) {
  if (!pr || pr->in_w == INVALID_HANDLE_VALUE) {
    return;
  }
  CloseHandle(pr->in_w);
  pr->in_w = INVALID_HANDLE_VALUE;
}

void process_close_stdout(struct process *const pr) {
  if (!pr || pr->out_r == INVALID_HANDLE_VALUE) {
    return;
  }
  CloseHandle(pr->out_r);
  pr->out_r = INVALID_HANDLE_VALUE;
}

void process_close_stderr(struct process *const pr) {
  if (!pr || pr->err_r == INVALID_HANDLE_VALUE) {
    return;
  }
  CloseHandle(pr->err_r);
  pr->err_r = INVALID_HANDLE_VALUE;
}

void process_abort(struct process *const pr) {
  if (!pr) {
    return;
  }
  if (pr->process == INVALID_HANDLE_VALUE || !process_isrunning(pr)) {
    return;
  }
  if (!TerminateProcess(pr->process, 1)) {
    ereport(errhr(HRESULT_FROM_WIN32(GetLastError())));
  }
}

NODISCARD error process_send_ctrl_break(struct process *const pr) {
  if (!pr) {
    return errg(err_invalid_arugment);
  }
  if (pr->process == INVALID_HANDLE_VALUE || !process_isrunning(pr)) {
    return eok();
  }
  error err = eok();
  HINSTANCE hinst = get_hinstance();
  wchar_t *module_path = NULL;
  wchar_t *cmdline = NULL;
  PROCESS_INFORMATION pi = {
      .hProcess = INVALID_HANDLE_VALUE,
      .hThread = INVALID_HANDLE_VALUE,
  };

  DWORD n = 0, r;
  for (;;) {
    err = OV_ARRAY_GROW(&module_path, n += MAX_PATH);
    if (efailed(err)) {
      err = ethru(err);
      goto cleanup;
    }
    r = GetModuleFileNameW(hinst, module_path, n);
    if (r == 0) {
      err = errhr(HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }
    if (r < n) {
      OV_ARRAY_SET_LENGTH(module_path, r);
      break;
    }
  }

  size_t const len = OV_ARRAY_LENGTH(module_path) + 64;
  err = OV_ARRAY_GROW(&cmdline, len);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  ov_snprintf(cmdline, len, NULL, L"rundll32 \"%ls\",SendCtrlBreak %d", module_path, (int)(GetProcessId(pr->process)));
  if (!CreateProcessW(NULL,
                      cmdline,
                      NULL,
                      NULL,
                      TRUE,
                      CREATE_NO_WINDOW | CREATE_NEW_CONSOLE,
                      NULL,
                      NULL,
                      &(STARTUPINFOW){
                          .cb = sizeof(STARTUPINFOW),
                          .dwFlags = STARTF_USESHOWWINDOW,
                          .wShowWindow = SW_HIDE,
                      },
                      &pi)) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
cleanup:
  if (pi.hThread != INVALID_HANDLE_VALUE) {
    CloseHandle(pi.hThread);
  }
  if (pi.hProcess != INVALID_HANDLE_VALUE) {
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
  }
  OV_ARRAY_DESTROY(&cmdline);
  OV_ARRAY_DESTROY(&module_path);
  return err;
}

bool process_isrunning(struct process const *const pr) { return WaitForSingleObject(pr->process, 0) == WAIT_TIMEOUT; }

static inline void flush_line_buffer(struct process_line_buffer_context *ctx) {
  ctx->buf[ctx->written] = '\0';
  ctx->on_line(ctx->userdata, ctx->buf);
  ctx->written = 0;
}

void process_line_buffer(struct process_line_buffer_context *ctx, void const *const ptr, size_t const len) {
  char const *const p = ptr;
  for (size_t i = 0; i < len; ++i) {
    switch (p[i]) {
    case '\r':
      if (i + 1 < len && p[i + 1] == '\n') {
        ++i;
      }
      flush_line_buffer(ctx);
      break;
    case '\n':
      flush_line_buffer(ctx);
      break;
    default:
      if (ctx->written == sizeof(ctx->buf) - 1) {
        flush_line_buffer(ctx);
      }
      ctx->buf[ctx->written++] = p[i];
      break;
    }
  }
}
