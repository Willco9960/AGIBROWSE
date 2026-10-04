#include <windows.h>

#include <cstdio>
#include <string>
#include <map>
#include <array>
#include <algorithm>
#include <set>
#include "apps/browser/lifecycle.h"
#include "lib/ipc/windows_channel.h"
#include "lib/privacy/publication.h"
#include "include/cef_jsdialog_handler.h"
#ifdef AGI_TRANSPORT
#include "lib/transport/server.h"
#endif

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_keyboard_handler.h"
#include "include/cef_command_line.h"
#include "include/cef_frame.h"
#include "include/cef_frame_handler.h"
#include "include/cef_process_message.h"
#include "include/cef_render_process_handler.h"
#include "include/cef_v8.h"
#include "include/cef_task.h"
#include "include/cef_sandbox_win.h"
#include "include/cef_version_info.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_fill_layout.h"
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
bool privacy_test = false;
bool privacy_test_passed = false;
bool privacy_dialog_seen = false;
bool tab_lifecycle_test = false, tab_lifecycle_passed = false;
unsigned test_unload_canceled = 0, test_unload_accepted = 0;
bool test_cancel_unload = false;
std::string test_root;
agi::ipc::HostChannel broker_channel;
#ifdef AGI_TRANSPORT
std::shared_ptr<agi::transport::PairingAuthority> pairing_authority;
std::shared_ptr<agi::transport::HostTransportAuthority> host_transport_authority;
#endif
agi::browser::Lifecycle tabs([](const std::string& profile, const std::string& tab) {
#ifdef AGI_TRANSPORT
  try { if (host_transport_authority) host_transport_authority->InvalidateNativeTab(profile, tab); }
  catch (...) { try { broker_channel.Stop(); } catch (...) {} throw; }
#endif
});
std::map<std::string, CefRefPtr<CefBrowserView>> tab_views;
std::map<std::string, CefString> tab_titles;
std::map<std::string, CefRefPtr<CefWindow>> native_windows;
std::map<std::pair<int,int>, std::string> pending_popups;
std::set<std::string> closing_windows;
int browser_count = 0;
std::string CreateTab(const std::string& window, CefRefPtr<CefRequestContext> context, const std::string& profile);
void ShowActive(const std::string& window);
void CreateNativeWindow(CefRefPtr<CefBrowserView> view, const std::string& window);
bool ReorderTab(const std::string& tab, size_t index);
bool MoveTab(const std::string& tab, const std::string& window);
void CancelReservation(const std::string& tab);
void RequestCloseTab(const std::string& tab);
void MaybeQuit();
constexpr uint64_t kCreationTimeoutMs = 10000;

using agi::privacy::Event;
void Record(Event event, unsigned long long value = 0) {
  agi::privacy::WriteDiagnostic(lifecycle_log, event, value);
}
void MaybeQuit() {
  if(browser_count || tabs.tab_count() || !native_windows.empty())return;
  if(tab_lifecycle_test && tab_lifecycle_passed && tab_views.empty() && tab_titles.empty() && pending_popups.empty())Record(Event::tab_fixture_resources_released);
  CefQuitMessageLoop();
}

