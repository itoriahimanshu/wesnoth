/*
	Copyright (C) 2016 - 2022
	by Charles Dang <exodia339gmail.com>
	Copyright (C) 2011, 2015 by Iris Morelle <shadowm2006@gmail.com>
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

#include "gui/dialogs/preferences_dialog.hpp"

#include "display.hpp"
#include "events.hpp"
#include "filesystem.hpp"
#include "formatter.hpp"
#include "formula/string_utils.hpp"
#include "gettext.hpp"
#include "hotkey/hotkey_item.hpp"
#include "lexical_cast.hpp"
#include "preferences/credentials.hpp"
#include "preferences/display.hpp"
#include "preferences/game.hpp"
#include "preferences/general.hpp"
#include "preferences/lobby.hpp"
#include "video.hpp"

// Sub-dialog includes
#include "gui/dialogs/game_cache_options.hpp"
#include "gui/dialogs/hotkey_bind.hpp"
#include "gui/dialogs/log_settings.hpp"
#include "gui/dialogs/multiplayer/mp_alerts_options.hpp"
#include "gui/dialogs/select_orb_colors.hpp"

#include "gui/auxiliary/find_widget.hpp"
#include "gui/dialogs/game_version_dialog.hpp"
#include "gui/dialogs/message.hpp"
#include "gui/dialogs/transient_message.hpp"
#include "gui/widgets/button.hpp"
#include "gui/widgets/menu_button.hpp"
#include "gui/widgets/multimenu_button.hpp"
#include "gui/widgets/grid.hpp"
#include "gui/widgets/image.hpp"
#include "gui/widgets/label.hpp"
#include "gui/widgets/listbox.hpp"
#include "gui/widgets/scroll_label.hpp"
#include "gui/widgets/settings.hpp"
#include "gui/widgets/slider.hpp"
#include "gui/widgets/stacked_widget.hpp"
#include "gui/widgets/status_label_helper.hpp"
#include "gui/widgets/text_box.hpp"
#include "gui/widgets/toggle_button.hpp"
#include "gui/widgets/window.hpp"

#include <functional>
#include <numeric>

namespace gui2::dialogs
{
namespace
{
template<typename W>
void disable_widget_on_toggle(window& window, widget& w, const std::string& id)
{
	find_widget<W>(&window, id, false).set_active(dynamic_cast<selectable_item&>(w).get_value_bool());
}

template<typename W>
void disable_widget_on_toggle_inverted(window& window, widget& w, const std::string& id)
{
	find_widget<W>(&window, id, false).set_active(!dynamic_cast<selectable_item&>(w).get_value_bool());
}

// Ensure the specified index is between 0 and one less than the max
// number of pager layers (since get_layer_count returns one-past-end).
int index_in_pager_range(const int first, const stacked_widget& pager)
{
	return std::clamp<int>(first, 0, pager.get_layer_count() - 1);
}

// Helper to make it easier to immediately apply sound toggles immediately.
template<bool(*fptr)(bool)>
void sound_toggle_on_change(window& window, const std::string& id_to_toggle, widget& w)
{
	std::invoke(fptr, dynamic_cast<selectable_item&>(w).get_value_bool());

	// Toggle the corresponding slider.
	disable_widget_on_toggle<slider>(window, w, id_to_toggle);
}

// Helper to make it easier to immediately apply volume (music, etc) setings on change.
template<void(*fptr)(int)>
void volume_setter_on_change(widget& w)
{
	std::invoke(fptr, dynamic_cast<integer_selector&>(w).get_value());
}

} // end anon namespace

using namespace preferences;
using avp = preferences::advanced_manager::option;

REGISTER_DIALOG(preferences_dialog)

preferences_dialog::preferences_dialog(const PREFERENCE_VIEW initial_view)
	: modal_dialog(window_id())
	, adv_preferences_(preferences::get_advanced_preferences())
	, resolutions_() // should be populated by set_resolution_list before use
	, last_selected_item_(0)
	, accl_speeds_({0.25, 0.5, 0.75, 1, 1.25, 1.5, 1.75, 2, 3, 4, 8, 16})
	, visible_hotkeys_()
	, visible_categories_()
	, initial_index_(pef_view_map[initial_view])
{
	initialize_callbacks();
}

// Helper function to refresh resolution list
void preferences_dialog::set_resolution_list(menu_button& res_list)
{
	resolutions_ = video::get_available_resolutions(true);

	std::vector<config> options;
	for(const point& res : resolutions_) {
		config option;
		option["label"] = formatter() << res.x << font::unicode_multiplication_sign << res.y;

		const int div = std::gcd(res.x, res.y);
		const int x_ratio = res.x / div;
		const int y_ratio = res.y / div;

		if(x_ratio <= 10 || y_ratio <= 10) {
			option["details"] = formatter() << "<span color='#777777'>(" << x_ratio << ':' << y_ratio << ")</span>";
		}

		options.push_back(std::move(option));
	}

	const unsigned current_res = std::distance(resolutions_.begin(), std::find(
		resolutions_.begin(), resolutions_.end(), video::current_resolution()));

	res_list.set_values(options, current_res);
}

widget_data preferences_dialog::get_friends_list_row_data(const acquaintance& entry)
{
	widget_data data;
	widget_item item;

	std::string image = "friend.png";
	std::string descriptor = _("friend");
	std::string notes;

	if(entry.get_status() == "ignore") {
		image = "ignore.png";
		descriptor = _("ignored");
	}

	if(!entry.get_notes().empty()) {
		notes = " <small>(" + entry.get_notes() + ")</small>";
	}

	item["use_markup"] = "true";

	item["label"] = "misc/status-" + image;
	data.emplace("friend_icon", item);

	item["label"] = entry.get_nick() + notes;
	data.emplace("friend_name", item);

	item["label"] = "<small>" + descriptor + "</small>";
	data.emplace("friend_status", item);

	return data;
}

void preferences_dialog::on_friends_list_select(listbox& list, text_box& textbox)
{
	const int num_friends = get_acquaintances().size();
	const int sel = list.get_selected_row();

	if(sel < 0 || sel >= num_friends) {
		return;
	}

	std::map<std::string, acquaintance>::const_iterator who = get_acquaintances().begin();
	std::advance(who, sel);

	textbox.set_value(who->second.get_nick() + " " + who->second.get_notes());
}

void preferences_dialog::update_friends_list_controls(listbox& list)
{
	const bool list_empty = list.get_item_count() == 0;

	if(!list_empty) {
		list.select_row(std::min(static_cast<int>(list.get_item_count()) - 1, list.get_selected_row()));
	}

	find_widget<button>(this, "remove", false).set_active(!list_empty);

	find_widget<label>(this, "no_friends_notice", false).set_visible(
		list_empty ? widget::visibility::visible : widget::visibility::invisible);
}

void preferences_dialog::add_friend_list_entry(const bool is_friend, text_box& textbox)
{
	std::string username = textbox.text();
	if(username.empty()) {
		gui2::show_transient_message("", _("No username specified"));
		return;
	}

	std::string reason;

	std::size_t pos = username.find_first_of(' ');
	if(pos != std::string::npos) {
		reason = username.substr(pos + 1);
		username = username.substr(0, pos);
	}

	auto [entry, added_new] = add_acquaintance(username, (is_friend ? "friend": "ignore"), reason);

	if(!entry) {
		gui2::show_transient_message(_("Error"), _("Invalid username"));
		return;
	}

	textbox.clear();

	listbox& list = find_widget<listbox>(this, "friends_list", false);

	//
	// If this is a new entry, just add a new row. If it's not, we find the relevant
	// row, remove it, and add a new row with the updated data. Should probably come
	// up with a more elegant way to do this... the only reason I'm using the remove
	// -and-replace method is to prevent any issues with the widgets' layout sizes.
	//
	if(added_new) {
		list.add_row(get_friends_list_row_data(*entry));
	} else {
		for(unsigned i = 0; i < list.get_item_count(); ++i) {
			grid* row_grid = list.get_row_grid(i);

			if(find_widget<label>(row_grid, "friend_name", false).get_label() == entry->get_nick()) {
				list.remove_row(i);
				list.add_row(get_friends_list_row_data(*entry), i);

				break;
			}
		}
	}

	update_friends_list_controls(list);
}

void preferences_dialog::remove_friend_list_entry(listbox& friends_list, text_box& textbox)
{
	const int selected_row = std::max(0, friends_list.get_selected_row());

	std::map<std::string, acquaintance>::const_iterator who = get_acquaintances().begin();
	std::advance(who, selected_row);

	const std::string to_remove = !textbox.text().empty() ? textbox.text() : who->second.get_nick();

	if(to_remove.empty()) {
		gui2::show_transient_message("", _("No username specified"));
		return;
	}

	if(!remove_acquaintance(to_remove)) {
		gui2::show_transient_error_message(_("Not on friends or ignore lists"));
		return;
	}

	textbox.clear();

	listbox& list = find_widget<listbox>(this, "friends_list", false);
	list.remove_row(selected_row);

	update_friends_list_controls(list);
}

template<bool(*toggle_getter)(), bool(*toggle_setter)(bool), int(*vol_getter)(), void(*vol_setter)(int)>
void preferences_dialog::initialize_sound_option_group(const std::string& id_suffix)
{
	const std::string toggle_widget_id = "sound_toggle_" + id_suffix;
	const std::string volume_widget_id = "sound_volume_" + id_suffix;

	// Set up the toggle. We utilize field_bool's callback-on-changed mechanism instead
	// of manually registering the callback. Since we want the effects to apply immediately,
	// the callback the setter callback is duplicated in the on-change callback. The field
	// class could possibly use some reworking to make this less redundant, but for now it
	// works well enough.
	register_bool(toggle_widget_id, true, toggle_getter, std::bind(toggle_setter, std::placeholders::_1),
		std::bind(sound_toggle_on_change<toggle_setter>, std::ref(*this), volume_widget_id, std::placeholders::_1), true);

	// Set up the volume slider. integer_field doesn't have a callback-on-changed mechanism.
	// To add one would either mean adding it to the base field class or make it a proper
	// class of is own.
	register_integer(volume_widget_id, true, vol_getter, vol_setter);

	// Callback to actually immediately apply the volume effect.
	connect_signal_notify_modified(find_widget<slider>(this, volume_widget_id, false),
		std::bind(volume_setter_on_change<vol_setter>, std::placeholders::_1));
}

void preferences_dialog::apply_pixel_scale()
{
	// Update pixel scale preference.
	slider& ps_slider = find_widget<slider>(this, "pixel_scale_slider", false);
	set_pixel_scale(ps_slider.get_value());

	// Update auto pixel scale preference.
	toggle_button& auto_ps_toggle =
		find_widget<toggle_button>(this, "auto_pixel_scale", false);
	set_auto_pixel_scale(auto_ps_toggle.get_value_bool());

	// Update draw buffers, taking these into account.
	video::update_buffers();

	// Update game display, if active
	if(::display* disp = display::get_singleton()) {
		disp->queue_rerender();
	}

	// Raise a window resize event so we can react to the change
	events::raise_resize_event();
}


/**
 * Sets up states and callbacks for each of the widgets
 */
