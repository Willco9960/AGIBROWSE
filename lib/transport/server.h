#pragma once
#include "lib/ipc/windows_channel.h"
#include "pairing.h"
#include "lib/ipc/scoped_authority.h"
#include <functional>
#include <memory>
namespace agi::transport {
std::string CertificateHash(const Certificate &cert);
class HostTransportAuthority final : public ipc::NativeTransportAuthority {
public:
  explicit HostTransportAuthority(std::shared_ptr<PairingAuthority> authority,
      ipc::ScopedAuthority::OriginValidator validator = [](const std::string&) { return false; });
  ~HostTransportAuthority() override { InvalidateSessions(); }
  ipc::Message Configuration() override;
  bool ValidateIdentity(const std::string &client,
                        const std::string &certificate_hash,
                        const std::string &session) override;
  bool DisconnectSession(const std::string& client,const std::string& session) override;
  void InvalidateSessions() override;
  // Native lifecycle barrier, never callable from IPC/page input.
  void InvalidateNativeTab(const std::string& profile, const std::string& tab) { scopes_->InvalidateNativeTab(profile, tab); }

private:
  friend struct HostTransportAuthorityTestAccess;
  std::shared_ptr<PairingAuthority> authority_;
  struct Binding {
    std::string client, certificate;
    uint64_t deadline;
    uint64_t credential_generation;
    bool live = true;
  };
  std::mutex mutex_;
  std::map<std::string, Binding> sessions_;
  bool live_ = true;
  std::shared_ptr<ipc::ScopedAuthority> scopes_;
  std::shared_ptr<PairingAuthority::RevocationCallback> revocation_callback_;
};
class LoopbackServer final : public ipc::BrokerTransport {
public:
  LoopbackServer();
  ~LoopbackServer();
  bool Start(const ipc::Message &config,
             std::function<bool(const std::string &, const std::string &,
                                const std::string &)>
                 authorize, std::function<void(const std::string&,const std::string&)> disconnect = {}) override;
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
