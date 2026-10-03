#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>
namespace agi::ipc {
constexpr size_t kMaxMessage = 2048;
constexpr size_t kMaxTransportConfig = 16384;
enum class Kind : uint8_t { challenge = 1, proof = 2, intent = 3, result = 4, stop = 5, transport_config = 6, identity_check = 7, identity_result = 8, transport_ready = 9, session_closed = 10 };
// Closed flat TLV: each tag occurs exactly once, has an exact type/length.
struct Message {
  Kind kind = Kind::stop;
  uint64_t sequence = 0, generation = 0, channel_generation = 0;
  std::string client, session, profile, tab, frame, document, operation;
  std::vector<uint8_t> challenge;
  uint8_t result = 0;
  uint8_t version = 1;
  std::string server_key, server_cert, ca_cert, certificate_hash;
  uint64_t port = 0;
};
bool Decode(const std::vector<uint8_t>& bytes, Message& out);
std::vector<uint8_t> Encode(const Message& message);
bool IsOperation(const std::string& operation);
bool IsIdentifier(const std::string& identifier);
struct Destination { std::string profile, tab, frame, document; };
// Internal native authority only. Never deserialized from the broker wire.
struct Grant {
  std::string client, session;
  Destination destination;
  uint64_t generation = 0, deadline_ms = 0;
  std::vector<std::string> operations;
  bool control = false, lease = false;
};
enum class Decision : uint8_t { denied = 1, unsupported = 2 };
class Boundary {
 public:
  explicit Boundary(uint64_t channel_generation) : channel_generation_(channel_generation) {}
  // Caller is native host authority; not exposed to IPC or renderer callbacks.
  bool RegisterNativeGrant(const Grant& grant);
  void RegisterDestination(Destination destination);
  Decision Admit(const Message& message, uint64_t now_ms);
  void Invalidate();
 private:
  uint64_t channel_generation_, last_sequence_ = 0;
  bool live_ = true;
  std::map<std::string, Grant> grants_;
  std::map<std::pair<std::string,std::string>, Destination> destinations_;
};
}  // namespace agi::ipc