void preferences_dialog::initialize_callbacks()
{
	//
	// GENERAL PANEL
	//

	/* SCROLL SPEED */
	register_integer("scroll_speed", true,
		scroll_speed, set_scroll_speed);

	/* ACCELERATED SPEED */
	register_bool("turbo_toggle", true, turbo, set_turbo);

	const auto accl_load = [this]()->int {
		return std::distance(accl_speeds_.begin(), std::find(accl_speeds_.begin(), accl_speeds_.end(), turbo_speed()));
	};

	const auto accl_save = [this](int i) {
		set_turbo_speed(accl_speeds_[i]);
	};

	register_integer("turbo_slider", true,
		accl_load, accl_save);

	// Set the value label transform function.
	find_widget<slider>(this, "turbo_slider", false).set_value_labels(
		[this](int pos, int /*max*/)->t_string { return lexical_cast<std::string>(accl_speeds_[pos]); }
	);

	/* SKIP AI MOVES */
	register_bool("skip_ai_moves", true,
		skip_ai_moves, set_skip_ai_moves);

	/* DISABLE AUTO MOVES */
	register_bool("disable_auto_moves", true,
		disable_auto_moves, set_disable_auto_moves);

	/* TURN DIALOG */
	register_bool("show_turn_dialog", true,
		turn_dialog, set_turn_dialog);

	/* ENABLE PLANNING MODE */
	register_bool("whiteboard_on_start", true,
		enable_whiteboard_mode_on_start, set_enable_whiteboard_mode_on_start);

	/* HIDE ALLY PLANS */
	register_bool("whiteboard_hide_allies", true,
		hide_whiteboard, set_hide_whiteboard);

	/* INTERRUPT ON SIGHTING */
	register_bool("interrupt_move_when_ally_sighted", true,
		interrupt_when_ally_sighted, set_interrupt_when_ally_sighted);

	/* SAVE REPLAYS */
	register_bool("save_replays", true,
		save_replays, set_save_replays);

	/* DELETE AUTOSAVES */
	register_bool("delete_saves", true,
		delete_saves, set_delete_saves);

	/* MAX AUTO SAVES */
	register_integer("max_saves_slider", true,
		autosavemax, set_autosavemax);

	/* CACHE MANAGE */
	connect_signal_mouse_left_click(find_widget<button>(this, "cachemg", false),
			std::bind(&gui2::dialogs::game_cache_options::display<>));

	//
	// DISPLAY PANEL
	//

	/* FULLSCREEN TOGGLE */
	toggle_button& toggle_fullscreen =
			find_widget<toggle_button>(this, "fullscreen", false);

	toggle_fullscreen.set_value(fullscreen());

	// We bind a special callback function, so setup_single_toggle() is not used
	connect_signal_mouse_left_click(toggle_fullscreen, std::bind(
			&preferences_dialog::fullscreen_toggle_callback, this));

	/* SET RESOLUTION */
	menu_button& res_list = find_widget<menu_button>(this, "resolution_set", false);

	res_list.set_use_markup(true);
	res_list.set_active(!fullscreen());

	set_resolution_list(res_list);

	connect_signal_notify_modified(res_list,
		std::bind(&preferences_dialog::handle_res_select, this));

	/* PIXEL SCALE */
	register_integer("pixel_scale_slider", true,
		pixel_scale, set_pixel_scale);

	slider& ps_slider =
		find_widget<slider>(this, "pixel_scale_slider", false);
	connect_signal_mouse_left_release(ps_slider,
		std::bind(&preferences_dialog::apply_pixel_scale, this));

	/* AUTOMATIC PIXEL SCALE */
	register_bool("auto_pixel_scale", true,
		auto_pixel_scale, set_auto_pixel_scale,
		[&](widget& w) { disable_widget_on_toggle_inverted<slider>(*this, w, "pixel_scale_slider"); }, true);

	toggle_button& auto_ps_toggle =
		find_widget<toggle_button>(this, "auto_pixel_scale", false);
	connect_signal_mouse_left_click(auto_ps_toggle,
		std::bind(&preferences_dialog::apply_pixel_scale, this));

	/* SHOW FLOATING LABELS */
	register_bool("show_floating_labels", true,
		show_floating_labels, set_show_floating_labels);

	/* SHOW TEAM COLORS */
	register_bool("show_ellipses", true,
		show_side_colors, set_show_side_colors);

	/* SHOW GRID */
	register_bool("show_grid", true,
		preferences::grid, set_grid);

	/* ANIMATE MAP */
	register_bool("animate_terrains", true, animate_map, set_animate_map,
		[&](widget& w) { disable_widget_on_toggle<toggle_button>(*this, w, "animate_water"); }, true);

	/* ANIMATE WATER */
	register_bool("animate_water", true,
		animate_water, set_animate_water);

	/* SHOW UNIT STANDING ANIMS */
	register_bool("animate_units_standing", true,
		show_standing_animations, set_show_standing_animations);

	/* SHOW UNIT IDLE ANIMS */
	register_bool("animate_units_idle", true, idle_anim, set_idle_anim,
		[&](widget& w) { disable_widget_on_toggle<slider>(*this, w, "idle_anim_frequency"); });

	register_integer("idle_anim_frequency", true,
		idle_anim_rate, set_idle_anim_rate);

	/* FONT SCALING */
	//register_integer("scaling_slider", true,
	//	font_scaling, set_font_scaling);

	/* FPS LIMITER */
	register_bool("fps_limiter", true,
		[]() { return draw_delay() != 0; }, [](bool v) { set_draw_delay(v ? -1 : 0); });

	/* VSYNC */
	register_bool("vsync", true, vsync, set_vsync);

	/* SELECT THEME */
	connect_signal_mouse_left_click(
			find_widget<button>(this, "choose_theme", false),
			std::bind(&show_theme_dialog));

	//
	// SOUND PANEL
	//

	/* SOUND FX */
	initialize_sound_option_group<sound_on, set_sound, sound_volume, set_sound_volume>("sfx");

	/* MUSIC */
	initialize_sound_option_group<music_on, set_music, music_volume, set_music_volume>("music");

	register_bool("sound_toggle_stop_music_in_background", true,
		stop_music_in_background, set_stop_music_in_background);

	/* TURN BELL */
	initialize_sound_option_group<turn_bell, set_turn_bell, bell_volume, set_bell_volume>("bell");

	/* UI FX */
	initialize_sound_option_group<UI_sound_on, set_UI_sound, UI_volume, set_UI_volume>("uisfx");

	//
	// MULTIPLAYER PANEL
	//

	/* CHAT LINES */
	register_integer("chat_lines", true,
		chat_lines, set_chat_lines);

	/* CHAT TIMESTAMPPING */
	register_bool("chat_timestamps", true,
		chat_timestamping, set_chat_timestamping);

	/* SAVE PASSWORD */
	register_bool("remember_password", true,
		remember_password, set_remember_password);

	/* WHISPERS FROM FRIENDS ONLY */
	register_bool("lobby_whisper_friends_only", true,
		whisper_friends_only, set_whisper_friends_only);

	/* LOBBY JOIN NOTIFICATIONS */
	lobby_joins_group.add_member(find_widget<toggle_button>(this, "lobby_joins_none", false, true), lobby_joins::show_none);
	lobby_joins_group.add_member(find_widget<toggle_button>(this, "lobby_joins_friends", false, true), lobby_joins::show_friends);
	lobby_joins_group.add_member(find_widget<toggle_button>(this, "lobby_joins_all", false, true), lobby_joins::show_all);

	lobby_joins_group.set_member_states(get_lobby_joins());

	lobby_joins_group.set_callback_on_value_change([&](widget&, const lobby_joins val) {
		_set_lobby_joins(val);
	});

	/* FRIENDS LIST */
	listbox& friends_list = find_widget<listbox>(this, "friends_list", false);

	friends_list.clear();

	for(const auto& entry : get_acquaintances()) {
		friends_list.add_row(get_friends_list_row_data(entry.second));
	}

	update_friends_list_controls(friends_list);

	text_box& textbox = find_widget<text_box>(this, "friend_name_box", false);

	connect_signal_mouse_left_click(
		find_widget<button>(this, "add_friend", false), std::bind(
			&preferences_dialog::add_friend_list_entry,
			this, true,
			std::ref(textbox)));

	connect_signal_mouse_left_click(
		find_widget<button>(this, "add_ignored", false), std::bind(
			&preferences_dialog::add_friend_list_entry,
			this, false,
			std::ref(textbox)));

	connect_signal_mouse_left_click(
		find_widget<button>(this, "remove", false), std::bind(
			&preferences_dialog::remove_friend_list_entry,
			this,
			std::ref(friends_list),
			std::ref(textbox)));

	connect_signal_notify_modified(friends_list, std::bind(
			&preferences_dialog::on_friends_list_select,
			this,
			std::ref(friends_list),
			std::ref(textbox)));

	/* ALERTS */
	connect_signal_mouse_left_click(
			find_widget<button>(this, "mp_alerts", false),
			std::bind(&gui2::dialogs::mp_alerts_options::display<>));

	/* SET WESNOTHD PATH */
	connect_signal_mouse_left_click(
			find_widget<button>(this, "mp_wesnothd", false), std::bind(
			&show_wesnothd_server_search));


	//
	// ADVANCED PANEL
	//

	listbox& advanced = find_widget<listbox>(this, "advanced_prefs", false);

	widget_data row_data;

	for(const auto& option : adv_preferences_) {
		const std::string& pref_name = option.field;

		row_data["pref_name"]["label"] = option.name;
		grid* main_grid = &advanced.add_row(row_data);

		grid& details_grid = find_widget<grid>(main_grid, "prefs_setter_grid", false);
		details_grid.set_visible(widget::visibility::invisible);

		// The toggle widget for toggle-type options (hidden for other types)
		toggle_button& toggle_box = find_widget<toggle_button>(main_grid, "value_toggle", false);
		toggle_box.set_visible(widget::visibility::hidden);

		if(!option.description.empty()) {
			find_widget<styled_widget>(main_grid, "description", false).set_label(option.description);
		}

		switch(option.type) {
			case avp::avd_type::TOGGLE: {
				//main_grid->remove_child("setter");

				toggle_box.set_visible(widget::visibility::visible);
				toggle_box.set_value(get(pref_name, option.cfg["default"].to_bool()));

				// We need to bind a lambda here since preferences::set is overloaded.
				// A lambda alone would be more verbose because it'd need to specify all the parameters.
				connect_signal_mouse_left_click(toggle_box, std::bind(
					[&, pref_name]() { set(pref_name, toggle_box.get_value_bool()); }
				));

				gui2::bind_status_label<toggle_button>(
					main_grid, "value_toggle", default_status_value_getter<toggle_button>, "value");

				break;
			}

			case avp::avd_type::SLIDER: {
				auto setter_widget = build_single_widget_instance<slider>(config {"definition", "minimal"});
				setter_widget->set_id("setter");
				// Maximum must be set first or this will assert
				setter_widget->set_value_range(option.cfg["min"].to_int(), option.cfg["max"].to_int());
				setter_widget->set_step_size(option.cfg["step"].to_int(1));

				details_grid.swap_child("setter", std::move(setter_widget), true);

				slider& slide = find_widget<slider>(&details_grid, "setter", false);

				slide.set_value(lexical_cast_default<int>(get(pref_name), option.cfg["default"].to_int()));

				// We need to bind a lambda here since preferences::set is overloaded.
				// A lambda alone would be more verbose because it'd need to specify all the parameters.
				connect_signal_notify_modified(slide, std::bind(
					[&, pref_name]() { set(pref_name, slide.get_value()); }
				));

				gui2::bind_status_label<slider>(main_grid, "setter", default_status_value_getter<slider>, "value");

				break;
			}

			case avp::avd_type::COMBO: {
				std::vector<config> menu_data;
				std::vector<std::string> option_ids;

				for(const config& choice : option.cfg.child_range("option")) {
					config menu_item;
					menu_item["label"] = choice["name"];
					if(choice.has_attribute("description")) {
						menu_item["details"] = std::string("<span color='#777'>") + choice["description"] + "</span>";
					}
					menu_data.push_back(menu_item);
					option_ids.push_back(choice["id"]);
				}

				// Attempt to find an initial selection
				int selected = std::distance(option_ids.begin(), std::find(option_ids.begin(), option_ids.end(),
					get(pref_name, option.cfg["default"].str())
				));

				// If the saved option value was invalid, reset selection to 0.
				if(selected < 0 || selected >= static_cast<int>(option_ids.size())) {
					selected = 0;
				}

				auto setter_widget = build_single_widget_instance<menu_button>();
				setter_widget->set_id("setter");

				details_grid.swap_child("setter", std::move(setter_widget), true);

				menu_button& menu = find_widget<menu_button>(&details_grid, "setter", false);

				menu.set_use_markup(true);
				menu.set_values(menu_data, selected);

				// We need to bind a lambda here since preferences::set is overloaded.
				// A lambda alone would be more verbose because it'd need to specify all the parameters.
				connect_signal_notify_modified(menu,
					std::bind([=](widget& w) { set(pref_name, option_ids[dynamic_cast<menu_button&>(w).get_value()]); }, std::placeholders::_1));

				gui2::bind_status_label<menu_button>(main_grid, "setter", default_status_value_getter<menu_button>, "value");

				break;
			}

			case avp::avd_type::SPECIAL: {
				//main_grid->remove_child("setter");

				auto value_widget = build_single_widget_instance<image>();
				value_widget->set_label("icons/arrows/arrows_blank_right_25.png~CROP(3,3,18,18)");

				main_grid->swap_child("value", std::move(value_widget), true);

				break;
			}
		}
	}

	connect_signal_notify_modified(advanced, std::bind(
		&preferences_dialog::on_advanced_prefs_list_select,
		this,
		std::ref(advanced)));

	on_advanced_prefs_list_select(advanced);

	//
	// HOTKEYS PANEL
	//

	multimenu_button& hotkey_menu = find_widget<multimenu_button>(this, "hotkey_category_menu", false);
	connect_signal_notify_modified(hotkey_menu,
		std::bind(&preferences_dialog::hotkey_filter_callback, this));

	listbox& hotkey_list = setup_hotkey_list();

	text_box& filter = find_widget<text_box>(this, "filter", false);
	filter.set_text_changed_callback(std::bind(&preferences_dialog::hotkey_filter_callback, this));

	// Action column
	hotkey_list.register_translatable_sorting_option(0, [this](const int i) { return visible_hotkeys_[i]->description.str(); });

	// Hotkey column
	hotkey_list.register_sorting_option(1, [this](const int i) { return hotkey::get_names(visible_hotkeys_[i]->id); });

	// Scope columns
	hotkey_list.register_sorting_option(2, [this](const int i) { return !visible_hotkeys_[i]->scope[hotkey::SCOPE_GAME]; });
	hotkey_list.register_sorting_option(3, [this](const int i) { return !visible_hotkeys_[i]->scope[hotkey::SCOPE_EDITOR]; });
	hotkey_list.register_sorting_option(4, [this](const int i) { return !visible_hotkeys_[i]->scope[hotkey::SCOPE_MAIN_MENU]; });

	hotkey_list.set_active_sorting_option({0, sort_order::type::ascending}, true);

	connect_signal_mouse_left_click(
		find_widget<button>(this, "btn_add_hotkey", false), std::bind(
			&preferences_dialog::add_hotkey_callback,
			this,
			std::ref(hotkey_list)));

	connect_signal_mouse_left_click(
		find_widget<button>(this, "btn_clear_hotkey", false), std::bind(
			&preferences_dialog::remove_hotkey_callback,
			this,
			std::ref(hotkey_list)));

	connect_signal_mouse_left_click(
		find_widget<button>(this, "btn_reset_hotkeys", false), std::bind(
			&preferences_dialog::default_hotkey_callback,
			this));
}

