#include "settings.h"

#include "plugin.h"

#include <eiface.h>
#include <tier1/convar.h>

#include <chrono>

namespace cs2fow
{

namespace
{

template <typename type>
void on_convar_changed(CConVar<type> *, CSplitScreenSlot, const type *,
	const type *);

} // namespace

CConVar<bool> cs2fow_enable("cs2fow_enable", FCVAR_NONE,
	"Enable CS2FOW when map data is valid", true, on_convar_changed<bool>);
CConVar<bool> cs2fow_smoke_occlusion("cs2fow_smoke_occlusion", FCVAR_NONE,
	"Use live CS2 smoke for visibility", true, on_convar_changed<bool>);
CConVar<float> cs2fow_he_clear_radius_units("cs2fow_he_clear_radius_units", FCVAR_NONE,
	"HE-cleared smoke channel radius", 100.0f, true, 0.0f, true, 320.0f,
	on_convar_changed<float>);
CConVar<float> cs2fow_he_clear_seconds("cs2fow_he_clear_seconds", FCVAR_NONE,
	"HE-cleared smoke channel duration", 2.5f, true, 0.0f, true, 10.0f,
	on_convar_changed<float>);
CConVar<bool> cs2fow_filter_teammates("cs2fow_filter_teammates", FCVAR_NONE,
	"Apply visibility filtering to teammates", false, on_convar_changed<bool>);
CConVar<int> cs2fow_update_interval_ms("cs2fow_update_interval_ms", FCVAR_NONE,
	"Visibility worker update interval", 1, true, 1, true, 100,
	on_convar_changed<int>);
CConVar<int> cs2fow_worker_threads("cs2fow_worker_threads", FCVAR_NONE,
	"Visibility worker thread count (applies on map activation)", 2, true, 1, true, 4,
	on_convar_changed<int>);
CConVar<float> cs2fow_shoulder_base_units("cs2fow_shoulder_base_units", FCVAR_NONE,
	"Minimum sideways shoulder origin distance", 48.0f, true, 0.0f, true, 256.0f,
	on_convar_changed<float>);
CConVar<float> cs2fow_shoulder_rtt_scale("cs2fow_shoulder_rtt_scale", FCVAR_NONE,
	"Sideways shoulder units per RTT millisecond, applied in 25 ms steps", 0.4f,
	true, 0.0f, true, 4.0f, on_convar_changed<float>);
CConVar<float> cs2fow_max_shoulder_units("cs2fow_max_shoulder_units", FCVAR_NONE,
	"Maximum sideways shoulder origin distance", 128.0f, true, 0.0f, true, 256.0f,
	on_convar_changed<float>);
CConVar<int> cs2fow_visibility_hold_ms("cs2fow_visibility_hold_ms", FCVAR_NONE,
	"Minimum revealed duration", 1000, true, 0, true, 1000, on_convar_changed<int>);
CConVar<bool> cs2fow_debug("cs2fow_debug", FCVAR_NONE,
	"Enable CS2FOW diagnostic logging", false, on_convar_changed<bool>);
CConVar<int> cs2fow_debug_los_player("cs2fow_debug_los_player", FCVAR_NONE,
	"Temporarily draw one 1-based player's live capsule axes, muzzle, and AABB corners; 0 removes them",
	0, true, 0, true, static_cast<int>(k_max_players), on_convar_changed<int>);
CConVar<bool> cs2fow_auto_update("cs2fow_auto_update", FCVAR_NONE,
	"Automatically download verified compatible stable updates", true,
	on_convar_changed<bool>);

namespace
{

constexpr auto k_config_timeout = std::chrono::seconds(5);

IVEngineServer2 *config_engine {};
ICvar *config_cvar {};
settings::change_callback config_change_callback {};
configuration_transaction config_transaction;
bool restoring_configuration {};

void on_configuration_changed()
{
	if (restoring_configuration || config_transaction.state() == configuration_load_state::pending)
	{
		return;
	}
	const uint32_t changes = config_transaction.apply_direct({
		cs2fow_enable.Get(),
		cs2fow_smoke_occlusion.Get(),
		cs2fow_he_clear_radius_units.Get(),
		cs2fow_he_clear_seconds.Get(),
		cs2fow_filter_teammates.Get(),
		cs2fow_update_interval_ms.Get(),
		cs2fow_worker_threads.Get(),
		cs2fow_shoulder_base_units.Get(),
		cs2fow_shoulder_rtt_scale.Get(),
		cs2fow_max_shoulder_units.Get(),
		cs2fow_visibility_hold_ms.Get(),
		cs2fow_debug.Get(),
		cs2fow_debug_los_player.Get(),
		cs2fow_auto_update.Get()
	});
	if (changes != setting_change_none && config_change_callback != nullptr)
	{
		config_change_callback(changes);
	}
}

template <typename type>
void on_convar_changed(CConVar<type> *, CSplitScreenSlot, const type *new_value,
	const type *old_value)
{
	if (new_value != nullptr && old_value != nullptr && *new_value != *old_value)
	{
		on_configuration_changed();
	}
}

runtime_configuration read_convars()
{
	return {
		cs2fow_enable.Get(),
		cs2fow_smoke_occlusion.Get(),
		cs2fow_he_clear_radius_units.Get(),
		cs2fow_he_clear_seconds.Get(),
		cs2fow_filter_teammates.Get(),
		cs2fow_update_interval_ms.Get(),
		cs2fow_worker_threads.Get(),
		cs2fow_shoulder_base_units.Get(),
		cs2fow_shoulder_rtt_scale.Get(),
		cs2fow_max_shoulder_units.Get(),
		cs2fow_visibility_hold_ms.Get(),
		cs2fow_debug.Get(),
		cs2fow_debug_los_player.Get(),
		cs2fow_auto_update.Get()
	};
}

void write_convars(const runtime_configuration &value)
{
	restoring_configuration = true;
	cs2fow_enable.Set(value.enable);
	cs2fow_smoke_occlusion.Set(value.smoke_occlusion);
	cs2fow_he_clear_radius_units.Set(value.he_clear_radius_units);
	cs2fow_he_clear_seconds.Set(value.he_clear_seconds);
	cs2fow_filter_teammates.Set(value.filter_teammates);
	cs2fow_update_interval_ms.Set(value.update_interval_ms);
	cs2fow_worker_threads.Set(value.worker_threads);
	cs2fow_shoulder_base_units.Set(value.shoulder_base_units);
	cs2fow_shoulder_rtt_scale.Set(value.shoulder_rtt_scale);
	cs2fow_max_shoulder_units.Set(value.max_shoulder_units);
	cs2fow_visibility_hold_ms.Set(value.visibility_hold_ms);
	cs2fow_debug.Set(value.debug);
	cs2fow_debug_los_player.Set(value.debug_los_player);
	cs2fow_auto_update.Set(value.automatic_updates);
	restoring_configuration = false;
}

} // namespace

namespace settings
{

void initialize(IVEngineServer2 *engine, ICvar *cvar, change_callback callback)
{
	config_engine = engine;
	config_cvar = cvar;
	config_change_callback = callback;
	config_transaction.apply_direct(read_convars());
}

void shutdown()
{
	config_engine = nullptr;
	config_cvar = nullptr;
	config_change_callback = nullptr;
}

const runtime_configuration &current()
{
	return config_transaction.active();
}

configuration_load_state load_state()
{
	return config_transaction.state();
}

std::chrono::steady_clock::time_point last_loaded()
{
	return config_transaction.last_loaded();
}

bool begin_load()
{
	if (config_engine == nullptr
		|| !config_transaction.begin(std::chrono::steady_clock::now()))
	{
		return false;
	}
	config_engine->ServerCommand("exec cs2fow.cfg");
	return true;
}

bool complete_load()
{
	const auto now = std::chrono::steady_clock::now();
	if (config_transaction.timed_out(now,
		std::chrono::duration_cast<std::chrono::milliseconds>(k_config_timeout)))
	{
		const runtime_configuration previous = config_transaction.active();
		config_transaction.rollback();
		write_convars(previous);
		return false;
	}
	const uint32_t changes = config_transaction.commit(read_convars(),
		now);
	if (changes != setting_change_none && config_change_callback != nullptr)
	{
		config_change_callback(changes);
	}
	return true;
}

bool poll_timeout()
{
	if (!config_transaction.timed_out(std::chrono::steady_clock::now(),
		std::chrono::duration_cast<std::chrono::milliseconds>(k_config_timeout)))
	{
		return false;
	}
	const runtime_configuration previous = config_transaction.active();
	if (!config_transaction.rollback())
	{
		return false;
	}
	write_convars(previous);
	return true;
}

bool cancel_load()
{
	if (!config_transaction.rollback())
	{
		return false;
	}
	write_convars(config_transaction.active());
	return true;
}

bool loading()
{
	return config_transaction.state() == configuration_load_state::pending;
}

bool donttransmit_mode(int &value)
{
	value = 0;
	if (config_cvar == nullptr)
	{
		return false;
	}
	const ConVarRef reference = config_cvar->FindConVar("sv_enable_donttransmit");
	if (!reference.IsValidRef())
	{
		return false;
	}
	value = CConVarRef<bool>(reference).Get() ? 1 : 0;
	return true;
}

bool playerid_mode(int &value)
{
	value = 0;
	if (config_cvar == nullptr)
	{
		return false;
	}
	const ConVarRef reference = config_cvar->FindConVar("mp_playerid");
	if (!reference.IsValidRef())
	{
		return false;
	}
	value = CConVarRef<int>(reference).Get();
	return true;
}

} // namespace settings

CON_COMMAND_F(cs2fow_status, "Show concise CS2FOW health and protection state", FCVAR_NONE)
{
	g_plugin.print_status();
}

CON_COMMAND_F(cs2fow_metrics, "Show detailed CS2FOW runtime metrics", FCVAR_NONE)
{
	g_plugin.print_metrics();
}

CON_COMMAND_F(cs2fow_help, "Show CS2FOW administrator commands", FCVAR_NONE)
{
	g_plugin.print_help();
}

CON_COMMAND_F(cs2fow_reload, "Reload and validate cs2fow.cfg", FCVAR_NONE)
{
	g_plugin.reload_config();
}

CON_COMMAND_F(cs2fow_check_config, "Check current CS2FOW settings without changing them", FCVAR_NONE)
{
	g_plugin.check_config();
}

CON_COMMAND_F(cs2fow_check_update, "Check for a CS2FOW update now", FCVAR_NONE)
{
	g_plugin.check_update();
}

CON_COMMAND_F(cs2fow_config_loaded, "Confirm that cs2fow.cfg finished loading", FCVAR_HIDDEN)
{
	g_plugin.config_loaded();
}

} // namespace cs2fow
