#pragma once

// Declares the one CS2FOW plugin object and the fixed runtime state shared by
// its modules. Live engine objects stay with game-thread/CheckTransmit callers;
// the worker receives copied snapshots, and uncertain state must fail open.

#include "automatic_baker.h"
#include "lifecycle_guard.h"
#include "map_source.h"
#include "runtime_compatibility.h"
#include "settings.h"
#include "transmit_debug.h"
#include "transmit_masks.h"
#include "updater.h"
#include "visibility_worker.h"

#include <ISmmPlugin.h>
#include <eiface.h>
#include <entity2/entitysystem.h>
#include <filesystem.h>
#include <iserver.h>
#include <igameevents.h>
#include <tier1/convar.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>

PLUGIN_GLOBALVARS();

namespace cs2fow
{

inline constexpr uint32_t k_max_weapons = 64;
inline constexpr uint32_t k_max_wearables = 32;
inline constexpr uint32_t k_max_recent_hide_records = 256;
inline constexpr uint32_t k_entity_scan_hard_limit = MAX_TOTAL_ENTITIES;
inline constexpr uint32_t k_max_entity_name = 64;
inline constexpr uint32_t k_max_hidden_player_entities = 1 + 2 + k_max_weapons + k_max_wearables + 1;
inline constexpr uint8_t k_life_alive = 0;
inline constexpr uint8_t k_team_t = 2;
inline constexpr uint8_t k_team_ct = 3;
inline constexpr auto k_lifecycle_fail_open = std::chrono::milliseconds(1000);
inline constexpr auto k_hidden_entity_quarantine = std::chrono::milliseconds(3000);
static_assert(MAX_EDICTS == 16384);

class game_resource_service
{
};

struct live_player
{
	CEntityInstance *pawn {};
	int pawn_entity {-1};
	uint8_t team {};
};

using visual_entity_group = hidden_entity_group<CEntityHandle, k_max_hidden_player_entities>;
static_assert(k_max_hidden_player_entities <= k_pair_visual_group_key_max);

struct target_transmit_cache
{
	CEntityInstance *pawn {};
	visual_entity_group group;
	visual_group_key group_key;
	bool group_valid {};
};

struct player_bone_cache
{
	CEntityInstance *pawn {};
	std::array<int32_t, k_visibility_capsule_count> indices {};
	std::chrono::steady_clock::time_point retry_after {};
	bool valid {};
};

struct los_debug_beam
{
	CEntityHandle handle;
	uint32_t color {};
};

enum class hide_reason : uint8_t
{
	current,
	quarantine
};

struct runtime_timing_stats
{
	double latest_ms {};
	double total_ms {};
	double maximum_ms {};
	uint64_t calls {};

	void record(double milliseconds)
	{
		latest_ms = milliseconds;
		total_ms += milliseconds;
		if (milliseconds > maximum_ms) maximum_ms = milliseconds;
		++calls;
	}

	double average_ms() const
	{
		return calls == 0 ? 0.0 : total_ms / static_cast<double>(calls);
	}
};

using recent_hide_log = transmit_debug_log<k_max_recent_hide_records, k_max_entity_name>;

struct qangle
{
	float x {};
	float y {};
	float z {};
};

int entity_index(CEntityInstance *entity);
CEntityHandle entity_handle(CEntityInstance *entity);
void copy_entity_name(CEntityInstance *entity, char (&name)[k_max_entity_name]);
bool valid_networked_edict_index(int index);
int resolve_entity_index(CGameEntitySystem *system, CEntityHandle handle);

class plugin final : public ISmmPlugin, public IMetamodListener, public IGameEventListener2
{
public:
	bool Load(PluginId id, ISmmAPI *api, char *error, size_t max_length, bool late) override;
	bool Unload(char *error, size_t max_length) override;
	void AllPluginsLoaded() override {}
	void OnLevelInit(char const *map_name, char const *, char const *, char const *, bool, bool) override;
	void OnLevelShutdown() override;
	void hook_game_frame(bool simulating, bool first_tick, bool last_tick);
	void hook_check_transmit(CCheckTransmitInfo **infos, int count, CBitVec<MAX_EDICTS> &, CBitVec<MAX_EDICTS> &,
		const Entity2Networkable_t **, const uint16 *, int);
	int hook_load_events_from_file(const char *filename, bool search_all);
	void FireGameEvent(IGameEvent *event) override;
	void print_status() const;
	void print_metrics() const;
	void print_help() const;
	void reload_config();
	void check_config() const;
	void check_update();
	void config_loaded();
	void settings_changed(uint32_t changes);
	void print_entities(int edict);
	void clear_entity_records();
	void reset_transmit_state(bool clear_debug_records = true);

