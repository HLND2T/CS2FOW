#include "plugin.h"

// Coordinates plugin load/unload, public commands, map changes, frames, bake
// validation, and worker submission on the game thread. It activates filtering
// only after CPU, gamedata, schema, map source, and bake checks all succeed.

#include "vpk.h"
#include "runtime_health.h"

#include <ISmmPlugin.h>
#include <eiface.h>
#include <entity2/entitysystem.h>
#include <filesystem.h>
#include <inetchannelinfo.h>
#include <iserver.h>
#include <schemasystem/schemasystem.h>
#include <tier1/convar.h>
#include <tier1/utlstring.h>
#include <tier1/utlvector.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace cs2fow
{

plugin g_plugin;
constexpr const char *k_game_event_manager_interface = "GAMEEVENTSMANAGER002";

GameEventKeySymbol_t game_event_key(const char *name)
{
	return {CUtlStringToken(MurmurHash2LowerCase(name, STRINGTOKEN_MURMURHASH_SEED)), name};
}

using create_entity_by_name_fn = CEntityInstance *(*)(const char *, int);
using dispatch_spawn_fn = void (*)(CEntityInstance *, void *);
using remove_entity_fn = void (*)(CEntityInstance *);
using teleport_entity_fn = void (*)(CEntityInstance *, const Vector *, const QAngle *, const Vector *);

constexpr uint32_t k_los_animated_color = 0x00ff00;
constexpr uint32_t k_los_muzzle_color = 0x00ffff;
constexpr uint32_t k_los_aabb_color = 0xffa500;
constexpr float k_los_beam_half_length = 1.0f;
constexpr auto k_los_debug_interval = std::chrono::microseconds(15625);

template <typename type>
type &entity_field(CEntityInstance *entity, uint32_t offset)
{
	return *reinterpret_cast<type *>(reinterpret_cast<uintptr_t>(entity) + offset);
}

Color los_debug_color(uint32_t color)
{
	return Color(static_cast<uint8_t>(color >> 16), static_cast<uint8_t>(color >> 8),
		static_cast<uint8_t>(color), 255);
}

bool teleport_entity(CEntityInstance *entity, uint32_t vtable_index, const Vector &origin)
{
	void **vtable = entity == nullptr ? nullptr : *reinterpret_cast<void ***>(entity);
	if (vtable == nullptr || vtable[vtable_index] == nullptr)
	{
		return false;
	}
	reinterpret_cast<teleport_entity_fn>(vtable[vtable_index])(entity, &origin, nullptr, nullptr);
	return true;
}

CON_COMMAND_F(cs2fow_entity, "List, filter, or clear actual CS2FOW transmit clears", FCVAR_NONE)
{
	if (args.ArgC() == 1)
	{
		g_plugin.print_entities(-1);
		return;
	}
	if (args.ArgC() != 2)
	{
		META_CONPRINTF("[CS2FOW] usage: cs2fow_entity [<edict>|clear]\n");
		return;
	}
	const char *text = args.Arg(1);
	if (std::strcmp(text, "clear") == 0)
	{
		g_plugin.clear_entity_records();
		return;
	}
	int edict = -1;
	const auto result = std::from_chars(text, text + std::strlen(text), edict);
	if (result.ec != std::errc {} || *result.ptr != '\0')
	{
		META_CONPRINTF("[CS2FOW] invalid edict: %s\n", text);
		return;
	}
	g_plugin.print_entities(edict);
}

SH_DECL_HOOK3_void(IServerGameDLL, GameFrame, SH_NOATTRIB, false, bool, bool, bool);
SH_DECL_HOOK7_void(ISource2GameEntities, CheckTransmit, SH_NOATTRIB, false, CCheckTransmitInfo **, int, CBitVec<MAX_EDICTS> &, CBitVec<MAX_EDICTS> &, const Entity2Networkable_t **, const uint16 *, int);
SH_DECL_HOOK2(IGameEventManager2, LoadEventsFromFile, SH_NOATTRIB, false, int, const char *, bool);

bool plugin::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();
	updater_service::apply_pending_update();
	api_ = ismm;
	GET_V_IFACE_CURRENT(GetEngineFactory, engine_, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, cvar_, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetFileSystemFactory, filesystem_, IFileSystem, FILESYSTEM_INTERFACE_VERSION);
	ISchemaSystem *schema {};
	GET_V_IFACE_CURRENT(GetEngineFactory, schema, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, game_resource_, game_resource_service, GAMERESOURCESERVICESERVER_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetServerFactory, server_, IServerGameDLL, INTERFACEVERSION_SERVERGAMEDLL);
	GET_V_IFACE_ANY(GetServerFactory, game_entities_, ISource2GameEntities, SOURCE2GAMEENTITIES_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, g_pNetworkServerService, INetworkServerService, NETWORKSERVERSERVICE_INTERFACE_VERSION);
	g_pCVar = cvar_;
	game_frame_hook_id_ = SH_ADD_HOOK(IServerGameDLL, GameFrame, server_, SH_MEMBER(this, &plugin::hook_game_frame), true);
	check_transmit_hook_id_ = SH_ADD_HOOK(ISource2GameEntities, CheckTransmit, game_entities_, SH_MEMBER(this, &plugin::hook_check_transmit), true);
	if (game_frame_hook_id_ == 0 || check_transmit_hook_id_ == 0)
	{
		if (game_frame_hook_id_ != 0) SH_REMOVE_HOOK_ID(game_frame_hook_id_);
		if (check_transmit_hook_id_ != 0) SH_REMOVE_HOOK_ID(check_transmit_hook_id_);
		game_frame_hook_id_ = 0;
		check_transmit_hook_id_ = 0;
		if (error != nullptr && maxlen != 0) ismm->Format(error, maxlen, "Could not install required SourceHook hooks");
		return false;
	}
	game_events_ = static_cast<IGameEventManager2 *>(ismm->VInterfaceMatch(ismm->GetEngineFactory(), k_game_event_manager_interface));
	if (game_events_ == nullptr)
	{
		game_events_ = static_cast<IGameEventManager2 *>(ismm->VInterfaceMatch(ismm->GetServerFactory(), k_game_event_manager_interface));
	}
	META_CONVAR_REGISTER(FCVAR_RELEASE | FCVAR_GAMEDLL);
	teammates_are_enemies_ = cvar_->FindConVar("mp_teammates_are_enemies");
	settings::initialize(engine_, cvar_, [](uint32_t changes)
	{
		g_plugin.settings_changed(changes);
	});
	if (!settings::begin_load())
	{
		META_CONPRINTF("[CS2FOW] warning: could not start the initial configuration load; compiled defaults remain active\n");
	}
	g_SMAPI->AddListener(this, this);

	compatibility_.initialize(ismm->GetBaseDir(), game_entities_, schema,
		game_events_ != nullptr);
	updater_.start(compatibility_.detected_server_binary_fingerprint());
	if (compatibility_.valid()
		&& compatibility_.game_event_manager_vtable() != nullptr)
	{
		auto *vtable = static_cast<IGameEventManager2 *>(
			compatibility_.game_event_manager_vtable());
		game_event_load_hook_id_ = SH_ADD_DVPHOOK(IGameEventManager2,
			LoadEventsFromFile, vtable,
			SH_MEMBER(this, &plugin::hook_load_events_from_file), true);
	}
	if (!compatibility_.valid())
	{
		disable(compatibility_.report().technical_detail);
		META_CONPRINTF("[CS2FOW] %s: %s\n",
			compatibility_state_name(compatibility_.report().state),
			disabled_reason_.c_str());
	}
	else if (!compatibility_.smoke_available())
	{
		META_CONPRINTF("[CS2FOW] smoke occlusion unavailable; wall filtering remains active\n");
	}
	else if (!he_event_available_ && game_event_load_hook_id_ == 0)
	{
		META_CONPRINTF("[CS2FOW] HE smoke clearing unavailable; ordinary smoke remains active\n");
	}
	META_CONPRINTF("[CS2FOW] loaded; culling is fail-open until a map bake validates\n");
	return true;
}

