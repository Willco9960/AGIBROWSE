#include "lib/transport/server.h"
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
using namespace agi::transport;
namespace {
unsigned checks = 0;
void Check(bool ok, const char *name) {
  ++checks;
  if (!ok)
    throw std::runtime_error(name);
  std::cout << "PASS " << name << "\n";
}
template <class F> bool Throws(F fn) {
  try {
    fn();
    return false;
  } catch (...) {
    return true;
  }
}
Credential Enroll(PairingAuthority &authority, const Key &key) {
  auto p = authority.Begin(MakeCsr(key), MonotonicMs());
  return authority.Complete(p, Sign(key, p.proof_input), MonotonicMs());
}
namespace net = boost::asio;
namespace ssl = net::ssl;
namespace beast = boost::beast;
namespace ws = beast::websocket;
using tcp = net::ip::tcp;
struct Connection {
  net::io_context io;
  ssl::context context{ssl::context::tls_client};
  ws::stream<beast::ssl_stream<beast::tcp_stream>, false> socket{io, context};
  Connection(unsigned short port, const Key &key, const Credential &credential,
             bool upgrade = true, int version = TLS1_3_VERSION) {
    auto native = context.native_handle();
    SSL_CTX_set_min_proto_version(native, version);
    SSL_CTX_set_max_proto_version(native, version);
    auto ca = ReadCertificate(credential.ca),
         cert = ReadCertificate(credential.cert);
    SSL_CTX_use_certificate(native, cert.get());
    SSL_CTX_use_PrivateKey(native, key.get());
    X509_STORE_add_cert(SSL_CTX_get_cert_store(native), ca.get());
    // SSL object already exists: apply the identity directly as well.
    SSL_use_certificate(socket.next_layer().native_handle(), cert.get());
    SSL_use_PrivateKey(socket.next_layer().native_handle(), key.get());
    SSL_set_min_proto_version(socket.next_layer().native_handle(), version);
    SSL_set_max_proto_version(socket.next_layer().native_handle(), version);
    beast::get_lowest_layer(socket).connect(
        {net::ip::make_address("127.0.0.1"), port});
    socket.next_layer().handshake(ssl::stream_base::client);
    if (upgrade)
      socket.handshake("127.0.0.1:" + std::to_string(port), "/transport/v1");
  }
  bool ClosedWithin(unsigned milliseconds) {
    auto &raw = beast::get_lowest_layer(socket).socket();
    raw.non_blocking(true);
    auto started = MonotonicMs();
    char b;
    while (MonotonicMs() - started < milliseconds) {
      boost::system::error_code ec;
      raw.read_some(net::buffer(&b, 1), ec);
      if (ec && ec != net::error::would_block && ec != net::error::try_again)
        return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
  }
};
} // namespace
int main(int argc, char **argv) {
  if(argc==4&&std::string(argv[1])=="--transport-store-fixture") {
    try {
      auto path=std::filesystem::path(argv[3]);std::filesystem::create_directories(path.parent_path());
      if(std::string(argv[2])=="corrupt"){WritePublicFile(path.wstring(),"invalid protected store fixture");return 0;}
      if(std::string(argv[2])!="expired")return 2;
      auto ca=MakeAuthority(),server=MakeServer(ca);X509_gmtime_adj(X509_getm_notAfter(server.cert.get()),-60);X509_sign(server.cert.get(),ca.key.get(),EVP_sha256());
      std::string bytes="APR1";auto field=[&](const std::string& value){for(int i=0;i<4;++i)bytes.push_back(char(value.size()>>(8*i)));bytes+=value;};
      field(KeyPem(ca.key));field(CertPem(ca.cert));field(KeyPem(server.key));field(CertPem(server.cert));
      bytes.push_back(1);bytes.append(15,'\0');ProtectFile(path.wstring(),bytes);return 0;
    }catch(...){return 2;}
  }
  if (argc == 2 && std::string(argv[1]) == "--private-child") {
    LoopbackServer server;
    return agi::ipc::RunPrivateBroker(&server);
  }
  std::cout << std::unitbuf;
  try {
    auto root = std::filesystem::temp_directory_path() /
                std::filesystem::path("agi-transport-" + RandomId());
    std::filesystem::create_directories(root);
    auto store = (root / L"authority.dpapi").wstring();
    auto authority = std::make_shared<PairingAuthority>(store);
    auto key = GenerateKey();
    auto credential = Enroll(*authority, key);
    Check(authority->Paired(credential.client, credential.generation),
          "host-issued identity registered");
    auto protected_key = (root / L"client.dpapi").wstring();
    ProtectFile(protected_key, KeyPem(key));
    Check(Pin(ReadKey(UnprotectFile(protected_key))) == credential.client,
          "actual current-user DPAPI key roundtrip");
    Check(ReadBoundedFile(protected_key).find("PRIVATE KEY") ==
              std::string::npos,
          "persisted key not plaintext PEM");
    PairingAuthority restarted(store);
    Check(Pin(restarted.server().cert) == credential.server_pin &&
              restarted.Paired(credential.client, credential.generation),
          "restart preserves server pin and explicit pairing");
    auto pending = authority->Begin(MakeCsr(GenerateKey()), 1000);
    Check(Throws([&] { authority->Complete(pending, "bad", 301000); }),
          "enrollment expiry at300000ms");
    auto proof_key = GenerateKey();
    pending = authority->Begin(MakeCsr(proof_key), 1000);
    for (int i = 0; i < 5; ++i)
      Check(Throws([&] { authority->Complete(pending, "bad", 1001); }),
            "failed enrollment proof bounded");
    Check(Throws([&] {
            authority->Complete(pending, Sign(proof_key, pending.proof_input),
                                1002);
          }),
          "five proof failures consume challenge");
    pending = authority->Begin(MakeCsr(proof_key), 1000);
    auto copy = pending;
    auto c = authority->Complete(pending, Sign(proof_key, pending.proof_input),
                                 1001);
    Check(Throws([&] {
            authority->Complete(pending, Sign(proof_key, pending.proof_input),
                                1002);
          }),
          "enrollment replay rejected");
    authority->Revoke(c.client);
    Check(Throws([&] {
            authority->Complete(copy, Sign(proof_key, copy.proof_input), 1002);
          }),
          "copied challenge cannot reenroll after revoke");
    auto other = GenerateKey();
    pending = authority->Begin(MakeCsr(other), 1000);
    Check(Throws([&] {
            authority->Complete(pending, Sign(key, pending.proof_input), 1001);
          }),
          "proof bound to CSR public key");
    Check(Throws([&] { authority->Begin(MakeCsr(key) + "extra", 1000); }),
          "CSR trailing data rejected");
    Check(Throws([&] { authority->Begin(std::string(4097, 'a'), 1000); }),
          "CSR bytes bound");
    HostTransportAuthority host(authority);
    auto config = host.Configuration();
    config.channel_generation = 7;
    auto wire = agi::ipc::Encode(config);
    agi::ipc::Message decoded;
    Check(agi::ipc::Decode(wire, decoded),
          "closed IPCv2 transport config roundtrip");
    auto wrong = wire;
    wrong[8] = 1;
    Check(!agi::ipc::Decode(wrong, decoded),
          "IPCv1 cannot introduce transport config");
    wrong = wire;
    wrong.insert(wrong.end(), wire.begin() + 4, wire.begin() + 9);
    Check(!agi::ipc::Decode(wrong, decoded), "duplicate IPCv2 tag rejected");
    LoopbackServer server;
    Check(server.Start(
              config,
              [&](const auto &client, const auto &hash, const auto &session) {
                return host.ValidateIdentity(client, hash, session);
              }),
          "actual native TLS listener starts");
    Check(server.address() == "127.0.0.1" && server.port() > 0,
          "listener literal loopback assertion");
    Check(Probe(server.port(), key, credential),
          "actual TLS1.3 mTLS WSS valid pairing zero grants");
    auto invalid = credential;
    invalid.server_pin = std::string(64, '0');
    Check(!Probe(server.port(), key, invalid),
          "wrong server SPKI pin rejected");
    Check(!Probe(server.port(), key, credential, {}, nullptr, "/transport/v1",
                 false),
          "no client certificate rejected");
    auto unpaired = GenerateKey();
    auto ca = MakeAuthority();
    auto unpaired_cred = credential;
    unpaired_cred.client = Pin(unpaired);
    unpaired_cred.cert = CertPem(IssueClient(ca, unpaired));
    Check(!Probe(server.port(), unpaired, unpaired_cred),
          "arbitrary self-issued certificate rejected");
    std::string origin = "null";
    Check(!Probe(server.port(), key, credential, {}, &origin),
          "Origin null rejected before upgrade");
    origin = "http://127.0.0.1";
    Check(!Probe(server.port(), key, credential, {}, &origin),
          "browser Origin rejected before upgrade");
    origin = "";
    Check(!Probe(server.port(), key, credential, {}, &origin),
          "empty Origin header rejected before upgrade");
    Check(!Probe(server.port(), key, credential,
                 "localhost:" + std::to_string(server.port())),
          "DNS alias Host rejected");
    Check(!Probe(server.port(), key, credential, "evil.test"),
          "unexpected Host rejected");
    Check(!Probe(server.port(), key, credential, {}, nullptr,
                 "/transport/v1?credential=forged"),
          "query secrets and alternate target rejected");
    Check(!Probe(server.port(), key, credential, {}, nullptr, "/transport/v1",
                 true, std::string(257, 'a')),
          "WebSocket message size rejected");
    Check(!Probe(server.port(), key, credential, {}, nullptr, "/transport/v1",
                 true, "{\"grant\":{\"origin\":\"*\"}}"),
          "nested JSON and scope data rejected");
    {
      Connection live(server.port(), key, credential);
      authority->Revoke(credential.client);
      Check(live.ClosedWithin(1500),
            "live WSS session closes after host credential revocation");
    }
    Check(!Probe(server.port(), key, credential),
          "revoked credential rejected on fresh connection");
    PairingAuthority revoked_restart(store);
    Check(!revoked_restart.Paired(credential.client, credential.generation),
          "revocation persists across host restart");
    server.Stop();
    Check(
        LiveTransportSessionsForTest() == 0,
        "all closed server sessions destroyed without control callback cycle");
    auto fixture_ca = MakeAuthority(), fixture_server = MakeServer(fixture_ca);
    auto fixture_key = GenerateKey();
    Credential fixture_credential{
        Pin(fixture_key), CertPem(IssueClient(fixture_ca, fixture_key)),
        CertPem(fixture_ca.cert), Pin(fixture_server.cert), 1};
    config.server_key = KeyPem(fixture_server.key);
    config.server_cert = CertPem(fixture_server.cert);
    config.ca_cert = fixture_credential.ca;
    Check(server.Start(config,
                       [&](const auto &client, const auto &hash, const auto &) {
                         return client == fixture_credential.client &&
                                hash == CertificateHash(ReadCertificate(
                                            fixture_credential.cert));
                       }),
          "native CA fixture listener starts");
    auto same_ca_unpaired = fixture_credential;
    same_ca_unpaired.cert = CertPem(IssueClient(fixture_ca, GenerateKey()));
    Check(!Probe(server.port(), key, same_ca_unpaired),
          "wrong private key cannot use certificate");
    auto rogue_key = GenerateKey();
    same_ca_unpaired.cert = CertPem(IssueClient(fixture_ca, rogue_key));
    Check(!Probe(server.port(), rogue_key, same_ca_unpaired),
          "valid same-CA unpaired identity rejected by allowlist");
    auto expired = fixture_credential;
    expired.cert = CertPem(IssueClient(fixture_ca, fixture_key, -60));
    Check(!Probe(server.port(), fixture_key, expired),
          "actual expired client certificate rejected by TLS");
    Check(Throws([&] {
            Connection tls12(server.port(), fixture_key, fixture_credential,
                             false, TLS1_2_VERSION);
          }),
          "TLS1.2 downgrade rejected");
    for (const auto &extra : std::vector<std::string>{
             "Origin: null\r\nOrigin: http://evil.test\r\n",
             "Host: evil.test\r\n",
             "Sec-WebSocket-Extensions: permessage-deflate\r\n"}) {
      Connection connection(server.port(), fixture_key, fixture_credential,
                            false);
      std::string request =
          "GET /transport/v1 HTTP/1.1\r\nHost: 127.0.0.1:" +
          std::to_string(server.port()) +
          "\r\nUpgrade: websocket\r\nConnection: "
          "Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: "
          "dGhlIHNhbXBsZSBub25jZQ==\r\n" +
          extra + "\r\n";
      net::write(connection.socket.next_layer(), net::buffer(request));
      Check(
          connection.ClosedWithin(1500),
          "duplicate Origin Host or compression offer rejected before upgrade");
    }
    {
      Connection http_idle(server.port(), fixture_key, fixture_credential,
                           false);
      Check(http_idle.ClosedWithin(4500),
            "authenticated incomplete HTTP handshake deadline");
    }
    {
      Connection idle(server.port(), fixture_key, fixture_credential);
      Check(idle.ClosedWithin(6500), "WSS idle lifetime bound");
    }
    {
      net::io_context io;
      tcp::socket raw(io);
      raw.connect({net::ip::make_address("127.0.0.1"), server.port()});
      raw.non_blocking(true);
      auto start = MonotonicMs();
      char b;
      bool closed = false;
      while (MonotonicMs() - start < 4500) {
        boost::system::error_code ec;
        raw.read_some(net::buffer(&b, 1), ec);
        if (ec && ec != net::error::would_block &&
            ec != net::error::try_again) {
          closed = true;
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      Check(closed, "unauthenticated silent TLS handshake deadline");
    }
    {
      Connection rate(server.port(), fixture_key, fixture_credential);
      bool denied = false;
      for (int i = 0; i < 20; ++i)
        try {
          rate.socket.binary(true);
          rate.socket.write(net::buffer("AT01", 4));
          beast::flat_buffer result;
          rate.socket.read(result);
        } catch (...) {
          denied = true;
          break;
        }
      Check(denied, "per-session frame and message rate bound");
    }
    {
      net::io_context io;
      std::vector<std::unique_ptr<tcp::socket>> pending;
      for (int i = 0; i < 9; ++i) {
        auto socket = std::make_unique<tcp::socket>(io);
        socket->connect({net::ip::make_address("127.0.0.1"), server.port()});
        pending.push_back(std::move(socket));
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      Check(LiveTransportSessionsForTest() <= 8,
            "simultaneous unauthenticated session cap");
    }
    server.Stop();
    Check(LiveTransportSessionsForTest() == 0,
          "idle and unauthenticated sessions release resources");
    {
      net::io_context io;
      tcp::acceptor silent(io, {net::ip::make_address("127.0.0.1"), 0});
      auto start = MonotonicMs();
      Check(!Probe(silent.local_endpoint().port(), fixture_key,
                   fixture_credential) &&
                MonotonicMs() - start < 8500,
            "production native client deadline against silent endpoint");
    }
    auto durable =
        std::make_shared<PairingAuthority>((root / L"durable.dpapi").wstring());
    auto durable_key = GenerateKey();
    auto durable_c = Enroll(*durable, durable_key);
    std::filesystem::create_directory(root / L"durable.dpapi.new");
    Check(Throws([&] { durable->Revoke(durable_c.client); }),
          "simulated revocation persist failure reports failure");
    Check(!durable->Paired(durable_c.client, durable_c.generation),
          "revocation persist failure denies current runtime");
    Check(Throws([&] {
            PairingAuthority blocked((root / L"durable.dpapi").wstring());
          }),
          "pending revocation marker prevents resurrection on restart");
    auto broker_authority =
        std::make_shared<PairingAuthority>((root / L"broker.dpapi").wstring());
    auto broker_key = GenerateKey();
    auto broker_c = Enroll(*broker_authority, broker_key);
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    agi::ipc::HostChannel channel;
    Check(channel.Start(executable, std::make_shared<HostTransportAuthority>(
                                        broker_authority)),
          "actual inherited host-broker IPCv2 configuration starts listener");
    Check(Probe(channel.transport_port(), broker_key, broker_c),
          "paired WSS identity forwarded through actual host broker channel");
    {
      Connection connected(channel.transport_port(), broker_key, broker_c);
      auto start = MonotonicMs();
      channel.Stop();
      Check(MonotonicMs() - start < 3000 && connected.ClosedWithin(1000),
            "host channel loss closes live TLS client and broker within bound");
    }
    Check(!channel.live(), "host channel shutdown closes transport broker");
    Check(agi::ipc::Boundary(7).Admit(agi::ipc::Message{}, MonotonicMs()) ==
              agi::ipc::Decision::denied,
          "pairing creates no IPC application grants");
    std::cout << "PASS transport native checks=" << checks << "\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL transport case: " << e.what() << "\n";
    return 1;
  }
}
