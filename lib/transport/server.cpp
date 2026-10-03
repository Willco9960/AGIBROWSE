#include "server.h"
#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <chrono>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <set>
#include <thread>
namespace agi::transport {
namespace net = boost::asio;
namespace ssl = net::ssl;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = net::ip::tcp;
using Error = boost::system::error_code;
namespace {
std::atomic<size_t> live_sessions{0};
void Require(bool value) {
  if (!value)
    throw std::runtime_error("TLS transport initialization failed");
}
void Context(ssl::context &ctx, const Identity &identity, const std::string &ca,
             bool server) {
  auto native = ctx.native_handle();
  Require(SSL_CTX_set_min_proto_version(native, TLS1_3_VERSION) == 1 &&
          SSL_CTX_set_max_proto_version(native, TLS1_3_VERSION) == 1);
  SSL_CTX_set_options(native, SSL_OP_NO_COMPRESSION | SSL_OP_NO_TICKET);
  SSL_CTX_set_session_cache_mode(native, SSL_SESS_CACHE_OFF);
  SSL_CTX_set_num_tickets(native, 0);
  SSL_CTX_set_max_early_data(native, 0);
  Require(SSL_CTX_use_certificate(native, identity.cert.get()) == 1 &&
          SSL_CTX_use_PrivateKey(native, identity.key.get()) == 1 &&
          SSL_CTX_check_private_key(native) == 1);
  auto authority = ReadCertificate(ca);
  Require(X509_STORE_add_cert(SSL_CTX_get_cert_store(native),
                              authority.get()) == 1);
  ctx.set_verify_mode(
      server ? (ssl::verify_peer | ssl::verify_fail_if_no_peer_cert)
             : ssl::verify_peer);
  ctx.set_verify_depth(1);
}
} // namespace
std::string CertificateHash(const Certificate &cert) {
  unsigned char hash[32];
  unsigned n = 0;
  Require(X509_digest(cert.get(), EVP_sha256(), hash, &n) == 1 && n == 32);
  static const char hex[] = "0123456789abcdef";
  std::string out;
  for (auto b : hash) {
    out += hex[b >> 4];
    out += hex[b & 15];
  }
  return out;
}
ipc::Message HostTransportAuthority::Configuration() {
  ipc::Message m;
  m.version = 2;
  m.kind = ipc::Kind::transport_config;
  auto server = authority_->server();
  m.server_key = KeyPem(server.key);
  m.server_cert = CertPem(server.cert);
  m.ca_cert = authority_->ca();
  return m;
}
bool HostTransportAuthority::ValidateIdentity(const std::string &client,
                                              const std::string &hash,
                                              const std::string &session) {
  if (session.size() != 64)
    return false;
  bool paired = false;
  for (const auto &c : authority_->Clients())
    if (c.client == client && authority_->Paired(client, c.generation) &&
        CertificateHash(ReadCertificate(c.cert)) == hash) {
      paired = true;
      break;
    }
  if (!paired)
    return false;
  std::lock_guard lock(mutex_);
  auto now = MonotonicMs();
  for (auto i = sessions_.begin(); i != sessions_.end();)
    if (i->second.deadline <= now)
      i = sessions_.erase(i);
    else
      ++i;
  auto found = sessions_.find(session);
  if (found != sessions_.end())
    return found->second.client == client && found->second.certificate == hash;
  if (sessions_.size() >= 64)
    return false;
  sessions_.emplace(session, Binding{client, hash, now + 60000});
  return true;
}
size_t LiveTransportSessionsForTest() { return live_sessions.load(); }
struct LoopbackServer::State : std::enable_shared_from_this<State> {
  net::io_context io;
  ssl::context tls{ssl::context::tls_server};
  tcp::acceptor acceptor{io};
  std::thread worker;
  std::function<bool(const std::string &, const std::string &,
                     const std::string &)>
      authorize;
  struct Session;
  std::set<std::shared_ptr<Session>> sessions;
  uint64_t rate_window = 0;
  unsigned accepted = 0;
  std::atomic<unsigned short> port{0};
  void Accept();
};
struct LoopbackServer::State::Session : std::enable_shared_from_this<Session> {
  std::shared_ptr<State> owner;
  websocket::stream<beast::ssl_stream<beast::tcp_stream>, false> ws;
  beast::flat_buffer buffer{4096};
  http::request_parser<http::empty_body> parser;
  net::steady_timer lifetime;
  std::string client, hash, id;
  uint64_t started = 0, rate_window = 0;
  unsigned messages = 0;
  bool stopped = false, authenticated = false;
  Session(std::shared_ptr<State> o, tcp::socket socket)
      : owner(std::move(o)), ws(std::move(socket), owner->tls),
        lifetime(owner->io) {
    ++live_sessions;
    parser.header_limit(4096);
    parser.body_limit(0);
  }
  ~Session() { --live_sessions; }
  void Close() {
    if (stopped)
      return;
    stopped = true;
    Error ec;
    lifetime.cancel();
    beast::get_lowest_layer(ws).socket().cancel(ec);
    beast::get_lowest_layer(ws).socket().shutdown(tcp::socket::shutdown_both,
                                                  ec);
    beast::get_lowest_layer(ws).socket().close(ec);
    owner->sessions.erase(shared_from_this());
  }
  void Start() {
    started = MonotonicMs();
    id = RandomId();
    Error ec;
    auto peer = beast::get_lowest_layer(ws).socket().remote_endpoint(ec);
    if (ec || !peer.address().is_loopback()) {
      Close();
      return;
    }
    beast::get_lowest_layer(ws).expires_after(std::chrono::seconds(3));
    ws.next_layer().async_handshake(
        ssl::stream_base::server,
        [self = shared_from_this()](Error ec) { self->TlsReady(ec); });
  }
  bool Live() {
    return authenticated && owner->authorize(client, hash, id) &&
           MonotonicMs() - started < 60000;
  }
  void TlsReady(Error ec) {
    if (ec || stopped ||
        SSL_version(ws.next_layer().native_handle()) != TLS1_3_VERSION ||
        SSL_get_verify_result(ws.next_layer().native_handle()) != X509_V_OK) {
      Close();
      return;
    }
    Certificate cert(SSL_get1_peer_certificate(ws.next_layer().native_handle()),
                     X509_free);
    if (!ValidCertificate(cert)) {
      Close();
      return;
    }
    client = Pin(cert);
    hash = CertificateHash(cert);
    authenticated = true;
    if (!Live()) {
      Close();
      return;
    }
    beast::get_lowest_layer(ws).expires_after(std::chrono::seconds(3));
    http::async_read(
        ws.next_layer(), buffer, parser,
        [self = shared_from_this()](Error ec, size_t) { self->HttpReady(ec); });
  }
  void HttpReady(Error ec) {
    if (ec || stopped || !Live()) {
      Close();
      return;
    }
    const auto &request = parser.get();
    unsigned hosts = 0;
    for (const auto &field : request) {
      if (field.name() == http::field::origin) {
        Close();
        return;
      }
      if (field.name() == http::field::host)
        ++hosts;
      if (field.name() == http::field::sec_websocket_extensions) {
        Close();
        return;
      }
    }
    if (hosts != 1 ||
        request[http::field::host] !=
            "127.0.0.1:" + std::to_string(owner->port) ||
        request.method() != http::verb::get || request.version() != 11 ||
        request.target() != "/transport/v1" ||
        !websocket::is_upgrade(request) || buffer.size() != 0) {
      Close();
      return;
    }
    beast::get_lowest_layer(ws).expires_never();
    websocket::stream_base::timeout bounds;
    bounds.handshake_timeout = std::chrono::seconds(3);
    bounds.idle_timeout = std::chrono::seconds(5);
    bounds.keep_alive_pings = false;
    ws.set_option(bounds);
    ws.read_message_max(256);
    ws.control_callback(
        [weak = weak_from_this()](websocket::frame_type, beast::string_view) {
          if (auto self = weak.lock())
            if (!self->Rate())
              self->Close();
        });
    ws.async_accept(request, [self = shared_from_this()](Error ec) {
      if (ec) {
        self->Close();
        return;
      }
      self->Tick();
      self->Read();
    });
  }
  bool Rate() {
    auto now = MonotonicMs();
    if (now - rate_window >= 1000) {
      rate_window = now;
      messages = 0;
    }
    return ++messages <= 16;
  }
  void Tick() {
    lifetime.expires_after(std::chrono::milliseconds(250));
    lifetime.async_wait([self = shared_from_this()](Error ec) {
      if (ec || self->stopped)
        return;
      if (!self->Live()) {
        self->Close();
        return;
      }
      self->Tick();
    });
  }
  void Read() {
    if (stopped)
      return;
    ws.async_read(buffer, [self = shared_from_this()](Error ec, size_t) {
      if (ec || self->stopped || !self->Live() || !self->Rate() ||
          !self->ws.got_binary() ||
          beast::buffers_to_string(self->buffer.data()) != "AT01") {
        self->Close();
        return;
      }
      self->buffer.consume(self->buffer.size());
      self->ws.binary(true);
      static constexpr char denied[] = "PERMISSION_DENIED";
      self->ws.async_write(net::buffer(denied, sizeof(denied) - 1),
                           [self](Error ec, size_t) {
                             if (ec)
                               self->Close();
                             else
                               self->Read();
                           });
    });
  }
};
void LoopbackServer::State::Accept() {
  acceptor.async_accept(
      [self = shared_from_this()](Error ec, tcp::socket socket) {
        if (ec)
          return;
        auto now = MonotonicMs();
        if (now - self->rate_window >= 1000) {
          self->rate_window = now;
          self->accepted = 0;
        }
        if (self->sessions.size() < 8 && ++self->accepted <= 64) {
          auto session = std::make_shared<Session>(self, std::move(socket));
          self->sessions.insert(session);
          session->Start();
        } else {
          Error ignored;
          socket.close(ignored);
        }
        self->Accept();
      });
}
LoopbackServer::LoopbackServer() = default;
LoopbackServer::~LoopbackServer() { Stop(); }
bool LoopbackServer::Start(
    const ipc::Message &config,
    std::function<bool(const std::string &, const std::string &,
                       const std::string &)>
        authorize) {
  if (state_ || !authorize)
    return false;
  try {
    auto state = std::make_shared<State>();
    Identity identity{ReadKey(config.server_key),
                      ReadCertificate(config.server_cert)};
    Require(ValidCertificate(identity.cert));
    Context(state->tls, identity, config.ca_cert, true);
    state->authorize = std::move(authorize);
    state->acceptor.open(tcp::v4());
    state->acceptor.bind({net::ip::make_address("127.0.0.1"), 0});
    state->acceptor.listen(8);
    state->port = state->acceptor.local_endpoint().port();
    state->Accept();
    state->worker = std::thread([state] { state->io.run(); });
    state_ = std::move(state);
    return true;
  } catch (...) {
    return false;
  }
}
void LoopbackServer::Stop() {
  if (!state_)
    return;
  auto state = state_;
  net::post(state->io, [state] {
    Error ec;
    state->acceptor.close(ec);
    auto sessions = state->sessions;
    for (auto &session : sessions)
      session->Close();
  });
  if (state->worker.joinable())
    state->worker.join();
  state_.reset();
}
unsigned short LoopbackServer::port() const {
  return state_ ? state_->port.load() : 0;
}
std::string LoopbackServer::address() const {
  return state_ ? state_->acceptor.local_endpoint().address().to_string() : "";
}
bool Probe(unsigned short port, const Key &key, const Credential &credential,
           const std::string &host, const std::string *origin,
           const std::string &target, bool send_certificate,
           const std::string &payload) {
  try {
    net::io_context io;
    ssl::context ctx{ssl::context::tls_client};
    Identity identity{key, ReadCertificate(credential.cert)};
    Context(ctx, identity, credential.ca, false);
    ssl::context empty{ssl::context::tls_client};
    if (!send_certificate) {
      Require(SSL_CTX_set_min_proto_version(empty.native_handle(),
                                            TLS1_3_VERSION) == 1 &&
              SSL_CTX_set_max_proto_version(empty.native_handle(),
                                            TLS1_3_VERSION) == 1);
      auto ca = ReadCertificate(credential.ca);
      Require(X509_STORE_add_cert(SSL_CTX_get_cert_store(empty.native_handle()),
                                  ca.get()) == 1);
      empty.set_verify_mode(ssl::verify_peer);
    }
    websocket::stream<beast::ssl_stream<beast::tcp_stream>, false> ws(
        io, send_certificate ? ctx : empty);
    Require(X509_VERIFY_PARAM_set1_ip_asc(
                SSL_get0_param(ws.next_layer().native_handle()), "127.0.0.1") ==
            1);
    bool valid = false;
    net::steady_timer deadline(io);
    deadline.expires_after(std::chrono::seconds(8));
    deadline.async_wait([&](Error ec) {
      if (!ec) {
        Error ignored;
        beast::get_lowest_layer(ws).socket().cancel(ignored);
        beast::get_lowest_layer(ws).socket().close(ignored);
      }
    });
    net::co_spawn(
        io,
        [&]() -> net::awaitable<void> {
          beast::get_lowest_layer(ws).expires_after(std::chrono::seconds(3));
          co_await beast::get_lowest_layer(ws).async_connect(
              tcp::endpoint(net::ip::make_address("127.0.0.1"), port),
              net::use_awaitable);
          co_await ws.next_layer().async_handshake(ssl::stream_base::client,
                                                   net::use_awaitable);
          Certificate server(
              SSL_get1_peer_certificate(ws.next_layer().native_handle()),
              X509_free);
          Require(ValidCertificate(server) &&
                  Pin(server) == credential.server_pin &&
                  SSL_get_verify_result(ws.next_layer().native_handle()) ==
                      X509_V_OK &&
                  SSL_version(ws.next_layer().native_handle()) ==
                      TLS1_3_VERSION);
          if (origin)
            ws.set_option(websocket::stream_base::decorator(
                [origin](websocket::request_type &request) {
                  request.set(http::field::origin, *origin);
                }));
          beast::get_lowest_layer(ws).expires_never();
          auto bounds = websocket::stream_base::timeout::suggested(
              beast::role_type::client);
          bounds.handshake_timeout = std::chrono::seconds(3);
          bounds.idle_timeout = std::chrono::seconds(3);
          bounds.keep_alive_pings = false;
          ws.set_option(bounds);
          ws.read_message_max(256);
          co_await ws.async_handshake(
              host.empty() ? "127.0.0.1:" + std::to_string(port) : host, target,
              net::use_awaitable);
          ws.binary(true);
          co_await ws.async_write(net::buffer(payload), net::use_awaitable);
          beast::flat_buffer buffer{256};
          co_await ws.async_read(buffer, net::use_awaitable);
          valid = ws.got_binary() && beast::buffers_to_string(buffer.data()) ==
                                         "PERMISSION_DENIED";
        },
        [&](std::exception_ptr error) {
          if (error)
            valid = false;
          deadline.cancel();
          Error ec;
          beast::get_lowest_layer(ws).socket().close(ec);
        });
    io.run();
    return valid;
  } catch (...) {
    return false;
  }
}
} // namespace agi::transport