listbox& preferences_dialog::setup_hotkey_list()
{
	widget_data row_data;

	t_string& row_icon   = row_data["img_icon"]["label"];
	t_string& row_action = row_data["lbl_desc"]["label"];
	t_string& row_hotkey = row_data["lbl_hotkey"]["label"];

	t_string& row_is_g   = row_data["lbl_is_game"]["label"];
	t_string& row_is_e   = row_data["lbl_is_editor"]["label"];
	t_string& row_is_m   = row_data["lbl_is_mainmenu"]["label"];

	listbox& hotkey_list = find_widget<listbox>(this, "list_hotkeys", false);

	hotkey_list.clear();
	visible_hotkeys_.clear();
	visible_categories_.clear();

	//
	// Main hotkeys list
	//

	// These translated initials should match those used in data/gui/window/preferences/02_hotkeys.cfg
	const std::string gh = "<span color='#0f0'>" + _("game_hotkeys^G") + "</span>";
	const std::string eh = "<span color='#0f0'>" + _("editor_hotkeys^E") + "</span>";
	const std::string mh = "<span color='#0f0'>" + _("mainmenu_hotkeys^M") + "</span>";

	for(const auto& [id, hotkey_item] : hotkey::get_hotkey_commands()) {
		if(hotkey_item.hidden) {
			continue;
		}

		visible_hotkeys_.push_back(&hotkey_item);
		visible_categories_.insert(hotkey_item.category);

		if(filesystem::file_exists(game_config::path + "/images/icons/action/" + hotkey_item.id + "_25.png")) {
			row_icon = "icons/action/" + hotkey_item.id + "_25.png~CROP(3,3,18,18)";
		} else {
			row_icon = "";
		}

		row_action = hotkey_item.description;
		row_hotkey = hotkey::get_names(hotkey_item.id);

		row_is_g = hotkey_item.scope[hotkey::SCOPE_GAME]      ? gh : "";
		row_is_e = hotkey_item.scope[hotkey::SCOPE_EDITOR]    ? eh : "";
		row_is_m = hotkey_item.scope[hotkey::SCOPE_MAIN_MENU] ? mh : "";

		hotkey_list.add_row(row_data);
	}

	//
	// Filter options
	//

	std::vector<config> filter_ops;
	for(const hotkey::HOTKEY_CATEGORY& cat : visible_categories_) {
		filter_ops.emplace_back("label", hotkey::get_translatable_category_name(cat), "checkbox", false);
	}

	find_widget<multimenu_button>(this, "hotkey_category_menu", false).set_values(filter_ops);

	return hotkey_list;
}