class BrowserClient final : public CefClient,
                            public CefLifeSpanHandler,
                            public CefDisplayHandler,
                            public CefLoadHandler,
                            public CefKeyboardHandler,
                            public CefJSDialogHandler,
                            public CefFrameHandler {
 public:
  explicit BrowserClient(std::string tab) : tab_(std::move(tab)) {}
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefFrameHandler> GetFrameHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
  CefRefPtr<CefJSDialogHandler> GetJSDialogHandler() override { return this; }
  bool OnConsoleMessage(CefRefPtr<CefBrowser>, cef_log_severity_t,
                        const CefString&, const CefString&, int) override {
    CEF_REQUIRE_UI_THREAD();
    // Entire untrusted message/source/line omitted before CEF's fallback logger.
    if (!console_seen_) { console_seen_ = true; Record(Event::page_console_suppressed); }
    return true;
  }
  bool OnJSDialog(CefRefPtr<CefBrowser>, const CefString&, JSDialogType,
                  const CefString&, const CefString&,
                  CefRefPtr<CefJSDialogCallback> callback, bool&) override {
    CEF_REQUIRE_UI_THREAD();
    // Harness-only bounded dismissal. Normal human website dialogs are intact.
    if (!privacy_test || privacy_dialog_seen) return false;
    privacy_dialog_seen = true; Record(Event::privacy_dialog_suppressed);
    callback->Continue(true, CefString()); return true;
  }
  bool OnBeforeUnloadDialog(CefRefPtr<CefBrowser> browser,const CefString&,bool is_reload,
      CefRefPtr<CefJSDialogCallback> callback) override {
    CEF_REQUIRE_UI_THREAD();
    // Test decisions still require an actual CEF beforeunload callback; the
    // harness cannot manufacture that callback or infer it from a timer.
    const bool fixture=tab_lifecycle_test && tab_==test_root && !is_reload;
    const bool allow=fixture ? !test_cancel_unload : MessageBoxW(browser->GetHost()->GetWindowHandle(),
        L"This page has unsaved changes. Leave the page?",L"AGI-BROWSE",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)==IDYES;
    if(fixture){if(allow)++test_unload_accepted;else ++test_unload_canceled;test_cancel_unload=false;}
    if(!allow && !is_reload) {
      auto tab=tabs.FindTab(tab_);auto window=tab?tab->window:std::string();
      tabs.CancelClose(tab_);closing_windows.erase(window);ShowActive(window);
    }
    callback->Continue(allow,CefString());return true;
  }
  bool OnPreKeyEvent(CefRefPtr<CefBrowser> browser,const CefKeyEvent& event,CefEventHandle os_event,bool* shortcut) override {
    CEF_REQUIRE_UI_THREAD();
    if(os_event && os_event->message==WM_KEYDOWN && os_event->wParam==event.windows_key_code && event.type==KEYEVENT_RAWKEYDOWN && (event.modifiers&EVENTFLAG_CONTROL_DOWN)) {
      auto tab=tabs.Resolve(tab_);
      if(tab && event.windows_key_code=='T' && !(event.modifiers&EVENTFLAG_SHIFT_DOWN)) {
        *shortcut=true;CreateTab(tab->window,browser->GetHost()->GetRequestContext(),tab->profile);return true;
      }
      if(tab && event.windows_key_code=='W' && !(event.modifiers&EVENTFLAG_SHIFT_DOWN)) {
        *shortcut=true;
        RequestCloseTab(tab_);return true;
      }
      if(tab && (event.modifiers&EVENTFLAG_SHIFT_DOWN) && (event.windows_key_code==VK_PRIOR || event.windows_key_code==VK_NEXT)) {
        *shortcut=true;const auto& order=tabs.FindWindow(tab->window)->tabs;
        auto at=static_cast<size_t>(std::find(order.begin(),order.end(),tab_)-order.begin());
        if(event.windows_key_code==VK_PRIOR && at)ReorderTab(tab_,at-1);
        if(event.windows_key_code==VK_NEXT && at+1<order.size())ReorderTab(tab_,at+1);
        return true;
      }
      if(tab && (event.modifiers&EVENTFLAG_SHIFT_DOWN) && event.windows_key_code=='N') {
        *shortcut=true;auto destination=tabs.NewWindow();if(destination.empty())return true;
        CreateNativeWindow(nullptr,destination);
        if(!MoveTab(tab_,destination)){auto native=native_windows.find(destination);if(native!=native_windows.end())native->second->Close();}
        return true;
      }
      if(tab && event.windows_key_code==VK_TAB) {
        *shortcut=true;const auto& order=tabs.FindWindow(tab->window)->tabs;
        auto current=std::find(order.begin(),order.end(),tab_);
        for(size_t i=1;i<=order.size();++i) {auto id=order[(static_cast<size_t>(current-order.begin())+i)%order.size()];if(tabs.Activate(id)){ShowActive(tab->window);break;}}
        return true;
      }
    }
#ifdef AGI_TRANSPORT
    if(!os_event||os_event->message!=WM_KEYDOWN||os_event->wParam!=event.windows_key_code||event.type!=KEYEVENT_RAWKEYDOWN||(event.modifiers&(EVENTFLAG_CONTROL_DOWN|EVENTFLAG_SHIFT_DOWN))!=(EVENTFLAG_CONTROL_DOWN|EVENTFLAG_SHIFT_DOWN))return false;
    auto owner=browser->GetHost()->GetWindowHandle();
    if(event.windows_key_code=='P') {
      *shortcut=true;if(pairing_authority&&broker_channel.live())agi::transport::NativeEnroll(owner,*pairing_authority);
      else MessageBoxW(owner,L"Agent transport unavailable. The protected pairing store may be expired, corrupt, or awaiting revocation recovery. Human browsing remains available. Re-pair from a fresh native installation after recovery; keys are never trusted automatically.",L"AGI-BROWSE transport unavailable",MB_OK|MB_ICONWARNING);
      return true;
    }
    if(event.windows_key_code=='I') {
      *shortcut=true;if(!pairing_authority||!broker_channel.live())return true;
      std::wstring text=L"Endpoint: wss://127.0.0.1:"+std::to_wstring(broker_channel.transport_port())+L"/transport/v1\nServer SHA256: ";auto pin=agi::transport::Pin(pairing_authority->server().cert);text.append(pin.begin(),pin.end());text+=L"\n\nCtrl+Shift+P: local enrollment\nCtrl+Shift+R: revoke a paired client\nPairing currently grants zero tabs or website access.";
      MessageBoxW(owner,text.c_str(),L"AGI-BROWSE native transport identity",MB_OK);return true;
    }
    if(event.windows_key_code=='R') {
      *shortcut=true;if(!pairing_authority)return true;
      for(const auto& client:pairing_authority->Clients()) {
        std::wstring text=L"Revoke this client certificate?\nSHA256: ";text.append(client.client.begin(),client.client.end());
        if(MessageBoxW(owner,text.c_str(),L"AGI-BROWSE native revocation",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)==IDYES)try{pairing_authority->Revoke(client.client);}catch(...){broker_channel.Stop();MessageBoxW(owner,L"Session permissions revoked; agent transport stopped. If the protected revocation marker could not be saved, old pairing may remain on disk. Recover and re-pair locally before restarting transport.",L"AGI-BROWSE",MB_OK|MB_ICONERROR);}
      }return true;
    }
#endif
    return false;
  }
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
      Record(Event::renderer_identity_rejected);return true;
    }
    auto args=message->GetArgumentList();
    if(privacy_test && frame->IsMain() && message->GetName()=="agi.test.privacy.result.v1" &&
       args->GetSize()==1 && args->GetType(0)==VTYPE_BOOL && args->GetBool(0) &&
       console_seen_ && privacy_dialog_seen) {
      privacy_test_passed=true;Record(Event::privacy_fixture_paths_exercised);return true;
    }
    // The renderer lane never calls the broker decoder/admission/dispatch.
    // Only host-requested diagnostics have a closed non-authorizing response.
    if(renderer_security_test && message->GetName()=="agi.test.result.v1" && args->GetSize()==2 && args->GetType(0)==VTYPE_BOOL && args->GetType(1)==VTYPE_BOOL && args->GetBool(0) && args->GetBool(1) && denied_[key]==8) {
      renderer_security_passed=true;Record(Event::renderer_application_escape_blocked);Record(Event::renderer_private_handles_absent);Record(Event::page_native_api_absent);return true;
    }
    // A future semantic lane is unavailable until its closed schema exists.
    auto& denied=denied_[key];
    if(denied<8) { ++denied;Record(Event::renderer_privileged_message_rejected); }
    return true;
  }

  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    CEF_REQUIRE_UI_THREAD();
    ++browser_count;
    auto reserved=tabs.FindTab(tab_);
    if(!reserved || (reserved->deadline && GetTickCount64()>=reserved->deadline) ||
        closing_windows.contains(reserved->window) || (!reserved->opener.empty() && !tabs.Resolve(reserved->opener))) {
      CancelReservation(tab_);browser->GetHost()->CloseBrowser(true);return;
    }
    if (!tabs.Bind(tab_,browser->GetIdentifier())) { browser->GetHost()->CloseBrowser(true); return; }
    for(auto it=pending_popups.begin();it!=pending_popups.end();) {
      if(it->second==tab_)it=pending_popups.erase(it);else ++it;
    }
    Record(Event::browser_created, browser->GetIdentifier());
  }

  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
    CEF_REQUIRE_UI_THREAD();
    auto view=tab_views.find(tab_);auto tab=tabs.FindTab(tab_);auto window=tab?tab->window:std::string();
    for(const auto& pending:tabs.PendingFrom(tab_))CancelReservation(pending);
    tabs.BeginClose(tab_);
    std::erase_if(frames_,[&](const auto& entry){return entry.first.first==browser->GetIdentifier();});
    std::erase_if(denied_,[&](const auto& entry){return entry.first.first==browser->GetIdentifier();});
    if(view!=tab_views.end()) {
      if(view->second->GetWindow())view->second->GetWindow()->RemoveChildView(view->second);
      tab_views.erase(view);
    }
    tabs.FinishClose(tab_);ShowActive(window);
    tab_titles.erase(tab_);
    if(auto w=tabs.FindWindow(window);w && w->tabs.empty()) {
      auto native=native_windows.find(window);if(native!=native_windows.end())native->second->Close();
    }
    Record(Event::browser_closed, browser->GetIdentifier());
    --browser_count;MaybeQuit();
  }

  bool DoClose(CefRefPtr<CefBrowser>) override {
    CEF_REQUIRE_UI_THREAD();
    // Alloy's default would close the entire shared native window. Destruction
    // of this BrowserView instead destroys only this tab's native child widget.
    tabs.BeginClose(tab_);
    auto view=tab_views.find(tab_);
    if(view!=tab_views.end()) {auto keep=view->second;auto window=keep->GetWindow();if(window)window->RemoveChildView(keep);tab_views.erase(view);}
    return true;
  }

  bool OnBeforePopup(CefRefPtr<CefBrowser> browser,CefRefPtr<CefFrame>,int popup_id,
      const CefString&,const CefString&,WindowOpenDisposition,bool,const CefPopupFeatures&,
      CefWindowInfo&,CefRefPtr<CefClient>& client,CefBrowserSettings&,CefRefPtr<CefDictionaryValue>&,bool*) override {
    CEF_REQUIRE_UI_THREAD();auto opener=tabs.Resolve(tab_);if(!opener)return true;
    auto window=tabs.NewWindow();if(window.empty())return true;
    auto tab=tabs.CreateTab(window,opener->profile,tab_,GetTickCount64()+kCreationTimeoutMs);
    if(tab.empty()){tabs.RemoveWindow(window);return true;}
    if(!pending_popups.emplace(std::make_pair(browser->GetIdentifier(),popup_id),tab).second){CancelReservation(tab);return true;}
    client=new BrowserClient(tab);return false;
  }
  void OnBeforePopupAborted(CefRefPtr<CefBrowser> browser,int popup_id) override {
    CEF_REQUIRE_UI_THREAD();auto p=pending_popups.find({browser->GetIdentifier(),popup_id});if(p==pending_popups.end())return;
    auto id=p->second;CancelReservation(id);
  }

  void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) override {
    CEF_REQUIRE_UI_THREAD();
    if (title == "AGI-BROWSE fixture ready") {
      fixture_ready = true;
      Record(Event::fixture_ready);
    }
    auto tab=tabs.Resolve(tab_);if(!tab)return;
    tab_titles[tab_]=title.ToString().substr(0,4096);
    auto view = CefBrowserView::GetForBrowser(browser);
    if (view && view->GetWindow() && tabs.FindWindow(tab->window)->active==tab_) {
      view->GetWindow()->SetTitle(title);
      if(privacy_test && title!="Privacy fixture loading" && title!="AGI-BROWSE fixture ready" &&
         view->GetWindow()->GetTitle()==title) Record(Event::privacy_human_title_preserved);
    }
  }

  void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                 CefRefPtr<CefFrame> frame,
                 int http_status_code) override {
    CEF_REQUIRE_UI_THREAD();
    if (frame->IsMain()) {
      Record(Event::main_frame_loaded, http_status_code);
      if(privacy_test) frame->SendProcessMessage(PID_RENDERER,CefProcessMessage::Create("agi.test.privacy.probe.v1"));
      if(renderer_security_test) {
        const auto endpoints=broker_channel.EndpointDiagnosticsForTest();
        if(endpoints.size()!=2) { load_failed=true;Record(Event::private_endpoint_diagnostics_failed);return; }
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
      Record(Event::main_frame_load_failed);
    }
  }

 private:
  const std::string tab_;
  bool console_seen_ = false;
  std::map<std::pair<int,std::string>,CefRefPtr<CefFrame>> frames_;
  std::map<std::pair<int,std::string>,unsigned> denied_;
  IMPLEMENT_REFCOUNTING(BrowserClient);
};