bool plugin::Unload(char *error, size_t max_length)
{
	if (game_events_ != nullptr) game_events_->RemoveListener(this);
	game_events_ = nullptr;
	he_event_available_ = false;
	if (game_event_load_hook_id_ != 0) SH_REMOVE_HOOK_ID(game_event_load_hook_id_);
	game_event_load_hook_id_ = 0;
	automatic_baker_.stop();
	worker_.stop();
	updater_.unload();
	destroy_los_debug_beams();
	if (game_frame_hook_id_ != 0) SH_REMOVE_HOOK_ID(game_frame_hook_id_);
	if (check_transmit_hook_id_ != 0) SH_REMOVE_HOOK_ID(check_transmit_hook_id_);
	game_frame_hook_id_ = 0;
	check_transmit_hook_id_ = 0;
	settings::cancel_load();
	settings::shutdown();
	ConVar_Unregister();
	return true;
}

int plugin::hook_load_events_from_file(const char *, bool)
{
	game_events_ = META_IFACEPTR(IGameEventManager2);
	RETURN_META_VALUE(MRES_IGNORED, 0);
}

void plugin::FireGameEvent(IGameEvent *event)
{
	if (event == nullptr || std::strcmp(event->GetName(), "hegrenade_detonate") != 0)
	{
		return;
	}
	const float missing = std::numeric_limits<float>::quiet_NaN();
	const vec3 center {event->GetFloat(game_event_key("x"), missing), event->GetFloat(game_event_key("y"), missing),
		event->GetFloat(game_event_key("z"), missing)};
	INetworkGameServer *network_server = g_pNetworkServerService == nullptr ? nullptr : g_pNetworkServerService->GetIGameServer();
	CGlobalVars *globals = network_server == nullptr ? nullptr : network_server->GetGlobals();
	const float game_time = globals == nullptr ? missing : globals->curtime;
	std::lock_guard<std::mutex> lock(transmit_state_mutex_);
	he_clearance_history_.record(center, game_time);
}
void plugin::OnLevelInit(char const *map_name, char const *, char const *, char const *, bool, bool)
{
	if (map_name != nullptr && map_name[0] != '\0')
	{
		request_map_change(map_name);
	}
}

void plugin::OnLevelShutdown()
{
	automatic_baker_.stop();
	worker_.stop();
	destroy_los_debug_beams(false);
	data_ = {};
	source_ = {};
	reset_transmit_state();
	map_.clear();
	pending_map_.clear();
	if (settings::cancel_load())
	{
		META_CONPRINTF("[CS2FOW] configuration load interrupted by map shutdown; previous settings restored\n");
	}
	if (compatibility_.valid())
	{
		disabled_reason_ = "no map loaded";
	}
}

void plugin::disable(std::string reason)
{
	worker_.stop();
	destroy_los_debug_beams();
	data_ = {};
	reset_transmit_state();
	disabled_reason_ = std::move(reason);
}