void preferences_dialog::add_hotkey_callback(listbox& hotkeys)
{
	int row_number = hotkeys.get_selected_row();
	if(row_number < 0) {
		gui2::show_transient_message("", _("No hotkey selected"));
		return;
	}

	const hotkey::hotkey_command& hotkey_item = *visible_hotkeys_[row_number];

	gui2::dialogs::hotkey_bind bind_dlg(hotkey_item.id);
	bind_dlg.show();

	hotkey::hotkey_ptr newhk = bind_dlg.get_new_binding();
	hotkey::hotkey_ptr oldhk;

	// only if not cancelled.
	if(newhk.get() == nullptr) {
		return;
	}

	for(const hotkey::hotkey_ptr& hk : hotkey::get_hotkeys()) {
		if(!hk->is_disabled() && newhk->bindings_equal(hk)) {
			oldhk = hk;
		}
	}

	if(oldhk && oldhk->get_command() == hotkey_item.id) {
		return;
	}

	if(oldhk && oldhk->get_command() != "null") {
		const std::string text = VGETTEXT("“<b>$hotkey_sequence|</b>” is in use by “<b>$old_hotkey_action|</b>”.\nDo you wish to reassign it to “<b>$new_hotkey_action|</b>”?", {
			{"hotkey_sequence",   oldhk->get_name()},
			{"old_hotkey_action", hotkey::get_hotkey_command(oldhk->get_command()).description},
			{"new_hotkey_action", hotkey::get_hotkey_command(newhk->get_command()).description}
		});

		const int res = gui2::show_message(_("Reassign Hotkey"), text, gui2::dialogs::message::yes_no_buttons, true);
		if(res != gui2::retval::OK) {
			return;
		}
	}

	hotkey::add_hotkey(newhk);

	// We need to recalculate all hotkey names in because we might have removed a hotkey from another command.
	for(std::size_t i = 0; i < hotkeys.get_item_count(); ++i) {
		const hotkey::hotkey_command& hotkey_item_row = *visible_hotkeys_[i];
		find_widget<label>(hotkeys.get_row_grid(i), "lbl_hotkey", false).set_label(hotkey::get_names(hotkey_item_row.id));
	}
}

