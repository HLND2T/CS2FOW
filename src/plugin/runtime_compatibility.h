#pragma once

#include "runtime_compatibility_model.h"
#include "transmit_masks.h"

#include <cstdint>
#include <filesystem>
#include <string>

class ISchemaSystem;
class ISource2GameEntities;

namespace cs2fow
{

struct schema_offsets
{
	uint32_t is_hltv {};
	uint32_t player_pawn {};
	uint32_t pawn_controller {};
	uint32_t death_time {};
	uint32_t health {};
	uint32_t life_state {};
	uint32_t team {};
	uint32_t body_component {};
	uint32_t scene_node {};
	uint32_t abs_origin {};
	uint32_t movement_services {};
	uint32_t movement_buttons {};
	uint32_t button_states {};
	uint32_t view_offset {};
	uint32_t view_x {};
	uint32_t view_y {};
	uint32_t view_z {};
	uint32_t eye_angles {};
	uint32_t collision {};
	uint32_t mins {};
	uint32_t maxs {};
	uint32_t weapon_services {};
	uint32_t weapons {};
	uint32_t active_weapon {};
	uint32_t last_weapon {};
	uint32_t attribute_manager {};
	uint32_t item {};
	uint32_t item_definition_index {};
	uint32_t wearables {};
	uint32_t hostage_services {};
	uint32_t is_spawning {};
	uint32_t death_flags {};
	uint32_t has_death_info {};
	uint32_t death_info_time {};
	uint32_t carried_hostage_prop {};
	uint32_t did_smoke_effect {};
	uint32_t beam_end_position {};
	uint32_t beam_width {};
	uint32_t beam_end_width {};
	uint32_t render_color {};
};

struct smoke_private_layout
{
	uint32_t volume {};
	uint32_t storage {};
	uint32_t frame {};
	uint32_t center {};
	uint32_t start_time {};
};

class runtime_compatibility
{
public:
	bool initialize(const std::filesystem::path &base_directory,
		ISource2GameEntities *game_entities, ISchemaSystem *schema,
		bool he_event_manager_available);

	bool valid() const { return report_.state == compatibility_state::compatible; }
	const compatibility_report &report() const { return report_; }
	server_binary_fingerprint detected_server_binary_fingerprint() const
	{
		return detected_server_binary_fingerprint_;
	}
	const schema_offsets &fields() const { return fields_; }
	const smoke_private_layout &smoke_layout() const { return smoke_layout_; }
	const checktransmit_private_offsets &transmit_offsets() const { return transmit_offsets_; }
	uint32_t recipient_slot_offset() const { return recipient_slot_offset_; }
	uint32_t entity_system_offset() const { return entity_system_offset_; }
	uint32_t teleport_vtable_index() const { return teleport_vtable_index_; }
	void *game_event_manager_vtable() const;
	void *lookup_bone() const { return lookup_bone_; }
	void *get_bone_transform() const { return get_bone_transform_; }
	void *create_entity_by_name() const { return create_entity_by_name_; }
	void *dispatch_spawn() const { return dispatch_spawn_; }
	void *remove_entity() const { return remove_entity_; }
	bool smoke_available() const { return smoke_gamedata_available_ && smoke_schema_available_; }
	bool weapon_item_available() const { return weapon_item_schema_available_; }
	bool debug_beam_available() const
	{
		return debug_beam_schema_available_ && create_entity_by_name_ != nullptr
			&& dispatch_spawn_ != nullptr && remove_entity_ != nullptr
			&& teleport_vtable_index_ != 0;
	}

private:
	bool read_gamedata(const std::filesystem::path &path, std::string &error);
	bool verify_server_binary(ISource2GameEntities *game_entities, std::string &error);
	bool resolve_private_functions(ISource2GameEntities *game_entities, std::string &error);
	bool resolve_schema(ISchemaSystem *schema, std::string &error);
	void set_report(compatibility_state state, std::string detail,
		bool he_event_manager_available);

	compatibility_report report_;
	schema_offsets fields_;
	smoke_private_layout smoke_layout_;
	checktransmit_private_offsets transmit_offsets_;
	uint32_t recipient_slot_offset_ {};
	uint32_t entity_system_offset_ {};
	uint32_t game_event_manager_vtable_rva_ {};
	uint32_t lookup_bone_rva_ {};
	uint32_t get_bone_transform_rva_ {};
	uint32_t create_entity_by_name_rva_ {};
	uint32_t dispatch_spawn_rva_ {};
	uint32_t remove_entity_rva_ {};
	uint32_t teleport_vtable_index_ {};
	server_binary_fingerprint server_binary_fingerprint_ {};
	server_binary_fingerprint detected_server_binary_fingerprint_;
	bool weapon_item_schema_available_ {};
	bool smoke_schema_available_ {};
	bool debug_beam_schema_available_ {};
	bool smoke_gamedata_available_ {};
	void *server_module_base_ {};
	void *lookup_bone_ {};
	void *get_bone_transform_ {};
	void *create_entity_by_name_ {};
	void *dispatch_spawn_ {};
	void *remove_entity_ {};
};

} // namespace cs2fow
