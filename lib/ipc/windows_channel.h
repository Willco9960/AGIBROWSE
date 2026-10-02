#pragma once
#ifdef _WIN32
#include <windows.h>
#include <atomic>
#include <memory>
#include <thread>
#include "lib/ipc/protocol.h"
namespace agi::ipc {
class HostChannel {
 public:
  HostChannel() = default;
  ~HostChannel();
  HostChannel(const HostChannel&) = delete;
  HostChannel& operator=(const HostChannel&) = delete;
  bool Start(const std::wstring& broker);
  void Stop();
  bool live() const { return live_; }
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
};
// Dedicated broker entry only: inherited handles are the sole admission path.
// No named endpoint, argument secret, network service or grant input.
int RunPrivateBroker();
}  // namespace agi::ipc
#endif