void preferences_dialog::default_hotkey_callback()
{
	gui2::show_transient_message(_("Hotkeys Reset"), _("All hotkeys have been reset to their default values."));

	clear_hotkeys();

	// Set up the list again and reselect the default sorting option.
	listbox& hotkey_list = setup_hotkey_list();
	hotkey_list.set_active_sorting_option({0, sort_order::type::ascending}, true);
}

void preferences_dialog::remove_hotkey_callback(listbox& hotkeys)
{
	int row_number = hotkeys.get_selected_row();
	if(row_number < 0) {
		gui2::show_transient_message("", _("No hotkey selected"));
		return;
	}

	const hotkey::hotkey_command& hotkey_item = *visible_hotkeys_[row_number];
	hotkey::clear_hotkeys(hotkey_item.id);
	find_widget<label>(hotkeys.get_row_grid(row_number), "lbl_hotkey", false).set_label(hotkey::get_names(hotkey_item.id));
}

void preferences_dialog::hotkey_filter_callback()
{
	const multimenu_button& hotkey_menu = find_widget<const multimenu_button>(this, "hotkey_category_menu", false);
	const text_box& name_filter = find_widget<const text_box>(this, "filter", false);

	boost::dynamic_bitset<> toggle_states = hotkey_menu.get_toggle_states();
	boost::dynamic_bitset<> res(visible_hotkeys_.size());

	std::string text = name_filter.get_value();

	// Nothing selected. It means that *all* categories are shown.
	if(toggle_states.none()) {
		toggle_states = ~toggle_states;
	}

	for(std::size_t h = 0; h < visible_hotkeys_.size(); ++h) {
		// Default to true if there is no filter text
		bool found = true;

		if(!text.empty()) {
			const std::string description = visible_hotkeys_[h]->description.str();

			for(const auto& word : utils::split(text, ' ')) {
				found = translation::ci_search(description, word);

				// No match, we're excluding this hotkey
				if(!found) {
					break;
				}
			}
		}

		unsigned cat_index = 0;

		// Filter categories
		for(const hotkey::HOTKEY_CATEGORY& cat : visible_categories_) {
			if(visible_hotkeys_[h]->category == cat) {
				break;
			} else {
				++cat_index;
			}
		}

		if(cat_index < toggle_states.size() && found) {
			res[h] = toggle_states[cat_index];
		} else {
			res[h] = false;
		}
	}

	find_widget<listbox>(this, "list_hotkeys", false).set_row_shown(res);
}

