#pragma once
#include <cstdio>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace agi::privacy {
// Native-only staging contract. Never deserialize these classifications from
// an agent request. Engine extraction/classification is not implemented yet.
enum class FieldKind { text, password, marked_sensitive };
struct DraftField { FieldKind kind; std::string name, value; };
struct DraftState { std::vector<DraftField> fields; std::vector<std::string> known_secrets; };

// Only Redact can construct a payload. There is no raw constructor or setter.
// Future dispatch must additionally use ScopedAuthority::WithCurrentScope at
// actual delivery; this value is a privacy boundary, never authorization.
class RedactedState final {
 public:
  void Deliver(const std::function<void(std::string_view)>& synchronous_sink) const;
 private:
  explicit RedactedState(std::string json) : json_(std::move(json)) {}
  std::string json_;
  friend std::optional<RedactedState> Redact(const DraftState&);
};
std::optional<RedactedState> Redact(const DraftState&);

// Current production exports use this closed diagnostic schema exclusively.
// No page string, source URL, line number, length, hash, or name is accepted.
enum class Event {
  renderer_identity_rejected, renderer_application_escape_blocked,
  renderer_private_handles_absent, page_native_api_absent,
  renderer_privileged_message_rejected, browser_created, browser_closed,
  fixture_ready, main_frame_loaded, private_endpoint_diagnostics_failed,
  main_frame_load_failed, window_created, window_destroyed,
  context_initialized, sandbox_bootstrap_verified, agent_transport_unavailable,
  private_broker_start_failed, private_broker_challenge_verified,
  initialization_failed, initialized, message_loop_exited,
  private_broker_stopped, shutdown_complete, page_console_suppressed,
  privacy_fixture_paths_exercised, privacy_dialog_suppressed, privacy_human_title_preserved,
  tab_fixture_popup_registered, tab_fixture_order_verified, tab_fixture_move_verified,
  tab_fixture_cancel_verified, tab_fixture_close_verified, tab_fixture_pending_expired,
  tab_fixture_resources_released, tab_fixture_failed,
  tab_fixture_click_issued, tab_fixture_click_acknowledged,
  tab_fixture_popup_requested, tab_fixture_failed_stage, tab_fixture_failed_reason,
  tab_fixture_cursor_relation, tab_fixture_cursor_destination,
  tab_fixture_cursor_destination_unavailable,
  tab_fixture_visibility_adjusted, tab_fixture_visibility_restored,
  tab_fixture_native_window_state, tab_fixture_native_window_state_unavailable,
  tab_fixture_native_window_cloak, browser_ui_probe_step, browser_ui_probe_failed_stage,
  profile_probe_step, profile_probe_failed_stage, profile_menu_requested,
  profile_native_create_selected, profile_probe_failed_reason, profile_setting_state,
  profile_native_menu_return, profile_action_phase, profile_action_posted, profile_cookie_flush
};
// Fixture-only native branches. No OS error, coordinate, HWND, page string or
// arbitrary numeric payload may be encoded by these diagnostic values.
enum class TabFixtureFailureReason : uint64_t {
  lifecycle_state = 1, deadline, window_missing, window_unregistered,
  window_mismatch, screen_conversion, tab_window_changed, engine_changed,
  context_changed, retained_window_changed, cursor_unavailable,
  cursor_target_missing, cursor_root_mismatch
};
// Values are accepted only for specific engine/native IDs and HTTP statuses.
// Call sites must derive them from engine/native APIs, never renderer payloads.
bool WriteDiagnostic(FILE*, Event, uint64_t native_value = 0);
}  // namespace agi::privacy
