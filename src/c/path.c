#include "path.h"

#include <ovarray.h>

#include <ovutil/win32.h>

#include <shlobj.h>

static NODISCARD error get_temp_directory(wchar_t **const path, size_t const extra_space, size_t *const len) {
  error err = eok();
  DWORD sz = GetTempPathW(0, NULL); // sz includes the null terminator
  if (sz == 0) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  --sz;
  err = OV_ARRAY_GROW(path, sz + extra_space + 2);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  if (GetTempPathW(sz + 2, *path) == 0) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  // According to the specification, a '\\' is always appended to the end, but check for fail-safe.
  wchar_t *p = *path;
  if (p[sz - 1] != L'\\' && p[sz - 1] != L'/') {
    p[sz++] = L'\\';
    p[sz] = L'\0';
  }
  if (len) {
    *len = sz;
  }
cleanup:
  return err;
}

NODISCARD error path_get_temp_file(wchar_t **const path, wchar_t const *const filename) {
  if (!path || !filename) {
    return errg(err_invalid_arugment);
  }
  size_t const filename_len = filename ? wcslen(filename) : 0;
  size_t len;
  error err = get_temp_directory(path, filename_len, &len);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  if (filename_len) {
    wcscpy(*path + len, filename);
  }
  OV_ARRAY_SET_LENGTH(*path, len + filename_len);
cleanup:
  return err;
}

NODISCARD error path_get_module_name(wchar_t **const module_path, HINSTANCE const hinst) {
  DWORD n = 0, r;
  error err = eok();
  for (;;) {
    err = OV_ARRAY_GROW(module_path, n += MAX_PATH);
    if (efailed(err)) {
      err = ethru(err);
      goto cleanup;
    }
    r = GetModuleFileNameW(hinst, *module_path, n);
    if (r == 0) {
      err = errhr(HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }
    if (r < n) {
      OV_ARRAY_SET_LENGTH(*module_path, r);
      break;
    }
  }
cleanup:
  return err;
}

static wchar_t *find_last_path_sep(wchar_t *const path) {
  wchar_t *sep = wcsrchr(path, L'\\');
  if (!sep) {
    sep = wcsrchr(path, L'/');
  }
  return sep;
}

wchar_t *path_extract_file_name_mut(wchar_t *const path) {
  if (!path) {
    return NULL;
  }
  wchar_t *sep = find_last_path_sep(path);
  if (!sep) {
    return path;
  }
  ++sep;
  return sep;
}

wchar_t const *path_extract_file_name_const(wchar_t const *const path) {
  return path_extract_file_name_mut(ov_deconster_(path));
}

NODISCARD error path_select_file(HWND const window,
                                 wchar_t const *const title,
                                 wchar_t const *const filter,
                                 GUID const *const client_id,
                                 wchar_t **const path) {
  error err = eok();
  IFileDialog *pfd = NULL;
  COMDLG_FILTERSPEC *fs = NULL;
  IShellItem *psiResult = NULL;
  PWSTR pszPath = NULL;
  HRESULT hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IFileDialog, (void **)&pfd);
  if (FAILED(hr)) {
    err = errhr(hr);
    goto cleanup;
  }
  hr = pfd->lpVtbl->SetTitle(pfd, title);
  if (FAILED(hr)) {
    err = errhr(hr);
    goto cleanup;
  }
  wchar_t const *f = filter;
  size_t filter_count = 0;
  while (*f) {
    ++filter_count;
    f += wcslen(f) + 1;
  }
  filter_count /= 2;
  err = OV_ARRAY_GROW(&fs, filter_count + 1);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  f = filter;
  for (size_t i = 0; i < filter_count; ++i) {
    fs[i].pszName = f;
    f += wcslen(f) + 1;
    fs[i].pszSpec = f;
    f += wcslen(f) + 1;
  }
  hr = pfd->lpVtbl->SetFileTypes(pfd, (UINT)filter_count, fs);
  if (FAILED(hr)) {
    err = errhr(hr);
    goto cleanup;
  }
  hr = pfd->lpVtbl->SetClientGuid(pfd, client_id);
  if (FAILED(hr)) {
    err = errhr(hr);
    goto cleanup;
  }
  hr = pfd->lpVtbl->Show(pfd, window);
  if (FAILED(hr)) {
    err = errhr(hr);
    goto cleanup;
  }
  hr = pfd->lpVtbl->GetResult(pfd, &psiResult);
  if (FAILED(hr)) {
    err = errhr(hr);
    goto cleanup;
  }
  hr = psiResult->lpVtbl->GetDisplayName(psiResult, SIGDN_FILESYSPATH, &pszPath);
  if (FAILED(hr)) {
    err = errhr(hr);
    goto cleanup;
  }
  err = OV_ARRAY_GROW(path, wcslen(pszPath) + 1);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  wcscpy(*path, pszPath);
cleanup:
  if (pszPath) {
    CoTaskMemFree(pszPath);
    pszPath = NULL;
  }
  if (psiResult) {
    psiResult->lpVtbl->Release(psiResult);
    psiResult = NULL;
  }
  if (fs) {
    OV_ARRAY_DESTROY(&fs);
  }
  if (pfd) {
    pfd->lpVtbl->Release(pfd);
    pfd = NULL;
  }
  return err;
}

NODISCARD error path_select_folder(HWND const window,
                                   wchar_t const *const title,
                                   GUID const *const client_id,
                                   wchar_t **const path) {
  error err = eok();
  IFileDialog *pfd = NULL;
  IShellItem *psiResult = NULL;
  PWSTR pszPath = NULL;
  HRESULT hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IFileDialog, (void **)&pfd);
  if (FAILED(hr)) {
    err = errhr(hr);
    goto cleanup;
  }
  DWORD dwOptions;
  hr = pfd->lpVtbl->GetOptions(pfd, &dwOptions);
  if (FAILED(hr)) {
    err = errhr(hr);
    goto cleanup;
  }
  hr = pfd->lpVtbl->SetOptions(pfd, dwOptions | FOS_PICKFOLDERS);
  if (FAILED(hr)) {
    err = errhr(hr);
    goto cleanup;
  }
  hr = pfd->lpVtbl->SetTitle(pfd, title);
  if (FAILED(hr)) {
    err = errhr(hr);
    goto cleanup;
  }
  hr = pfd->lpVtbl->SetClientGuid(pfd, client_id);
  if (FAILED(hr)) {
    err = errhr(hr);
    goto cleanup;
  }
  hr = pfd->lpVtbl->Show(pfd, window);
  if (FAILED(hr)) {
    err = errhr(hr);
    goto cleanup;
  }
  hr = pfd->lpVtbl->GetResult(pfd, &psiResult);
  if (FAILED(hr)) {
    err = errhr(hr);
    goto cleanup;
  }
  hr = psiResult->lpVtbl->GetDisplayName(psiResult, SIGDN_FILESYSPATH, &pszPath);
  if (FAILED(hr)) {
    err = errhr(hr);
    goto cleanup;
  }
  err = OV_ARRAY_GROW(path, wcslen(pszPath) + 1);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  wcscpy(*path, pszPath);
cleanup:
  if (pszPath) {
    CoTaskMemFree(pszPath);
    pszPath = NULL;
  }
  if (psiResult) {
    psiResult->lpVtbl->Release(psiResult);
    psiResult = NULL;
  }
  if (pfd) {
    pfd->lpVtbl->Release(pfd);
    pfd = NULL;
  }
  return err;
}
