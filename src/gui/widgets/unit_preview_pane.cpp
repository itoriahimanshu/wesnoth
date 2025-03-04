/*
	Copyright (C) 2016 - 2022
	Part of the Battle for Wesnoth Project https://www.wesnoth.org/

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY.

	See the COPYING file for more details.
*/

#define GETTEXT_DOMAIN "wesnoth-lib"

#include "gui/widgets/unit_preview_pane.hpp"

#include "gui/auxiliary/find_widget.hpp"

#include "gui/core/register_widget.hpp"
#include "gui/widgets/button.hpp"
#include "gui/widgets/drawing.hpp"
#include "gui/widgets/image.hpp"
#include "gui/widgets/label.hpp"
#include "gui/widgets/settings.hpp"
#include "gui/widgets/window.hpp"
#include "gui/widgets/tree_view.hpp"
#include "gui/widgets/tree_view_node.hpp"

#include "font/text_formatting.hpp"
#include "formatter.hpp"
#include "formula/string_utils.hpp"
#include "language.hpp"
#include "preferences/game.hpp"
#include "gettext.hpp"
#include "help/help.hpp"
#include "help/help_impl.hpp"
#include "play_controller.hpp"
#include "resources.hpp"
#include "team.hpp"
#include "terrain/movement.hpp"
#include "terrain/type_data.hpp"
#include "units/attack_type.hpp"
#include "units/types.hpp"
#include "units/helper.hpp"
#include "units/unit.hpp"

#include <functional>