bool plugin::resolve_map_source(const std::string &map, map_source &source, std::string &error) const
{
	if (!valid_map_name(map))
	{
		error = "map name is not a safe relative path";
		return false;
	}
	const std::string virtual_path = "maps/" + map + ".vpk";
	CUtlVector<CUtlString> paths;
	filesystem_->FindFileAbsoluteList(paths, virtual_path.c_str(), "GAME");
	std::vector<std::filesystem::path> candidates;
	auto add_candidates = [&](const std::string &path)
	{
		for (const std::filesystem::path &candidate : vpk_path_candidates(path))
		{
			if (std::find(candidates.begin(), candidates.end(), candidate) == candidates.end())
			{
				candidates.push_back(candidate);
			}
		}
	};
	for (int i = 0; i < paths.Count(); ++i)
	{
		add_candidates(paths[i].Get());
	}
	add_candidates((std::filesystem::path(api_->GetBaseDir()) / "maps" / (map + ".vpk")).string());

	std::vector<std::string> tried;
	for (const std::filesystem::path &candidate : candidates)
	{
		tried.push_back(candidate.filename().string());
		std::string source_error;
		if (find_map_source(candidate, map, source, source_error))
		{
			return true;
		}
	}

	std::ostringstream message;
	message << "could not resolve mounted map VPK: " << virtual_path;
	if (!tried.empty())
	{
		message << " tried=";
		for (size_t i = 0; i < tried.size(); ++i)
		{
			if (i != 0)
			{
				message << ",";
			}
			message << tried[i];
		}
	}
	error = message.str();
	return false;
}

bool plugin::load_map_bake(const std::filesystem::path &path, const std::string &map, const map_source &source,
	bvh8_data &data, std::string &error) const
{
	if (!load_bvh8(path, data, error))
	{
		return false;
	}
	if (map != data.header.map_name)
	{
		error = "bake map name does not match current map";
		data = {};
		return false;
	}
	if (data.header.flags != source.flags || data.header.source_crc32 != source.metadata.crc32 || data.header.source_size != source.metadata.size)
	{
		error = "bake source CRC or size does not match current VPK";
		data = {};
		return false;
	}
	return true;
}

void plugin::activate(bvh8_data data)
{
	worker_.stop();
	reset_transmit_state();
	data_ = std::move(data);
	active_worker_threads_ = static_cast<uint32_t>(settings::current().worker_threads);
	if (!worker_.start(&data_, active_worker_threads_))
	{
		active_worker_threads_ = 0;
		disable("could not start visibility worker threads");
		return;
	}
	disabled_reason_.clear();
	META_CONPRINTF("[CS2FOW] active for %s: crc=0x%08x, triangles=%u, nodes=%u, packets=%u\n", map_.c_str(), data_.header.source_crc32,
		data_.header.triangle_count, data_.header.node_count, data_.header.packet_count);
}

void plugin::request_map_change(const std::string &map)
{
	if (map.empty() || pending_map_ == map)
	{
		return;
	}
	automatic_baker_.stop();
	worker_.stop();
	active_worker_threads_ = 0;
	destroy_los_debug_beams();
	data_ = {};
	source_ = {};
	reset_transmit_state();
	pending_map_ = map;
	disabled_reason_ = "loading configuration";
	if (settings::loading())
	{
		settings::cancel_load();
	}
	if (!settings::begin_load())
	{
		META_CONPRINTF("[CS2FOW] warning: cs2fow.cfg could not be queued; keeping the previous settings\n");
		finish_config_load(false);
		return;
	}
	META_CONPRINTF("[CS2FOW] loading cs2fow.cfg before activating %s\n", map.c_str());
}

void plugin::finish_config_load(bool success)
{
	if (success)
	{
		META_CONPRINTF("[CS2FOW] cs2fow.cfg loaded and committed\n");
	}
	else
	{
		META_CONPRINTF("[CS2FOW] cs2fow.cfg did not finish; previous settings restored\n");
	}
	if (pending_map_.empty())
	{
		return;
	}
	std::string map = std::move(pending_map_);
	pending_map_.clear();
	change_map(map);
}

void plugin::reload_config()
{
	if (settings::loading())
	{
		META_CONPRINTF("[CS2FOW] a configuration load is already in progress; wait for it to finish\n");
		return;
	}
	if (!settings::begin_load())
	{
		META_CONPRINTF("[CS2FOW] cs2fow.cfg could not be queued because the server command service is unavailable\n");
		return;
	}
	META_CONPRINTF("[CS2FOW] reloading cs2fow.cfg; changes commit only after its final marker\n");
}

void plugin::config_loaded()
{
	if (!settings::loading())
	{
		META_CONPRINTF("[CS2FOW] ignored an unexpected cs2fow_config_loaded marker\n");
		return;
	}
	finish_config_load(settings::complete_load());
}

void plugin::settings_changed(uint32_t changes)
{
	if ((changes & setting_change_visibility) != 0)
	{
		reset_transmit_state(false);
	}
}

void plugin::print_help() const
{
	META_CONPRINTF("[CS2FOW] administrator commands:\n");
	META_CONPRINTF("[CS2FOW] cs2fow_status - Show concise protection, map, configuration, and performance health.\n");
	META_CONPRINTF("[CS2FOW] cs2fow_metrics - Show the complete technical runtime counters.\n");
	META_CONPRINTF("[CS2FOW] cs2fow_reload - Transactionally reload cs2fow.cfg.\n");
	META_CONPRINTF("[CS2FOW] cs2fow_check_config - Check settings without changing them.\n");
	META_CONPRINTF("[CS2FOW] cs2fow_check_update - Check for an update now instead of waiting.\n");
	META_CONPRINTF("[CS2FOW] cs2fow_entity [<edict>|clear] - Inspect actual debug-mode transmit clears.\n");
}

