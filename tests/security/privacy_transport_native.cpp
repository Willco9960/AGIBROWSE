#include "lib/transport/server.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace agi::transport;
namespace {
unsigned checks=0;
void Check(bool ok,const char* name) {++checks;if(!ok)throw std::runtime_error(name);std::cout<<"PASS "<<name<<'\n';}
struct TempStore {
  static std::filesystem::path Parent() {
    auto path=std::filesystem::absolute(std::filesystem::temp_directory_path()).lexically_normal();
    // Win32's temp directory commonly has a terminal separator. Remove only
    // that empty filename component before exact child-parent comparisons.
    if(path.filename().empty())path=path.parent_path();
    return path;
  }
  const std::filesystem::path parent=Parent();
  const std::string name="agi-privacy-"+RandomId();
  const std::filesystem::path root=(parent/name).lexically_normal();
  TempStore(){
    if(root.parent_path()!=parent || root.filename()!=name || !std::filesystem::create_directory(root))
      throw std::runtime_error("owned temporary privacy store unavailable");
  }
  bool Cleanup() noexcept {
    if(root.parent_path()!=parent || root.filename()!=name)return false;
    std::error_code error;std::filesystem::remove_all(root,error);
    return !error && !std::filesystem::exists(root,error) && !error;
  }
  ~TempStore(){Cleanup();}
};
}
int main(int argc,char** argv) {
  try {
    Check(argc==2,"actual production broker path provided");
    TempStore store;
    auto authority=std::make_shared<PairingAuthority>((store.root/L"authority.dpapi").wstring());
    auto key=GenerateKey();auto challenge=authority->Begin(MakeCsr(key),MonotonicMs());
    auto credential=authority->Complete(challenge,Sign(key,challenge.proof_input),MonotonicMs());
    agi::ipc::HostChannel channel;
    Check(channel.Start(std::filesystem::path(argv[1]).wstring(),std::make_shared<HostTransportAuthority>(authority)),"actual production broker private channel and TLS listener started");
    // Production Probe validates TLS1.3, CA/IP/SPKI and exact binary response
    // equality with PERMISSION_DENIED. No native fixture grants are installed.
    Check(Probe(channel.transport_port(),key,credential),"actual broker AT01 exports exactly generic PERMISSION_DENIED");
    Check(!Probe(channel.transport_port(),key,credential,{},nullptr,"/transport/v1",true,"SENTINEL_PASSWORD_010"),"sentinel payload cannot become broker publication");
    Check(Probe(channel.transport_port(),key,credential),"broker remains healthy with zero grants after sentinel rejection");
    channel.Stop();Check(!channel.live(),"actual production broker stops cleanly");
    Check(store.Cleanup(),"exact owned temporary credential store removed");
    std::cout<<"PASS privacy broker checks="<<checks<<'\n';return 0;
  } catch(...) {std::cerr<<"FAIL privacy broker export test closed\n";return 1;}
}
