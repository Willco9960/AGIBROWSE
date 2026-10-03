#include "lib/ipc/windows_channel.h"
#include <bcrypt.h>
#include <array>
#include <chrono>
#include <mutex>
namespace agi::ipc {
namespace {
void Close(HANDLE& h) { if(h && h!=INVALID_HANDLE_VALUE) CloseHandle(h); h=nullptr; }
bool Valid(HANDLE h) { return h && h!=INVALID_HANDLE_VALUE; }
enum class ReadResult { ok, timeout, closed, invalid };
ReadResult ReadExact(HANDLE pipe, HANDLE peer, uint8_t* bytes, size_t size, uint64_t deadline) {
  size_t done=0;
  while (done<size) {
    if (WaitForSingleObject(peer,0)!=WAIT_TIMEOUT) return ReadResult::closed;
    if (GetTickCount64()>=deadline) return done?ReadResult::closed:ReadResult::timeout;
    DWORD available=0;
    if (!PeekNamedPipe(pipe,nullptr,0,nullptr,&available,nullptr)) return ReadResult::closed;
    if (!available) { Sleep(5); continue; }
    DWORD read=0;
    if (!ReadFile(pipe,bytes+done,static_cast<DWORD>((std::min)(size-done,size_t(available))),&read,nullptr) || !read) return ReadResult::closed;
    done+=read;
  }
  return ReadResult::ok;
}
ReadResult ReadMessage(HANDLE pipe,HANDLE peer,Message& message,uint32_t timeout,bool transport_config=false) {
  uint8_t length[4]; const auto deadline=GetTickCount64()+timeout;
  auto result=ReadExact(pipe,peer,length,4,deadline); if(result!=ReadResult::ok) return result;
  uint32_t size=0; for(int i=0;i<4;++i) size|=uint32_t(length[i])<<(8*i);
  if(size>(transport_config?kMaxTransportConfig:kMaxMessage) || size<14) return ReadResult::invalid;
  std::vector<uint8_t> bytes(size);
  result=ReadExact(pipe,peer,bytes.data(),bytes.size(),deadline);
  // A partial frame must never be treated as a harmless idle timeout.
  if(result!=ReadResult::ok) return ReadResult::closed;
  return Decode(bytes,message)?ReadResult::ok:ReadResult::invalid;
}
bool WriteMessage(HANDLE pipe,const Message& message) {
  auto bytes=Encode(message); if(bytes.size()>(message.kind==Kind::transport_config?kMaxTransportConfig:kMaxMessage)) return false;
  std::vector<uint8_t> frame;
  for(int i=0;i<4;++i) frame.push_back(static_cast<uint8_t>(bytes.size()>>(8*i)));
  frame.insert(frame.end(),bytes.begin(),bytes.end());
  struct WriteState { HANDLE pipe; std::vector<uint8_t>* bytes; DWORD written=0; BOOL ok=FALSE; } state{pipe,&frame};
  HANDLE writer=CreateThread(nullptr,0,[](LPVOID data)->DWORD {
    auto& s=*static_cast<WriteState*>(data);
    s.ok=WriteFile(s.pipe,s.bytes->data(),static_cast<DWORD>(s.bytes->size()),&s.written,nullptr);
    return 0;
  },&state,0,nullptr);
  if(!writer) return false;
  const bool timed_out=WaitForSingleObject(writer,500)!=WAIT_OBJECT_0;
  if(timed_out) {
    CancelSynchronousIo(writer);
    // A broken Windows cancellation primitive cannot leave stack pointers in
    // an abandoned writer. Fail closed with host/job teardown, never hang GUI.
    if(WaitForSingleObject(writer,1000)!=WAIT_OBJECT_0) RaiseFailFastException(nullptr,nullptr,0);
  }
  CloseHandle(writer);
  return !timed_out && state.ok && state.written==frame.size();
}
}
HostChannel::~HostChannel() { Stop(); }
std::vector<std::pair<uintptr_t,std::wstring>> HostChannel::EndpointDiagnosticsForTest() const {
  std::vector<std::pair<uintptr_t,std::wstring>> result;
  for(auto handle:{input_,output_}) {
    std::array<uint8_t,4096> buffer{};
    if(GetFileInformationByHandleEx(handle,FileNameInfo,buffer.data(),static_cast<DWORD>(buffer.size()))) {
      auto info=reinterpret_cast<FILE_NAME_INFO*>(buffer.data());
      result.emplace_back(reinterpret_cast<uintptr_t>(handle),std::wstring(info->FileName,info->FileNameLength/sizeof(wchar_t)));
    }
  }
  return result;
}
bool HostChannel::Start(const std::wstring& broker,std::shared_ptr<NativeTransportAuthority> authority) {
  if(process_ || broker.empty()) return false;
  generation_=0;transport_port_=0;transport_authority_=std::move(authority);
  SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
  HANDLE child_read=nullptr,child_write=nullptr,parent=nullptr,thread=nullptr;
  bool success=false;
  SIZE_T bytes=0;
  InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
  std::vector<uint8_t> storage(bytes);
  auto attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
  bool initialized=false;
  do {
    if(!CreatePipe(&child_read,&output_,&security,4096) || !CreatePipe(&input_,&child_write,&security,4096)) break;
    if(!SetHandleInformation(output_,HANDLE_FLAG_INHERIT,0) || !SetHandleInformation(input_,HANDLE_FLAG_INHERIT,0)) break;
    parent=OpenProcess(SYNCHRONIZE,TRUE,GetCurrentProcessId()); if(!parent) break;
    job_=CreateJobObjectW(nullptr,nullptr); if(!job_) break;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(job_,JobObjectExtendedLimitInformation,&limits,sizeof(limits))) break;
    if(!InitializeProcThreadAttributeList(attributes,1,0,&bytes)) break;
    initialized=true;
    HANDLE allowlist[]={child_read,child_write,parent};
    if(!UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,allowlist,sizeof(allowlist),nullptr,nullptr)) break;
    STARTUPINFOEXW startup{}; startup.StartupInfo.cb=sizeof(startup); startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput=child_read; startup.StartupInfo.hStdOutput=child_write; startup.StartupInfo.hStdError=parent; startup.lpAttributeList=attributes;
    PROCESS_INFORMATION info{};
    std::wstring command=L"\""+broker+L"\" --private-child";
    if(!CreateProcessW(broker.c_str(),command.data(),nullptr,nullptr,TRUE,EXTENDED_STARTUPINFO_PRESENT|CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,nullptr,&startup.StartupInfo,&info)) break;
    process_=info.hProcess; thread=info.hThread; process_id_=info.dwProcessId;
    if(!AssignProcessToJobObject(job_,process_)) break;
    // Close inherited copies in the parent before any CEF child is created.
    Close(child_read); Close(child_write); Close(parent);
    std::array<uint8_t,40> random{};
    if(BCryptGenRandom(nullptr,random.data(),static_cast<ULONG>(random.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0) break;
    for(int i=0;i<8;++i) generation_|=uint64_t(random[32+i])<<(8*i);
    if(!generation_) break;
    if(ResumeThread(thread)==DWORD(-1)) break;
    Message challenge; challenge.kind=Kind::challenge; challenge.generation=generation_; challenge.challenge.assign(random.begin(),random.begin()+32);
    if(!WriteMessage(output_,challenge)) break;
    Message proof;
    if(ReadMessage(input_,process_,proof,3000)!=ReadResult::ok || proof.kind!=Kind::proof || proof.generation!=generation_) break;
    uint8_t difference=0; for(size_t i=0;i<32;++i) difference|=proof.challenge[i]^challenge.challenge[i];
    if(difference) break;
    if(transport_authority_) {
      auto configuration=transport_authority_->Configuration();configuration.channel_generation=generation_;
      if(configuration.kind!=Kind::transport_config||configuration.version!=2||!WriteMessage(output_,configuration))break;
      SecureZeroMemory(configuration.server_key.data(),configuration.server_key.size());
      Message ready;
      if(ReadMessage(input_,process_,ready,3000)!=ReadResult::ok||ready.kind!=Kind::transport_ready||ready.channel_generation!=generation_)break;
      transport_port_=static_cast<unsigned short>(ready.port);
    }
    success=true;
  } while(false);
  if(initialized) DeleteProcThreadAttributeList(attributes);
  Close(child_read); Close(child_write); Close(parent); Close(thread);
  if(!success) {
    // Assignment may have failed while the child is still suspended/outside
    // the job. Terminate that exact launched process before closing handles.
    if(process_) { TerminateProcess(process_,70); WaitForSingleObject(process_,1000); }
    Stop(); return false;
  }
  stopping_=false; live_=true; worker_=std::thread(&HostChannel::Serve,this); return true;
}
void HostChannel::Serve() {
  Boundary boundary(generation_);  // intentionally zero grants in production
  uint64_t identity_sequence=0;
  while(!stopping_) {
    Message intent; auto result=ReadMessage(input_,process_,intent,250);
    if(result==ReadResult::timeout) continue;
    if(result!=ReadResult::ok) break;
    if(intent.kind==Kind::identity_check) {
      if(!transport_authority_||intent.channel_generation!=generation_||identity_sequence==UINT64_MAX||intent.sequence!=identity_sequence+1)break;
      identity_sequence=intent.sequence;
      Message reply;reply.version=2;reply.kind=Kind::identity_result;reply.channel_generation=generation_;reply.sequence=intent.sequence;
      reply.result=transport_authority_->ValidateIdentity(intent.client,intent.certificate_hash,intent.session)?1:2;
      if(!WriteMessage(output_,reply))break;
      continue;
    }
    if(intent.kind!=Kind::intent)break;
    Message reply; reply.kind=Kind::result; reply.sequence=intent.sequence; reply.generation=generation_;
    reply.result=static_cast<uint8_t>(boundary.Admit(intent,GetTickCount64()));
    if(!WriteMessage(output_,reply)) break;
  }
  boundary.Invalidate(); live_=false;
  // Trust loss kills the launched broker, preventing retained channel authority.
  if(!stopping_ && job_) TerminateJobObject(job_,70);
}
void HostChannel::Stop() {
  stopping_=true;
  if(worker_.joinable()) worker_.join();
  if(process_ && WaitForSingleObject(process_,0)==WAIT_TIMEOUT) {
    Message stop; stop.kind=Kind::stop;
    WriteMessage(output_,stop);
    if(WaitForSingleObject(process_,1000)!=WAIT_OBJECT_0 && job_) TerminateJobObject(job_,70);
    WaitForSingleObject(process_,1000);
  }
  live_=false; Close(input_); Close(output_); Close(process_); Close(job_);
  transport_port_=0;
}
int RunPrivateBroker(BrokerTransport* transport) {
  HANDLE input=GetStdHandle(STD_INPUT_HANDLE),output=GetStdHandle(STD_OUTPUT_HANDLE),parent=GetStdHandle(STD_ERROR_HANDLE);
  if(!Valid(input)||!Valid(output)||!Valid(parent) || GetFileType(input)!=FILE_TYPE_PIPE || GetFileType(output)!=FILE_TYPE_PIPE || WaitForSingleObject(parent,0)!=WAIT_TIMEOUT) return 71;
  // Broker must never propagate any privileged endpoint/lifetime handle.
  for(auto h:{input,output,parent}) if(!SetHandleInformation(h,HANDLE_FLAG_INHERIT,0)) return 71;
  Message challenge; if(ReadMessage(input,parent,challenge,3000)!=ReadResult::ok || challenge.kind!=Kind::challenge) return 72;
  Message proof=challenge; proof.kind=Kind::proof;
  if(!WriteMessage(output,proof)) return 73;
  std::mutex channel_mutex;
  uint64_t sequence=0;
  std::atomic<bool> transport_stopping{false};
  if(transport) {
    Message configuration;
    if(ReadMessage(input,parent,configuration,3000,true)!=ReadResult::ok||configuration.kind!=Kind::transport_config||configuration.channel_generation!=challenge.generation)return 76;
    const auto epoch=challenge.generation;
    if(!transport->Start(configuration,[&,epoch](const std::string& client,const std::string& hash,const std::string& session){
      std::lock_guard lock(channel_mutex);if(sequence==UINT64_MAX)return false;
      Message check;check.kind=Kind::identity_check;check.version=2;check.sequence=++sequence;check.channel_generation=epoch;check.client=client;check.certificate_hash=hash;check.session=session;
      if(!WriteMessage(output,check))return false;
      Message reply;auto result=ReadMessage(input,parent,reply,1000);
      if(result==ReadResult::ok&&reply.kind==Kind::stop){transport_stopping=true;return false;}
      return result==ReadResult::ok&&reply.kind==Kind::identity_result&&reply.sequence==sequence&&reply.channel_generation==epoch&&reply.result==1;
    }))return 77;
    SecureZeroMemory(configuration.server_key.data(),configuration.server_key.size());
    Message ready;ready.version=2;ready.kind=Kind::transport_ready;ready.channel_generation=epoch;ready.port=transport->port();if(!ready.port||!WriteMessage(output,ready)){transport->Stop();return 78;}
  }
  // 008 supplies paired WSS, 009 supplies host-owned scope sessions. Until then
  // there is no intake and no action dispatch; only authenticated lifetime.
  for(;;) {
    if(transport_stopping){if(transport)transport->Stop();return 0;}
    Message m;ReadResult result;
    {std::lock_guard lock(channel_mutex);result=ReadMessage(input,parent,m,25);}
    if(transport)Sleep(2);
    if(result==ReadResult::timeout) continue;
    if(transport)transport->Stop();
    if(result!=ReadResult::ok) return 74;
    if(m.kind==Kind::stop) return 0;
    return 75;
  }
}
}  // namespace agi::ipc