void plugin::check_update()
{
	updater_.check_now();
}

void plugin::check_config() const
{
	const runtime_configuration &configuration = settings::current();
	int findings = 0;
	const uint32_t configuration_findings = validate_configuration(configuration);
	META_CONPRINTF("[CS2FOW] checking the committed configuration; nothing will be changed\n");
	if (settings::loading())
	{
		META_CONPRINTF("[CS2FOW] review: cs2fow.cfg is still loading; committed settings remain active meanwhile\n");
		++findings;
	}
	else if (settings::load_state() == configuration_load_state::failed)
	{
		META_CONPRINTF("[CS2FOW] review: the last cs2fow.cfg load did not reach cs2fow_config_loaded; previous settings were restored\n");
		++findings;
	}
	if ((configuration_findings & configuration_finding_disabled) != 0)
	{
		META_CONPRINTF("[CS2FOW] review cs2fow_enable: protection is disabled by configuration\n");
		++findings;
	}
	if ((configuration_findings & configuration_finding_shoulder_range) != 0)
	{
		META_CONPRINTF("[CS2FOW] review shoulder settings: max is below base, so the effective maximum equals the base\n");
		++findings;
	}
	if ((configuration_findings & configuration_finding_he_pair) != 0)
	{
		META_CONPRINTF("[CS2FOW] review HE clearing: set both radius and duration to zero to disable it consistently\n");
		++findings;
	}
	if (configuration.smoke_occlusion && !compatibility_.smoke_available())
	{
		META_CONPRINTF("[CS2FOW] review smoke occlusion: it is requested but unavailable for this CS2 build\n");
		++findings;
	}
	if (active_worker_threads_ != 0
		&& active_worker_threads_ != static_cast<uint32_t>(configuration.worker_threads))
	{
		META_CONPRINTF("[CS2FOW] note: worker threads are configured as %d; %u remain active until the next map\n",
			configuration.worker_threads, active_worker_threads_);
		++findings;
	}
	if ((configuration_findings & configuration_finding_debug_los) != 0)
	{
		META_CONPRINTF("[CS2FOW] note: temporary LOS debug is enabled for player %d\n",
			configuration.debug_los_player);
		++findings;
	}
	int donttransmit = 0;
	if (!settings::donttransmit_mode(donttransmit))
	{
		META_CONPRINTF("[CS2FOW] review sv_enable_donttransmit: the Valve setting could not be read\n");
		++findings;
	}
	else
	{
		META_CONPRINTF("[CS2FOW] sv_enable_donttransmit=%d is supported\n", donttransmit);
	}
	int playerid = 0;
	if (!settings::playerid_mode(playerid))
	{
		META_CONPRINTF("[CS2FOW] review mp_playerid: the target-ID setting could not be read\n");
		++findings;
	}
	else if (playerid == 0)
	{
		META_CONPRINTF("[CS2FOW] review mp_playerid: set it to 1 to prevent stale enemy names at hidden positions\n");
		++findings;
	}
	else
	{
		META_CONPRINTF("[CS2FOW] mp_playerid=%d prevents hidden enemy target IDs\n", playerid);
	}
	if (findings == 0)
	{
		META_CONPRINTF("[CS2FOW] configuration check passed; everything is ready\n");
	}
	else
	{
		META_CONPRINTF("[CS2FOW] configuration check found %d item%s to review\n",
			findings, findings == 1 ? "" : "s");
	}
}

void plugin::start_automatic_bake(const std::string &map, const map_source &source, const std::filesystem::path &output, const std::string &reason)
{
	const std::filesystem::path base = api_->GetBaseDir();
#if defined(_WIN32)
	const std::filesystem::path baker = base / "tools" / "cs2fow_baker.exe";
	const std::filesystem::path vrf = base / "tools" / "vrf" / "win64" / "Source2Viewer-CLI.exe";
#else
	const std::filesystem::path baker = base / "tools" / "cs2fow_baker";
	const std::filesystem::path vrf = base / "tools" / "vrf" / "linux64" / "Source2Viewer-CLI";
#endif
	if (!std::filesystem::is_regular_file(baker) || !std::filesystem::is_regular_file(vrf))
	{
		disable("automatic baker or VRF is missing");
		return;
	}
#if !defined(_WIN32)
	const auto check_executable = [&](const std::filesystem::path &path)
	{
		std::error_code ec;
		const auto status = std::filesystem::status(path, ec);
		return !ec && (status.permissions() & (std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec)) != std::filesystem::perms::none;
	};
	if (!check_executable(baker))
	{
		disable("baker missing execute permission (chmod +x " + baker.string() + ")");
		return;
	}
	if (!check_executable(vrf))
	{
		disable("VRF missing execute permission (chmod +x " + vrf.string() + ")");
		return;
	}
#endif
	disabled_reason_ = "automatic bake in progress";
	META_CONPRINTF("[CS2FOW] %s for %s; starting automatic bake\n", reason.c_str(), map.c_str());
	if (!automatic_baker_.start({map, source, base.parent_path().parent_path(), output, baker, vrf}))
	{
		disable("could not start automatic baker thread");
	}
}

