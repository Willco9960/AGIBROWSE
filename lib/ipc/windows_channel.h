#pragma once
#ifdef _WIN32
#include <windows.h>
#include <atomic>
#include <memory>
#include <thread>
#include <functional>
#include "lib/ipc/protocol.h"
namespace agi::ipc {
class NativeTransportAuthority {
 public:
  virtual ~NativeTransportAuthority() = default;
  virtual Message Configuration() = 0;
  virtual bool ValidateIdentity(const std::string& client,const std::string& certificate_hash,const std::string& session) = 0;
};
class BrokerTransport {
 public:
  virtual ~BrokerTransport() = default;
  virtual bool Start(const Message& configuration,std::function<bool(const std::string&,const std::string&,const std::string&)> authorize) = 0;
  virtual void Stop() = 0;
  virtual unsigned short port() const = 0;
};
class HostChannel {
 public:
  HostChannel() = default;
  ~HostChannel();
  HostChannel(const HostChannel&) = delete;
  HostChannel& operator=(const HostChannel&) = delete;
  bool Start(const std::wstring& broker,std::shared_ptr<NativeTransportAuthority> authority = nullptr);
  void Stop();
  bool live() const { return live_; }
  unsigned short transport_port() const { return transport_port_; }
  DWORD process_id() const { return process_id_; }
  HANDLE process_handle_for_test() const { return process_; }
  std::vector<std::pair<uintptr_t,std::wstring>> EndpointDiagnosticsForTest() const;
 private:
  HANDLE input_ = nullptr, output_ = nullptr, process_ = nullptr, job_ = nullptr;
  DWORD process_id_ = 0;
  uint64_t generation_ = 0;
  std::atomic<bool> live_{false}, stopping_{false};
  std::thread worker_;
  void Serve();
  std::shared_ptr<NativeTransportAuthority> transport_authority_;
  unsigned short transport_port_ = 0;
};
// Dedicated broker entry only: inherited handles are the sole admission path.
// No named endpoint, argument secret, network service or grant input.
int RunPrivateBroker(BrokerTransport* transport = nullptr);
}  // namespace agi::ipc
#endif