	const char *GetAuthor() override { return "karola3vax"; }
	const char *GetName() override { return "CS2FOW"; }
	const char *GetDescription() override { return "Server-side fog-of-war visibility culling for Counter-Strike 2"; }
	const char *GetURL() override { return "https://github.com/karola3vax/CS2FOW"; }
	const char *GetLicense() override { return "MIT"; }
	const char *GetVersion() override { return CS2FOW_VERSION; }
	const char *GetDate() override { return __DATE__; }
	const char *GetLogTag() override { return "CS2FOW"; }

private:
	bool resolve_map_source(const std::string &map, map_source &source, std::string &error) const;
	bool load_map_bake(const std::filesystem::path &path, const std::string &map, const map_source &source,
		bvh8_data &data, std::string &error) const;
	void start_automatic_bake(const std::string &map, const map_source &source, const std::filesystem::path &output,
		const std::string &reason);
	void poll_automatic_bake();
	void draw_los_debug(const visibility_snapshot &value);
	void destroy_los_debug_beams(bool remove_entities = true);
	void activate(bvh8_data data);
	void request_map_change(const std::string &map);
	void finish_config_load(bool success);
	void change_map(const std::string &map);
	void disable(std::string reason);
	CGameEntitySystem *entity_system() const;
	CEntityInstance *controller(uint32_t slot) const;
	CEntityInstance *pawn(CEntityInstance *controller) const;
	lifecycle_key player_lifecycle(uint32_t slot, CGameEntitySystem *system, live_player *live) const;
	weapon_muzzle_class active_weapon_muzzle_class(CGameEntitySystem *system, CEntityInstance *pawn) const;
	void collect_smoke_entities(CGameEntitySystem *system,
		std::array<CEntityInstance *, k_max_smoke_volumes> &smokes, size_t &smoke_count, bool &smoke_overflow);
	bool collect_player_visual_group(CGameEntitySystem *system, CEntityInstance *pawn, visual_entity_group &group) const;
	bool group_fully_marked(CGameEntitySystem *system, CBitVec<MAX_EDICTS> *bits, const visual_entity_group &group) const;
	void withhold_group(CGameEntitySystem *system, CBitVec<MAX_EDICTS> *primary, CBitVec<MAX_EDICTS> *dont_transmit,
		const visual_entity_group &group, int recipient_slot, hide_reason reason, std::chrono::steady_clock::time_point now);
	void record_hidden_entity(CGameEntitySystem *system, size_t member_index, int edict, const visual_entity_group &group,
		int recipient_slot, hide_reason reason, std::chrono::steady_clock::time_point now);
	bool capture(visibility_snapshot &value, float game_time);
	bool capture_animated_capsules(CEntityInstance *pawn, uint32_t slot, player_state &player,
		std::chrono::steady_clock::time_point now);
	bool capture_smokes(const std::array<CEntityInstance *, k_max_smoke_volumes> &entities, size_t count,
		bool overflow, float game_time, visibility_snapshot &value);
	bool teammates_are_enemies() const;

	ISmmAPI *api_ {};
	IServerGameDLL *server_ {};
	ISource2GameEntities *game_entities_ {};
	IVEngineServer2 *engine_ {};
	ICvar *cvar_ {};
	ConVarRef teammates_are_enemies_;
	IFileSystem *filesystem_ {};
	IGameEventManager2 *game_events_ {};
	game_resource_service *game_resource_ {};
	runtime_compatibility compatibility_;
	updater_service updater_;
	int game_frame_hook_id_ {};
	int check_transmit_hook_id_ {};
	int game_event_load_hook_id_ {};
	std::string map_;
	std::string pending_map_;
	std::string disabled_reason_ {"no map loaded"};
	map_source source_;
	bvh8_data data_;
	visibility_worker worker_;
	automatic_baker automatic_baker_;
	bool he_event_available_ {};
	he_clearance_history he_clearance_history_;
	recent_hide_log recent_hides_;
	std::array<lifecycle_guard, k_max_players> lifecycle_;
	std::array<std::array<pair_guard, k_max_players>, k_max_players> pair_guards_;
	std::array<std::array<visual_entity_group, k_max_players>, k_max_players> hidden_groups_;
	std::array<target_transmit_cache, k_max_players> transmit_target_cache_;
	std::array<player_bone_cache, k_max_players> player_bone_cache_;
	mutable std::mutex transmit_state_mutex_;
	runtime_timing_stats capture_timing_;
	runtime_timing_stats bone_timing_;
	runtime_timing_stats transmit_timing_;
	uint32_t capsule_players_ {};
	uint32_t capsule_failed_players_ {};
	std::chrono::steady_clock::time_point last_snapshot_ {};
	std::chrono::steady_clock::time_point last_los_debug_draw_ {};
	std::array<los_debug_beam, k_visibility_debug_beam_count_max> los_debug_beams_;
	bool los_debug_failed_ {};
	uint64_t snapshot_sequence_ {};
	uint32_t active_worker_threads_ {};
};

extern plugin g_plugin;

} // namespace cs2fow