void plugin::poll_automatic_bake()
{
	bake_completion completion;
	if (!automatic_baker_.poll(completion))
	{
		return;
	}
	if (completion.cancelled || completion.request.map != map_)
	{
		return;
	}
	if (!completion.success)
	{
		disable("automatic bake failed: " + completion.error);
		META_CONPRINTF("[CS2FOW] automatic bake failed for %s: %s\n", map_.c_str(), completion.error.c_str());
		return;
	}
	map_source current;
	std::string error;
	if (!resolve_map_source(map_, current, error) || !same_map_source(current, completion.request.source))
	{
		disable("map source changed during automatic bake");
		return;
	}
	source_ = std::move(current);
	activate(std::move(completion.data));
}

void plugin::change_map(const std::string &map)
{
	automatic_baker_.stop();
	worker_.stop();
	destroy_los_debug_beams();
	data_ = {};
	source_ = {};
	reset_transmit_state();
	map_ = map;
	if (!compatibility_.valid())
	{
		return;
	}
	disabled_reason_ = "validating map";
	const std::filesystem::path base = api_->GetBaseDir();
	const std::filesystem::path bake = base / "addons" / "cs2fow" / "data" / "maps" / (map + ".bvh8");
	std::string error;
	if (!resolve_map_source(map, source_, error))
	{
		disable(error);
		return;
	}
	bvh8_data data;
	if (!load_map_bake(bake, map, source_, data, error))
	{
		start_automatic_bake(map, source_, bake, error);
		return;
	}
	activate(std::move(data));
}

void plugin::hook_game_frame(bool simulating, bool first_tick, bool last_tick)
{
	if (settings::poll_timeout())
	{
		finish_config_load(false);
	}
	updater_.on_game_frame();
	if (!he_event_available_ && game_events_ != nullptr)
	{
		he_event_available_ = game_events_->AddListener(this, "hegrenade_detonate", true);
	}
	INetworkGameServer *network_server = g_pNetworkServerService == nullptr ? nullptr : g_pNetworkServerService->GetIGameServer();
	if (network_server == nullptr)
	{
		destroy_los_debug_beams(false);
		return;
	}
	const char *current_map = network_server->GetMapName();
	if (current_map != nullptr && map_ != current_map && pending_map_ != current_map)
	{
		request_map_change(current_map);
	}
	poll_automatic_bake();
	const runtime_configuration &configuration = settings::current();
	if (!simulating || !configuration.enable || !disabled_reason_.empty())
	{
		destroy_los_debug_beams();
		return;
	}
	CGameEntitySystem *system = entity_system();
	if (system == nullptr)
	{
		disable("game entity system is unavailable");
		return;
	}
	const auto now = std::chrono::steady_clock::now();
	if (now - last_snapshot_ < std::chrono::milliseconds(configuration.update_interval_ms))
	{
		return;
	}
	visibility_snapshot value;
	CGlobalVars *globals = network_server->GetGlobals();
	const float game_time = globals == nullptr ? std::numeric_limits<float>::quiet_NaN() : globals->curtime;
	const auto capture_started = std::chrono::steady_clock::now();
	if (!capture(value, game_time))
	{
		disable("game entity system is unavailable");
		return;
	}
	const double capture_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - capture_started).count();
	{
		std::lock_guard<std::mutex> lock(transmit_state_mutex_);
		capture_timing_.record(capture_ms);
	}
	draw_los_debug(value);
	last_snapshot_ = now;
	worker_.submit(std::move(value), static_cast<uint32_t>(configuration.visibility_hold_ms), {
		configuration.shoulder_base_units,
		configuration.shoulder_rtt_scale,
		configuration.max_shoulder_units
	});
}

