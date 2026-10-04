#include "lib/transport/server.h"
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <filesystem>
#include <iostream>
#include <thread>
#include <atomic>
using namespace agi::transport;
using namespace agi::ipc;
// Native test-only access. No CLI, IPC, renderer or public grant entry exists.
namespace agi::transport {
struct HostTransportAuthorityTestAccess {
  static auto Scope(HostTransportAuthority& host) { return host.scopes_; }
  static std::vector<std::string> Sessions(HostTransportAuthority& host) {
    std::lock_guard lock(host.mutex_); std::vector<std::string> ids;
    for(const auto& [id,binding] : host.sessions_)
      if(binding.live && host.scopes_->IsBoundAuthenticatedSession(binding.client,id))ids.push_back(id);
    return ids;
  }
};
}
namespace {
unsigned checks=0;
void Check(bool ok,const char* name) { ++checks; if(!ok)throw std::runtime_error(name); std::cout << "PASS " << name << "\n"; }
template<class F> bool Until(F fn,uint64_t timeout=2000) {
  const auto end=MonotonicMs()+timeout;
  do { if(fn())return true; std::this_thread::sleep_for(std::chrono::milliseconds(5)); } while(MonotonicMs()<end);
  return false;
}
Credential Enroll(PairingAuthority& authority,const Key& key) {
  auto pending=authority.Begin(MakeCsr(key),MonotonicMs());
  return authority.Complete(pending,Sign(key,pending.proof_input),MonotonicMs());
}
bool SendTestFrame(HANDLE handle,const std::vector<uint8_t>& bytes) {
  std::vector<uint8_t> frame;for(unsigned i=0;i<4;++i)frame.push_back(uint8_t(bytes.size()>>(8*i)));
  frame.insert(frame.end(),bytes.begin(),bytes.end());DWORD n=0;
  return WriteFile(handle,frame.data(),static_cast<DWORD>(frame.size()),&n,nullptr)&&n==frame.size();
}
bool ReceiveTestFrame(HANDLE handle,Message& message) {
  uint8_t length[4];DWORD n=0;if(!ReadFile(handle,length,4,&n,nullptr)||n!=4)return false;
  uint32_t size=0;for(unsigned i=0;i<4;++i)size|=uint32_t(length[i])<<(8*i);
  if(size>kMaxTransportConfig)return false;std::vector<uint8_t> bytes(size);
  return ReadFile(handle,bytes.data(),size,&n,nullptr)&&n==size&&Decode(bytes,message);
}
std::string TestEnvironment(const wchar_t* name) {
  wchar_t text[256]{};const auto size=GetEnvironmentVariableW(name,text,256);
  return size&&size<256?std::string(text,text+size):std::string{};
}
int HostilePrivateChild(const std::string& mode) {
  const auto input=GetStdHandle(STD_INPUT_HANDLE),output=GetStdHandle(STD_OUTPUT_HANDLE);
  Message challenge;if(!ReceiveTestFrame(input,challenge))return 80;
  auto proof=challenge;proof.kind=Kind::proof;if(!SendTestFrame(output,Encode(proof)))return 81;
  Message config;if(!ReceiveTestFrame(input,config))return 82;
  SecureZeroMemory(config.server_key.data(),config.server_key.size());
  Message ready;ready.version=2;ready.kind=Kind::transport_ready;ready.channel_generation=challenge.generation;ready.port=1;
  if(!SendTestFrame(output,Encode(ready)))return 83;
  Message check;check.version=2;check.kind=Kind::identity_check;check.sequence=1;check.channel_generation=challenge.generation;
  check.client=TestEnvironment(L"AGI_SCOPE_TEST_CLIENT");check.session=TestEnvironment(L"AGI_SCOPE_TEST_SESSION");
  check.certificate_hash=TestEnvironment(L"AGI_SCOPE_TEST_HASH");
  if(!SendTestFrame(output,Encode(check)))return 84;
  Message reply;if(!ReceiveTestFrame(input,reply)||reply.result!=1)return 85;
  Message closed=check;closed.kind=Kind::session_closed;closed.sequence=2;
  if(mode=="wrong-client")closed.client="other";
  if(mode=="wrong-session")closed.session="unknown";
  if(mode=="wrong-epoch")closed.channel_generation=challenge.generation==1?2:1;
  if(mode=="replay-sequence")closed.sequence=1;
  if(mode=="future-sequence")closed.sequence=3;
  auto bytes=Encode(closed);
  if(mode=="missing-session")bytes.resize(bytes.size()-closed.session.size()-4);
  if(!SendTestFrame(output,bytes))return 86;
  // A rejected frame must kill this exact launched child before this sleep ends.
  Sleep(10000);return 87;
}
namespace net=boost::asio; namespace ssl=net::ssl; namespace beast=boost::beast; namespace ws=beast::websocket;
using tcp=net::ip::tcp;
struct Connection {
  net::io_context io; ssl::context context{ssl::context::tls_client};
  ws::stream<beast::ssl_stream<beast::tcp_stream>,false> socket{io,context};
  Connection(unsigned short port,const Key& key,const Credential& c) {
    auto ca=ReadCertificate(c.ca),cert=ReadCertificate(c.cert); auto native=socket.next_layer().native_handle();
    SSL_CTX_set_min_proto_version(context.native_handle(),TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(context.native_handle(),TLS1_3_VERSION);
    SSL_set_min_proto_version(native,TLS1_3_VERSION); SSL_set_max_proto_version(native,TLS1_3_VERSION);
    X509_STORE_add_cert(SSL_CTX_get_cert_store(context.native_handle()),ca.get());
    SSL_use_certificate(native,cert.get()); SSL_use_PrivateKey(native,key.get());
    SSL_set_verify(native,SSL_VERIFY_PEER,nullptr);
    X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(native),"127.0.0.1");
    beast::get_lowest_layer(socket).connect({net::ip::make_address("127.0.0.1"),port});
    socket.next_layer().handshake(ssl::stream_base::client);
    Certificate peer(SSL_get1_peer_certificate(native),X509_free);
    if(!peer || Pin(peer)!=c.server_pin)throw std::runtime_error("test client pin mismatch");
    socket.handshake("127.0.0.1:"+std::to_string(port),"/transport/v1");
  }
  void Close() { socket.close(ws::close_code::normal); }
  bool ClosedWithin(uint64_t timeout) {
    auto& raw=beast::get_lowest_layer(socket).socket(); raw.non_blocking(true);
    return Until([&] { char byte; boost::system::error_code ec; raw.read_some(net::buffer(&byte,1),ec);
      return ec && ec!=net::error::would_block && ec!=net::error::try_again; },timeout);
  }
};
std::string OneSession(HostTransportAuthority& host) {
  auto sessions=HostTransportAuthorityTestAccess::Sessions(host);
  Check(sessions.size()==1,"exact host authenticated live binding"); return sessions[0];
}
ScopedGrant GrantFor(const Credential& c,const std::string& session) {
  return {1,"native-test-grant",c.client,session,"agent",{"tab"},{"observe","wait","click"},
          {{ContextKind::origin,"https://example.test",FramePolicy::top}},true,1,30000};
}
void Approve(ScopedAuthority& scope,const ScopedGrant& g,const Destination& doc) {
  Check(scope.RegisterNativeDocument({doc,ContextKind::origin,"https://example.test",true}),"test native document provenance");
  Check(scope.ApproveNativeGrant(g,MonotonicMs()),"test native approval only");
  Check(scope.AcquireNativeLease(g.client,g.session,"tab",MonotonicMs()),"test native control lease");
}
void Wire() {
  Message closed; closed.version=2; closed.kind=Kind::session_closed;closed.sequence=1;closed.channel_generation=7;closed.client="client";closed.session="session";
  Message decoded; auto bytes=Encode(closed);
  Check(Decode(bytes,decoded)&&decoded.kind==Kind::session_closed,"closed IPCv2 close schema roundtrip");
  auto wrong=bytes;wrong[8]=1;Check(!Decode(wrong,decoded),"legacy IPCv1 cannot close session");
  wrong=bytes;wrong.insert(wrong.end(),bytes.begin()+4,bytes.begin()+9);Check(!Decode(wrong,decoded),"duplicate close tags rejected");
  wrong=bytes;wrong.insert(wrong.end(),{18,3,1,0,'a'});Check(!Decode(wrong,decoded),"close disallows certificate or scope payload");
  closed.client="*";Check(!Decode(Encode(closed),decoded),"close wildcard rejected");closed.client="client";
  closed.sequence=0;Check(!Decode(Encode(closed),decoded),"close zero sequence rejected");
}
void NativeFailures() {
  const auto root=std::filesystem::temp_directory_path()/std::filesystem::path("agi-scoped-failure-"+RandomId());
  std::filesystem::create_directories(root);const auto store=(root/L"authority.dpapi").wstring();
  auto pairing=std::make_shared<PairingAuthority>(store);auto key=GenerateKey();const auto c=Enroll(*pairing,key);
  auto throwing=std::make_shared<PairingAuthority::RevocationCallback>([](const std::string&) { throw std::runtime_error("trusted callback failed"); });
  pairing->RegisterNativeRevocationCallback(throwing);
  auto validator=[](const std::string& origin) { return origin=="https://example.test"; };
  auto first=std::make_shared<HostTransportAuthority>(pairing,validator);
  auto second=std::make_shared<HostTransportAuthority>(pairing,validator);
  const auto hash=CertificateHash(ReadCertificate(c.cert));const auto first_id=RandomId(),second_id=RandomId();
  Check(first->ValidateIdentity(c.client,hash,first_id)&&second->ValidateIdentity(c.client,hash,second_id),"two native host owners bind paired sessions");
  const Destination doc{"agent","tab","top","doc"};auto first_scope=HostTransportAuthorityTestAccess::Scope(*first);
  auto second_scope=HostTransportAuthorityTestAccess::Scope(*second);
  Approve(*first_scope,GrantFor(c,first_id),doc);Approve(*second_scope,GrantFor(c,second_id),doc);
  ScopeRequest one{c.client,first_id,"observe",doc},two{c.client,second_id,"observe",doc};
  const auto queued_one=first_scope->QueueScopeCheck(one,MonotonicMs()),queued_two=second_scope->QueueScopeCheck(two,MonotonicMs());
  bool failed=false;
  try { pairing->Revoke(c.client); } catch(...) { failed=true; }
  Check(failed&&!pairing->Paired(c.client,c.generation),"throwing revoke listener still removes credential and propagates native failure");
  Check(!first_scope->WithCurrentScope(queued_one,[] {})&&!second_scope->WithCurrentScope(queued_two,[] {}),"all live host owners revoke despite throwing listener");
  PairingAuthority restarted(store);Check(!restarted.Paired(c.client,c.generation),"throwing listener does not resurrect disk pairing");
  throwing.reset();const auto next=Enroll(*pairing,key);const auto next_id=RandomId();
  Check(second->ValidateIdentity(next.client,CertificateHash(ReadCertificate(next.cert)),next_id),"fresh credential after failed callback binds zero grants");
  Approve(*second_scope,GrantFor(next,next_id),doc);ScopeRequest read{next.client,next_id,"observe",doc};
  const auto queued=second_scope->QueueScopeCheck(read,MonotonicMs());
  std::filesystem::create_directory(std::filesystem::path(store+L".revoking"));failed=false;
  try { pairing->Revoke(next.client); } catch(...) { failed=true; }
  Check(failed&&!pairing->Paired(next.client,next.generation),"failed protected marker write removes runtime pairing");
  Check(!second_scope->WithCurrentScope(queued,[] {})&&!second_scope->QueueScopeCheck(read,MonotonicMs()),"persistence failure cannot deliver queued or subsequent observation");
  second.reset();Check(!second_scope->BindAuthenticatedSession("client","after-destruction"),"host authority destruction invalidates retained scope handles");
  std::filesystem::remove_all(root);
}
void HostileCloseFrames() {
  const auto root=std::filesystem::temp_directory_path()/std::filesystem::path("agi-close-frames-"+RandomId());
  std::filesystem::create_directories(root);auto pairing=std::make_shared<PairingAuthority>((root/L"authority.dpapi").wstring());
  const auto key=GenerateKey(),unused=key;const auto c=Enroll(*pairing,key);const auto hash=CertificateHash(ReadCertificate(c.cert));
  const auto session=RandomId();auto widen=[](const std::string& value) { return std::wstring(value.begin(),value.end()); };
  SetEnvironmentVariableW(L"AGI_SCOPE_TEST_CLIENT",widen(c.client).c_str());
  SetEnvironmentVariableW(L"AGI_SCOPE_TEST_SESSION",widen(session).c_str());
  SetEnvironmentVariableW(L"AGI_SCOPE_TEST_HASH",widen(hash).c_str());
  wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
  for(const auto& mode : {"wrong-client","wrong-session","wrong-epoch","replay-sequence","future-sequence","missing-session"}) {
    SetEnvironmentVariableW(L"AGI_SCOPE_TEST_CLOSE_MODE",widen(mode).c_str());
    auto host=std::make_shared<HostTransportAuthority>(pairing);auto scope=HostTransportAuthorityTestAccess::Scope(*host);HostChannel channel;
    Check(channel.Start(executable,host),"actual launched hostile private close peer starts");
    Check(Until([&] { return !channel.live(); }),"invalid close client/session/epoch/sequence/shape closes actual host channel");
    Check(!scope->IsBoundAuthenticatedSession(c.client,session),"invalid close trust loss invalidates native session authority");
    channel.Stop();
  }
  for(const auto& name : {L"AGI_SCOPE_TEST_CLIENT",L"AGI_SCOPE_TEST_SESSION",L"AGI_SCOPE_TEST_HASH",L"AGI_SCOPE_TEST_CLOSE_MODE"})SetEnvironmentVariableW(name,nullptr);
  std::filesystem::remove_all(root);
}
void Lifecycle() {
  auto root=std::filesystem::temp_directory_path()/std::filesystem::path("agi-scoped-"+RandomId());
  std::filesystem::create_directories(root);
  auto pairing=std::make_shared<PairingAuthority>((root/L"authority.dpapi").wstring());
  auto key=GenerateKey();auto credential=Enroll(*pairing,key);
  auto host=std::make_shared<HostTransportAuthority>(pairing,[](const std::string& origin) { return origin=="https://example.test"; });
  auto scope=HostTransportAuthorityTestAccess::Scope(*host);HostChannel channel;wchar_t executable[32768]{};
  GetModuleFileNameW(nullptr,executable,32768);
  Check(channel.Start(executable,host),"real inherited host and TLS broker started");
  const Destination doc{"agent","tab","top","doc"};
  Check(Probe(channel.transport_port(),key,credential),"real mTLS pin checked probe has zero grants");
  Check(Until([&] { return HostTransportAuthorityTestAccess::Sessions(*host).empty(); }),"probe socket close reaches host without stranded reply");
  std::string revoked_session;
  {
    Connection connection(channel.transport_port(),key,credential); revoked_session=OneSession(*host);
    ScopeRequest read{credential.client,revoked_session,"observe",doc};
    Check(!scope->QueueScopeCheck(read,MonotonicMs()),"real authenticated host session starts at zero authority");
    Approve(*scope,GrantFor(credential,revoked_session),doc);
    auto cross=read;cross.destination.profile="human";Check(!scope->QueueScopeCheck(cross,MonotonicMs()),"actual host scope rejects cross profile read");
    cross.operation="click";Check(!scope->QueueScopeCheck(cross,MonotonicMs()),"actual host scope rejects cross profile write");
    auto ticket=scope->QueueScopeCheck(read,MonotonicMs());bool observed=false;
    Check(scope->WithCurrentScope(ticket,[&] { observed=true; })&&observed,"authorized native observation handoff executes");
    const auto queued_read=scope->QueueScopeCheck(read,MonotonicMs());auto write=read;write.operation="click";
    const auto queued_write=scope->QueueScopeCheck(write,MonotonicMs());Check(queued_read&&queued_write,"read and command queued before native revoke");
    pairing->Revoke(credential.client); bool sent=false;
    Check(!scope->WithCurrentScope(queued_read,[&] { sent=true; })&&!sent,"real native pairing revoke blocks queued observation delivery");
    Check(!scope->WithCurrentScope(queued_write,[&] { sent=true; })&&!sent,"real native pairing revoke blocks queued command handoff");
    Check(!scope->QueueScopeCheck(read,MonotonicMs()),"native revoke blocks subsequent observations");
    Check(connection.ClosedWithin(2000),"revoked identity actually closes active TLS socket");
  }
  credential=Enroll(*pairing,key);
  {
    Connection connection(channel.transport_port(),key,credential);const auto id=OneSession(*host);
    Check(id!=revoked_session,"same key reenrollment uses fresh session");
    ScopeRequest read{credential.client,id,"observe",doc};Check(!scope->QueueScopeCheck(read,MonotonicMs()),"reconnect and reenrollment restore zero grants");
    Check(!host->ValidateIdentity(credential.client,CertificateHash(ReadCertificate(credential.cert)),revoked_session),"new credential generation cannot revive revoked binding");
    Approve(*scope,GrantFor(credential,id),doc);
    auto queued=scope->QueueScopeCheck(read,MonotonicMs());
    Check(!host->DisconnectSession("wrong-client",id),"foreign client cannot disconnect host binding");
    connection.Close();Check(Until([&] { return HostTransportAuthorityTestAccess::Sessions(*host).empty(); }),"real socket close propagates private session_closed");
    bool sent=false;Check(!scope->WithCurrentScope(queued,[&] { sent=true; })&&!sent,"socket disconnect blocks already queued observation");
    Check(!scope->QueueScopeCheck(read,MonotonicMs()),"socket disconnect blocks subsequent observation");
    Check(host->DisconnectSession(credential.client,id),"known duplicate close idempotently stays revoked");
    Check(channel.live(),"duplicate known close preserves healthy host channel");
  }
  {
    Connection connection(channel.transport_port(),key,credential);const auto id=OneSession(*host);
    auto grant=GrantFor(credential,id);Approve(*scope,grant,doc);
    ScopeRequest read{credential.client,id,"observe",doc};const auto queued=scope->QueueScopeCheck(read,MonotonicMs());
    const auto started=MonotonicMs();channel.Stop();bool sent=false;
    Check(!scope->WithCurrentScope(queued,[&] { sent=true; })&&!sent,"host channel stop invalidates queued observation before teardown");
    Check(!scope->QueueScopeCheck(read,MonotonicMs()),"host channel loss blocks subsequent observation");
    Check(MonotonicMs()-started<3000&&connection.ClosedWithin(1000),"actual channel stop closes TLS socket within bound");
  }
  std::filesystem::remove_all(root);
}
void SocketChurn() {
  const auto root=std::filesystem::temp_directory_path()/std::filesystem::path("agi-scoped-churn-"+RandomId());
  std::filesystem::create_directories(root);
  auto pairing=std::make_shared<PairingAuthority>((root/L"authority.dpapi").wstring());
  auto key=GenerateKey();const auto credential=Enroll(*pairing,key);
  auto host=std::make_shared<HostTransportAuthority>(pairing);HostChannel channel;wchar_t executable[32768]{};
  GetModuleFileNameW(nullptr,executable,32768);Check(channel.Start(executable,host),"real host broker churn fixture started");
  const auto started=MonotonicMs();
  for(unsigned i=0;i<70;++i) {
    const auto probe_started=MonotonicMs();
    Check(Probe(channel.transport_port(),key,credential),"sequential real TLS socket churn after 64 sessions");
    const auto probe_finished=MonotonicMs();
    Check(Until([&] { return HostTransportAuthorityTestAccess::Sessions(*host).empty(); }),"churn close retires host binding");
    std::cout << "TIMING socket=" << i+1 << " probe_ms=" << probe_finished-probe_started
              << " close_ms=" << MonotonicMs()-probe_finished << " total_ms=" << MonotonicMs()-started << "\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  Check(MonotonicMs()-started<280000&&channel.live(),"70 socket sequence bounded and channel remains healthy");
  Check(Probe(channel.transport_port(),key,credential),"final identity exchange has no stranded close reply");
  channel.Stop();std::filesystem::remove_all(root);
}
}
int main(int argc,char** argv) {
  if(argc==2&&std::string(argv[1])=="--private-child") {
    const auto mode=TestEnvironment(L"AGI_SCOPE_TEST_CLOSE_MODE");if(!mode.empty())return HostilePrivateChild(mode);
    LoopbackServer server; return RunPrivateBroker(&server);
  }
  std::cout << std::unitbuf;
  try {
    if(argc==2&&std::string(argv[1])=="--socket-churn-test")SocketChurn();
    else { Wire(); NativeFailures(); HostileCloseFrames(); Lifecycle(); }
  }
  catch(const std::exception& e) { std::cerr << "FAIL " << e.what() << "\n";return 1; }
  std::cout << "scoped authenticated host lifecycle: " << checks << " checks passed\n";
}