class WindowDelegate final : public CefWindowDelegate {
 public:
  WindowDelegate(CefRefPtr<CefBrowserView> view,std::string window) : view_(view),window_(std::move(window)) {}

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    native_windows[window_]=window;
    window->SetToFillLayout();
    if(view_)window->AddChildView(view_);
    window->SetTitle("AGI-BROWSE");
    window->Show();
    if(view_)view_->RequestFocus();
    view_=nullptr;
    Record(Event::window_created, reinterpret_cast<unsigned long long>(window->GetWindowHandle()));
  }

  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override {
    Record(Event::window_destroyed);
    view_ = nullptr;
    native_windows.erase(window_);tabs.RemoveWindow(window_);
    closing_windows.erase(window_);MaybeQuit();
  }

  bool CanClose(CefRefPtr<CefWindow> window) override {
    auto w=tabs.FindWindow(window_);if(!w)return true;
    closing_windows.insert(window_);
    auto ids=w->tabs;bool ready=true;
    for(const auto& id:ids) {
      auto tab=tabs.FindTab(id);
      if(tab && !tab->engine){CancelReservation(id);continue;}
      auto view=tab_views.find(id);if(view==tab_views.end()){ready=false;continue;}
      auto browser=view->second->GetBrowser();
      if(browser) {if(!tabs.BeginClose(id) || !browser->GetHost()->TryCloseBrowser())ready=false;}
      else ready=false;
    }
    return ready;
  }
  cef_runtime_style_t GetWindowRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }

  CefSize GetPreferredSize(CefRefPtr<CefView> view) override {
    return CefSize(1100, 760);
  }

 private:
  CefRefPtr<CefBrowserView> view_;
  const std::string window_;
  IMPLEMENT_REFCOUNTING(WindowDelegate);
};

