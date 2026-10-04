#pragma once
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace agi::browser {
// Host-only identities. No renderer/client decoder can create or select them.
// The Windows implementation retains non-delete-sharing ancestor handles.
class ProfileStore final {
 public:
  struct Profile { std::string id; std::filesystem::path cache; bool opened = false, usable = true; };
  explicit ProfileStore(const std::filesystem::path& root);
  ~ProfileStore();
  ProfileStore(const ProfileStore&) = delete;
  ProfileStore& operator=(const ProfileStore&) = delete;
  const std::filesystem::path& root() const { return root_; }
  const Profile* Find(const std::string& id) const;
  std::vector<std::string> HumanProfiles() const;
  std::string CreateHuman();
  bool MarkContextOpened(const std::string& id);
  // Always false until task014 implements native permission approval. A valid
  // host profile alone supplies no application or sensitive-site authority.
  bool SitePermissionAllowed(const std::string& id) const;
  bool CanDelete(const std::string& id) const;
  // Explicit native confirmation and synchronous revocation required. Once a
  // context has existed, deletion requires restarting the host first.
  bool DeleteConfirmed(const std::string& id, bool native_confirmed,
                       const std::function<void(const std::string&)>& revoke);
 private:
  struct Handles;
  std::filesystem::path root_;
  std::unique_ptr<Handles> handles_;
  std::map<std::string, Profile> profiles_;
};
}  // namespace agi::browser