namespace gui2
{

// ------------ WIDGET -----------{

REGISTER_WIDGET(unit_preview_pane)

unit_preview_pane::unit_preview_pane(const implementation::builder_unit_preview_pane& builder)
	: container_base(builder, type())
	, current_type_()
	, icon_type_(nullptr)
	, icon_race_(nullptr)
	, icon_alignment_(nullptr)
	, label_name_(nullptr)
	, label_level_(nullptr)
	, label_race_(nullptr)
	, label_details_(nullptr)
	, tree_details_(nullptr)
	, button_profile_(nullptr)
	, image_mods_()
{
}

void unit_preview_pane::finalize_setup()
{
	// Icons
	icon_type_              = find_widget<drawing>(this, "type_image", false, false);
	icon_race_              = find_widget<image>(this, "type_race", false, false);
	icon_alignment_         = find_widget<image>(this, "type_alignment", false, false);

	// Labels
	label_name_             = find_widget<label>(this, "type_name", false, false);
	label_level_            = find_widget<label>(this, "type_level", false, false);
	label_race_             = find_widget<label>(this, "type_race_label", false, false);
	label_details_          = find_widget<styled_widget>(this, "type_details_minimal", false, false);

	tree_details_           = find_widget<tree_view>(this, "type_details", false, false);

	// Profile button
	button_profile_ = find_widget<button>(this, "type_profile", false, false);

	if(button_profile_) {
		connect_signal_mouse_left_click(*button_profile_,
			std::bind(&unit_preview_pane::profile_button_callback, this));
	}
}

static inline tree_view_node& add_name_tree_node(tree_view_node& header_node, const std::string& type, const t_string& label, const t_string& tooltip = "")
{
	/* Note: We have to pass data instead of just doing 'child_label.set_label(label)' below
	 * because the tree_view_node::add_child needs to have the correct size of the
	 * node child widgets for its internal size calculations.
	 * Same is true for 'use_markup'
	 */
	auto& child_node = header_node.add_child(type, { { "name",{ { "label", label },{ "use_markup", "true" } } } });
	auto& child_label = find_widget<styled_widget>(&child_node, "name", true);

	child_label.set_tooltip(tooltip);
	return child_node;
}

static inline std::string get_hp_tooltip(
	const utils::string_map_res& res, const std::function<int(const std::string&, bool)>& get)
{
	std::ostringstream tooltip;

	std::vector<std::string> resistances_table;

	bool att_def_diff = false;
	for(const utils::string_map_res::value_type &resist : res) {
		std::ostringstream line;
		line << translation::dgettext("wesnoth", resist.first.c_str()) << ": ";

		// Some units have different resistances when attacking or defending.
		const int res_att = 100 - get(resist.first, true);
		const int res_def = 100 - get(resist.first, false);

		if(res_att == res_def) {
			line << "<span color='" << unit_helper::resistance_color(res_def) << "'>\t" << utils::signed_percent(res_def) << "</span>";
		} else {
			line << "<span color='" << unit_helper::resistance_color(res_att) << "'>\t" << utils::signed_percent(res_att) << "</span>" << "/"
			     << "<span color='" << unit_helper::resistance_color(res_def) << "'>"   << utils::signed_percent(res_def) << "</span>";
			att_def_diff = true;
		}

		resistances_table.push_back(line.str());
	}

	tooltip << "<big>" << _("Resistances: ") << "</big>";
	if(att_def_diff) {
		tooltip << _("(Att / Def)");
	}

	for(const std::string &line : resistances_table) {
		tooltip << '\n' << font::unicode_bullet << " " << line;
	}

	return tooltip.str();
}

static inline std::string get_mp_tooltip(int total_movement, std::function<int (t_translation::terrain_code)> get)
{
	std::set<terrain_movement> terrain_moves;
	std::ostringstream tooltip;
	tooltip << "<big>" << _("Movement Costs:") << "</big>";

	std::shared_ptr<terrain_type_data> tdata = help::load_terrain_types_data();

	if(!tdata) {
		return "";
	}

	for(t_translation::terrain_code terrain : preferences::encountered_terrains()) {
		if(terrain == t_translation::FOGGED || terrain == t_translation::VOID_TERRAIN || t_translation::terrain_matches(terrain, t_translation::ALL_OFF_MAP)) {
			continue;
		}

		const terrain_type& info = tdata->get_terrain_info(terrain);
		if(info.is_indivisible() && info.is_nonnull()) {
			terrain_moves.emplace(info.name(), get(terrain));
		}
	}

	for(const terrain_movement& tm: terrain_moves)
	{
		tooltip << '\n' << font::unicode_bullet << " " << tm.name << ": ";

		// movement  -  range: 1 .. 5, movetype::UNREACHABLE=impassable
		const bool cannot_move = tm.moves > total_movement;     // cannot move in this terrain
		double movement_red_to_green = 100.0 - 25.0 * tm.moves;

		// passing true to select the less saturated red-to-green scale
		std::string color = game_config::red_to_green(movement_red_to_green, true).to_hex_string();

		tooltip << "<span color='" << color << "'>";

		// A 5 MP margin; if the movement costs go above the unit's max moves + 5, we replace it with dashes.
		if(cannot_move && (tm.moves > total_movement + 5)) {
			tooltip << font::unicode_figure_dash;
		} else if (cannot_move) {
			tooltip << "(" << tm.moves << ")";
		} else {
			tooltip << tm.moves;
		}
		if(tm.moves != 0) {
			const int movement_hexes_per_turn = total_movement / tm.moves;
			tooltip << " ";
			for(int i = 0; i < movement_hexes_per_turn; ++i) {
				// Unicode horizontal black hexagon and Unicode zero width space (to allow a line break)
				tooltip << "\u2b23\u200b";
			}
		}

		tooltip << "</span>";
	}

	return tooltip.str();
}

/*
 * Both unit and unit_type use the same format (vector of attack_types) for their
 * attack data, meaning we can keep this as a helper function.
 */
template<typename T>
void unit_preview_pane::print_attack_details(T attacks, tree_view_node& parent_node)
{
	if(attacks.empty()) {
		return;
	}

	auto& header_node = add_name_tree_node(
		parent_node,
		"header",
		"<b>" + _("Attacks") + "</b>"
	);

	for(const auto& a : attacks) {
		const std::string range_png = std::string("icons/profiles/") + a.range() + "_attack.png~SCALE_INTO(16,16)";
		const std::string type_png = std::string("icons/profiles/") + a.type() + ".png~SCALE_INTO(16,16)";
		const bool range_png_exists = ::image::locator(range_png).file_exists();
		const bool type_png_exists = ::image::locator(type_png).file_exists();

		const t_string& range = string_table["range_" + a.range()];
		const t_string& type = string_table["type_" + a.type()];

		const std::string label = (formatter()
			 << font::span_color(font::unit_type_color)
			 << a.damage() << font::weapon_numbers_sep << a.num_attacks()
			 << " " << a.name() << "</span>").str();

		auto& subsection = header_node.add_child(
			"item_image",
			{
				{ "image_range", { { "label", range_png } } },
				{ "image_type", { { "label", type_png } } },
				{ "name", { { "label", label }, { "use_markup", "true" } } },
			}
		);

		find_widget<styled_widget>(&subsection, "image_range", true).set_tooltip(range);
		find_widget<styled_widget>(&subsection, "image_type", true).set_tooltip(type);

		if(!range_png_exists || !type_png_exists) {
			add_name_tree_node(
				subsection,
				"item",
				(formatter()
				 << font::span_color(font::weapon_details_color)
				 << range << font::weapon_details_sep
				 << type << "</span>"
				 ).str()
			);
		}

		for(const auto& pair : a.special_tooltips()) {
			add_name_tree_node(
				subsection,
				"item",
				(formatter() << font::span_color(font::weapon_details_color) << pair.first << "</span>").str(),
				(formatter() << "<span size='x-large'>" << pair.first << "</span>" << "\n" << pair.second).str()
			);
		}
	}
}

void unit_preview_pane::set_displayed_type(const unit_type& type)
{
	// Sets the current type id for the profile button callback to use
	current_type_ = type;

	if(icon_type_) {
		std::string mods;

		if(resources::controller) {
			mods = "~RC(" + type.flag_rgb() + ">" +
				 team::get_side_color_id(resources::controller->current_side())
				 + ")";
		}

		mods += image_mods_;

		icon_type_->set_label((type.icon().empty() ? type.image() : type.icon()) + mods);
	}

	if(label_name_) {
		label_name_->set_label("<big>" + type.type_name() + "</big>");
		label_name_->set_use_markup(true);
	}

	if(label_level_) {
		std::string l_str = VGETTEXT("Lvl $lvl", {{"lvl", std::to_string(type.level())}});

		label_level_->set_label("<b>" + l_str + "</b>");
		label_level_->set_tooltip(unit_helper::unit_level_tooltip(type));
		label_level_->set_use_markup(true);
	}

	if(label_race_) {
		label_race_ ->set_label(type.race()->name(type.genders().front()));
	}

	if(icon_race_) {
		icon_race_->set_label(type.race()->get_icon_path_stem() + "_30.png");
	}

	if(icon_alignment_) {
		const std::string& alignment_name = unit_alignments::get_string(type.alignment());

		icon_alignment_->set_label("icons/alignments/alignment_" + alignment_name + "_30.png");
		icon_alignment_->set_tooltip(unit_type::alignment_description(
			type.alignment(),
			type.genders().front()));
	}

	if(label_details_) {
		std::stringstream str;

		str << "<span size='large'> </span>" << "\n";

		str << font::span_color(font::unit_type_color) << type.type_name() << "</span>" << "\n";

		std::string l_str = VGETTEXT("Lvl $lvl", {{"lvl", std::to_string(type.level())}});
		str << l_str << "\n";

		str << unit_alignments::get_string(type.alignment()) << "\n";

		str << "\n"; // Leave a blank line where traits would be

		str <<  _("HP: ") << type.hitpoints() << "\n";

		str << _("XP: ") << type.experience_needed(true);

		label_details_->set_label(str.str());
		label_details_->set_use_markup(true);
	}

	if(tree_details_) {

		tree_details_->clear();
		tree_details_->add_node("hp_xp_mp", {
			{ "hp",{
				{ "label", (formatter() << "<small>" << font::span_color(unit::hp_color_max()) << "<b>" << _("HP: ") << "</b>" << type.hitpoints() << "</span>" << " | </small>").str() },
				{ "use_markup", "true" },
				{ "tooltip", get_hp_tooltip(type.movement_type().get_resistances().damage_table(), [&type](const std::string& dt, bool is_attacker) { return type.resistance_against(dt, is_attacker); }) }
			} },
			{ "xp",{
				{ "label", (formatter() << "<small>" << font::span_color(unit::xp_color(100, type.can_advance(), true)) << "<b>" << _("XP: ") << "</b>" << type.experience_needed() << "</span>" << " | </small>").str() },
				{ "use_markup", "true" },
				{ "tooltip", (formatter() << _("Experience Modifier: ") << unit_experience_accelerator::get_acceleration() << '%').str() }
			} },
			{ "mp",{
				{ "label", (formatter() << "<small>" << "<b>" << _("MP: ") << "</b>" << type.movement() << "</small>").str() },
				{ "use_markup", "true" },
				{ "tooltip", get_mp_tooltip(type.movement(), [&type](t_translation::terrain_code terrain) { return type.movement_type().movement_cost(terrain); }) }
			} },
		});

		// Print trait details
		{
			tree_view_node* header_node = nullptr;

			for(const auto& tr : type.possible_traits()) {
				t_string name = tr[type.genders().front() == unit_race::FEMALE ? "female_name" : "male_name"];
				if(tr["availability"] != "musthave" || name.empty()) {
					continue;
				}

				if(header_node == nullptr) {
					header_node = &add_name_tree_node(
						tree_details_->get_root_node(),
						"header",
						"<b>" + _("Traits") + "</b>"
					);
				}

				add_name_tree_node(
					*header_node,
					"item",
					name
				);
			}
		}

		// Print ability details
		if(!type.abilities_metadata().empty()) {

			auto& header_node = add_name_tree_node(
				tree_details_->get_root_node(),
				"header",
				"<b>" + _("Abilities") + "</b>"
			);

			for(const auto& ab : type.abilities_metadata()) {
				if(!ab.name.empty()) {
					add_name_tree_node(
						header_node,
						"item",
						ab.name,
						(formatter() << "<span size='x-large'>" << ab.name << "</span>\n" << ab.description).str()
					);
				}
			}
		}

		print_attack_details(type.attacks(), tree_details_->get_root_node());
	}
}

void unit_preview_pane::set_displayed_unit(const unit& u)
{
	// Sets the current type id for the profile button callback to use
	current_type_ = u.type();

	if(icon_type_) {
		std::string mods = u.image_mods();

		if(u.can_recruit()) {
			mods += "~BLIT(" + unit::leader_crown() + ")";
		}

		for(const std::string& overlay : u.overlays()) {
			mods += "~BLIT(" + overlay + ")";
		}

		mods += image_mods_;

		icon_type_->set_label(u.absolute_image() + mods);
	}

	if(label_name_) {
		std::string name;
		if(!u.name().empty()) {
			name = "<span size='large'>" + u.name() + "</span>" + "\n" + "<small>" + font::span_color(font::unit_type_color) + u.type_name() + "</span></small>";
		} else {
			name = "<span size='large'>" + u.type_name() + "</span>\n";
		}

		label_name_->set_label(name);
		label_name_->set_use_markup(true);
	}

	if(label_level_) {
		std::string l_str = VGETTEXT("Lvl $lvl", {{"lvl", std::to_string(u.level())}});

		label_level_->set_label("<b>" + l_str + "</b>");
		label_level_->set_tooltip(unit_helper::unit_level_tooltip(u));
		label_level_->set_use_markup(true);
	}

	if(label_race_) {
		label_race_->set_label(u.race()->name(u.gender()));
	}

	if(icon_race_) {
		icon_race_->set_label(u.race()->get_icon_path_stem() + "_30.png");
	}

	if(icon_alignment_) {
		const std::string& alignment_name = unit_alignments::get_string(u.alignment());

		icon_alignment_->set_label("icons/alignments/alignment_" + alignment_name + "_30.png");
		icon_alignment_->set_tooltip(unit_type::alignment_description(
			u.alignment(),
			u.gender()));
	}

	if(label_details_) {
		std::stringstream str;

		const std::string name = "<span size='large'>" + (!u.name().empty() ? u.name() : " ") + "</span>";
		str << name << "\n";

		str << font::span_color(font::unit_type_color) << u.type_name() << "</span>" << "\n";

		std::string l_str = VGETTEXT("Lvl $lvl", {{"lvl", std::to_string(u.level())}});
		str << l_str << "\n";

		str << unit_type::alignment_description(u.alignment(), u.gender()) << "\n";

		str << utils::join(u.trait_names(), ", ") << "\n";

		str << font::span_color(u.hp_color())
			<< _("HP: ") << u.hitpoints() << "/" << u.max_hitpoints() << "</span>" << "\n";

		str << font::span_color(u.xp_color()) << _("XP: ");
		if(u.can_advance()) {
			str << u.experience() << "/" << u.max_experience();
		} else {
			str << font::unicode_en_dash;
		}
		str << "</span>";

		label_details_->set_label(str.str());
		label_details_->set_use_markup(true);
	}

	if(tree_details_) {
		tree_details_->clear();
		const std::string unit_xp = u.can_advance() ? (formatter() << u.experience() << "/" << u.max_experience()).str() : font::unicode_en_dash;
		tree_details_->add_node("hp_xp_mp", {
			{ "hp",{
				{ "label", (formatter() << "<small>" << font::span_color(u.hp_color()) << "<b>" << _("HP: ") << "</b>" << u.hitpoints() << "/" << u.max_hitpoints() << "</span>" << " | </small>").str() },
				{ "use_markup", "true" },
				{ "tooltip", get_hp_tooltip(u.get_base_resistances(), [&u](const std::string& dt, bool is_attacker) { return u.resistance_against(dt, is_attacker, u.get_location()); }) }
			} },
			{ "xp",{
				{ "label", (formatter() << "<small>" << font::span_color(u.xp_color()) << "<b>" << _("XP: ") << "</b>" << unit_xp << "</span>" << " | </small>").str() },
				{ "use_markup", "true" },
				{ "tooltip", (formatter() << _("Experience Modifier: ") << unit_experience_accelerator::get_acceleration() << '%').str() }
			} },
			{ "mp",{
				{ "label", (formatter() << "<small>" << "<b>" << _("MP: ") << "</b>" << u.movement_left() << "/" << u.total_movement() << "</small>").str() },
				{ "use_markup", "true" },
				{ "tooltip", get_mp_tooltip(u.total_movement(), [&u](t_translation::terrain_code terrain) { return u.movement_cost(terrain); }) }
			} },
		});

		if(!u.trait_names().empty()) {
			auto& header_node = add_name_tree_node(
				tree_details_->get_root_node(),
				"header",
				"<b>" + _("Traits") + "</b>"
			);

			assert(u.trait_names().size() == u.trait_descriptions().size());
			for (std::size_t i = 0; i < u.trait_names().size(); ++i) {
				add_name_tree_node(
					header_node,
					"item",
					u.trait_names()[i],
					u.trait_descriptions()[i]
				);
			}
		}

		if(!u.get_ability_list().empty()) {
			auto& header_node = add_name_tree_node(
				tree_details_->get_root_node(),
				"header",
				"<b>" + _("Abilities") + "</b>"
			);

			for(const auto& ab : u.ability_tooltips()) {
				add_name_tree_node(
					header_node,
					"item",
					std::get<2>(ab),
					std::get<3>(ab)
				);
			}
		}
		print_attack_details(u.attacks(), tree_details_->get_root_node());
	}
}

void unit_preview_pane::profile_button_callback()
{
	if(get_window() && current_type_) {
		help::show_unit_description(*current_type_);
	}
}

void unit_preview_pane::set_image_mods(const std::string& mods)
{
	image_mods_ = mods;
}

void unit_preview_pane::set_active(const bool /*active*/)
{
	/* DO NOTHING */
}

bool unit_preview_pane::get_active() const
{
	return true;
}

unsigned unit_preview_pane::get_state() const
{
	return ENABLED;
}

void unit_preview_pane::set_self_active(const bool /*active*/)
{
	/* DO NOTHING */
}

// }---------- DEFINITION ---------{

unit_preview_pane_definition::unit_preview_pane_definition(const config& cfg)
	: styled_widget_definition(cfg)
{
	DBG_GUI_P << "Parsing unit preview pane " << id;

	load_resolutions<resolution>(cfg);
}

unit_preview_pane_definition::resolution::resolution(const config& cfg)
	: resolution_definition(cfg), grid()
{
	state.emplace_back(cfg.optional_child("background"));
	state.emplace_back(cfg.optional_child("foreground"));

	auto child = cfg.optional_child("grid");
	VALIDATE(child, _("No grid defined."));

	grid = std::make_shared<builder_grid>(*child);
}

// }---------- BUILDER -----------{

namespace implementation
{

builder_unit_preview_pane::builder_unit_preview_pane(const config& cfg)
	: builder_styled_widget(cfg)
	, image_mods_(cfg["image_mods"])
{
}

std::unique_ptr<widget> builder_unit_preview_pane::build() const
{
	auto widget = std::make_unique<unit_preview_pane>(*this);

	DBG_GUI_G << "Window builder: placed unit preview pane '" << id
			  << "' with definition '" << definition << "'.";

	const auto conf = widget->cast_config_to<unit_preview_pane_definition>();
	assert(conf);

	widget->init_grid(*conf->grid);
	widget->finalize_setup();
	widget->set_image_mods(image_mods_);

	return widget;
}

} // namespace implementation

// }------------ END --------------

} // namespace gui2