void ShowActive(const std::string& window) {
  auto w=tabs.FindWindow(window);if(!w)return;
  for(const auto& id:w->tabs) {
    auto view=tab_views.find(id);if(view==tab_views.end())continue;
    view->second->SetVisible(id==w->active);
    if(id==w->active) {
      view->second->RequestFocus();auto native=view->second->GetWindow();
      if(native){auto title=tab_titles.find(id);native->SetTitle(title==tab_titles.end()?CefString("AGI-BROWSE"):title->second);}
    }
  }
}
void CreateNativeWindow(CefRefPtr<CefBrowserView> view,const std::string& window) {
  CefWindow::CreateTopLevelWindow(new WindowDelegate(view,window));
}
void CancelReservation(const std::string& id) {
  auto tab=tabs.FindTab(id);if(!tab || tab->engine)return;
  auto window=tab->window;
  if(!tabs.FinishClose(id))return;
  std::erase_if(pending_popups,[&](const auto& entry){return entry.second==id;});
  auto view=tab_views.find(id);
  if(view!=tab_views.end()) {auto keep=view->second;auto native=keep->GetWindow();if(native)native->RemoveChildView(keep);tab_views.erase(view);}
  ShowActive(window);
  if(auto w=tabs.FindWindow(window);w && w->tabs.empty()) {
    auto native=native_windows.find(window);
    if(native!=native_windows.end() && !closing_windows.contains(window))native->second->Close();
    else if(native==native_windows.end())tabs.RemoveWindow(window);
  }
  MaybeQuit();
}
bool ReorderTab(const std::string& id,size_t index) {
  auto tab=tabs.Resolve(id);auto view=tab_views.find(id);if(!tab || view==tab_views.end())return false;
  auto native=view->second->GetWindow();if(!native || !tabs.Reorder(id,index))return false;
  const auto& order=tabs.FindWindow(tab->window)->tabs;
  // Pending tabs need not have attached child Views yet.
  int child=0;for(const auto& target:order){auto item=tab_views.find(target);if(item!=tab_views.end() && item->second->GetWindow() && item->second->GetWindow()->IsSame(native))native->ReorderChildView(item->second,child++);}
  native->Layout();return true;
}
bool MoveTab(const std::string& id,const std::string& window) {
  auto tab=tabs.Resolve(id);auto item=tab_views.find(id);auto target=native_windows.find(window);
  if(!tab || item==tab_views.end() || target==native_windows.end() || closing_windows.contains(window))return false;
  auto source_id=tab->window;auto keep=item->second;auto source=keep->GetWindow();
  if(!source || source_id==window || !keep->GetBrowser())return false;
  const int engine=keep->GetBrowser()->GetIdentifier();
  source->RemoveChildView(keep);target->second->AddChildView(keep);
  auto attached=keep->GetWindow();
  if(!attached || !attached->IsSame(target->second) || !keep->GetBrowser() || keep->GetBrowser()->GetIdentifier()!=engine || !tabs.Move(id,window,tabs.FindWindow(window)->tabs.size())) {
    if(attached)attached->RemoveChildView(keep);source->AddChildView(keep);ShowActive(source_id);return false;
  }
  ShowActive(source_id);ShowActive(window);source->Layout();target->second->Layout();target->second->Activate();
  if(tabs.FindWindow(source_id)->tabs.empty())source->Close();
  return true;
}
class PendingCreationSweep final : public CefTask {
 public:
  void Execute() override {
    CEF_REQUIRE_UI_THREAD();
    for(const auto& id:tabs.PendingExpired(GetTickCount64()))CancelReservation(id);
    if(tabs.tab_count() || browser_count)CefPostDelayedTask(TID_UI,this,250);
  }
 private:
  IMPLEMENT_REFCOUNTING(PendingCreationSweep);
};
class TabViewDelegate final : public CefBrowserViewDelegate {
 public:
  cef_runtime_style_t GetBrowserRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }
  void OnBrowserCreated(CefRefPtr<CefBrowserView> view,CefRefPtr<CefBrowser> browser) override {
    CEF_REQUIRE_UI_THREAD();auto tab=tabs.ForEngine(browser->GetIdentifier());if(!tab)return;
    tab_views[tab->id]=view;
    auto window=native_windows.find(tab->window);
    if(window!=native_windows.end() && !view->GetWindow())window->second->AddChildView(view);
    ShowActive(tab->window);
  }
  bool OnPopupBrowserViewCreated(CefRefPtr<CefBrowserView>,CefRefPtr<CefBrowserView> popup,bool) override {
    CEF_REQUIRE_UI_THREAD();auto tab=tabs.ForEngine(popup->GetBrowser()->GetIdentifier());
    if(!tab){popup->GetBrowser()->GetHost()->CloseBrowser(true);return true;}
    CreateNativeWindow(popup,tab->window);return true;
  }
 private:
  IMPLEMENT_REFCOUNTING(TabViewDelegate);
};
std::string CreateTab(const std::string& window,CefRefPtr<CefRequestContext> context,const std::string& profile) {
  if(closing_windows.contains(window))return {};
  auto tab=tabs.CreateTab(window,profile,{},GetTickCount64()+kCreationTimeoutMs);if(tab.empty())return {};
  CefBrowserSettings settings;
  auto view=CefBrowserView::CreateBrowserView(new BrowserClient(tab),"about:blank",settings,nullptr,context,new TabViewDelegate);
  if(!view){CancelReservation(tab);return {};}
  tab_views[tab]=view;
  auto native=native_windows.find(window);if(native!=native_windows.end())native->second->AddChildView(view);
  ShowActive(window);return tab;
}
void RequestCloseTab(const std::string& id) {
  auto tab=tabs.Resolve(id);auto item=tab_views.find(id);if(!tab || item==tab_views.end())return;
  auto browser=item->second->GetBrowser();if(!browser)return;
  auto window=tab->window;
  if(tabs.FindWindow(window)->tabs.size()==1 && CreateTab(window,browser->GetHost()->GetRequestContext(),tab->profile).empty())return;
  if(tabs.BeginClose(id)){ShowActive(window);browser->GetHost()->CloseBrowser(false);}
}
class TabLifecycleFixture final : public CefTask {
 public:
  void Execute() override {
    CEF_REQUIRE_UI_THREAD();
    if(GetTickCount64()>deadline_){Fail();return;}
    auto root=tabs.Resolve(test_root);auto root_view=tab_views.find(test_root);
    if(stage_>0 && stage_<4 && (!root || root_view==tab_views.end())){Fail();return;}
    if(stage_==0) {
      if(!fixture_ready || !root || root_view==tab_views.end() || !root_view->second->IsDrawn()) {Again();return;}
      auto browser=root_view->second->GetBrowser();if(!browser){Again();return;}
      engine_=browser->GetIdentifier();context_=browser->GetHost()->GetRequestContext();source_=root->window;
      CefMouseEvent click;click.x=60;click.y=20;
      browser->GetHost()->SendMouseClickEvent(click,MBT_LEFT,false,1);
      browser->GetHost()->SendMouseClickEvent(click,MBT_LEFT,true,1);
      stage_=1;
    } else if(stage_==1) {
      std::string popup;
      for(const auto& [id,view]:tab_views) {auto tab=tabs.Resolve(id);if(tab && tab->opener==test_root && tab->engine && view->GetWindow())popup=id;}
      if(popup.empty()){Again();return;}
      auto browser=tab_views.at(popup)->GetBrowser();auto tab=tabs.Resolve(popup);
      if(!browser || tab->profile!=root->profile || !context_->IsSame(browser->GetHost()->GetRequestContext())){Fail();return;}
      Record(Event::tab_fixture_popup_registered);
      a_=CreateTab(source_,context_,root->profile);b_=CreateTab(source_,context_,root->profile);
      if(a_.empty() || b_.empty()){Fail();return;}stage_=2;
    } else if(stage_==2) {
      auto a=tabs.Resolve(a_),b=tabs.Resolve(b_);if(!a || !b || !a->engine || !b->engine){Again();return;}
      if(!ReorderTab(test_root,2) || tabs.ForEngine(engine_)->id!=test_root){Fail();return;}
      auto native=native_windows.at(source_);
      if(native->GetChildViewCount()!=3 || !native->GetChildViewAt(2)->IsSame(root_view->second)){Fail();return;}
      tabs.Activate(a_);ShowActive(source_);
      auto active_title=tab_titles.find(a_);
      if(!tab_views.at(a_)->IsVisible() || root_view->second->IsVisible() ||
          native->GetTitle()!=(active_title==tab_titles.end()?CefString("AGI-BROWSE"):active_title->second)){Fail();return;}
      RequestCloseTab(b_);stage_=3;
    } else if(stage_==3) {
      if(tabs.FindTab(b_)){Again();return;}
      if(!tabs.Resolve(a_) || !native_windows.contains(source_) || !tab_views.at(a_)->IsDrawn()){Fail();return;}
      Record(Event::tab_fixture_order_verified);
      destination_=tabs.NewWindow();if(destination_.empty()){Fail();return;}
      CreateNativeWindow(nullptr,destination_);
      if(!MoveTab(test_root,destination_)){Fail();return;}
      auto browser=root_view->second->GetBrowser();auto native=root_view->second->GetWindow();
      if(!browser || browser->GetIdentifier()!=engine_ || !context_->IsSame(browser->GetHost()->GetRequestContext()) ||
          !native || !native->IsSame(native_windows.at(destination_)) || native->GetTitle()!=tab_titles.at(test_root) || tabs.ForEngine(engine_)->id!=test_root || tabs.Resolve(test_root)->profile!="human") {Fail();return;}
      Record(Event::tab_fixture_move_verified);
      test_cancel_unload=true;RequestCloseTab(test_root);stage_=4;
    } else if(stage_==4) {
      if(!test_unload_canceled){Again();return;}
      root=tabs.Resolve(test_root);
      if(!root || root_view==tab_views.end() || root->engine!=engine_ || !root_view->second->IsVisible() || tabs.FindWindow(destination_)->tabs.size()!=2){Fail();return;}
      Record(Event::tab_fixture_cancel_verified);
      RequestCloseTab(test_root);stage_=5;
    } else if(stage_==5) {
      if(tabs.FindTab(test_root)){Again();return;}
      auto w=tabs.FindWindow(destination_);
      if(test_unload_canceled!=1 || test_unload_accepted!=1 || tab_views.contains(test_root) || !w || w->tabs.size()!=1 || !tabs.Resolve(w->active) || !tabs.Resolve(a_)) {Fail();return;}
      auto blank=tab_views.find(w->active);
      if(blank==tab_views.end() || !blank->second->GetBrowser() || !blank->second->IsDrawn()){Again();return;}
      Record(Event::tab_fixture_close_verified);
      pending_=tabs.CreateTab(destination_,"human",{},GetTickCount64()+300);stage_=6;
    } else if(stage_==6) {
      if(tabs.FindTab(pending_)){Again();return;}
      auto w=tabs.FindWindow(destination_);
      if(!w || tab_views.contains(pending_) || !tabs.Resolve(w->active) || !tab_views.at(w->active)->IsDrawn()){Fail();return;}
      Record(Event::tab_fixture_pending_expired);stage_=7;hold_until_=GetTickCount64()+5000;Again();return;
    } else if(stage_==7) {
      // Observation hold only: renderer-token inspection gets five seconds;
      // beforeunload evidence already came from the actual callback above.
      if(GetTickCount64()<hold_until_){Again();return;}
      tab_lifecycle_passed=true;
      // Final native window shutdown uses CanClose, not the final-tab shortcut.
      std::vector<CefRefPtr<CefWindow>> windows;for(const auto& [id,window]:native_windows)windows.push_back(window);
      for(const auto& window:windows)window->Close();return;
    }
    Again();
  }
 private:
  void Again(){CefPostDelayedTask(TID_UI,this,100);}
  void Fail(){load_failed=true;Record(Event::tab_fixture_failed);test_cancel_unload=false;std::vector<CefRefPtr<CefWindow>> windows;for(const auto& [id,window]:native_windows)windows.push_back(window);for(const auto& window:windows)window->Close();}
  uint64_t deadline_=GetTickCount64()+30000;
  uint64_t hold_until_=0;
  int stage_=0,engine_=0;
  std::string source_,destination_,a_,b_,pending_;
  CefRefPtr<CefRequestContext> context_;
  IMPLEMENT_REFCOUNTING(TabLifecycleFixture);
};

