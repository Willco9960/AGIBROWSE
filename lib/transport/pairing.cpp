#include "pairing.h"
// Win32 common-dialog declarations require the core Windows declarations first.
#include <windows.h>
#include <array>
#include <commdlg.h>
#include <filesystem>
#include <fstream>
#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/x509v3.h>
#include <stdexcept>
#include <wincrypt.h>
#include <windows.h>
namespace agi::transport {
namespace {
void Check(bool ok) {
  if (!ok)
    throw std::runtime_error("transport credential validation failed");
}
std::string BioString(BIO *bio) {
  char *p = nullptr;
  auto n = BIO_get_mem_data(bio, &p);
  Check(n > 0 && n <= kPemLimit);
  return {p, size_t(n)};
}
Key CsrKey(const std::string &pem) {
  Check(!pem.empty() && pem.size() <= kPemLimit);
  auto bio = std::unique_ptr<BIO, decltype(&BIO_free)>(
      BIO_new_mem_buf(pem.data(), int(pem.size())), BIO_free);
  auto csr = std::unique_ptr<X509_REQ, decltype(&X509_REQ_free)>(
      PEM_read_bio_X509_REQ(bio.get(), nullptr, nullptr, nullptr),
      X509_REQ_free);
  Check(bool(csr) && BIO_ctrl_pending(bio.get()) == 0 &&
        X509_REQ_get_version(csr.get()) == 0);
  Key key(X509_REQ_get_pubkey(csr.get()), EVP_PKEY_free);
  Check(bool(key));
  char group[64]{};
  size_t len = 0;
  Check(EVP_PKEY_is_a(key.get(), "EC") &&
        EVP_PKEY_get_utf8_string_param(key.get(), OSSL_PKEY_PARAM_GROUP_NAME,
                                       group, sizeof(group), &len) == 1 &&
        std::string(group) == "prime256v1");
  Check(X509_REQ_get_signature_nid(csr.get()) == NID_ecdsa_with_SHA256 &&
        X509_REQ_verify(csr.get(), key.get()) == 1);
  // Requested names/extensions never become certificate authority or scope.
  Check(X509_REQ_get_attr_count(csr.get()) == 0);
  return key;
}
Certificate Issue(const Identity &ca, const Key &key, const char *name,
                  bool authority, bool server, int seconds) {
  Certificate cert(X509_new(), X509_free);
  Check(bool(cert));
  Check(X509_set_version(cert.get(), 2) == 1);
  std::array<unsigned char, 16> serial{};
  Check(RAND_bytes(serial.data(), int(serial.size())) == 1);
  serial[0] &= 0x7f;
  serial[0] |= 1;
  auto bn = std::unique_ptr<BIGNUM, decltype(&BN_free)>(
      BN_bin2bn(serial.data(), int(serial.size()), nullptr), BN_free);
  auto number = std::unique_ptr<ASN1_INTEGER, decltype(&ASN1_INTEGER_free)>(
      BN_to_ASN1_INTEGER(bn.get(), nullptr), ASN1_INTEGER_free);
  Check(X509_set_serialNumber(cert.get(), number.get()) == 1 &&
        X509_gmtime_adj(X509_getm_notBefore(cert.get()), -30) &&
        X509_gmtime_adj(X509_getm_notAfter(cert.get()), seconds));
  Check(X509_set_pubkey(cert.get(), key.get()) == 1);
  auto subject = X509_get_subject_name(cert.get());
  Check(X509_NAME_add_entry_by_txt(
            subject, "CN", MBSTRING_ASC,
            reinterpret_cast<const unsigned char *>(name), -1, -1, 0) == 1);
  Check(X509_set_issuer_name(
            cert.get(),
            authority ? subject : X509_get_subject_name(ca.cert.get())) == 1);
  X509V3_CTX context{};
  X509V3_set_ctx(&context, authority ? cert.get() : ca.cert.get(), cert.get(),
                 nullptr, nullptr, 0);
  auto extension = [&](int nid, const char *value) {
    auto ext = std::unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)>(
        X509V3_EXT_conf_nid(nullptr, &context, nid, const_cast<char *>(value)),
        X509_EXTENSION_free);
    Check(bool(ext) && X509_add_ext(cert.get(), ext.get(), -1) == 1);
  };
  extension(NID_basic_constraints,
            authority ? "critical,CA:TRUE,pathlen:0" : "critical,CA:FALSE");
  extension(NID_key_usage, authority ? "critical,keyCertSign,cRLSign"
                                     : "critical,digitalSignature");
  if (!authority)
    extension(NID_ext_key_usage, server ? "serverAuth" : "clientAuth");
  if (server)
    extension(NID_subject_alt_name, "IP:127.0.0.1");
  Check(X509_sign(cert.get(), authority ? key.get() : ca.key.get(),
                  EVP_sha256()) > 0);
  return cert;
}
void Field(std::string &out, const std::string &value) {
  Check(value.size() <= kPemLimit);
  uint32_t n = uint32_t(value.size());
  for (int i = 0; i < 4; ++i)
    out.push_back(char(n >> (8 * i)));
  out += value;
}
std::string Take(const std::string &bytes, size_t &pos) {
  Check(pos + 4 <= bytes.size());
  uint32_t n = 0;
  for (int i = 0; i < 4; ++i)
    n |= uint32_t(uint8_t(bytes[pos++])) << (8 * i);
  Check(n <= kPemLimit && pos + n <= bytes.size());
  auto value = bytes.substr(pos, n);
  pos += n;
  return value;
}
void Integer(std::string &out, uint64_t n) {
  for (int i = 0; i < 8; ++i)
    out.push_back(char(n >> (8 * i)));
}
uint64_t Number(const std::string &bytes, size_t &pos) {
  Check(pos + 8 <= bytes.size());
  uint64_t n = 0;
  for (int i = 0; i < 8; ++i)
    n |= uint64_t(uint8_t(bytes[pos++])) << (8 * i);
  return n;
}
bool Pick(HWND owner, bool save, const wchar_t *title, std::wstring &path) {
  wchar_t buffer[32768]{};
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = owner;
  dialog.lpstrFile = buffer;
  dialog.nMaxFile = 32768;
  dialog.lpstrTitle = title;
  dialog.lpstrFilter = L"Enrollment files\0*.agipair;*.pem\0All files\0*.*\0";
  dialog.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST |
                 (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
  if (!(save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog)))
    return false;
  path = buffer;
  return true;
}
} // namespace
uint64_t MonotonicMs() { return GetTickCount64(); }
Key GenerateKey() {
  Key key(EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "prime256v1"),
          EVP_PKEY_free);
  Check(bool(key));
  return key;
}
Key ReadKey(const std::string &pem) {
  Check(pem.size() <= kPemLimit);
  auto bio = std::unique_ptr<BIO, decltype(&BIO_free)>(
      BIO_new_mem_buf(pem.data(), int(pem.size())), BIO_free);
  Key key(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr),
          EVP_PKEY_free);
  Check(bool(key) && BIO_ctrl_pending(bio.get()) == 0);
  return key;
}
Certificate ReadCertificate(const std::string &pem) {
  Check(!pem.empty() && pem.size() <= kPemLimit);
  auto bio = std::unique_ptr<BIO, decltype(&BIO_free)>(
      BIO_new_mem_buf(pem.data(), int(pem.size())), BIO_free);
  Certificate cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr),
                   X509_free);
  Check(bool(cert) && BIO_ctrl_pending(bio.get()) == 0);
  return cert;
}
std::string KeyPem(const Key &key) {
  auto bio =
      std::unique_ptr<BIO, decltype(&BIO_free)>(BIO_new(BIO_s_mem()), BIO_free);
  Check(PEM_write_bio_PrivateKey(bio.get(), key.get(), nullptr, nullptr, 0,
                                 nullptr, nullptr) == 1);
  return BioString(bio.get());
}
std::string CertPem(const Certificate &cert) {
  auto bio =
      std::unique_ptr<BIO, decltype(&BIO_free)>(BIO_new(BIO_s_mem()), BIO_free);
  Check(PEM_write_bio_X509(bio.get(), cert.get()) == 1);
  return BioString(bio.get());
}
std::string Pin(const Key &key) {
  unsigned char *der = nullptr;
  int n = i2d_PUBKEY(key.get(), &der);
  Check(n > 0);
  unsigned char digest[32];
  unsigned size = 0;
  Check(EVP_Digest(der, n, digest, &size, EVP_sha256(), nullptr) == 1 &&
        size == 32);
  OPENSSL_free(der);
  std::string out;
  static const char hex[] = "0123456789abcdef";
  for (auto b : digest) {
    out += hex[b >> 4];
    out += hex[b & 15];
  }
  return out;
}
std::string Pin(const Certificate &cert) {
  Key key(X509_get_pubkey(cert.get()), EVP_PKEY_free);
  return Pin(key);
}
std::string MakeCsr(const Key &key) {
  auto csr = std::unique_ptr<X509_REQ, decltype(&X509_REQ_free)>(X509_REQ_new(),
                                                                 X509_REQ_free);
  Check(X509_REQ_set_version(csr.get(), 0) == 1 &&
        X509_REQ_set_pubkey(csr.get(), key.get()) == 1);
  Check(X509_REQ_sign(csr.get(), key.get(), EVP_sha256()) > 0);
  auto bio =
      std::unique_ptr<BIO, decltype(&BIO_free)>(BIO_new(BIO_s_mem()), BIO_free);
  Check(PEM_write_bio_X509_REQ(bio.get(), csr.get()) == 1);
  return BioString(bio.get());
}
std::string Sign(const Key &key, const std::string &input) {
  auto ctx = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>(
      EVP_MD_CTX_new(), EVP_MD_CTX_free);
  Check(EVP_DigestSignInit(ctx.get(), nullptr, EVP_sha256(), nullptr,
                           key.get()) == 1);
  size_t n = 0;
  Check(EVP_DigestSign(ctx.get(), nullptr, &n,
                       reinterpret_cast<const unsigned char *>(input.data()),
                       input.size()) == 1);
  std::string result(n, '\0');
  Check(EVP_DigestSign(ctx.get(),
                       reinterpret_cast<unsigned char *>(result.data()), &n,
                       reinterpret_cast<const unsigned char *>(input.data()),
                       input.size()) == 1);
  result.resize(n);
  return result;
}
bool Verify(const Key &key, const std::string &input,
            const std::string &signature) {
  if (signature.empty() || signature.size() > 128)
    return false;
  auto ctx = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>(
      EVP_MD_CTX_new(), EVP_MD_CTX_free);
  return EVP_DigestVerifyInit(ctx.get(), nullptr, EVP_sha256(), nullptr,
                              key.get()) == 1 &&
         EVP_DigestVerify(
             ctx.get(),
             reinterpret_cast<const unsigned char *>(signature.data()),
             signature.size(),
             reinterpret_cast<const unsigned char *>(input.data()),
             input.size()) == 1;
}
Identity MakeAuthority() {
  Identity ca;
  ca.key = GenerateKey();
  ca.cert =
      Issue(ca, ca.key, "AGI-BROWSE local enrollment", true, false, 315360000);
  return ca;
}
Identity MakeServer(const Identity &ca) {
  Identity server;
  server.key = GenerateKey();
  server.cert = Issue(ca, server.key, "127.0.0.1", false, true, 2592000);
  return server;
}
Certificate IssueClient(const Identity &ca, const Key &key, int seconds) {
  return Issue(ca, key, "AGI-BROWSE paired client", false, false, seconds);
}
bool ValidCertificate(const Certificate &cert) {
  return cert && X509_cmp_current_time(X509_get0_notBefore(cert.get())) < 0 &&
         X509_cmp_current_time(X509_get0_notAfter(cert.get())) > 0;
}
std::string RandomId() {
  std::array<unsigned char, 32> bytes{};
  Check(RAND_bytes(bytes.data(), int(bytes.size())) == 1);
  static const char hex[] = "0123456789abcdef";
  std::string id;
  for (auto b : bytes) {
    id += hex[b >> 4];
    id += hex[b & 15];
  }
  return id;
}
std::string ReadBoundedFile(const std::wstring &path, size_t limit) {
  std::ifstream input(std::filesystem::path(path),
                      std::ios::binary | std::ios::ate);
  Check(bool(input));
  auto size = input.tellg();
  Check(size > 0 && uint64_t(size) <= limit);
  std::string bytes(size_t(size), '\0');
  input.seekg(0);
  input.read(bytes.data(), size);
  Check(bool(input));
  return bytes;
}
void WritePublicFile(const std::wstring &path, const std::string &bytes) {
  {
    std::ofstream file(std::filesystem::path(path + L".tmp"),
                       std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), bytes.size());
    file.flush();
    Check(bool(file));
  }
  Check(MoveFileExW((path + L".tmp").c_str(), path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ==
        TRUE);
}
void ProtectFile(const std::wstring &path, const std::string &bytes) {
  Check(bytes.size() <= 262144);
  DATA_BLOB input{DWORD(bytes.size()),
                  reinterpret_cast<BYTE *>(const_cast<char *>(bytes.data()))},
      output{};
  Check(CryptProtectData(&input, L"AGI-BROWSE transport v1", nullptr, nullptr,
                         nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output) == TRUE);
  std::string blob(reinterpret_cast<char *>(output.pbData), output.cbData);
  LocalFree(output.pbData);
  WritePublicFile(path + L".new", blob);
  Check(MoveFileExW((path + L".new").c_str(), path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ==
        TRUE);
}
std::string UnprotectFile(const std::wstring &path) {
  auto bytes = ReadBoundedFile(path, 262144);
  DATA_BLOB input{DWORD(bytes.size()), reinterpret_cast<BYTE *>(bytes.data())},
      output{};
  Check(CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                           CRYPTPROTECT_UI_FORBIDDEN, &output) == TRUE);
  std::string value(reinterpret_cast<char *>(output.pbData), output.cbData);
  SecureZeroMemory(output.pbData, output.cbData);
  LocalFree(output.pbData);
  return value;
}
std::string EncodeCredential(const Credential &c) {
  std::string bytes = "ACR1";
  Field(bytes, c.client);
  Field(bytes, c.cert);
  Field(bytes, c.ca);
  Field(bytes, c.server_pin);
  Integer(bytes, c.generation);
  return bytes;
}
Credential DecodeCredential(const std::string &bytes) {
  Check(bytes.size() <= 16384 && bytes.substr(0, 4) == "ACR1");
  size_t pos = 4;
  Credential c;
  c.client = Take(bytes, pos);
  c.cert = Take(bytes, pos);
  c.ca = Take(bytes, pos);
  c.server_pin = Take(bytes, pos);
  c.generation = Number(bytes, pos);
  Check(pos == bytes.size() && c.client.size() == 64 &&
        c.server_pin.size() == 64 && c.generation > 0 &&
        Pin(ReadCertificate(c.cert)) == c.client);
  ReadCertificate(c.ca);
  return c;
}
PairingAuthority::PairingAuthority(std::wstring path) : path_(std::move(path)) {
  std::filesystem::create_directories(
      std::filesystem::path(path_).parent_path());
  Check(!std::filesystem::exists(path_ + L".revoking"));
  if (!std::filesystem::exists(path_)) {
    authority_ = MakeAuthority();
    server_ = MakeServer(authority_);
    Save();
    return;
  }
  auto bytes = UnprotectFile(path_);
  Check(bytes.substr(0, 4) == "APR1");
  size_t pos = 4;
  authority_.key = ReadKey(Take(bytes, pos));
  authority_.cert = ReadCertificate(Take(bytes, pos));
  server_.key = ReadKey(Take(bytes, pos));
  server_.cert = ReadCertificate(Take(bytes, pos));
  generation_ = Number(bytes, pos);
  auto count = Number(bytes, pos);
  Check(count <= kPairLimit && generation_ > 0);
  for (uint64_t i = 0; i < count; ++i) {
    auto encoded = Take(bytes, pos);
    auto c = DecodeCredential(encoded);
    Check(clients_.emplace(c.client, c).second);
  }
  Check(pos == bytes.size() && ValidCertificate(server_.cert) &&
        ValidCertificate(authority_.cert) &&
        X509_check_private_key(authority_.cert.get(), authority_.key.get()) ==
            1 &&
        X509_check_private_key(server_.cert.get(), server_.key.get()) == 1 &&
        X509_check_ca(authority_.cert.get()) > 0 &&
        X509_verify(authority_.cert.get(), authority_.key.get()) == 1 &&
        X509_verify(server_.cert.get(), authority_.key.get()) == 1);
  for (const auto &[id, c] : clients_)
    Check(c.ca == ca() && c.server_pin == Pin(server_.cert) &&
          X509_verify(ReadCertificate(c.cert).get(), authority_.key.get()) ==
              1);
  OPENSSL_cleanse(bytes.data(), bytes.size());
}
void PairingAuthority::Save() {
  std::string bytes = "APR1";
  Field(bytes, KeyPem(authority_.key));
  Field(bytes, CertPem(authority_.cert));
  Field(bytes, KeyPem(server_.key));
  Field(bytes, CertPem(server_.cert));
  Integer(bytes, generation_);
  Integer(bytes, clients_.size());
  for (const auto &[id, c] : clients_)
    Field(bytes, EncodeCredential(c));
  ProtectFile(path_, bytes);
  OPENSSL_cleanse(bytes.data(), bytes.size());
}
Pending PairingAuthority::Begin(const std::string &csr, uint64_t now) {
  std::lock_guard lock(mutex_);
  auto key = CsrKey(csr);
  Check(now <= UINT64_MAX - 300000);
  Pending p;
  p.csr = csr;
  p.client = Pin(key);
  p.deadline = now + 300000;
  p.proof_input =
      "AEN1\n" + p.client + "\n" + Pin(server_.cert) + "\n" + RandomId();
  pending_.clear();
  pending_.emplace(p.proof_input, p);
  return p;
}
Credential PairingAuthority::Complete(Pending &supplied,
                                      const std::string &signature,
                                      uint64_t now) {
  std::lock_guard lock(mutex_);
  auto found = pending_.find(supplied.proof_input);
  Check(found != pending_.end());
  auto &p = found->second;
  Check(!p.consumed && now < p.deadline && p.failures < 5 &&
        p.csr == supplied.csr && p.client == supplied.client);
  auto key = CsrKey(p.csr);
  if (!Verify(key, p.proof_input, signature)) {
    ++p.failures;
    if (p.failures == 5)
      p.consumed = true;
    supplied = p;
    throw std::runtime_error("enrollment proof rejected");
  }
  p.consumed = true;
  supplied = p;
  Check(clients_.size() < kPairLimit && clients_.count(p.client) == 0 &&
        generation_ < UINT64_MAX);
  Credential c{p.client, CertPem(IssueClient(authority_, key)), ca(),
               Pin(server_.cert), ++generation_};
  clients_.emplace(c.client, c);
  try {
    Save();
  } catch (...) {
    clients_.erase(c.client);
    throw;
  }
  return c;
}
bool PairingAuthority::Paired(const std::string &client,
                              uint64_t generation) const {
  std::lock_guard lock(mutex_);
  auto found = clients_.find(client);
  return found != clients_.end() && found->second.generation == generation &&
         ValidCertificate(ReadCertificate(found->second.cert));
}
std::vector<Credential> PairingAuthority::Clients() const {
  std::lock_guard lock(mutex_);
  std::vector<Credential> result;
  for (const auto &[id, c] : clients_)
    result.push_back(c);
  return result;
}
void PairingAuthority::Revoke(const std::string &client) {
  std::lock_guard lock(mutex_);
  if (clients_.count(client)) {
    Check(generation_ < UINT64_MAX);
    ProtectFile(path_ + L".revoking", client);
    clients_.erase(client);
    ++generation_;
    Save();
    Check(DeleteFileW((path_ + L".revoking").c_str()) == TRUE);
  }
}
bool NativeEnroll(void *owner, PairingAuthority &authority) {
  std::string issued;
  try {
    auto window = static_cast<HWND>(owner);
    std::wstring path;
    if (!Pick(window, false, L"Select the local client signed CSR", path))
      return false;
    auto pending =
        authority.Begin(ReadBoundedFile(path, kPemLimit), MonotonicMs());
    std::wstring display = L"Pair this local client?\n\nClient SHA256: ";
    display.append(pending.client.begin(), pending.client.end());
    display += L"\nServer SHA256: ";
    auto pin = Pin(authority.server().cert);
    display.append(pin.begin(), pin.end());
    display += L"\n\nCompare both fingerprints with your native client. "
               L"Pairing grants no tabs or website access.";
    if (MessageBoxW(window, display.c_str(), L"AGI-BROWSE native enrollment",
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
      return false;
    if (!Pick(window, true,
              L"Save one-use enrollment challenge; expires in 300 seconds",
              path))
      return false;
    WritePublicFile(path, pending.proof_input);
    Credential credential;
    bool complete = false;
    while (!pending.consumed && pending.failures < 5 &&
           MonotonicMs() < pending.deadline) {
      if (!Pick(window, false, L"Select the client's signed challenge proof",
                path))
        return false;
      try {
        credential = authority.Complete(pending, ReadBoundedFile(path, 128),
                                        MonotonicMs());
        issued = credential.client;
        complete = true;
        break;
      } catch (...) {
        MessageBoxW(
            window,
            L"Proof rejected or expired. At most five proofs are allowed.",
            L"Enrollment", MB_OK | MB_ICONERROR);
      }
    }
    if (!complete)
      return false;
    if (!Pick(window, true,
              L"Save issued client certificate and pinned server identity",
              path)) {
      authority.Revoke(issued);
      return false;
    }
    WritePublicFile(path, EncodeCredential(credential));
    return true;
  } catch (...) {
    if (!issued.empty()) {
      try {
        authority.Revoke(issued);
      } catch (...) {
      }
    }
    MessageBoxW(static_cast<HWND>(owner),
                L"Enrollment failed closed. No network approval is available.",
                L"AGI-BROWSE enrollment", MB_OK | MB_ICONERROR);
    return false;
  }
}
} // namespace agi::transport