void preferences_dialog::on_advanced_prefs_list_select(listbox& list)
{
	const int selected_row = list.get_selected_row();
	const auto& pref = adv_preferences_[selected_row];

	if(pref.type == avp::avd_type::SPECIAL) {
		if(pref.field == "logging") {
			gui2::dialogs::log_settings::display();
		} else if(pref.field == "orb_color") {
			gui2::dialogs::select_orb_colors::display();
		} else {
			WRN_GUI_L << "Invalid or unimplemented custom advanced prefs option: " << pref.field;
		}

		// Add more options here as needed
	}

	const bool has_description = !pref.description.empty();

	if(has_description || (pref.type != avp::avd_type::SPECIAL && pref.type != avp::avd_type::TOGGLE)) {
		find_widget<widget>(list.get_row_grid(selected_row), "prefs_setter_grid", false)
			.set_visible(widget::visibility::visible);
	}

	if(last_selected_item_ != selected_row) {
		find_widget<widget>(list.get_row_grid(last_selected_item_), "prefs_setter_grid", false)
			.set_visible(widget::visibility::invisible);

		last_selected_item_ = selected_row;
	}
}

void preferences_dialog::initialize_tabs(listbox& selector)
{
	//
	// MULTIPLAYER TABS
	//
	connect_signal_notify_modified(selector,
		std::bind(&preferences_dialog::on_tab_select, this));
}