void plugin::draw_los_debug(const visibility_snapshot &value)
{
	const int player_number = settings::current().debug_los_player;
	if (player_number == 0)
	{
		destroy_los_debug_beams();
		return;
	}
	if (!compatibility_.debug_beam_available() || los_debug_failed_
		|| value.captured - last_los_debug_draw_ < k_los_debug_interval)
	{
		return;
	}
	const player_state &player = value.players[static_cast<size_t>(player_number - 1)];
	if (!player.valid)
	{
		destroy_los_debug_beams();
		return;
	}
	CGameEntitySystem *system = entity_system();
	if (system == nullptr)
	{
		destroy_los_debug_beams(false);
		return;
	}

	last_los_debug_draw_ = value.captured;
	const uint32_t capsule_count = player.capsule_count == k_visibility_capsule_count
		? player.capsule_count : 0u;
	vec3 muzzle;
	const bool has_muzzle = visibility_muzzle_point(visibility_sample(player), muzzle);
	const auto aabb_points = visibility_aabb_points(visibility_sample(player));
	const uint32_t aabb_start = capsule_count + static_cast<uint32_t>(has_muzzle);
	const uint32_t debug_count = aabb_start + (capsule_count == 0 ? 0u : k_visibility_aabb_point_count);
	auto create_entity = reinterpret_cast<create_entity_by_name_fn>(
		compatibility_.create_entity_by_name());
	auto dispatch_spawn = reinterpret_cast<dispatch_spawn_fn>(
		compatibility_.dispatch_spawn());
	auto remove_entity = reinterpret_cast<remove_entity_fn>(
		compatibility_.remove_entity());
	for (uint32_t index = 0; index < los_debug_beams_.size(); ++index)
	{
		los_debug_beam &beam = los_debug_beams_[index];
		CEntityInstance *entity = beam.handle.IsValid() ? system->GetEntityInstance(beam.handle) : nullptr;
		if (index >= debug_count)
		{
			if (entity != nullptr) remove_entity(entity);
			beam = {};
			continue;
		}

		const bool capsule_axis = index < capsule_count;
		const bool muzzle_axis = !capsule_axis && has_muzzle && index == capsule_count;
		const vec3 point = muzzle_axis ? muzzle : aabb_points[index - aabb_start];
		const uint32_t color = capsule_axis ? k_los_animated_color
			: muzzle_axis ? k_los_muzzle_color : k_los_aabb_color;
		const vec3 start_point = capsule_axis ? player.capsules[index].start
			: vec3 {point.x, point.y, point.z - k_los_beam_half_length};
		const vec3 end_point = capsule_axis ? player.capsules[index].end
			: vec3 {point.x, point.y, point.z + k_los_beam_half_length};
		Vector start(start_point.x, start_point.y, start_point.z);
		Vector end(end_point.x, end_point.y, end_point.z);
		if (entity == nullptr)
		{
			entity = create_entity("env_beam", -1);
			const CEntityHandle handle = entity_handle(entity);
			if (entity == nullptr || !handle.IsValid() || !teleport_entity(entity, compatibility_.teleport_vtable_index(), start))
			{
				if (entity != nullptr) remove_entity(entity);
				entity = nullptr;
			}
			else
			{
				entity_field<Vector>(entity, compatibility_.fields().beam_end_position) = end;
				entity_field<float>(entity, compatibility_.fields().beam_width) = 1.25f;
				entity_field<float>(entity, compatibility_.fields().beam_end_width) = 1.25f;
				entity_field<Color>(entity, compatibility_.fields().render_color) = los_debug_color(color);
				dispatch_spawn(entity, nullptr);
				entity = system->GetEntityInstance(handle);
				if (entity != nullptr) beam = {handle, color};
			}
			if (entity == nullptr || !beam.handle.IsValid())
			{
				destroy_los_debug_beams();
				los_debug_failed_ = true;
				META_CONPRINTF("[CS2FOW] temporary LOS beam creation failed; set cs2fow_debug_los_player 0 before retrying\n");
				return;
			}
			continue;
		}

		if (!teleport_entity(entity, compatibility_.teleport_vtable_index(), start))
		{
			destroy_los_debug_beams();
			los_debug_failed_ = true;
			META_CONPRINTF("[CS2FOW] temporary LOS beam movement failed; set cs2fow_debug_los_player 0 before retrying\n");
			return;
		}
		entity_field<Vector>(entity, compatibility_.fields().beam_end_position) = end;
		entity->NetworkStateChanged(NetworkStateChangedData(compatibility_.fields().beam_end_position));
		if (beam.color != color)
		{
			entity_field<Color>(entity, compatibility_.fields().render_color) = los_debug_color(color);
			entity->NetworkStateChanged(NetworkStateChangedData(compatibility_.fields().render_color));
			beam.color = color;
		}
	}
}

void plugin::destroy_los_debug_beams(bool remove_entities)
{
	CGameEntitySystem *system = remove_entities ? entity_system() : nullptr;
	auto remove_entity = reinterpret_cast<remove_entity_fn>(
		compatibility_.remove_entity());
	for (los_debug_beam &beam : los_debug_beams_)
	{
		if (system != nullptr && remove_entity != nullptr && beam.handle.IsValid())
		{
			if (CEntityInstance *entity = system->GetEntityInstance(beam.handle); entity != nullptr)
			{
				remove_entity(entity);
			}
		}
		beam = {};
	}
	last_los_debug_draw_ = {};
	los_debug_failed_ = false;
}

