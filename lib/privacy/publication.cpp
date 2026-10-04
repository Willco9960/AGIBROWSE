#include "publication.h"
#include <array>
#include <limits>
#include <set>

namespace agi::privacy {
namespace {
constexpr size_t kFields = 128, kSecrets = 32, kString = 4096, kBytes = 65536;
bool Printable(const std::string& text) {
  // This initial native contract supports printable ASCII only. Unsupported
  // encodings/control bytes fail closed; UTF-8 semantics are a future gate.
  for (unsigned char c : text) if (c < 32 || c > 126) return false;
  return text.size() <= kString;
}
std::string Quote(const std::string& text) {
  std::string result = "\"";
  for (char c : text) { if (c == '"' || c == '\\') result += '\\'; result += c; }
  return result + '"';
}
}
void RedactedState::Deliver(const std::function<void(std::string_view)>& sink) const {
  if (sink) sink(json_);
}
std::optional<RedactedState> Redact(const DraftState& draft) {
  if (draft.fields.size() > kFields || draft.known_secrets.size() > kSecrets) return {};
  size_t bytes = 0;
  auto bounded = [&](const std::string& s) {
    if (!Printable(s) || s.size() > kBytes - bytes) return false;
    bytes += s.size(); return true;
  };
  std::vector<std::string_view> secrets;
  for (const auto& s : draft.known_secrets) {
    if (s.empty() || !bounded(s)) return {};
    secrets.push_back(s);
  }
  std::set<std::string_view> names;
  for (const auto& field : draft.fields) {
    if (field.name.empty() || !bounded(field.name) || !bounded(field.value) ||
        !names.insert(field.name).second) return {};
    switch (field.kind) {
      case FieldKind::text: break;
      case FieldKind::password: case FieldKind::marked_sensitive:
        secrets.push_back(field.name);
        if (!field.value.empty()) secrets.push_back(field.value); break;
      default: return {};
    }
  }
  auto reflected = [&](const std::string& text) {
    for (auto secret : secrets) if (text.find(secret) != std::string::npos) return true;
    return false;
  };
  std::string json = "{\"fields\":[";
  bool first = true;
  for (const auto& field : draft.fields) {
    if (field.kind != FieldKind::text || reflected(field.name) || reflected(field.value)) continue;
    if (!first) json += ',';
    first = false;
    json += "{\"name\":" + Quote(field.name) + ",\"value\":" + Quote(field.value) + '}';
  }
  json += "]}";
  if (json.size() > kBytes) return {};
  return RedactedState(std::move(json));
}
bool WriteDiagnostic(FILE* file, Event event, uint64_t value) {
  static constexpr std::array names = {
    "renderer_identity_rejected", "renderer_application_escape_blocked",
    "renderer_private_handles_absent", "page_native_api_absent",
    "renderer_privileged_message_rejected", "browser_created", "browser_closed",
    "fixture_ready", "main_frame_loaded", "private_endpoint_diagnostics_failed",
    "main_frame_load_failed", "window_created", "window_destroyed",
    "context_initialized", "sandbox_bootstrap_verified", "agent_transport_unavailable",
    "private_broker_start_failed", "private_broker_challenge_verified",
    "initialization_failed", "initialized", "message_loop_exited",
    "private_broker_stopped", "shutdown_complete", "page_console_suppressed",
    "privacy_fixture_paths_exercised", "privacy_dialog_suppressed", "privacy_human_title_preserved",
    "tab_fixture_popup_registered", "tab_fixture_order_verified", "tab_fixture_move_verified",
    "tab_fixture_cancel_verified", "tab_fixture_close_verified", "tab_fixture_pending_expired",
    "tab_fixture_resources_released", "tab_fixture_failed",
    "tab_fixture_click_issued", "tab_fixture_click_acknowledged",
    "tab_fixture_popup_requested", "tab_fixture_failed_stage", "tab_fixture_failed_reason",
    "tab_fixture_cursor_relation", "tab_fixture_cursor_destination",
    "tab_fixture_cursor_destination_unavailable",
    "tab_fixture_visibility_adjusted", "tab_fixture_visibility_restored",
    "tab_fixture_native_window_state", "tab_fixture_native_window_state_unavailable",
    "tab_fixture_native_window_cloak", "browser_ui_probe_step", "browser_ui_probe_failed_stage",
    "profile_probe_step", "profile_probe_failed_stage", "profile_menu_requested",
    "profile_native_create_selected", "profile_probe_failed_reason", "profile_setting_state",
    "profile_native_menu_return", "profile_action_phase", "profile_action_posted"
  };
  auto index = static_cast<size_t>(event);
  if (index >= names.size()) return false;
  switch (event) {
    case Event::tab_fixture_failed_stage: if(value>7)return false;break;
    case Event::browser_ui_probe_step: case Event::browser_ui_probe_failed_stage: if(value<1||value>9)return false;break;
    case Event::profile_probe_step: case Event::profile_probe_failed_stage: if(value<1||value>9)return false;break;
    case Event::profile_probe_failed_reason: if(value<1||value>8)return false;break;
    case Event::profile_setting_state: if(value>511)return false;break;
    case Event::profile_native_menu_return: if(value>4)return false;break;
    case Event::profile_action_phase: if(value<1||value>14)return false;break;
    case Event::profile_action_posted: if(value>1)return false;break;
    case Event::tab_fixture_cursor_relation: if(value>7)return false;break;
    case Event::tab_fixture_cursor_destination: if(value>15)return false;break;
    case Event::tab_fixture_native_window_state: if(value>63)return false;break;
    case Event::tab_fixture_native_window_cloak: if(value>2)return false;break;
    case Event::tab_fixture_failed_reason:
      if(value<static_cast<uint64_t>(TabFixtureFailureReason::lifecycle_state) ||
          value>static_cast<uint64_t>(TabFixtureFailureReason::cursor_root_mismatch))return false;
      break;
    case Event::browser_created: case Event::browser_closed:
    case Event::sandbox_bootstrap_verified: case Event::private_broker_challenge_verified:
      if (!value || value > std::numeric_limits<uint32_t>::max()) return false;
      break;
    case Event::window_created: if (!value) return false; break;
    case Event::main_frame_loaded: if (value != 0 && (value < 100 || value > 599)) return false; break;
    default: if (value) return false;
  }
  if (!file) return true;
  return std::fprintf(file, "{\"event\":\"%s\",\"value\":%llu}\n", names[index],
                      static_cast<unsigned long long>(value)) >= 0 && std::fflush(file) == 0;
}
}  // namespace agi::privacy