class RendererApp final : public CefApp,public CefRenderProcessHandler {
 public:
  CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override { return this; }
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,CefRefPtr<CefFrame> frame,CefProcessId source,CefRefPtr<CefProcessMessage> message) override {
    CEF_REQUIRE_RENDERER_THREAD();
    if(source==PID_BROWSER && message->GetName()=="agi.test.privacy.probe.v1" && message->GetArgumentList()->GetSize()==0) {
      auto context=frame->GetV8Context();bool proof=false;
      if(context && context->Enter()) {
        CefRefPtr<CefV8Value> value;CefRefPtr<CefV8Exception> exception;
        proof=context->Eval("Array.isArray(globalThis.privacyProof) && globalThis.privacyProof.length === 6 && globalThis.privacyProof.every(x => x === true)","agi-privacy-probe",0,value,exception) && value && value->IsBool() && value->GetBoolValue();
        context->Exit();
      }
      // The supported proof result carries only a closed boolean. The separate
      // forged canary below exercises the rejected unknown renderer lane.
      auto forged=CefProcessMessage::Create("agi.renderer.unsupported.privacy.v1");
      forged->GetArgumentList()->SetString(0,"SENTINEL_PASSWORD_010 PRIVATE_MARKED_NAME_010");
      frame->SendProcessMessage(PID_BROWSER,forged);
      auto result=CefProcessMessage::Create("agi.test.privacy.result.v1");
      result->GetArgumentList()->SetBool(0,proof);frame->SendProcessMessage(PID_BROWSER,result);return true;
    }
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
  void OnBeforeCommandLineProcessing(const CefString& process_type,
                                    CefRefPtr<CefCommandLine> command_line) override {
    if(process_type.empty()) {
      // Chromium's logging destination is independent of CEF's minimum level.
      // Disable the destination before Chrome initializes it, preventing both
      // default debug.log and inherited enable-logging=handle/log-file flags.
      // Source: pinned chrome/common/logging_chrome.cc GetLoggingDest.
      command_line->AppendSwitch("disable-logging");
    }
  }

  void OnContextInitialized() override {
    CEF_REQUIRE_UI_THREAD();
    Record(Event::context_initialized);
    CefBrowserSettings settings;
    auto window=tabs.NewWindow();auto tab=tabs.CreateTab(window,"human",{},GetTickCount64()+kCreationTimeoutMs);
    auto view = CefBrowserView::CreateBrowserView(new BrowserClient(tab), url_, settings,
                                                 nullptr, nullptr, new TabViewDelegate);
    if(!view){tabs.FinishClose(tab);tabs.RemoveWindow(window);CefQuitMessageLoop();return;}
    tab_views[tab]=view;CreateNativeWindow(view,window);
    if(tab_lifecycle_test){test_root=tab;CefPostDelayedTask(TID_UI,new TabLifecycleFixture,100);}
    CefPostDelayedTask(TID_UI,new PendingCreationSweep,250);
  }

 private:
  const std::string url_;
  IMPLEMENT_REFCOUNTING(BrowserApp);
};

