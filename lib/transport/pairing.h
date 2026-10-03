#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <string>
#include <vector>
namespace agi::transport {
using Key = std::shared_ptr<EVP_PKEY>;
using Certificate = std::shared_ptr<X509>;
constexpr size_t kPemLimit = 4096, kPairLimit = 32;
struct Identity {
  Key key;
  Certificate cert;
};
struct Credential {
  std::string client, cert, ca, server_pin;
  uint64_t generation = 0;
};
struct Pending {
  std::string client, csr, proof_input;
  uint64_t deadline = 0;
  unsigned failures = 0;
  bool consumed = false;
};
Key GenerateKey();
Key ReadKey(const std::string &pem);
Certificate ReadCertificate(const std::string &pem);
std::string KeyPem(const Key &key);
std::string CertPem(const Certificate &cert);
std::string Pin(const Key &key);
std::string Pin(const Certificate &cert);
std::string MakeCsr(const Key &key);
std::string Sign(const Key &key, const std::string &input);
bool Verify(const Key &key, const std::string &input,
            const std::string &signature);
Identity MakeAuthority();
Identity MakeServer(const Identity &authority);
Certificate IssueClient(const Identity &authority, const Key &key,
                        int valid_seconds = 2592000);
bool ValidCertificate(const Certificate &cert);
std::string RandomId();
uint64_t MonotonicMs();
void ProtectFile(const std::wstring &path, const std::string &bytes);
std::string UnprotectFile(const std::wstring &path);
void WritePublicFile(const std::wstring &path, const std::string &bytes);
std::string ReadBoundedFile(const std::wstring &path, size_t limit = 16384);
// Host-only enrollment/allowlist. This class is never constructed by the
// broker.
class PairingAuthority {
public:
  explicit PairingAuthority(std::wstring path);
  Pending Begin(const std::string &csr, uint64_t now);
  Credential Complete(Pending &pending, const std::string &signature,
                      uint64_t now);
  bool Paired(const std::string &client, uint64_t generation) const;
  void Revoke(const std::string &client);
  Identity server() const { return server_; }
  std::string ca() const { return CertPem(authority_.cert); }
  std::vector<Credential> Clients() const;

private:
  std::wstring path_;
  Identity authority_, server_;
  mutable std::mutex mutex_;
  std::map<std::string, Credential> clients_;
  uint64_t generation_ = 1;
  std::map<std::string, Pending> pending_;
  void Save();
};
// Only a native host gesture calls this. File selection/signing stays local.
bool NativeEnroll(void *owner, PairingAuthority &authority);
std::string EncodeCredential(const Credential &credential);
Credential DecodeCredential(const std::string &bytes);
} // namespace agi::transport
