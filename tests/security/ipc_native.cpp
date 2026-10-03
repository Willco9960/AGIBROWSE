#include "lib/ipc/protocol.h"
#include "lib/ipc/windows_channel.h"
#include <cstdlib>
#include <iostream>
#include <functional>
#include <thread>
using namespace agi::ipc;
namespace {
unsigned checks=0;
void Check(bool ok,const char* name) { ++checks; if(!ok) { std::cerr<<"FAIL "<<name<<"\n"; std::exit(1); } }
Message Intent() { Message m; m.kind=Kind::intent;m.sequence=1;m.generation=3;m.channel_generation=7;m.client="client";m.session="session";m.profile="agent";m.tab="tab";m.frame="top";m.document="doc";m.operation="observe";return m; }
Grant NativeGrant() { Grant g;g.client="client";g.session="session";g.destination={"agent","tab","top","doc"};g.generation=3;g.deadline_ms=1000;g.operations={"observe","wait"};return g; }
bool Allowed(Message m,Grant g=NativeGrant(),uint64_t now=1) { Boundary b(7);Check(b.RegisterNativeGrant(g),"native grant fixture");b.RegisterDestination(g.destination);return b.Admit(m,now)==Decision::unsupported; }
#ifdef _WIN32
bool Send(HANDLE h,const Message& m) { auto b=Encode(m);std::vector<uint8_t> f;for(int i=0;i<4;++i)f.push_back(static_cast<uint8_t>(b.size()>>(8*i)));f.insert(f.end(),b.begin(),b.end());DWORD n;return WriteFile(h,f.data(),static_cast<DWORD>(f.size()),&n,nullptr)&&n==f.size(); }
bool Receive(HANDLE h,Message& m) { uint8_t l[4];DWORD n;if(!ReadFile(h,l,4,&n,nullptr)||n!=4)return false;uint32_t size=0;for(int i=0;i<4;++i)size|=uint32_t(l[i])<<(8*i);if(size>2048)return false;std::vector<uint8_t>b(size);return ReadFile(h,b.data(),size,&n,nullptr)&&n==size&&Decode(b,m); }
int Child() {
  wchar_t sentinel[64]{};if(GetEnvironmentVariableW(L"AGI_IPC_TEST_SENTINEL",sentinel,64)) {
    // Numeric values can alias unrelated CRT handles in the child. Compare the
    // actual kernel object name, not merely whether that number is occupied.
    wchar_t expected[128]{};GetEnvironmentVariableW(L"AGI_IPC_TEST_SENTINEL_NAME",expected,128);
    using Query=LONG(NTAPI*)(HANDLE,ULONG,PVOID,ULONG,PULONG);
    auto query=reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryObject"));
    struct Name { USHORT length,maximum;PWSTR value; };
    std::vector<uint8_t> buffer(4096);ULONG needed=0;
    if(query && query(reinterpret_cast<HANDLE>(_wcstoui64(sentinel,nullptr,10)),1,buffer.data(),static_cast<ULONG>(buffer.size()),&needed)>=0) {
      auto name=reinterpret_cast<Name*>(buffer.data());
      if(name->value && std::wstring(name->value,name->length/2).find(expected)!=std::wstring::npos)return 81;
    }
  }
  wchar_t mode[64]{};GetEnvironmentVariableW(L"AGI_IPC_TEST_MODE",mode,64);
  if(std::wstring(mode)==L"normal")return RunPrivateBroker();
  auto input=GetStdHandle(STD_INPUT_HANDLE),output=GetStdHandle(STD_OUTPUT_HANDLE);
  Message m;if(!Receive(input,m))return 82;m.kind=Kind::proof;
  if(std::wstring(mode)==L"wrong-proof")m.challenge[0]^=1;
  if(!Send(output,m))return 83;
  if(std::wstring(mode)==L"wrong-proof")return 84;
  if(std::wstring(mode)==L"partial-header") { DWORD n;uint8_t prefix[]={1,0};WriteFile(output,prefix,2,&n,nullptr);Sleep(10000);return 85; }
  if(std::wstring(mode)==L"oversize" || std::wstring(mode)==L"partial-body") {
    DWORD n;uint8_t prefix[]={static_cast<uint8_t>(std::wstring(mode)==L"oversize"?1:32),static_cast<uint8_t>(std::wstring(mode)==L"oversize"?8:0),0,0,0};
    WriteFile(output,prefix,std::wstring(mode)==L"oversize"?4:5,&n,nullptr);Sleep(10000);return 89;
  }
  if(std::wstring(mode)==L"unauthorized-intent") {
    auto intent=Intent();intent.channel_generation=m.generation;
    if(!Send(output,intent))return 90;
    Message reply;if(!Receive(input,reply)||reply.kind!=Kind::result||reply.result!=static_cast<uint8_t>(Decision::denied)||reply.sequence!=1||reply.generation!=m.generation)return 91;
    Message stop;if(!Receive(input,stop)||stop.kind!=Kind::stop)return 92;
    return 0;
  }
  if(std::wstring(mode)==L"nonreading") {
    auto intent=Intent();intent.channel_generation=m.generation;
    for(int i=0;i<500;++i){intent.sequence=i+1;if(!Send(output,intent))return 86;}
    Sleep(10000);return 87;
  }
  return 88;
}
void WindowsTests() {
  wchar_t path[32768];Check(GetModuleFileNameW(nullptr,path,32768)>0,"test executable path");
  const auto sentinel_name=L"AGI_IPC_SENTINEL_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(GetTickCount64());
  SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE sentinel=CreateEventW(&sa,TRUE,FALSE,(L"Local\\"+sentinel_name).c_str());
  Check(sentinel!=nullptr,"extra inheritable sentinel created");
  SetEnvironmentVariableW(L"AGI_IPC_TEST_SENTINEL",std::to_wstring(reinterpret_cast<uintptr_t>(sentinel)).c_str());
  SetEnvironmentVariableW(L"AGI_IPC_TEST_SENTINEL_NAME",sentinel_name.c_str());
  SetEnvironmentVariableW(L"AGI_IPC_TEST_MODE",L"normal");
  HostChannel c;Check(c.Start(path),"private launch challenge and extra handle exclusion");Check(c.live(),"launched broker live");
  auto begin=GetTickCount64();c.Stop();Check(GetTickCount64()-begin<2500,"normal shutdown bounded");
  Check(c.Start(path),"fresh generation restart");
  TerminateProcess(c.process_handle_for_test(),91);
  for(int i=0;i<200 && c.live();++i)Sleep(5);
  Check(!c.live(),"broker death invalidates authority");c.Stop();
  SetEnvironmentVariableW(L"AGI_IPC_TEST_MODE",L"wrong-proof");begin=GetTickCount64();
  Check(!c.Start(path),"wrong startup challenge rejected");Check(GetTickCount64()-begin<4000,"failed startup bounded");
  for(auto mode:{L"partial-header",L"partial-body",L"oversize",L"nonreading"}) {
    SetEnvironmentVariableW(L"AGI_IPC_TEST_MODE",mode);Check(c.Start(path),"adversarial peer starts");
    begin=GetTickCount64();for(int i=0;i<1000&&c.live();++i)Sleep(5);
    Check(!c.live(),"partial header/nonreading peer closes channel");c.Stop();Check(GetTickCount64()-begin<6500,"hostile peer shutdown bounded");
  }
  SetEnvironmentVariableW(L"AGI_IPC_TEST_MODE",L"unauthorized-intent");Check(c.Start(path),"actual unauthorized wire intent starts");
  HANDLE process_copy=nullptr;Check(DuplicateHandle(GetCurrentProcess(),c.process_handle_for_test(),GetCurrentProcess(),&process_copy,PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,0),"receipt process handle retained");
  Sleep(100);c.Stop();DWORD exit=1;GetExitCodeProcess(process_copy,&exit);CloseHandle(process_copy);Check(exit==0,"zero-grant wire denial and clean shutdown");
  SetEnvironmentVariableW(L"AGI_IPC_TEST_MODE",nullptr);SetEnvironmentVariableW(L"AGI_IPC_TEST_SENTINEL",nullptr);SetEnvironmentVariableW(L"AGI_IPC_TEST_SENTINEL_NAME",nullptr);CloseHandle(sentinel);
  Check(RunPrivateBroker()==71,"unlaunched broker cannot connect");
}
#endif
}
int main(int argc,char** argv) {
#ifdef _WIN32
  if(argc==2&&std::string(argv[1])=="--private-child")return Child();
#endif
  auto m=Intent();Message decoded;auto bytes=Encode(m);Check(Decode(bytes,decoded),"valid closed intent");
  auto bad=bytes;bad.insert(bad.end(),bytes.begin()+4,bytes.begin()+9);Check(!Decode(bad,decoded),"duplicate version field");
  bad=bytes;bad.insert(bad.end(),{15,1,1,0,1});Check(!Decode(bad,decoded),"unknown tag");
  bad=bytes;bad[9]=99;Check(!Decode(bad,decoded),"unknown message kind");
  bad=bytes;bad[5]=3;Check(!Decode(bad,decoded),"wrong scalar type");
  bad=bytes;bad[8]=2;Check(!Decode(bad,decoded),"unknown version");
  bad=bytes;bad[6]=255;bad[7]=255;Check(!Decode(bad,decoded),"overflow length");
  bad=bytes;bad.pop_back();Check(!Decode(bad,decoded),"truncated field");
  bad=bytes;bad.resize(2049);Check(!Decode(bad,decoded),"message byte bound");
  for(auto op:{"shell","javascript","cdp","approve","grant","path"}) { auto n=m;n.operation=op;Check(!Decode(Encode(n),decoded),"prohibited operation"); }
  auto n=m;n.client=std::string(129,'a');Check(!Decode(Encode(n),decoded),"identifier bound");
  n=m;n.client="*";Check(!Decode(Encode(n),decoded),"wildcard identity");
  n=m;n.client=std::string("a\0b",3);Check(!Decode(Encode(n),decoded),"embedded NUL");
  Boundary zero(7);Check(zero.Admit(m,1)==Decision::denied,"production zero grants");
  Check(Allowed(m),"current native observe permission reaches unsupported execution");
  for(const auto& change:std::vector<std::function<void(Message&)>>{
    [](auto& x){x.client="other";},[](auto& x){x.session="other";},[](auto& x){x.profile="human";},[](auto& x){x.tab="other";},[](auto& x){x.frame="other";},[](auto& x){x.document="old";},[](auto& x){x.generation=2;},[](auto& x){x.channel_generation=6;},[](auto& x){x.operation="click";},[](auto& x){x.sequence=2;}}) { n=m;change(n);Check(!Allowed(n),"sender destination operation epoch/order denied"); }
  Check(!Allowed(m,NativeGrant(),1000),"monotonic grant expiry");
  auto g=NativeGrant();g.control=true;g.operations={"click"};n=m;n.operation="click";Check(!Allowed(n,g),"control lease required");g.lease=true;Check(Allowed(n,g),"exact control permission only reaches unsupported");
  Boundary replay(7);Check(replay.RegisterNativeGrant(NativeGrant()),"replay fixture");replay.RegisterDestination(NativeGrant().destination);
  Check(replay.Admit(m,1)==Decision::unsupported,"first sequence");Check(replay.Admit(m,1)==Decision::denied,"identical replay denied");n=m;n.operation="wait";Check(replay.Admit(n,1)==Decision::denied,"changed-payload replay denied");
  n=m;n.sequence=2;replay.Invalidate();Check(replay.Admit(n,1)==Decision::denied,"channel loss purges authority");
  Boundary collision(7);g=NativeGrant();g.destination.tab="a:b";g.destination.frame="c";Check(collision.RegisterNativeGrant(g),"tuple fixture");collision.RegisterDestination(g.destination);collision.RegisterDestination({"human","a","b:c","other"});
  n=m;n.tab="a:b";n.frame="c";Check(collision.Admit(n,1)==Decision::unsupported,"tuple destination no collision");
  g=NativeGrant();g.operations={"observe","observe"};Boundary invalid(7);Check(!invalid.RegisterNativeGrant(g),"duplicate native grant operation");g.operations={"click"};Check(!invalid.RegisterNativeGrant(g),"observe grant cannot contain action");
  // Every truncation and one-byte mutation must parse safely. Mutations that
  // preserve the exact schema may validly parse; security follows authority.
  for(size_t i=0;i<bytes.size();++i){bad.assign(bytes.begin(),bytes.begin()+i);Check(!Decode(bad,decoded),"all truncations rejected");bad=bytes;bad[i]^=0xff;Decode(bad,decoded);}
#ifdef _WIN32
  WindowsTests();
#endif
  std::cout<<"PASS "<<checks<<" native IPC security checks\n";
}