void preferences_dialog::pre_show(window& window)
{
	set_always_save_fields(true);

	connect_signal_mouse_left_click(find_widget<button>(&window, "about", false), std::bind(&game_version::display<>));

	//
	// Status labels
	// These need to be set here in pre_show, once the fields are initialized. For some reason, this
	// is not the case for those in Advanced
	//

	gui2::bind_status_label<slider>(&window, "max_saves_slider");
	gui2::bind_status_label<slider>(&window, "turbo_slider");
	gui2::bind_status_label<slider>(&window, "pixel_scale_slider");

	//gui2::bind_status_label<slider>(&window, "scaling_slider",   [](slider& s)->std::string {
	//	return s.get_value_label() + "%";
	//});

	listbox& selector = find_widget<listbox>(&window, "selector", false);
	stacked_widget& pager = find_widget<stacked_widget>(&window, "pager", false);

	pager.set_find_in_all_layers(true);

	connect_signal_notify_modified(selector,
		std::bind(&preferences_dialog::on_page_select, this));

	window.keyboard_capture(&selector);

	VALIDATE(selector.get_item_count() == pager.get_layer_count(),
		"The preferences pager and its selector listbox do not have the same number of items.");

	const int main_index = index_in_pager_range(initial_index_.first, pager);

	// Loops through each pager layer and checks if it has both a tab bar
	// and stack. If so, it initializes the options for the former and
	// selects the specified layer of the latter.
	for(unsigned int i = 0; i < pager.get_layer_count(); ++i) {
		listbox* tab_selector = find_widget<listbox>(
			pager.get_layer_grid(i), "tab_selector", false, false);

		stacked_widget* tab_pager = find_widget<stacked_widget>(
			pager.get_layer_grid(i), "tab_pager", false, false);

		if(tab_pager && tab_selector) {
			const int ii = static_cast<int>(i);
			const int tab_index = index_in_pager_range(initial_index_.second, *tab_pager);
			const int to_select = (ii == main_index ? tab_index : 0);

			// Initialize tabs for this page
			initialize_tabs(*tab_selector);

			tab_selector->select_row(to_select);
			tab_pager->select_layer(to_select);
		}
	}

	// Finally, select the initial main page
	selector.select_row(main_index);
	pager.select_layer(main_index);
}

