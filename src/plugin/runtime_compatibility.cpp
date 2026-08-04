#include "runtime_compatibility.h"

#include "bvh8.h"
#include "visibility_sampling.h"

#include <eiface.h>
#include <schemasystem/schemasystem.h>

#include <cstddef>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string_view>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

namespace cs2fow
{
namespace
{

constexpr uint32_t k_max_gamedata_offset = 4096;
constexpr uint32_t k_max_vtable_index = 1024;
constexpr uint32_t k_max_module_rva = 512u * 1024u * 1024u;

void *module_base(const void *address)
{
#if defined(_WIN32)
	HMODULE module {};
	return GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
		| GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCSTR>(address), &module) ? module : nullptr;
#else
	Dl_info info {};
	return dladdr(address, &info) != 0 ? info.dli_fbase : nullptr;
#endif
}

std::filesystem::path module_path(const void *address)
{
#if defined(_WIN32)
	HMODULE module {};
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
		| GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCSTR>(address), &module))
	{
		return {};
	}
	std::wstring path(32768, L'\0');
	const DWORD length = GetModuleFileNameW(module, path.data(),
		static_cast<DWORD>(path.size()));
	if (length == 0 || length >= path.size())
	{
		return {};
	}
	path.resize(length);
	return path;
#else
	Dl_info info {};
	return dladdr(address, &info) != 0 && info.dli_fname != nullptr
		? std::filesystem::path(info.dli_fname) : std::filesystem::path {};
#endif
}

bool find_declared_field(SchemaClassInfoData_t *class_info, const char *name,
	uint32_t &offset)
{
	if (class_info == nullptr)
	{
		return false;
	}
	for (int i = 0; i < class_info->m_nFieldCount; ++i)
	{
		if (std::strcmp(class_info->m_pFields[i].m_pszName, name) == 0)
		{
			offset = class_info->m_pFields[i].m_nSingleInheritanceOffset;
			return true;
		}
	}
	return false;
}

bool resolve_field(ISchemaSystem *schema, const char *class_name,
	const char *field_name, uint32_t &offset)
{
#if defined(_WIN32)
	CSchemaSystemTypeScope *scope = schema->FindTypeScopeForModule("server.dll");
#else
	CSchemaSystemTypeScope *scope = schema->FindTypeScopeForModule("libserver.so");
#endif
	return scope != nullptr && find_declared_field(
		scope->FindDeclaredClass(class_name).Get(), field_name, offset);
}

bool resolve_global_field(ISchemaSystem *schema, const char *class_name,
	const char *field_name, uint32_t &offset)
{
	CSchemaSystemTypeScope *scope = schema->GlobalTypeScope();
	return scope != nullptr && find_declared_field(
		scope->FindDeclaredClass(class_name).Get(), field_name, offset);
}

} // namespace

bool runtime_compatibility::initialize(const std::filesystem::path &base_directory,
	ISource2GameEntities *game_entities, ISchemaSystem *schema,
	bool he_event_manager_available)
{
	*this = {};
	if (!cpu_supports_avx())
	{
		set_report(compatibility_state::unsupported_system,
			"AVX and operating-system AVX state support are required.",
			he_event_manager_available);
		return false;
	}
	std::string error;
	const std::filesystem::path gamedata = base_directory / "addons" / "cs2fow"
		/ "gamedata" / "cs2fow.games.txt";
	if (!read_gamedata(gamedata, error))
	{
		set_report(compatibility_state::error, std::move(error),
			he_event_manager_available);
		return false;
	}
	if (!verify_server_binary(game_entities, error))
	{
		set_report(compatibility_state::update_required, std::move(error),
			he_event_manager_available);
		return false;
	}
	if (!resolve_private_functions(game_entities, error)
		|| !resolve_schema(schema, error))
	{
		set_report(compatibility_state::update_required, std::move(error),
			he_event_manager_available);
		return false;
	}
	set_report(compatibility_state::compatible,
		"Strict server fingerprint, required private layouts, and schema fields match.",
		he_event_manager_available);
	return true;
}

