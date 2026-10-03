#pragma once
#include "lib/ipc/windows_channel.h"
#include "pairing.h"
#include <functional>
#include <memory>
namespace agi::transport {
std::string CertificateHash(const Certificate &cert);
class HostTransportAuthority final : public ipc::NativeTransportAuthority {
public:
  explicit HostTransportAuthority(std::shared_ptr<PairingAuthority> authority)
      : authority_(std::move(authority)) {}
  ipc::Message Configuration() override;
  bool ValidateIdentity(const std::string &client,
                        const std::string &certificate_hash,
                        const std::string &session) override;

private:
  std::shared_ptr<PairingAuthority> authority_;
  struct Binding {
    std::string client, certificate;
    uint64_t deadline;
  };
  std::mutex mutex_;
  std::map<std::string, Binding> sessions_;
};
class LoopbackServer final : public ipc::BrokerTransport {
public:
  LoopbackServer();
  ~LoopbackServer();
  bool Start(const ipc::Message &config,
             std::function<bool(const std::string &, const std::string &,
                                const std::string &)>
                 authorize) override;
  void Stop() override;
  unsigned short port() const override;
  std::string address() const;

private:
  struct State;
  std::shared_ptr<State> state_;
};
// Native client: enrolled CA chain, IP identity, time validity AND explicit
// SPKI. No trust-on-first-use, no plaintext fallback. Probe grants no
// application scope.
bool Probe(unsigned short port, const Key &key, const Credential &credential,
           const std::string &host = {}, const std::string *origin = nullptr,
           const std::string &target = "/transport/v1",
           bool send_certificate = true, const std::string &payload = "AT01");
size_t LiveTransportSessionsForTest();
} // namespace agi::transport