void preferences_dialog::set_visible_page(unsigned int page, const std::string& pager_id)
{
	find_widget<stacked_widget>(this, pager_id, false).select_layer(page);
}

// Special fullsceen callback
void preferences_dialog::fullscreen_toggle_callback()
{
	const bool ison = find_widget<toggle_button>(this, "fullscreen", false).get_value_bool();
	video::set_fullscreen(ison);

	menu_button& res_list = find_widget<menu_button>(this, "resolution_set", false);

	set_resolution_list(res_list);
	res_list.set_active(!ison);
}

void preferences_dialog::handle_res_select()
{
	menu_button& res_list = find_widget<menu_button>(this, "resolution_set", false);

	if(video::set_resolution(resolutions_[res_list.get_value()])) {
		set_resolution_list(res_list);
	}
}

void preferences_dialog::on_page_select()
{
	const int selected_row =
		std::max(0, find_widget<listbox>(this, "selector", false).get_selected_row());
	set_visible_page(static_cast<unsigned int>(selected_row), "pager");
}

void preferences_dialog::on_tab_select()
{
	const int selected_row =
		std::max(0, find_widget<listbox>(this, "tab_selector", false).get_selected_row());
	set_visible_page(static_cast<unsigned int>(selected_row), "tab_pager");
}

void preferences_dialog::post_show(window& /*window*/)
{
	save_hotkeys();

	// Save new prefs to disk. This also happens on app close, but doing
	// it here too ensures nothing is lost in case of, say, a crash.
	write_preferences();
}

} // namespace dialogs
