#include <windows.h>

#include <cstdio>
#include <string>

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_command_line.h"
#include "include/cef_frame.h"
#include "include/cef_sandbox_win.h"
#include "include/cef_version_info.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
#include "include/wrapper/cef_helpers.h"

namespace {

// Browser-process-only diagnostics. No page strings or renderer file access.
FILE* lifecycle_log = nullptr;
bool fixture_ready = false;
bool load_failed = false;

void Record(const char* event, unsigned long long value = 0) {
  if (lifecycle_log) {
    std::fprintf(lifecycle_log, "{\"event\":\"%s\",\"value\":%llu}\n", event, value);
    std::fflush(lifecycle_log);
  }
}

class BrowserClient final : public CefClient,
                            public CefLifeSpanHandler,
                            public CefDisplayHandler,
                            public CefLoadHandler {
 public:
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }

  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    CEF_REQUIRE_UI_THREAD();
    ++browser_count_;
    Record("browser_created", browser->GetIdentifier());
  }

  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
    CEF_REQUIRE_UI_THREAD();
    Record("browser_closed", browser->GetIdentifier());
    if (--browser_count_ == 0) {
      CefQuitMessageLoop();
    }
  }

  void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) override {
    CEF_REQUIRE_UI_THREAD();
    if (title == "AGI-BROWSE fixture ready") {
      fixture_ready = true;
      Record("fixture_ready");
    }
    auto view = CefBrowserView::GetForBrowser(browser);
    if (view && view->GetWindow()) {
      view->GetWindow()->SetTitle(title);
    }
  }

  void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                 CefRefPtr<CefFrame> frame,
                 int http_status_code) override {
    CEF_REQUIRE_UI_THREAD();
    if (frame->IsMain()) {
      Record("main_frame_loaded", http_status_code);
    }
  }

  void OnLoadError(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   ErrorCode error_code,
                   const CefString& error_text,
                   const CefString& failed_url) override {
    CEF_REQUIRE_UI_THREAD();
    if (frame->IsMain() && error_code != ERR_ABORTED) {
      load_failed = true;
      Record("main_frame_load_failed");
    }
  }

 private:
  int browser_count_ = 0;
  IMPLEMENT_REFCOUNTING(BrowserClient);
};

class WindowDelegate final : public CefWindowDelegate {
 public:
  explicit WindowDelegate(CefRefPtr<CefBrowserView> view) : view_(view) {}

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    window->AddChildView(view_);
    window->SetTitle("AGI-BROWSE");
    window->Show();
    view_->RequestFocus();
    Record("window_created", reinterpret_cast<unsigned long long>(window->GetWindowHandle()));
  }

  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override {
    Record("window_destroyed");
    view_ = nullptr;
  }

  bool CanClose(CefRefPtr<CefWindow> window) override {
    auto browser = view_ ? view_->GetBrowser() : nullptr;
    return !browser || browser->GetHost()->TryCloseBrowser();
  }

  CefSize GetPreferredSize(CefRefPtr<CefView> view) override {
    return CefSize(1100, 760);
  }

 private:
  CefRefPtr<CefBrowserView> view_;
  IMPLEMENT_REFCOUNTING(WindowDelegate);
};

class BrowserApp final : public CefApp, public CefBrowserProcessHandler {
 public:
  explicit BrowserApp(std::string url) : url_(std::move(url)) {}
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }

  void OnContextInitialized() override {
    CEF_REQUIRE_UI_THREAD();
    Record("context_initialized");
    CefBrowserSettings settings;
    auto view = CefBrowserView::CreateBrowserView(new BrowserClient, url_, settings,
                                                 nullptr, nullptr, nullptr);
    CefWindow::CreateTopLevelWindow(new WindowDelegate(view));
  }

 private:
  const std::string url_;
  IMPLEMENT_REFCOUNTING(BrowserApp);
};

bool UnsafeSwitches(CefRefPtr<CefCommandLine> args) {
  for (const auto* flag : {"no-sandbox", "disable-sandbox", "disable-gpu-sandbox",
                           "disable-web-security", "single-process", "in-process-gpu",
                           "remote-debugging-port", "remote-debugging-pipe"}) {
    if (args->HasSwitch(flag)) {
      return true;
    }
  }
  return false;
}

}  // namespace

// Called by the unmodified upstream bootstrap in every CEF process. The bootstrap
// creates sandbox_info before loading this DLL, using Chromium's own toolchain.
CEF_BOOTSTRAP_EXPORT int RunWinMain(HINSTANCE instance,
                                    LPWSTR command_line,
                                    int show_state,
                                    void* sandbox_info,
                                    cef_version_info_t* version_info) {
  if (!sandbox_info || !version_info ||
      version_info->size < CEF_VERSION_INFO_SIZE_WITH_SANDBOX_HASH ||
      version_info->cef_version_major != CEF_VERSION_MAJOR ||
      version_info->cef_version_minor != CEF_VERSION_MINOR ||
      version_info->cef_version_patch != CEF_VERSION_PATCH ||
      version_info->chrome_version_build != CHROME_VERSION_BUILD ||
      version_info->chrome_version_patch != CHROME_VERSION_PATCH ||
      std::string(version_info->sandbox_compat_hash) != "265fca9293e6b6ef") {
    return 65;
  }

  auto args = CefCommandLine::CreateCommandLine();
  args->InitFromString(::GetCommandLineW());
  if (UnsafeSwitches(args)) {
    return 64;
  }

  CefMainArgs main_args(instance);
  const int subprocess_exit = CefExecuteProcess(main_args, nullptr, sandbox_info);
  if (subprocess_exit >= 0) {
    return subprocess_exit;
  }

  if (args->HasSwitch("lifecycle-log")) {
    const std::wstring log_path = args->GetSwitchValue("lifecycle-log").ToWString();
    lifecycle_log = _wfopen(log_path.c_str(), L"wb");
    if (!lifecycle_log) {
      return 66;
    }
  }
  Record("sandbox_bootstrap_verified", ::GetCurrentProcessId());
  CefSettings settings;
  settings.no_sandbox = false;
  // CEF requires an absolute cache root. The caller's isolated profile is used
  // for tests; the normal default is under the user's LocalAppData directory.
  std::wstring profile = args->GetSwitchValue("profile-dir").ToWString();
  if (profile.empty()) {
    wchar_t local_app_data[32768] = {};
    const DWORD length = ::GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, 32768);
    if (!length || length >= 32768) {
      if (lifecycle_log) { std::fclose(lifecycle_log); lifecycle_log = nullptr; }
      return 67;
    }
    profile = std::wstring(local_app_data) + L"\\AGI-BROWSE\\Profile";
  }
  CefString(&settings.root_cache_path) = profile;
  CefString(&settings.cache_path) = profile + L"\\Default";
  std::string url = args->GetSwitchValue("url");
  if (url.empty()) { url = "about:blank"; }
  auto app = CefRefPtr<BrowserApp>(new BrowserApp(url));
  if (!CefInitialize(main_args, settings, app, sandbox_info)) {
    Record("initialization_failed");
    if (lifecycle_log) { std::fclose(lifecycle_log); lifecycle_log = nullptr; }
    const int code = CefGetExitCode();
    return code == 0 ? 68 : code;
  }
  Record("initialized");
  CefRunMessageLoop();
  Record("message_loop_exited");
  CefShutdown();
  Record("shutdown_complete");
  app = nullptr;
  if (lifecycle_log) { std::fclose(lifecycle_log); lifecycle_log = nullptr; }
  return load_failed || (args->HasSwitch("require-fixture") && !fixture_ready) ? 69 : 0;
}
