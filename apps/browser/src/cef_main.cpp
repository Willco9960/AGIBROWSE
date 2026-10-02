#include <windows.h>

#include <cstdio>
#include <string>
#include <map>
#include <array>
#include "lib/ipc/windows_channel.h"

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_command_line.h"
#include "include/cef_frame.h"
#include "include/cef_frame_handler.h"
#include "include/cef_process_message.h"
#include "include/cef_render_process_handler.h"
#include "include/cef_v8.h"
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
bool renderer_security_test = false;
bool renderer_security_passed = false;
agi::ipc::HostChannel broker_channel;

void Record(const char* event, unsigned long long value = 0) {
  if (lifecycle_log) {
    std::fprintf(lifecycle_log, "{\"event\":\"%s\",\"value\":%llu}\n", event, value);
    std::fflush(lifecycle_log);
  }
}

class BrowserClient final : public CefClient,
                            public CefLifeSpanHandler,
                            public CefDisplayHandler,
                            public CefLoadHandler,
                            public CefFrameHandler {
 public:
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefFrameHandler> GetFrameHandler() override { return this; }
  void OnFrameAttached(CefRefPtr<CefBrowser> browser,CefRefPtr<CefFrame> frame,bool reattached) override {
    CEF_REQUIRE_UI_THREAD();
    frames_[{browser->GetIdentifier(),frame->GetIdentifier().ToString()}]=frame;
  }
  void OnFrameDetached(CefRefPtr<CefBrowser> browser,CefRefPtr<CefFrame> frame) override {
    CEF_REQUIRE_UI_THREAD();
    frames_.erase({browser->GetIdentifier(),frame->GetIdentifier().ToString()});
    denied_.erase({browser->GetIdentifier(),frame->GetIdentifier().ToString()});
  }
  void OnFrameDestroyed(CefRefPtr<CefBrowser> browser,CefRefPtr<CefFrame> frame) override {
    OnFrameDetached(browser,frame);
  }
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,CefRefPtr<CefFrame> frame,CefProcessId source,CefRefPtr<CefProcessMessage> message) override {
    CEF_REQUIRE_UI_THREAD();
    const auto key=std::make_pair(browser->GetIdentifier(),frame->GetIdentifier().ToString());
    auto found=frames_.find(key);
    // CEF creates distinct C++ wrappers for the same underlying frame across
    // callbacks. Stable engine identifiers + current engine lookup establish
    // identity; wrapper memory addresses do not.
    auto current=browser->GetFrameByIdentifier(frame->GetIdentifier());
    if(source!=PID_RENDERER || found==frames_.end() || !found->second->IsValid() || !frame->IsValid() || !current || !current->IsValid() || current->GetBrowser()->GetIdentifier()!=browser->GetIdentifier() || found->second->GetIdentifier()!=current->GetIdentifier()) {
      Record("renderer_identity_rejected");return true;
    }
    auto args=message->GetArgumentList();
    // The renderer lane never calls the broker decoder/admission/dispatch.
    // Only host-requested diagnostics have a closed non-authorizing response.
    if(renderer_security_test && message->GetName()=="agi.test.result.v1" && args->GetSize()==2 && args->GetType(0)==VTYPE_BOOL && args->GetType(1)==VTYPE_BOOL && args->GetBool(0) && args->GetBool(1) && denied_[key]==8) {
      renderer_security_passed=true;Record("renderer_application_escape_blocked");Record("renderer_private_handles_absent");Record("page_native_api_absent");return true;
    }
    // A future semantic lane is unavailable until its closed schema exists.
    auto& denied=denied_[key];
    if(denied<8) { ++denied;Record("renderer_privileged_message_rejected"); }
    return true;
  }

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
      if(renderer_security_test) {
        const auto endpoints=broker_channel.EndpointDiagnosticsForTest();
        if(endpoints.size()!=2) { load_failed=true;Record("private_endpoint_diagnostics_failed");return; }
        auto probe=CefProcessMessage::Create("agi.test.probe.v1");
        auto values=probe->GetArgumentList();
        for(size_t i=0;i<endpoints.size();++i) {
          values->SetString(i*2,std::to_string(endpoints[i].first));
          values->SetString(i*2+1,endpoints[i].second);
        }
        frame->SendProcessMessage(PID_RENDERER,probe);
      }
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
  std::map<std::pair<int,std::string>,CefRefPtr<CefFrame>> frames_;
  std::map<std::pair<int,std::string>,unsigned> denied_;
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