void plugin::print_status() const
{
	const runtime_configuration &configuration = settings::current();
	const worker_stats stats = worker_.stats();
	const std::shared_ptr<const visibility_result> result = worker_.result();
	const double age_ms = result == nullptr ? -1.0
		: std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - result->captured).count();
	uint32_t players = 0;
	if (result != nullptr)
	{
		for (const player_state &player : result->players)
		{
			if (player.valid)
			{
				++players;
			}
		}
	}

	runtime_health_state state = runtime_health_state::starting;
	const char *action = nullptr;
	if (settings::loading())
	{
		state = runtime_health_state::loading_configuration;
	}
	else if (!compatibility_.valid())
	{
		const compatibility_report &report = compatibility_.report();
		state = report.state == compatibility_state::unsupported_system
			? runtime_health_state::unsupported_system
			: report.state == compatibility_state::update_required
				? runtime_health_state::update_required : runtime_health_state::error;
		action = report.operator_action.c_str();
	}
	else if (!configuration.enable)
	{
		state = runtime_health_state::disabled;
		action = "Set cs2fow_enable 1 or update cs2fow.cfg.";
	}
	else if (disabled_reason_.empty())
	{
		state = runtime_health_state::protected_state;
	}
	else if (disabled_reason_ == "automatic bake in progress")
	{
		state = runtime_health_state::baking;
		action = "Wait for the automatic map bake to finish.";
	}
	else if (disabled_reason_ == "validating map" || disabled_reason_ == "no map loaded"
		|| disabled_reason_ == "loading configuration")
	{
		state = runtime_health_state::loading_map;
	}
	else
	{
		state = runtime_health_state::error;
		action = "Run cs2fow_metrics and check the first reported error.";
	}
	if (action == nullptr
		&& settings::load_state() == configuration_load_state::failed)
	{
		action = "Run cs2fow_check_config, fix cs2fow.cfg, then run cs2fow_reload.";
	}
	int playerid = 0;
	const bool playerid_safe = settings::playerid_mode(playerid) && playerid != 0;
	if (action == nullptr && !playerid_safe)
	{
		action = "Set mp_playerid 1 to prevent stale enemy target IDs.";
	}

	META_CONPRINTF("[CS2FOW] CS2FOW %s: %s\n", CS2FOW_VERSION,
		runtime_health_state_name(state));
	const char *configuration_state = settings::loading() ? "loading"
		: settings::load_state() == configuration_load_state::failed ? "previous settings restored after a failed load"
		: settings::load_state() == configuration_load_state::loaded ? "loaded" : "compiled defaults";
	META_CONPRINTF("[CS2FOW] Configuration: %s; worker threads configured=%d active=%u; automatic updates=%s\n",
		configuration_state, configuration.worker_threads, active_worker_threads_,
		configuration.automatic_updates ? "on" : "off");
	META_CONPRINTF("[CS2FOW] Map: %s%s%s\n",
		pending_map_.empty() ? (map_.empty() ? "<none>" : map_.c_str()) : pending_map_.c_str(),
		disabled_reason_.empty() ? "" : "; ",
		disabled_reason_.empty() ? "" : disabled_reason_.c_str());
	const bool smoke_available = result != nullptr ? result->smoke_available
		: compatibility_.smoke_available();
	const bool ffa = teammates_are_enemies();
	const bool protection_active = disabled_reason_.empty() && configuration.enable;
	META_CONPRINTF("[CS2FOW] Protection: walls=%s smoke=%s HE=%s teammates=%s target_ids=%s\n",
		protection_active ? "on" : "off",
		configuration.smoke_occlusion && smoke_available ? "on" : "off",
		configuration.smoke_occlusion && configuration.he_clear_radius_units > 0.0f
			&& configuration.he_clear_seconds > 0.0f && he_event_available_
				? "on" : "off",
		visibility_teammate_filter_enabled(configuration.filter_teammates, ffa) ? "filtered" : "not filtered",
		playerid_safe ? "safe" : "unrestricted");
	META_CONPRINTF("[CS2FOW] Runtime: players=%u pairs=%u recent_p99=%.3fms snapshot_age=%.1fms\n",
		players, stats.evaluated_pairs, stats.recent_p99_ms, age_ms);
	if (action != nullptr)
	{
		META_CONPRINTF("[CS2FOW] Next action: %s\n", action);
	}
}