void runtime_compatibility::set_report(compatibility_state state,
	std::string detail, bool he_event_manager_available)
{
	std::vector<std::string> missing;
	if (state == compatibility_state::compatible)
	{
		if (!smoke_available()) missing.emplace_back("smoke occlusion");
		if (!he_event_manager_available) missing.emplace_back("HE event listener");
		if (!weapon_item_available()) missing.emplace_back("weapon muzzle classification");
		if (!debug_beam_available()) missing.emplace_back("temporary LOS debug beams");
	}
	report_ = make_compatibility_report(state, std::move(detail), std::move(missing));
}

bool runtime_compatibility::read_gamedata(const std::filesystem::path &path,
	std::string &error)
{
	std::ifstream stream(path);
	if (!stream)
	{
		error = "missing gamedata: " + path.string();
		return false;
	}
	smoke_gamedata_available_ = true;
#if defined(_WIN32)
	constexpr std::string_view recipient_key = "recipient_slot_offset_windows";
	constexpr std::string_view server_size_key = "server_binary_size_windows";
	constexpr std::string_view server_crc_key = "server_binary_crc32_windows";
	constexpr std::string_view entity_system_key = "game_entity_system_offset_windows";
	constexpr std::string_view full_update_key = "checktransmit_full_update_offset_windows";
	constexpr std::string_view smoke_volume_key = "smoke_volume_offset_windows";
	constexpr std::string_view smoke_storage_key = "smoke_storage_offset_windows";
	constexpr std::string_view smoke_frame_key = "smoke_frame_offset_windows";
	constexpr std::string_view smoke_center_key = "smoke_center_offset_windows";
	constexpr std::string_view smoke_start_time_key = "smoke_start_time_offset_windows";
	constexpr std::string_view game_event_vtable_key = "game_event_manager_vtable_rva_windows";
	constexpr std::string_view lookup_bone_key = "lookup_bone_rva_windows";
	constexpr std::string_view get_bone_transform_key = "get_bone_transform_rva_windows";
	constexpr std::string_view create_entity_key = "create_entity_by_name_rva_windows";
	constexpr std::string_view dispatch_spawn_key = "dispatch_spawn_rva_windows";
	constexpr std::string_view remove_entity_key = "remove_entity_rva_windows";
	constexpr std::string_view teleport_key = "teleport_vtable_index_windows";
#else
	constexpr std::string_view recipient_key = "recipient_slot_offset_linux";
	constexpr std::string_view server_size_key = "server_binary_size_linux";
	constexpr std::string_view server_crc_key = "server_binary_crc32_linux";
	constexpr std::string_view entity_system_key = "game_entity_system_offset_linux";
	constexpr std::string_view full_update_key = "checktransmit_full_update_offset_linux";
	constexpr std::string_view smoke_volume_key = "smoke_volume_offset_linux";
	constexpr std::string_view smoke_storage_key = "smoke_storage_offset_linux";
	constexpr std::string_view smoke_frame_key = "smoke_frame_offset_linux";
	constexpr std::string_view smoke_center_key = "smoke_center_offset_linux";
	constexpr std::string_view smoke_start_time_key = "smoke_start_time_offset_linux";
	constexpr std::string_view game_event_vtable_key = "game_event_manager_vtable_rva_linux";
	constexpr std::string_view lookup_bone_key = "lookup_bone_rva_linux";
	constexpr std::string_view get_bone_transform_key = "get_bone_transform_rva_linux";
	constexpr std::string_view create_entity_key = "create_entity_by_name_rva_linux";
	constexpr std::string_view dispatch_spawn_key = "dispatch_spawn_rva_linux";
	constexpr std::string_view remove_entity_key = "remove_entity_rva_linux";
	constexpr std::string_view teleport_key = "teleport_vtable_index_linux";
#endif
	std::string line;
	while (std::getline(stream, line))
	{
		const size_t equals = line.find('=');
		if (equals == std::string::npos || line.starts_with("//"))
		{
			continue;
		}
		const std::string key = line.substr(0, equals);
		const bool smoke_key = key == smoke_volume_key || key == smoke_storage_key
			|| key == smoke_frame_key || key == smoke_center_key
			|| key == smoke_start_time_key;
		const bool debug_key = key == create_entity_key || key == dispatch_spawn_key
			|| key == remove_entity_key || key == teleport_key;
		if (key != server_size_key && key != server_crc_key && key != recipient_key
			&& key != entity_system_key && key != full_update_key
			&& key != game_event_vtable_key && key != lookup_bone_key
			&& key != get_bone_transform_key && !debug_key && !smoke_key)
		{
			continue;
		}
		uint32_t value {};
		const std::string_view text(line.data() + equals + 1u,
			line.size() - equals - 1u);
		if (!parse_gamedata_uint32(text, value))
		{
			if (smoke_key || debug_key || key == game_event_vtable_key)
			{
				if (smoke_key) smoke_gamedata_available_ = false;
				continue;
			}
			error = "invalid gamedata value for " + key;
			return false;
		}
		if (key == server_size_key) server_binary_fingerprint_.size = value;
		if (key == server_crc_key) server_binary_fingerprint_.crc32 = value;
		if (key == recipient_key) recipient_slot_offset_ = value;
		if (key == entity_system_key) entity_system_offset_ = value;
		if (key == full_update_key) transmit_offsets_.full_update_offset = value;
		if (key == smoke_volume_key) smoke_layout_.volume = value;
		if (key == smoke_storage_key) smoke_layout_.storage = value;
		if (key == smoke_frame_key) smoke_layout_.frame = value;
		if (key == smoke_center_key) smoke_layout_.center = value;
		if (key == smoke_start_time_key) smoke_layout_.start_time = value;
		if (key == game_event_vtable_key) game_event_manager_vtable_rva_ = value;
		if (key == lookup_bone_key) lookup_bone_rva_ = value;
		if (key == get_bone_transform_key) get_bone_transform_rva_ = value;
		if (key == create_entity_key) create_entity_by_name_rva_ = value;
		if (key == dispatch_spawn_key) dispatch_spawn_rva_ = value;
		if (key == remove_entity_key) remove_entity_rva_ = value;
		if (key == teleport_key) teleport_vtable_index_ = value;
	}
	if (server_binary_fingerprint_.size == 0
		|| server_binary_fingerprint_.crc32 == 0
		|| recipient_slot_offset_ == 0 || entity_system_offset_ == 0
		|| transmit_offsets_.full_update_offset == 0 || lookup_bone_rva_ == 0
		|| get_bone_transform_rva_ == 0)
	{
		error = "gamedata does not contain this platform's required values";
		return false;
	}
	if (recipient_slot_offset_ < sizeof(CCheckTransmitInfo)
		|| recipient_slot_offset_ > k_max_gamedata_offset
		|| recipient_slot_offset_ % alignof(int) != 0
		|| entity_system_offset_ < sizeof(void *)
		|| entity_system_offset_ > k_max_gamedata_offset
		|| entity_system_offset_ % alignof(void *) != 0
		|| !valid_gamedata_offset(transmit_offsets_.full_update_offset,
			static_cast<uint32_t>(alignof(bool)), k_max_gamedata_offset)
		|| !valid_gamedata_offset(lookup_bone_rva_, 1, k_max_module_rva)
		|| !valid_gamedata_offset(get_bone_transform_rva_, 1, k_max_module_rva))
	{
		error = "gamedata contains invalid offsets for this platform";
		return false;
	}
	smoke_gamedata_available_ = smoke_gamedata_available_
		&& valid_gamedata_offset(smoke_layout_.volume,
			static_cast<uint32_t>(alignof(void *)), k_max_gamedata_offset)
		&& valid_gamedata_offset(smoke_layout_.storage,
			static_cast<uint32_t>(alignof(void *)), k_max_gamedata_offset)
		&& valid_gamedata_offset(smoke_layout_.frame,
			static_cast<uint32_t>(alignof(int32_t)), k_max_gamedata_offset)
		&& valid_gamedata_offset(smoke_layout_.center,
			static_cast<uint32_t>(alignof(float)), k_max_gamedata_offset)
		&& valid_gamedata_offset(smoke_layout_.start_time,
			static_cast<uint32_t>(alignof(float)), k_max_gamedata_offset);
	if (!valid_gamedata_offset(game_event_manager_vtable_rva_,
		static_cast<uint32_t>(alignof(void *)), k_max_module_rva))
	{
		game_event_manager_vtable_rva_ = 0;
	}
	if (!valid_gamedata_offset(create_entity_by_name_rva_, 1, k_max_module_rva)
		|| !valid_gamedata_offset(dispatch_spawn_rva_, 1, k_max_module_rva)
		|| !valid_gamedata_offset(remove_entity_rva_, 1, k_max_module_rva)
		|| teleport_vtable_index_ == 0
		|| teleport_vtable_index_ > k_max_vtable_index)
	{
		create_entity_by_name_rva_ = 0;
		dispatch_spawn_rva_ = 0;
		remove_entity_rva_ = 0;
		teleport_vtable_index_ = 0;
	}
	return true;
}