class RendererApp final : public CefApp,public CefRenderProcessHandler {
 public:
  CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override { return this; }
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,CefRefPtr<CefFrame> frame,CefProcessId source,CefRefPtr<CefProcessMessage> message) override {
    CEF_REQUIRE_RENDERER_THREAD();
    if(source!=PID_BROWSER || message->GetName()!="agi.test.probe.v1")return false;
    auto args=message->GetArgumentList();if(args->GetSize()!=4)return true;
    bool absent=true;
    for(size_t i=0;i<2;++i) {
      if(args->GetType(i*2)!=VTYPE_STRING || args->GetType(i*2+1)!=VTYPE_STRING)return true;
      auto handle=reinterpret_cast<HANDLE>(std::stoull(args->GetString(i*2).ToString()));
      std::array<uint8_t,4096> buffer{};
      if(GetFileInformationByHandleEx(handle,FileNameInfo,buffer.data(),static_cast<DWORD>(buffer.size()))) {
        auto info=reinterpret_cast<FILE_NAME_INFO*>(buffer.data());
        if(std::wstring(info->FileName,info->FileNameLength/sizeof(wchar_t))==args->GetString(i*2+1).ToWString())absent=false;
      }
    }
    auto context=frame->GetV8Context();bool no_api=false;
    if(context && context->Enter()) {
      CefRefPtr<CefV8Value> value;CefRefPtr<CefV8Exception> exception;
      no_api=context->Eval("['aibrowese','agiBrowse','nativeHost','broker','cefQuery'].every(k => typeof globalThis[k] === 'undefined')","agi-security-probe",0,value,exception) && value && value->IsBool() && value->GetBoolValue();
      context->Exit();
    }
    for(auto tag:{"AIPC","intent","grant","shell","cdp","agi.broker.intent.v1","agi.renderer.v1","agi.renderer.v1"}) {
      auto forged=CefProcessMessage::Create(tag);
      forged->GetArgumentList()->SetString(0,"client=attacker;profile=human;grant=approved;destination=host");
      frame->SendProcessMessage(PID_BROWSER,forged);
    }
    auto result=CefProcessMessage::Create("agi.test.result.v1");result->GetArgumentList()->SetBool(0,no_api);result->GetArgumentList()->SetBool(1,absent);frame->SendProcessMessage(PID_BROWSER,result);
    return true;
  }
 private:
  IMPLEMENT_REFCOUNTING(RendererApp);
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
  const int subprocess_exit = CefExecuteProcess(main_args, new RendererApp, sandbox_info);
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
  renderer_security_test=args->HasSwitch("ipc-renderer-test");
  wchar_t executable[32768]{};
  const DWORD executable_length=GetModuleFileNameW(nullptr,executable,32768);
  if(!executable_length || executable_length>=32768) return 70;
  std::wstring broker_path(executable);
  broker_path=broker_path.substr(0,broker_path.find_last_of(L"\\/"))+L"\\agi-browse-broker.exe";
  if(!broker_channel.Start(broker_path)) {
    Record("private_broker_start_failed");if(lifecycle_log){std::fclose(lifecycle_log);lifecycle_log=nullptr;}return 70;
  }
  Record("private_broker_challenge_verified",broker_channel.process_id());
  CefSettings settings;
  settings.no_sandbox = false;
  // CEF requires an absolute cache root. The caller's isolated profile is used
  // for tests; the normal default is under the user's LocalAppData directory.
  std::wstring profile = args->GetSwitchValue("profile-dir").ToWString();
  if (profile.empty()) {
    wchar_t local_app_data[32768] = {};
    const DWORD length = ::GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, 32768);
    if (!length || length >= 32768) {
      broker_channel.Stop();
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
    broker_channel.Stop();
    Record("initialization_failed");
    if (lifecycle_log) { std::fclose(lifecycle_log); lifecycle_log = nullptr; }
    const int code = CefGetExitCode();
    return code == 0 ? 68 : code;
  }
  Record("initialized");
  CefRunMessageLoop();
  Record("message_loop_exited");
  CefShutdown();
  broker_channel.Stop();
  Record("private_broker_stopped");
  Record("shutdown_complete");
  app = nullptr;
  if (lifecycle_log) { std::fclose(lifecycle_log); lifecycle_log = nullptr; }
  return load_failed || (args->HasSwitch("require-fixture") && !fixture_ready) || (renderer_security_test && !renderer_security_passed) ? 69 : 0;
}