void plugin::print_metrics() const
{
	const compatibility_report &compatibility = compatibility_.report();
	META_CONPRINTF("[CS2FOW] compatibility state=%s detail=%s\n",
		compatibility_state_name(compatibility.state),
		compatibility.technical_detail.c_str());
	for (const std::string &capability : compatibility.missing_capabilities)
	{
		META_CONPRINTF("[CS2FOW] optional capability unavailable: %s\n",
			capability.c_str());
	}
	const worker_stats stats = worker_.stats();
	const std::shared_ptr<const visibility_result> result = worker_.result();
	runtime_timing_stats capture_timing;
	runtime_timing_stats bone_timing;
	runtime_timing_stats transmit_timing;
	uint32_t capsule_players = 0;
	uint32_t capsule_failed_players = 0;
	{
		std::lock_guard<std::mutex> lock(transmit_state_mutex_);
		capture_timing = capture_timing_;
		bone_timing = bone_timing_;
		transmit_timing = transmit_timing_;
		capsule_players = capsule_players_;
		capsule_failed_players = capsule_failed_players_;
	}
	const double age_ms = result ? std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - result->captured).count() : -1.0;
	META_CONPRINTF("[CS2FOW] %s; map=%s crc=0x%08x version=%u triangles=%u nodes=%u packets=%u bytes=%llu depth=%u\n",
		disabled_reason_.empty() && settings::current().enable ? "active" : (disabled_reason_.empty() ? "disabled by convar" : disabled_reason_.c_str()), map_.c_str(),
		data_.header.source_crc32, data_.header.version, data_.header.triangle_count, data_.header.node_count, data_.header.packet_count,
		static_cast<unsigned long long>(data_.header.file_size), data_.header.max_depth);
	META_CONPRINTF("[CS2FOW] worker threads=%u wall=%.3fms active=%.3fms recent_p95=%.3fms recent_p99=%.3fms lifetime_average=%.3fms maximum=%.3fms snapshot_age=%.1fms\n",
		stats.thread_count, stats.latest_ms, stats.latest_active_ms, stats.recent_p95_ms, stats.recent_p99_ms,
		stats.average_ms, stats.maximum_ms, age_ms);
	META_CONPRINTF("[CS2FOW] workload pairs=%u visible=%u hidden=%u hold=%u pixels=%u rays=%u nodes=%u triangles=%u cache=%u/%u budget=%llu cycles=%llu\n",
		stats.evaluated_pairs, stats.visible_pairs, stats.hidden_pairs, stats.hold_reuses,
		stats.sampled_pixels, stats.traced_rays,
		stats.visited_nodes, stats.rasterized_triangles,
		stats.occluder_cache_hits, stats.occluder_cache_hits + stats.occluder_cache_misses,
		static_cast<unsigned long long>(stats.budget_exhaustions),
		static_cast<unsigned long long>(stats.cycles));
	const double average_proof_leaves = stats.rebuilt_proofs == 0 ? 0.0
		: static_cast<double>(stats.rebuilt_proof_leaves) / static_cast<double>(stats.rebuilt_proofs);
	META_CONPRINTF("[CS2FOW] MOC draws=%u rects=%u proofs=%u proof_leaves=%.1f/%u cache_capacity=%u saturated=%u compact=%u/%u saved=%u uncached_blocked=%u\n",
		stats.moc_render_calls, stats.moc_rect_tests, stats.rebuilt_proofs, average_proof_leaves,
		stats.max_rebuilt_proof_leaves, k_capsule_occluder_cache_size,
		stats.cache_saturations, stats.cache_compactions, stats.cache_compaction_trials,
		stats.cache_compaction_leaves_saved, stats.uncached_blocked);
	META_CONPRINTF("[CS2FOW] capture latest=%.3fms average=%.3fms maximum=%.3fms calls=%llu\n",
		capture_timing.latest_ms, capture_timing.average_ms(), capture_timing.maximum_ms,
		static_cast<unsigned long long>(capture_timing.calls));
	META_CONPRINTF("[CS2FOW] bones latest=%.3fms average=%.3fms maximum=%.3fms calls=%llu capsules=%u failed=%u\n",
		bone_timing.latest_ms, bone_timing.average_ms(), bone_timing.maximum_ms,
		static_cast<unsigned long long>(bone_timing.calls), capsule_players, capsule_failed_players);
	META_CONPRINTF("[CS2FOW] transmit latest=%.3fms average=%.3fms maximum=%.3fms calls=%llu\n",
		transmit_timing.latest_ms, transmit_timing.average_ms(), transmit_timing.maximum_ms,
		static_cast<unsigned long long>(transmit_timing.calls));
	const bool smoke_available = result != nullptr ? result->smoke_available
		: compatibility_.smoke_available();
	META_CONPRINTF("[CS2FOW] smoke enabled=%d available=%d captured=%u he_listener=%d he_active=%u\n",
		settings::current().smoke_occlusion ? 1 : 0, smoke_available ? 1 : 0, result == nullptr ? 0u : result->smoke_count,
		he_event_available_ ? 1 : 0, result == nullptr ? 0u : result->he_clearance_count);
	const bool ffa = teammates_are_enemies();
	META_CONPRINTF("[CS2FOW] teammate filtering configured=%d ffa=%d effective=%d\n",
		settings::current().filter_teammates ? 1 : 0, ffa ? 1 : 0,
		visibility_teammate_filter_enabled(settings::current().filter_teammates, ffa) ? 1 : 0);
	int playerid = 0;
	const bool playerid_readable = settings::playerid_mode(playerid);
	META_CONPRINTF("[CS2FOW] mp_playerid readable=%d value=%d\n",
		playerid_readable ? 1 : 0, playerid);
	META_CONPRINTF("[CS2FOW] automatic updates=%d source=GitHub stable releases install=next restart\n",
		settings::current().automatic_updates ? 1 : 0);
	const uint32_t debug_beams = static_cast<uint32_t>(std::count_if(los_debug_beams_.begin(), los_debug_beams_.end(),
		[](const los_debug_beam &beam) { return beam.handle.IsValid(); }));
	META_CONPRINTF("[CS2FOW] temporary LOS debug player=%d available=%d beams=%u failed=%d\n",
		settings::current().debug_los_player,
		compatibility_.debug_beam_available() ? 1 : 0,
		debug_beams, los_debug_failed_ ? 1 : 0);
	std::string bake_map;
	double bake_elapsed_ms = 0;
	if (automatic_baker_.status(bake_map, bake_elapsed_ms))
	{
		META_CONPRINTF("[CS2FOW] auto-bake map=%s elapsed=%.1fms\n", bake_map.c_str(), bake_elapsed_ms);
	}
}

bool plugin::teammates_are_enemies() const
{
	return teammates_are_enemies_.IsValidRef() && ConVarRefAbstract(teammates_are_enemies_).GetBool();
}

} // namespace cs2fow

CEntityIdentity *CEntitySystem::GetEntityIdentity(CEntityIndex entity_index)
{
	if (entity_index.Get() < 0 || entity_index.Get() >= MAX_TOTAL_ENTITIES - 1)
	{
		return nullptr;
	}
	CEntityIdentity *chunk = m_EntityList.m_pIdentityChunks[entity_index.Get() / MAX_ENTITIES_IN_LIST];
	if (chunk == nullptr)
	{
		return nullptr;
	}
	CEntityIdentity *identity = &chunk[entity_index.Get() % MAX_ENTITIES_IN_LIST];
	return identity->GetEntityIndex() == entity_index ? identity : nullptr;
}

CEntityIdentity *CEntitySystem::GetEntityIdentity(const CEntityHandle &handle)
{
	if (!handle.IsValid())
	{
		return nullptr;
	}
	const int index = handle.GetEntryIndex();
	if (index < 0 || index >= MAX_TOTAL_ENTITIES - 1)
	{
		return nullptr;
	}
	CEntityIdentity *chunk = m_EntityList.m_pIdentityChunks[index / MAX_ENTITIES_IN_LIST];
	if (chunk == nullptr)
	{
		return nullptr;
	}
	CEntityIdentity *identity = &chunk[index % MAX_ENTITIES_IN_LIST];
	return identity->GetRefEHandle() == handle ? identity : nullptr;
}

PLUGIN_EXPOSE(cs2fow::plugin, cs2fow::g_plugin);
