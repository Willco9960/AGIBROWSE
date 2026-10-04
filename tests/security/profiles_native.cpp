#include "apps/browser/profiles.h"
#include "lib/ipc/scoped_authority.h"
#include <windows.h>
#include <winioctl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
using agi::browser::ProfileStore;
namespace {
unsigned checks=0;
void Check(bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(name);std::cout<<"PASS "<<name<<'\n';}
void ReparseWriteDenied(const std::filesystem::path& path) {
  // This handle otherwise supplies exactly the access needed by
  // FSCTL_SET_REPARSE_POINT while sharing the existing pin's read access.
  auto handle=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
    nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);
  const auto error=GetLastError();if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);
  Check(handle==INVALID_HANDLE_VALUE&&error==ERROR_SHARING_VIOLATION,"pinned directory denies junction-mutation write handle");
}
void Junction(const std::filesystem::path& link,const std::filesystem::path& target) {
  std::filesystem::create_directory(link);
  auto handle=CreateFileW(link.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);
  Check(handle!=INVALID_HANDLE_VALUE,"fixture junction handle");
  struct Header {DWORD tag;WORD size,reserved,sub_offset,sub_length,print_offset,print_length;};
  auto substitute=L"\\??\\"+target.wstring(),print=target.wstring();
  const size_t bytes=sizeof(Header)+(substitute.size()+print.size()+2)*sizeof(wchar_t);
  std::vector<unsigned char> buffer(bytes);auto h=reinterpret_cast<Header*>(buffer.data());h->tag=IO_REPARSE_TAG_MOUNT_POINT;
  h->size=static_cast<WORD>(bytes-8);h->sub_length=static_cast<WORD>(substitute.size()*sizeof(wchar_t));
  h->print_offset=static_cast<WORD>((substitute.size()+1)*sizeof(wchar_t));h->print_length=static_cast<WORD>(print.size()*sizeof(wchar_t));
  memcpy(buffer.data()+sizeof(Header),substitute.c_str(),(substitute.size()+1)*sizeof(wchar_t));
  memcpy(buffer.data()+sizeof(Header)+h->print_offset,print.c_str(),(print.size()+1)*sizeof(wchar_t));DWORD returned=0;
  bool ok=DeviceIoControl(handle,FSCTL_SET_REPARSE_POINT,buffer.data(),static_cast<DWORD>(bytes),nullptr,0,&returned,nullptr)!=0;CloseHandle(handle);Check(ok,"fixture junction created");
}
}
int main() {
  const auto fixture=std::filesystem::temp_directory_path()/(L"agi-profile-test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
  try {
    Check(!std::filesystem::exists(fixture),"fresh narrowly named temporary fixture");std::filesystem::create_directory(fixture);
    const auto root=fixture/L"root",outside=fixture/L"outside";std::filesystem::create_directory(outside);std::ofstream(outside/L"sentinel")<<"preserve";
    const std::string legacy_id="p-00000000000000000000000000000000";
    std::filesystem::create_directories(root/L"Profiles"/legacy_id);
    std::ofstream(root/L"Profiles"/legacy_id/L"profile.identity")<<"AGI-BROWSE profile v1\n"<<legacy_id<<'\n';
    std::ofstream(root/L"Profiles"/L"sentinel")<<"preserve";
    std::string saved,retired;
    {
      ProfileStore store(root);
      Check(store.Find("human")->cache==root/L"Default","legacy human Default preserved");
      Check(store.Find("agent")->cache!=store.Find("human")->cache,"agent storage separate");
      Check(store.Find("agent")->cache==root/L"Agent","agent persistent cache is a direct root child");
      Check(store.Find("human")->cache.parent_path()==root,"human persistent cache is a direct root child");
      Check(!store.Find(legacy_id)&&std::filesystem::exists(root/L"Profiles"/legacy_id/L"profile.identity")&&std::filesystem::exists(root/L"Profiles"/L"sentinel"),"unsupported nested layout remains untouched and undiscovered");
      ReparseWriteDenied(store.Find("human")->cache);
      Check(!store.Find("../human")&&!store.Find("renderer-supplied"),"unknown profile identity denied");
      Check(!store.SitePermissionAllowed("human")&&!store.SitePermissionAllowed("agent")&&!store.SitePermissionAllowed("invented"),"all site permissions start denied");
      saved=store.CreateHuman();auto disposable=store.CreateHuman();Check(!saved.empty()&&!disposable.empty()&&saved!=disposable,"native random profiles created");
      Check(store.Find(saved)->cache==root/saved&&store.Find(disposable)->cache.parent_path()==root,"created persistent caches are direct root children");
      Check(store.MarkContextOpened(saved)&&!store.CanDelete(saved),"opened context prevents deletion until restart");
      ReparseWriteDenied(store.Find(saved)->cache);
      auto invalid=store.CreateHuman();std::ofstream(store.Find(invalid)->cache/L"profile.identity",std::ios::trunc)<<"invalid";
      Check(!store.MarkContextOpened(invalid)&&!store.MarkContextOpened(invalid)&&!store.Find(invalid),"failed identity transition stays denied on retry");
      Check(!store.DeleteConfirmed(saved,true,[](const auto&){}),"opened profile cannot delete");
      Check(!store.DeleteConfirmed("human",true,[](const auto&){} )&&!store.DeleteConfirmed("agent",true,[](const auto&){}),"built-in profiles protected");
      Check(!store.DeleteConfirmed(disposable,false,[](const auto&){}),"explicit confirmation required");
      Check(!store.DeleteConfirmed(disposable,true,{}),"revocation barrier required");
      auto cache=store.Find(disposable)->cache;std::filesystem::create_directory(cache/L"nested");std::ofstream(cache/L"nested"/L"data")<<"fixture";
      unsigned revoked=0;Check(store.DeleteConfirmed(disposable,true,[&](const auto& id){Check(id==disposable,"exact profile revocation");++revoked;}),"temporary never-opened profile deletion");
      Check(revoked==1&&!store.Find(disposable)&&!std::filesystem::exists(cache),"exact selected profile removed");
      Check(store.Find(saved)&&std::filesystem::exists(outside/L"sentinel")&&std::filesystem::exists(root/L"Default"),"siblings and human data untouched");
      retired=store.CreateHuman();auto partial=store.Find(retired)->cache;std::ofstream(partial/L"busy")<<"fixture";
      auto busy=CreateFileW((partial/L"busy").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);Check(busy!=INVALID_HANDLE_VALUE,"fixture sharing lock");
      Check(!store.DeleteConfirmed(retired,true,[](const auto&){}),"busy entry refuses unsafe cleanup");CloseHandle(busy);
      Check(!store.Find(retired)&&std::filesystem::exists(partial/L"deletion.pending"),"failed cleanup durably retires native identity");
      auto linked=store.CreateHuman();auto path=store.Find(linked)->cache;Junction(path/L"redirect",outside);
      Check(!store.DeleteConfirmed(linked,true,[](const auto&){}),"reparse descendant deletion denied");
      Check(std::filesystem::exists(outside/L"sentinel"),"outside sentinel survives refused deletion");Check(RemoveDirectoryW((path/L"redirect").c_str())!=0,"fixture junction removed without traversal");
      Check(!MoveFileW((root/L"Default").c_str(),(root/L"substituted").c_str()),"pinned profile root cannot be renamed");
    }
    {
      ProfileStore store(root);Check(store.Find(saved)&&store.CanDelete(saved),"closed profile discovered after restart");
      Check(store.Find(saved)->cache.parent_path()==root,"restart discovers only supported direct-child cache");
      Check(!store.Find(legacy_id)&&std::filesystem::exists(root/L"Profiles"/legacy_id/L"profile.identity"),"restart preserves and excludes unsupported nested identity");
      Check(!store.Find(retired),"partial cleanup cannot resurrect on restart");
      Check(store.DeleteConfirmed(saved,true,[](const auto&){}),"previously marked profile deletion after native restart");
    }
    Junction(fixture/L"alias",root);
    bool rejected=false;try{ProfileStore unsafe(fixture/L"alias");}catch(...){rejected=true;}
    Check(rejected,"junction profile root rejected");Check(RemoveDirectoryW((fixture/L"alias").c_str())!=0,"root junction removed without traversal");
    for(const auto& unsafe:{std::filesystem::path(L"relative"),std::filesystem::path(root.wstring()+L"\\..\\outside"),std::filesystem::path(L"\\\\localhost\\share"),std::filesystem::path(root.wstring()+L":stream")}) {
      rejected=false;try{ProfileStore store(unsafe);}catch(...){rejected=true;}Check(rejected,"noncanonical profile root rejected");
    }
    // Profile-wide revocation cancels only that profile's grants/tickets. It
    // never widens a grant to human when the default agent is invalidated.
    using namespace agi::ipc;uint64_t now=100;ScopedAuthority authority([](const auto& o){return o=="https://fixture.test";},[&]{return now;});
    for(const auto& id:{std::string("agent"),std::string("human")}) {
      Check(authority.BindAuthenticatedSession(id,"s-"+id),"native profile test session");
      Check(authority.RegisterNativeDocument({{id,"tab","top","doc"},ContextKind::origin,"https://fixture.test",true}),"native profile document");
      Check(authority.ApproveNativeGrant({1,"g-"+id,id,"s-"+id,id,{"tab"},{"observe"},{{ContextKind::origin,"https://fixture.test",FramePolicy::top}},false,1,1000},now),"exact native profile grant");
    }
    ScopeRequest agent{"agent","s-agent","observe",{"agent","tab","top","doc"}},human{"human","s-human","observe",{"human","tab","top","doc"}};
    auto ticket=authority.QueueScopeCheck(agent,now);Check(ticket!=0,"profile delivery admitted before retirement");authority.InvalidateNativeProfile("agent");
    Check(!authority.WithCurrentScope(ticket,[]{} )&&!authority.QueueScopeCheck(agent,now),"retired profile tickets and grants revoked");
    agent.destination.profile="human";Check(!authority.QueueScopeCheck(agent,now),"retirement cannot fall back to human authority");
    Check(authority.QueueScopeCheck(human,now)!=0,"other profile authority remains independent");
    Check(std::filesystem::equivalent(fixture.parent_path(),std::filesystem::temp_directory_path())&&fixture.filename().wstring().rfind(L"agi-profile-test-",0)==0,"cleanup stays within exact temporary fixture");
    std::filesystem::remove_all(fixture);std::cout<<"PASS profile isolation checks="<<checks<<'\n';return 0;
  }catch(const std::exception& e){std::cerr<<"FAIL profile isolation case: "<<e.what()<<'\n';return 1;}
}
