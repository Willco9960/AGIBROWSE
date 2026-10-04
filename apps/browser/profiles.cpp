#include "apps/browser/profiles.h"
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <fstream>
#include <stdexcept>

namespace agi::browser {
namespace {
using Path = std::filesystem::path;
struct Handle {
  HANDLE value = INVALID_HANDLE_VALUE;
  Handle() = default;
  explicit Handle(HANDLE h) : value(h) { if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("profile storage unavailable"); }
  ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
  Handle(Handle&& other) noexcept : value(other.value){other.value=INVALID_HANDLE_VALUE;}
  Handle& operator=(Handle&& other) noexcept {if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);value=other.value;other.value=INVALID_HANDLE_VALUE;return *this;}
  Handle(const Handle&)=delete;
};
bool UserId(const std::string& id) {
  return id.size()==34 && id.substr(0,2)=="p-" && id.find_first_not_of("0123456789abcdef",2)==std::string::npos;
}
Handle Lock(const Path& path, bool deleting=false) {
  // Deny write-sharing as well as delete-sharing: a writable directory handle
  // could otherwise set a reparse point without renaming this pinned object.
  Handle handle(CreateFileW(path.c_str(),GENERIC_READ|(deleting?DELETE:0),FILE_SHARE_READ,
    nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
  BY_HANDLE_FILE_INFORMATION info{};
  if(!GetFileInformationByHandle(handle.value,&info) || (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))
    throw std::runtime_error("unsafe profile storage");
  return handle;
}
void Directory(const Path& path) {
  if(!CreateDirectoryW(path.c_str(),nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS)throw std::runtime_error("profile storage unavailable");
  auto h=Lock(path);BY_HANDLE_FILE_INFORMATION info{};
  if(!GetFileInformationByHandle(h.value,&info)||!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))throw std::runtime_error("unsafe profile storage");
}
std::string Marker(const std::string& id){return "AGI-BROWSE profile v1\n"+id+"\n";}
bool Owned(const Path& path,const std::string& id) {
  try {
    if(GetFileAttributesW((path/L"deletion.pending").c_str())!=INVALID_FILE_ATTRIBUTES)return false;
    auto held=Lock(path/L"profile.identity");
    std::ifstream file(path/L"profile.identity",std::ios::binary);
    std::array<char,128> bytes{};file.read(bytes.data(),bytes.size());
    return file.eof() && std::string(bytes.data(),static_cast<size_t>(file.gcount()))==Marker(id);
  } catch(...) {return false;}
}
// Open every entry without delete-sharing before any destructive operation.
// Root and ancestors remain held, so rename/junction substitution cannot move
// these path-based enumerations outside the approved native root.
void Collect(const Path& path,std::vector<Handle>& handles,unsigned depth=0) {
  if(depth>32 || handles.size()+depth>=8190)throw std::runtime_error("profile deletion bound exceeded");
  auto h=Lock(path,true);BY_HANDLE_FILE_INFORMATION info{};
  if(!GetFileInformationByHandle(h.value,&info))throw std::runtime_error("profile storage unavailable");
  if(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) {
    for(const auto& child:std::filesystem::directory_iterator(path))Collect(child.path(),handles,depth+1);
  }
  handles.push_back(std::move(h));
}
}
struct ProfileStore::Handles { std::vector<Handle> ancestors; std::map<std::string,Handle> profiles; };
ProfileStore::ProfileStore(const Path& root):root_(root),handles_(std::make_unique<Handles>()) {
  const auto value=root.native();
  // No UNC/device roots, ADS, relative paths, dot traversal or normalization
  // aliases. CEF root and Transport always remain outside deletion targets.
  if(value.size()<3 || value[1]!=L':' || (value[2]!=L'\\'&&value[2]!=L'/') ||
      value.find(L':',2)!=std::wstring::npos || value.find(L'\0')!=std::wstring::npos)
    throw std::runtime_error("unsafe profile root");
  Path current=root.root_path();handles_->ancestors.push_back(Lock(current));
  for(const auto& part:root.relative_path()) {
    auto component=part.native();if(component.empty() || component==L"." || component==L".." || component.back()==L'.' || component.back()==L' ')
      throw std::runtime_error("unsafe profile root");
    current/=part;Directory(current);handles_->ancestors.push_back(Lock(current));
  }
  root_=current;
  // ChromeBrowserContext accepts only direct children of user_data_dir for
  // persistent profiles. Nested paths silently become off-the-record profiles
  // inheriting restrictions from Default. Leave old nested data untouched.
  Directory(root_/L"Default");Directory(root_/L"Agent");
  profiles_.emplace("human",Profile{"human",root_/L"Default"});
  profiles_.emplace("agent",Profile{"agent",root_/L"Agent"});
  handles_->profiles.emplace("human",Lock(root_/L"Default"));
  handles_->profiles.emplace("agent",Lock(root_/L"Agent"));
  unsigned visited=0;
  for(const auto& item:std::filesystem::directory_iterator(root_)) {
    if(++visited>128)throw std::runtime_error("profile bound exceeded");
    auto id=item.path().filename().string();
    if(UserId(id) && Owned(item.path(),id)) {
      if(profiles_.size()>=18)throw std::runtime_error("profile bound exceeded");
      handles_->profiles.emplace(id,Lock(item.path(),true));profiles_.emplace(id,Profile{id,item.path()});
    }
  }
  if(profiles_.size()>18)throw std::runtime_error("profile bound exceeded");
}
ProfileStore::~ProfileStore()=default;
const ProfileStore::Profile* ProfileStore::Find(const std::string& id) const {auto p=profiles_.find(id);return p==profiles_.end()||!p->second.usable?nullptr:&p->second;}
std::vector<std::string> ProfileStore::HumanProfiles() const {
  std::vector<std::string> result{"human"};for(const auto& [id,p]:profiles_)if(UserId(id)&&p.usable)result.push_back(p.id);return result;
}
std::string ProfileStore::CreateHuman() {
  if(profiles_.size()>=18)return {};
  std::array<unsigned char,16> random{};
  if(BCryptGenRandom(nullptr,random.data(),static_cast<ULONG>(random.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)return {};
  std::string id="p-";constexpr char hex[]="0123456789abcdef";for(auto byte:random){id+=hex[byte>>4];id+=hex[byte&15];}
  Path path=root_/id;
  if(!CreateDirectoryW(path.c_str(),nullptr))return {};
  auto held=Lock(path,true);
  Handle marker(CreateFileW((path/L"profile.identity").c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
  auto text=Marker(id);DWORD written=0;
  if(!WriteFile(marker.value,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)||written!=text.size()||!FlushFileBuffers(marker.value))return {};
  handles_->profiles.emplace(id,std::move(held));profiles_.emplace(id,Profile{id,path});return id;
}
bool ProfileStore::MarkContextOpened(const std::string& id) {
  auto p=profiles_.find(id);if(p==profiles_.end())return false;
  if(!p->second.usable)return false;
  if(p->second.opened)return true;
  // Never-opened user profiles hold a DELETE-capable handle for safe cleanup.
  // Transition to a CEF-compatible non-DELETE pin while retaining identity
  // through an intermediate handle; compare exact volume/file identity after
  // the final path reopen. Permanently bar deletion before this transition.
  p->second.opened=true;
  try {
    if(UserId(id)) {
      if(!Owned(p->second.cache,id))throw std::runtime_error("profile identity unavailable");
      Handle identity(CreateFileW(p->second.cache.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
      BY_HANDLE_FILE_INFORMATION before{},after{};
      if(!GetFileInformationByHandle(identity.value,&before))throw std::runtime_error("profile identity unavailable");
      handles_->profiles.erase(id);auto final=Lock(p->second.cache);
      if(!GetFileInformationByHandle(final.value,&after)||before.dwVolumeSerialNumber!=after.dwVolumeSerialNumber||
          before.nFileIndexHigh!=after.nFileIndexHigh||before.nFileIndexLow!=after.nFileIndexLow||!Owned(p->second.cache,id))throw std::runtime_error("profile identity changed");
      handles_->profiles.emplace(id,std::move(final));
    }else {auto held=Lock(p->second.cache);}
    return true;
  }catch(...){p->second.usable=false;return false;}
}
bool ProfileStore::SitePermissionAllowed(const std::string&) const {return false;}
bool ProfileStore::CanDelete(const std::string& id) const {auto p=Find(id);return UserId(id)&&p&&!p->opened;}
bool ProfileStore::DeleteConfirmed(const std::string& id,bool confirmed,const std::function<void(const std::string&)>& revoke) {
  if(!confirmed||!revoke||!CanDelete(id))return false;
  auto p=profiles_.find(id);const auto path=p->second.cache;
  try {
    if(path!=root_/id || path.parent_path()!=root_ || !Owned(path,id))return false;
    // Flush durable retirement first; partial cleanup cannot resurrect the
    // native identity after restart. The pinned DELETE-capable root handle
    // stays held throughout, including descendant enumeration and revocation.
    revoke(id);
    {
      Handle marker(CreateFileW((path/L"deletion.pending").c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
      profiles_.erase(p);
      constexpr char pending[]="AGI-BROWSE retired profile v1\n";DWORD written=0;
      if(!WriteFile(marker.value,pending,sizeof(pending)-1,&written,nullptr)||written!=sizeof(pending)-1||!FlushFileBuffers(marker.value)){handles_->profiles.erase(id);return false;}
    }
    std::vector<Handle> entries;
    auto retirement=Lock(path/L"deletion.pending",true);
    for(const auto& child:std::filesystem::directory_iterator(path))if(child.path().filename()!=L"deletion.pending")Collect(child.path(),entries,1);
    // Retirement is the last file removed, after profile.identity and every
    // ordinary child. Any partial failure retains a durable exclusion marker.
    entries.push_back(std::move(retirement));
    entries.push_back(std::move(handles_->profiles.at(id)));handles_->profiles.erase(id);
    FILE_DISPOSITION_INFO disposition{TRUE};
    for(auto& h:entries) {
      if(!SetFileInformationByHandle(h.value,FileDispositionInfo,&disposition,sizeof(disposition)))return false;
      h=Handle();
    }
    return true;
  }catch(...){if(!profiles_.contains(id))handles_->profiles.erase(id);return false;}
}
}  // namespace agi::browser