bool UnsafeSwitches(CefRefPtr<CefCommandLine> args) {
  for (const auto* flag : {"no-sandbox", "disable-sandbox", "disable-gpu-sandbox",
                           "disable-web-security", "single-process", "in-process-gpu",
                           "remote-debugging-port", "remote-debugging-pipe",
                           "enable-logging", "log-file", "log-severity", "v", "vmodule",
                           "log-net-log", "net-log-capture-mode", "trace-startup",
                           "trace-startup-file", "trace-to-console", "enable-crash-reporter",
                           "crash-dumps-dir", "js-flags"}) {
    if (args->HasSwitch(flag)) {
      if (std::string_view(flag)=="log-severity" && args->HasSwitch("type") &&
          args->GetSwitchValue("log-severity")=="disable") continue;
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
  // Browser invocation is checked before CEF initializes logging. Engine child
  // commands inherit CEF's own log-severity switch from the trusted parent.
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
  Record(Event::sandbox_bootstrap_verified, ::GetCurrentProcessId());
  renderer_security_test=args->HasSwitch("ipc-renderer-test");
  privacy_test=args->HasSwitch("privacy-renderer-test");
  tab_lifecycle_test=args->HasSwitch("tab-lifecycle-test");
  wchar_t executable[32768]{};
  const DWORD executable_length=GetModuleFileNameW(nullptr,executable,32768);
  if(!executable_length || executable_length>=32768) return 70;
  std::wstring broker_path(executable);
  broker_path=broker_path.substr(0,broker_path.find_last_of(L"\\/"))+L"\\agi-browse-broker.exe";
#ifdef AGI_TRANSPORT
  std::shared_ptr<agi::ipc::NativeTransportAuthority> transport_authority;
  try {
    auto profile=args->GetSwitchValue("profile-dir").ToWString();
    if(profile.empty()) {wchar_t local[32768]{};auto n=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);if(!n||n>=32768)throw std::runtime_error("protected storage unavailable");profile=std::wstring(local)+L"\\AGI-BROWSE";}
    pairing_authority=std::make_shared<agi::transport::PairingAuthority>(profile+L"\\Transport\\authority.dpapi");host_transport_authority=std::make_shared<agi::transport::HostTransportAuthority>(pairing_authority);transport_authority=host_transport_authority;
  } catch(...) {Record(Event::agent_transport_unavailable);}
  if(transport_authority&&!broker_channel.Start(broker_path,transport_authority)) {
#else
  if(!broker_channel.Start(broker_path)) {
#endif
    Record(Event::private_broker_start_failed);if(lifecycle_log){std::fclose(lifecycle_log);lifecycle_log=nullptr;}return 70;
  }
  if(broker_channel.live())Record(Event::private_broker_challenge_verified,broker_channel.process_id());
  CefSettings settings;
  settings.no_sandbox = false;
  // Pinned cef_types.h: DISABLE prevents file logging; FATAL still writes
  // stderr. This does not promise suppression of crash/OS dumps or fatal data.
  settings.log_severity = LOGSEVERITY_DISABLE;
  settings.command_line_args_disabled = true;
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
    Record(Event::initialization_failed);
    if (lifecycle_log) { std::fclose(lifecycle_log); lifecycle_log = nullptr; }
    const int code = CefGetExitCode();
    return code == 0 ? 68 : code;
  }
  Record(Event::initialized);
  CefRunMessageLoop();
  Record(Event::message_loop_exited);
  CefShutdown();
  broker_channel.Stop();
  Record(Event::private_broker_stopped);
  Record(Event::shutdown_complete);
  app = nullptr;
  if (lifecycle_log) { std::fclose(lifecycle_log); lifecycle_log = nullptr; }
  return load_failed || (args->HasSwitch("require-fixture") && !fixture_ready) || (renderer_security_test && !renderer_security_passed) || (privacy_test && !privacy_test_passed) || (tab_lifecycle_test && !tab_lifecycle_passed) ? 69 : 0;
}
