#ifdef _WIN32
// Match the CEF host include order. The linked library translation unit does
// not include Windows, so accidental Win32 macro-renamed API symbols fail here.
#include <windows.h>
#endif
#include "apps/browser/lifecycle.h"
#include "apps/browser/popup_reservation.h"
#include "lib/ipc/scoped_authority.h"
#include <iostream>
#include <stdexcept>
#include <memory>
using agi::browser::Lifecycle;
using namespace agi::ipc;
unsigned checks=0;
void Check(bool value,const char* label) {++checks;if(!value)throw std::runtime_error(label);}
int main() {
 try {
  uint64_t now=1;ScopedAuthority scope([](const auto&){return true;},[&]{return now;});
  unsigned invalidations=0;
  Lifecycle life([&](const auto& profile,const auto& tab){++invalidations;scope.InvalidateNativeTab(profile,tab);});
  auto w=life.NewWindow(), other=life.NewWindow();
  auto a=life.CreateTab(w,"human"),b=life.CreateTab(w,"human",a);
  Check(!a.empty() && a!=b,"stable distinct reservations");
  Check(life.Bind(a,1) && life.Bind(b,2),"engine bind");
  Check(!life.Bind(b,1) && !life.Bind(a,3),"duplicate bind rejected");
  Check(life.Reorder(a,1) && life.LookupWindow(w)->tabs[1]==a && life.ForEngine(1)->id==a,"reorder preserves engine identity");
  Check(life.Move(a,other,0) && life.Resolve(a)->window==other && life.ForEngine(1)->id==a,"window move preserves engine identity");
  Check(!life.Move(a,"missing",0) && !life.Reorder(a,7),"bad placement unchanged");
  Check(life.Activate(b) && life.LookupWindow(w)->active==b,"activation");
  Check(life.CreateTab(w,"agent",b).empty(),"popup profile mismatch rejected");
  Check(scope.BindAuthenticatedSession("client","session"),"session binding");
  Destination da{"human",a,"top","doc"},db{"human",b,"top","doc"};
  Check(scope.RegisterNativeDocument({da,ContextKind::origin,"https://test",true}) && scope.RegisterNativeDocument({db,ContextKind::origin,"https://test",true}),"two documents");
  ScopedGrant grant{1,"grant","client","session","human",{a,b},{"observe","click"},{{ContextKind::origin,"https://test",FramePolicy::top}},true,1,1000};
  Check(scope.ApproveNativeGrant(grant,now),"native test grant");
  Check(scope.AcquireNativeLease("client","session",a,now),"lease");
  auto read=scope.QueueScopeCheck({"client","session","observe",da},now);
  auto action=scope.QueueScopeCheck({"client","session","click",da},now);
  auto unaffected=scope.QueueScopeCheck({"client","session","observe",db},now);
  Check(read && action && unaffected,"queued read and action");
  Check(life.BeginClose(a) && !life.Resolve(a) && !life.ForEngine(1),"close immediately invalidates handles");
  Check(!scope.WithCurrentScope(read,[]{}) && !scope.WithCurrentScope(action,[]{}),"close consumes queued read and action");
  Check(scope.WithCurrentScope(unaffected,[]{}),"other tab unaffected");
  Check(!scope.QueueScopeCheck({"client","session","observe",da},now),"subsequent read denied");
  Check(scope.RegisterNativeDocument({da,ContextKind::origin,"https://test",true}),"test-only re-registration");
  Check(!scope.QueueScopeCheck({"client","session","observe",da},now),"closed tab removed from existing native grant");
  Check(!scope.QueueScopeCheck({"client","session","click",da},now),"old lease removed");
  Check(life.CancelClose(a) && life.Resolve(a) && life.Activate(a),"cancel close restores human lifecycle");
  Check(!scope.QueueScopeCheck({"client","session","observe",da},now),"cancel never revives scope");
  Check(life.BeginClose(a) && life.BeginClose(a) && invalidations==2,"close idempotence after cancel");
  Check(life.FinishClose(a) && life.RemoveWindow(other),"resource release");
  auto c=life.CreateTab(w,"human");Check(c!=a && life.Bind(c,1) && !life.Resolve(a) && life.ForEngine(1)->id==c,"engine reuse never revives old handle");
  auto pending=life.CreateTab(w,"human");Check(life.FinishClose(pending) && !life.Resolve(pending),"pending reservation canceled");
  Check(!life.RemoveWindow(w),"live window not released");
  Check(life.FinishClose(b) && life.FinishClose(c) && life.RemoveWindow(w) && life.tab_count()==0,"teardown complete");
  Lifecycle bound;auto bw=bound.NewWindow();for(unsigned i=0;i<64;++i)Check(!bound.CreateTab(bw,"human").empty(),"reservation admitted");
  Check(bound.CreateTab(bw,"human").empty(),"pending plus live tabs bounded");
  for(unsigned i=1;i<32;++i)Check(!bound.NewWindow().empty(),"window admitted");
  Check(bound.NewWindow().empty(),"windows bounded");
  Lifecycle deadlines;auto dw=deadlines.NewWindow();auto opener=deadlines.CreateTab(dw,"human");Check(deadlines.Bind(opener,7),"deadline opener bound");
  auto due=deadlines.CreateTab(dw,"human",opener,100),later=deadlines.CreateTab(dw,"human",opener,200);
  Check(deadlines.PendingExpired(99).empty() && deadlines.PendingExpired(100)==std::vector<std::string>{due},"pending deadline exact boundary");
  Check(deadlines.PendingFrom(opener).size()==2,"opener pending reservations located");
  Check(deadlines.FinishClose(due) && !deadlines.Bind(due,8),"expired reservation rejects late creation");
  Check(deadlines.Bind(later,8) && deadlines.PendingExpired(300).empty() && deadlines.PendingFrom(opener).empty(),"bound popup no longer pending");
  unsigned calls=0;Lifecycle failing([&](const auto&,const auto&){if(++calls==1)throw std::runtime_error("native barrier failed");});
  auto fw=failing.NewWindow(), ft=failing.CreateTab(fw,"human");Check(failing.Bind(ft,9),"throwing barrier fixture bound");
  Check(!failing.BeginClose(ft) && !failing.Resolve(ft) && !failing.CancelClose(ft),"barrier exception contained and handle fails closed");
  Check(failing.BeginClose(ft) && calls==2 && failing.CancelClose(ft),"failed barrier retried before human cancellation");
  Check(failing.FinishClose(ft) && failing.RemoveWindow(fw),"throwing barrier fixture eventually released");
  unsigned repeated=0;Lifecycle repeat([&](const auto&,const auto&){if(++repeated==2)throw std::runtime_error("second attempt fails");});
  auto rw=repeat.NewWindow(),rt=repeat.CreateTab(rw,"human");Check(repeat.Bind(rt,10),"repeated barrier fixture bound");
  Check(repeat.BeginClose(rt) && repeat.CancelClose(rt),"successful first barrier canceled");
  Check(!repeat.BeginClose(rt) && repeated==2 && !repeat.CancelClose(rt),"new attempt cannot reuse former barrier result");
  Check(repeat.BeginClose(rt) && repeated==3,"new failed barrier actually retried");
  // Actual pinned Alloy order: popup Views callback arrives while the native
  // reservation has no engine; binding follows successful View attachment.
  Lifecycle popup_life;auto parent_window=popup_life.NewWindow();
  auto parent=popup_life.CreateTab(parent_window,"human");Check(popup_life.Bind(parent,41),"popup opener bound");
  auto popup_window=popup_life.NewWindow();auto popup=popup_life.CreateTab(popup_window,"human",parent,100);
  struct NativeClient {};
  struct Reservation {std::string tab;std::shared_ptr<NativeClient> client;};
  auto popup_client=std::make_shared<NativeClient>(),wrong_client=std::make_shared<NativeClient>();
  std::map<int,Reservation> reservations{{1,{popup,popup_client}}};
  Check(!popup_life.ForEngine(42) && popup_life.FindTab(popup)->engine==0,"pre-creation callback has no engine binding");
  auto resolve=[&](NativeClient* client,int engine=41,uint64_t tick=1){return agi::browser::ResolvePopupReservation(popup_life,reservations,client,engine,tick);};
  Check(resolve(popup_client.get())==popup,"host client identifies unbound popup before engine binding");
  Check(resolve(wrong_client.get()).empty() && resolve(nullptr).empty(),"unknown and null clients cannot borrow popup reservation");
  Check(resolve(popup_client.get(),99).empty(),"wrong opener engine cannot borrow popup reservation");
  Check(resolve(popup_client.get(),41,100).empty(),"expired popup cannot attach from stale native callback");
  reservations.emplace(2,Reservation{popup,popup_client});
  Check(resolve(popup_client.get()).empty(),"ambiguous duplicated client identity rejected");reservations.erase(2);
  Check(popup_life.Bind(popup,42) && resolve(popup_client.get()).empty(),"real creation binds once after pre-bind popup resolution");
  Check(popup_life.FinishClose(popup) && resolve(popup_client.get()).empty(),"canceled popup cannot be recovered by retained client");
  reservations.clear();Check(resolve(popup_client.get()).empty(),"removed reservation cannot be recovered by client identity");
  auto late=popup_life.CreateTab(popup_window,"human",parent,100);reservations.emplace(3,Reservation{late,popup_client});
  Check(popup_life.FinishClose(late) && resolve(popup_client.get()).empty(),"canceled unbound popup cannot attach or bind");
  late=popup_life.CreateTab(popup_window,"human",parent,100);reservations.at(3).tab=late;
  Check(popup_life.BeginClose(parent) && resolve(popup_client.get()).empty(),"closing opener cannot attach a pending popup");
  std::cout<<checks<<" lifecycle checks passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