bool runtime_compatibility::verify_server_binary(
	ISource2GameEntities *game_entities, std::string &error)
{
	std::ostringstream expected;
	expected << "expected size=" << std::dec << server_binary_fingerprint_.size
		<< " crc=0x" << std::hex << server_binary_fingerprint_.crc32;
	if (game_entities == nullptr)
	{
		error = "server game-entities interface is unavailable; " + expected.str()
			+ ", actual unavailable";
		return false;
	}
	const std::filesystem::path path = module_path(
		*reinterpret_cast<void **>(game_entities));
	if (path.empty())
	{
		error = "could not resolve the loaded server binary path; "
			+ expected.str() + ", actual unavailable";
		return false;
	}
	uint64_t actual_size = 0;
	uint32_t actual_crc = 0;
	std::string fingerprint_error;
	if (!file_crc32(path, actual_size, actual_crc, fingerprint_error))
	{
		error = "could not verify loaded server binary: " + fingerprint_error
			+ "; " + expected.str() + ", actual unavailable";
		return false;
	}
	if (actual_size <= UINT32_MAX)
	{
		detected_server_binary_fingerprint_ = {
			static_cast<uint32_t>(actual_size), actual_crc
		};
	}
	if (actual_size != server_binary_fingerprint_.size
		|| actual_crc != server_binary_fingerprint_.crc32)
	{
		std::ostringstream message;
		message << "server binary does not match verified gamedata: "
			<< expected.str() << ", actual size=" << std::dec << actual_size
			<< " crc=0x" << std::hex << actual_crc;
		error = message.str();
		return false;
	}
	return true;
}

bool runtime_compatibility::resolve_private_functions(
	ISource2GameEntities *game_entities, std::string &error)
{
	server_module_base_ = game_entities == nullptr ? nullptr
		: module_base(*reinterpret_cast<void **>(game_entities));
	if (server_module_base_ == nullptr)
	{
		error = "could not resolve the loaded server binary base for animated capsules";
		return false;
	}
	lookup_bone_ = static_cast<uint8_t *>(server_module_base_) + lookup_bone_rva_;
	get_bone_transform_ = static_cast<uint8_t *>(server_module_base_)
		+ get_bone_transform_rva_;
	if (create_entity_by_name_rva_ != 0)
	{
		create_entity_by_name_ = static_cast<uint8_t *>(server_module_base_)
			+ create_entity_by_name_rva_;
		dispatch_spawn_ = static_cast<uint8_t *>(server_module_base_)
			+ dispatch_spawn_rva_;
		remove_entity_ = static_cast<uint8_t *>(server_module_base_)
			+ remove_entity_rva_;
	}
	return true;
}

bool runtime_compatibility::resolve_schema(ISchemaSystem *schema,
	std::string &error)
{
	if (schema == nullptr)
	{
		error = "schema system is unavailable";
		return false;
	}
	auto require = [&](uint32_t &target, const char *class_name,
		const char *field_name)
	{
		if (!resolve_field(schema, class_name, field_name, target))
		{
			if (!error.empty()) error += ", ";
			error += class_name;
			error += "::";
			error += field_name;
		}
	};
	auto optional = [&](uint32_t &target, const char *class_name,
		const char *field_name)
	{
		return resolve_field(schema, class_name, field_name, target);
	};
	auto require_global = [&](uint32_t &target, const char *class_name,
		const char *field_name)
	{
		if (!resolve_global_field(schema, class_name, field_name, target))
		{
			if (!error.empty()) error += ", ";
			error += class_name;
			error += "::";
			error += field_name;
		}
	};
	require(fields_.is_hltv, "CBasePlayerController", "m_bIsHLTV");
	require(fields_.player_pawn, "CCSPlayerController", "m_hPlayerPawn");
	require(fields_.pawn_controller, "CBasePlayerPawn", "m_hController");
	require(fields_.death_time, "CBasePlayerPawn", "m_flDeathTime");
	require(fields_.health, "CBaseEntity", "m_iHealth");
	require(fields_.life_state, "CBaseEntity", "m_lifeState");
	require(fields_.team, "CBaseEntity", "m_iTeamNum");
	require(fields_.body_component, "CBaseEntity", "m_CBodyComponent");
	require(fields_.scene_node, "CBodyComponent", "m_pSceneNode");
	require(fields_.abs_origin, "CGameSceneNode", "m_vecAbsOrigin");
	require(fields_.movement_services, "CBasePlayerPawn", "m_pMovementServices");
	require(fields_.movement_buttons, "CPlayer_MovementServices", "m_nButtons");
	require_global(fields_.button_states, "CInButtonState", "m_pButtonStates");
	require(fields_.view_offset, "CBaseModelEntity", "m_vecViewOffset");
	require(fields_.view_x, "CNetworkViewOffsetVector", "m_vecX");
	require(fields_.view_y, "CNetworkViewOffsetVector", "m_vecY");
	require(fields_.view_z, "CNetworkViewOffsetVector", "m_vecZ");
	require(fields_.eye_angles, "CCSPlayerPawn", "m_angEyeAngles");
	require(fields_.collision, "CBaseEntity", "m_pCollision");
	require(fields_.mins, "CCollisionProperty", "m_vecMins");
	require(fields_.maxs, "CCollisionProperty", "m_vecMaxs");
	require(fields_.weapon_services, "CBasePlayerPawn", "m_pWeaponServices");
	require(fields_.weapons, "CPlayer_WeaponServices", "m_hMyWeapons");
	require(fields_.active_weapon, "CPlayer_WeaponServices", "m_hActiveWeapon");
	require(fields_.last_weapon, "CPlayer_WeaponServices", "m_hLastWeapon");
	weapon_item_schema_available_ =
		optional(fields_.attribute_manager, "CEconEntity", "m_AttributeManager")
		&& optional(fields_.item, "CAttributeContainer", "m_Item")
		&& optional(fields_.item_definition_index, "CEconItemView",
			"m_iItemDefinitionIndex");
	require(fields_.wearables, "CBaseCombatCharacter", "m_hMyWearables");
	require(fields_.hostage_services, "CCSPlayerPawn", "m_pHostageServices");
	require(fields_.is_spawning, "CCSPlayerPawn", "m_bIsSpawning");
	require(fields_.death_flags, "CCSPlayerPawn", "m_iDeathFlags");
	require(fields_.has_death_info, "CCSPlayerPawn", "m_bHasDeathInfo");
	require(fields_.death_info_time, "CCSPlayerPawn", "m_flDeathInfoTime");
	require(fields_.carried_hostage_prop, "CCSPlayer_HostageServices",
		"m_hCarriedHostageProp");
	smoke_schema_available_ = optional(fields_.did_smoke_effect,
		"CSmokeGrenadeProjectile", "m_bDidSmokeEffect");
	debug_beam_schema_available_ = optional(fields_.beam_end_position, "CBeam",
		"m_vecEndPos")
		&& optional(fields_.beam_width, "CBeam", "m_fWidth")
		&& optional(fields_.beam_end_width, "CBeam", "m_fEndWidth")
		&& optional(fields_.render_color, "CBaseModelEntity", "m_clrRender");
	if (!error.empty())
	{
		error = "missing schema fields: " + error;
		return false;
	}
	return true;
}

void *runtime_compatibility::game_event_manager_vtable() const
{
	return server_module_base_ == nullptr || game_event_manager_vtable_rva_ == 0
		? nullptr : static_cast<uint8_t *>(server_module_base_)
			+ game_event_manager_vtable_rva_;
}

} // namespace cs2fow
