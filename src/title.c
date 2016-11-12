#pragma region Copyright (c) 2014-2016 OpenRCT2 Developers
/*****************************************************************************
 * OpenRCT2, an open source clone of Roller Coaster Tycoon 2.
 *
 * OpenRCT2 is the work of many authors, a full list can be found in contributors.md
 * For more information, visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * A full copy of the GNU General Public License can be found in licence.txt
 *****************************************************************************/
#pragma endregion

#include <time.h>
#include "audio/audio.h"
#include "config.h"
#include "drawing/drawing.h"
#include "editor.h"
#include "game.h"
#include "input.h"
#include "localisation/date.h"
#include "localisation/localisation.h"
#include "interface/screenshot.h"
#include "interface/title_sequences.h"
#include "interface/viewport.h"
#include "intro.h"
#include "management/news_item.h"
#include "management/research.h"
#include "network/network.h"
#include "OpenRCT2.h"
#include "peep/staff.h"
#include "ride/ride.h"
#include "scenario.h"
#include "ScenarioRepository.h"
#include "ScenarioSources.h"
#include "title.h"
#include "title/TitleSequence.h"
#include "title/TitleSequenceManager.h"
#include "util/util.h"
#include "world/climate.h"
#include "world/map.h"
#include "world/park.h"
#include "world/scenery.h"
#include "world/sprite.h"
#include "windows/error.h"

static const int gRandomShowcase = 0;

bool gTitleHideVersionInfo = false;

sint32 gTitleScriptCommand = -1;
uint8 gTitleScriptSave = 0xFF;
sint32 gTitleScriptSkipTo = -1;
sint32 gTitleScriptSkipLoad = -1;
rct_xy16 _titleScriptCurrentCentralPosition = { -1, -1 };

#pragma region Showcase script

#define WAIT(t)				TITLE_SCRIPT_WAIT, t
#define LOADMM()			TITLE_SCRIPT_LOADMM
#define LOCATION(x, y)		TITLE_SCRIPT_LOCATION, x, y
#define ROTATE(n)			TITLE_SCRIPT_ROTATE, n
#define ZOOM(d)				TITLE_SCRIPT_ZOOM, d
#define RESTART()			TITLE_SCRIPT_RESTART
#define LOAD(i)				TITLE_SCRIPT_LOAD, i
#define LOADRCT1(i)			TITLE_SCRIPT_LOADRCT1, i

static const uint8 _magicMountainScript[] = {
	LOADMM(),
	LOCATION(210, 112), WAIT(13),
	ROTATE(1), LOCATION(210, 112), WAIT(14),
	ROTATE(3), LOCATION(167, 180), WAIT(12),
	ROTATE(1), LOCATION(155, 189), WAIT(12),
	LOCATION(106, 39), WAIT(12),
	LOCATION(182, 50), WAIT(12),
	ROTATE(3), LOCATION(209, 47), WAIT(12),
	ROTATE(1), LOCATION(159, 93), WAIT(12),
	RESTART(),
};

static uint8* _loadedScript;
static const uint8* _currentScript;
static uint8 _lastOpcode;
static int _scriptNoLoadsSinceRestart;
static int _scriptWaitCounter;
static int _scriptCurrentPreset;

static int _loadedTitleSequenceId = -1;
static TitleSequence * _loadedTitleSequence = NULL;

static void title_init_showcase();
static void title_update_showcase();

static uint8 *generate_random_script();

#pragma endregion

static bool title_load_park(SDL_RWops * rw, bool isScenario);

/**
 *
 *  rct2: 0x0068E8DA
 */
void title_load()
{
	log_verbose("loading title");

	if (gGamePaused & GAME_PAUSED_NORMAL)
		pause_toggle();

	gScreenFlags = SCREEN_FLAGS_TITLE_DEMO;

#ifndef DISABLE_NETWORK
	network_close();
#endif
	reset_park_entrances();
	user_string_clear_all();
	reset_sprite_list();
	ride_init_all();
	window_guest_list_init_vars_a();
	staff_reset_modes();
	map_init(150);
	park_init();
	date_reset();
	climate_reset(CLIMATE_COOL_AND_WET);
	scenery_set_default_placement_configuration();
	window_new_ride_init_vars();
	window_guest_list_init_vars_b();
	window_staff_list_init_vars();
	map_update_tile_pointers();
	reset_sprite_spatial_index();
	audio_stop_all_music_and_sounds();
	viewport_init_all();
	news_item_init_queue();
	window_main_open();
	title_create_windows();
	title_init_showcase();
	gfx_invalidate_screen();
	audio_start_title_music();
	gScreenAge = 0;

	if (gOpenRCT2ShowChangelog) {
		gOpenRCT2ShowChangelog = false;
		window_changelog_open();
	}

	log_verbose("loading title finished");
}

/**
 * Creates the windows shown on the title screen; New game, load game,
 * tutorial, toolbox and exit.
 *  rct2: 0x0066B5C0 (part of 0x0066B3E8)
 */
void title_create_windows()
{
	window_title_menu_open();
	window_title_exit_open();
	window_title_options_open();
	window_title_logo_open();
	window_resize_gui(gScreenWidth, gScreenHeight);
	gTitleHideVersionInfo = false;
}

/**
 *
 *  rct2: 0x00678680
 */
static void title_init_showcase()
{
	size_t seqId = title_sequence_manager_get_index_for_config_id(gConfigInterface.current_title_sequence_preset);
	if (seqId == SIZE_MAX) {
		seqId = title_sequence_manager_get_index_for_config_id("*OPENRCT2");
		if (seqId == SIZE_MAX) {
			seqId = 0;
		}
	}
	title_sequence_change_preset((int)seqId);
	title_refresh_sequence();
}

static bool title_load_park_from_file(const char * path)
{
	bool success = false;
	bool isScenario = _strcmpi(path_get_extension(path), ".sv6");
	SDL_RWops * rw = SDL_RWFromFile(path, "rb");
	if (rw != NULL) {
		success = title_load_park(rw, isScenario);
		SDL_RWclose(rw);
	}
	return success;
}

static bool title_load_park_from_zip(const char * path)
{
	return false;
}

static bool title_load_park(SDL_RWops * rw, bool isScenario)
{
	rct_window* w;
	int successfulLoad = 0;

	if (isScenario) {
		successfulLoad = scenario_load_rw(rw);
	} else {
		successfulLoad = game_load_sv6(rw);
	}
	if (!successfulLoad)
		return false;

	w = window_get_main();
	w->viewport_target_sprite = -1;
	w->saved_view_x = gSavedViewX;
	w->saved_view_y = gSavedViewY;

	{
		char zoomDifference = gSavedViewZoom - w->viewport->zoom;
		w->viewport->zoom = gSavedViewZoom;
		gCurrentRotation = gSavedViewRotation;
		if (zoomDifference != 0) {
			if (zoomDifference < 0) {
				zoomDifference = -zoomDifference;
				w->viewport->view_width >>= zoomDifference;
				w->viewport->view_height >>= zoomDifference;
			} else {
				w->viewport->view_width <<= zoomDifference;
				w->viewport->view_height <<= zoomDifference;
			}
		}
		w->saved_view_x -= w->viewport->view_width >> 1;
		w->saved_view_y -= w->viewport->view_height >> 1;
	}

	window_invalidate(w);
	reset_sprite_spatial_index();
	reset_all_sprite_quadrant_placements();
	window_new_ride_init_vars();
	if (!isScenario) {
		sub_684AC3();
	}
	scenery_set_default_placement_configuration();
	news_item_init_queue();
	load_palette();
	gfx_invalidate_screen();
	window_tile_inspector_clear_clipboard();
	gScreenAge = 0;
	gGameSpeed = 1;
	return true;
}

/**
 * Sets the map location to the given tile coordinates. Z is automatic.
 * @param x X position in map tiles.
 * @param y Y position in map tiles.
 */
static void title_set_location(int x, int y)
{
	int z = map_element_height(x, y);

	// Update viewport
	rct_window* w = window_get_main();
	if (w != NULL) {
		window_scroll_to_location(w, x, y, z);
		w->flags &= ~WF_SCROLLING_TO_LOCATION;
		viewport_update_position(w);
	}

	// Save known tile position in case of window resize
	_titleScriptCurrentCentralPosition.x = (sint16)x;
	_titleScriptCurrentCentralPosition.y = (sint16)y;
}

/**
 * Re-centres the map location to the last scripted tile position.
 */
void title_fix_location()
{
	if (gScreenFlags == SCREEN_FLAGS_TITLE_DEMO) {
		rct_xy16 position = _titleScriptCurrentCentralPosition;
		if (position.x != -1) {
			title_set_location(position.x, position.y);
		}
	}
}

static void title_skip_opcode()
{
	uint8 script_opcode;

	script_opcode = *_currentScript++;
	gTitleScriptCommand++;
	_lastOpcode = script_opcode;

	switch (script_opcode) {
	case TITLE_SCRIPT_WAIT:
		_currentScript++;
		break;
	case TITLE_SCRIPT_LOADMM:
		_currentScript++;
		break;
	case TITLE_SCRIPT_LOCATION:
		_currentScript++;
		_currentScript++;
		break;
	case TITLE_SCRIPT_ROTATE:
		_currentScript++;
		break;
	case TITLE_SCRIPT_RESTART:
		break;
	case TITLE_SCRIPT_LOAD:
		do {
			_currentScript++;
		} while (*(_currentScript - 1) != 0);
		break;
	}
}

static void title_do_next_script_opcode()
{
	int i;
	short x, y;
	uint8 script_opcode, script_operand;
	rct_window* w;
	gTitleScriptCommand++;
	script_opcode = *_currentScript++;
	if (gTitleScriptSkipTo != -1) {
		if (gTitleScriptSkipTo == gTitleScriptCommand) {
			gTitleScriptSkipTo = -1;
			gTitleScriptSkipLoad = -1;
		}
		else if (gTitleScriptSkipLoad == gTitleScriptCommand) {
			gTitleScriptSkipLoad = -1;
		}
		else if (gTitleScriptSkipLoad != -1) {
			gTitleScriptCommand--;
			_currentScript--;
			title_skip_opcode();
			return;
		}
	}

	_lastOpcode = script_opcode;

	switch (script_opcode) {
	case TITLE_SCRIPT_END:
		_scriptWaitCounter = 1;
		break;
	case TITLE_SCRIPT_WAIT:
		_scriptWaitCounter = (*_currentScript++) * 32;
		break;
	case TITLE_SCRIPT_LOADMM:
		if (!title_load_park_from_file(get_file_path(PATH_ID_SIXFLAGS_MAGICMOUNTAIN))) {
			log_fatal("OpenRCT2 can not currently cope when unable to load title screen scenario.");
			exit(-1);
		}
		gTitleScriptSave = 0xFF;
		break;
	case TITLE_SCRIPT_LOCATION:
		x = (*_currentScript++) * 32 + 16;
		y = (*_currentScript++) * 32 + 16;
		title_set_location(x, y);
		break;
	case TITLE_SCRIPT_ROTATE:
		script_operand = (*_currentScript++);
		w = window_get_main();
		if (w != NULL)
			for (i = 0; i < script_operand; i++)
				window_rotate_camera(w, 1);
		break;
	case TITLE_SCRIPT_ZOOM:
		script_operand = (*_currentScript++);
		w = window_get_main();
		if (w != NULL && w->viewport != NULL)
			window_zoom_set(w, script_operand);
		break;
	case TITLE_SCRIPT_SPEED:
		script_operand = (*_currentScript++);
		gGameSpeed = max(1, min(4, script_operand));
		break;
	case TITLE_SCRIPT_RESTART:
		_scriptNoLoadsSinceRestart = 1;
		gTitleScriptCommand = -1;
		gTitleScriptSave = 0xFF;
		_currentScript = _loadedScript;
		if (gRandomShowcase) {
			if (_currentScript != NULL)
				free((uint8*)_currentScript);
			_currentScript = generate_random_script();
		}
		break;
	case TITLE_SCRIPT_LOAD:
		{
			uint8 saveIndex = _loadedTitleSequence->Commands[gTitleScriptCommand].SaveIndex;
			TitleSequenceParkHandle * parkHandle = TitleSequenceGetParkHandle(_loadedTitleSequence, saveIndex);
			bool loadSuccess = title_load_park(parkHandle->RWOps, parkHandle->IsScenario);
			TitleSequenceCloseParkHandle(parkHandle);

			if (loadSuccess) {
				_scriptNoLoadsSinceRestart = 0;
				gTitleScriptSave = _loadedTitleSequence->Commands[gTitleScriptCommand].SaveIndex;
			} else {
				log_error("Failed to load: \"%s\" for the title sequence.", _loadedTitleSequence->Saves[saveIndex]);
				script_opcode = *_currentScript;
				while (script_opcode != TITLE_SCRIPT_LOADMM && script_opcode != TITLE_SCRIPT_LOAD && script_opcode != TITLE_SCRIPT_RESTART && script_opcode != TITLE_SCRIPT_END) {
					title_skip_opcode();
					script_opcode = *_currentScript;
				}

				if ((script_opcode == TITLE_SCRIPT_RESTART || script_opcode == TITLE_SCRIPT_END) && _scriptNoLoadsSinceRestart) {
					if (_currentScript != _magicMountainScript) {
						_scriptNoLoadsSinceRestart = 1;
						gTitleScriptCommand = -1;
						gTitleScriptSave = 0xFF;
						_currentScript = _magicMountainScript;
						gCurrentPreviewTitleSequence = 0;
						gTitleScriptSkipTo = -1;
						gTitleScriptSkipLoad = -1;
						_scriptCurrentPreset = 0;
						//window_invalidate_by_class(WC_TITLE_EDITOR);
					} else {
						log_fatal("OpenRCT2 can not currently cope when unable to load title screen scenario.");
						exit(-1);
					}
				}
			}
		}
		break;
	case TITLE_SCRIPT_LOADRCT1:
		script_operand = (*_currentScript++);

		source_desc sourceDesc;
		if (!scenario_get_source_desc_by_id(script_operand, &sourceDesc) || sourceDesc.index == -1) {
			log_fatal("Invalid scenario id.");
			exit(-1);
		}

		const utf8 *path = NULL;
		size_t numScenarios = scenario_repository_get_count();
		for (size_t i = 0; i < numScenarios; i++) {
			const scenario_index_entry * scenario = scenario_repository_get_by_index(i);
			if (scenario->source_index == sourceDesc.index) {
				path = scenario->path;
				break;
			}
		}

		if (path == NULL || !title_load_park_from_file(path)) {
			script_opcode = *_currentScript;
			while (script_opcode != TITLE_SCRIPT_LOADRCT1 && script_opcode != TITLE_SCRIPT_RESTART && script_opcode != TITLE_SCRIPT_END) {
				title_skip_opcode();
				script_opcode = *_currentScript;
			}
			if (script_opcode == TITLE_SCRIPT_RESTART) {
				title_sequence_change_preset(4);
				title_refresh_sequence();
				config_save_default();
				return;
			}
		}
		gTitleScriptSave = 0xFF;
		break;
	}
	window_invalidate_by_class(WC_TITLE_EDITOR);
}

/**
 *
 *  rct2: 0x00678761
 */
static void title_update_showcase()
{
	int i, numUpdates;
	// Loop used for scene skip functionality
	// Only loop here when the appropriate save hasn't been loaded yet since no game updates are required
	do {
		do {
			if (_scriptWaitCounter <= 0) {
				do {
					title_do_next_script_opcode();
				} while (_scriptWaitCounter == 0);
			}

			if (gTitleScriptSkipTo != -1 && gTitleScriptSkipLoad != -1)
				_scriptWaitCounter = 0;
			else if (_lastOpcode != TITLE_SCRIPT_END)
				_scriptWaitCounter--;
		} while (gTitleScriptSkipTo != -1 && gTitleScriptSkipLoad != -1);

		if (gTitleScriptSkipTo != -1 && gTitleScriptSkipLoad == -1) {
			if (gGameSpeed > 1) {
				numUpdates = 1 << (gGameSpeed - 1);
			} else {
				numUpdates = 1;
			}
			for (i = 0; i < numUpdates; i++) {
				game_logic_update();
			}
			update_palette_effects();
			// update_rain_animation();
		}
	} while (gTitleScriptSkipTo != -1 && gTitleScriptSkipLoad == -1);
}

void DrawOpenRCT2(rct_drawpixelinfo *dpi, int x, int y)
{
	utf8 buffer[256];

	// Write format codes
	utf8 *ch = buffer;
	ch = utf8_write_codepoint(ch, FORMAT_MEDIUMFONT);
	ch = utf8_write_codepoint(ch, FORMAT_OUTLINE);
	ch = utf8_write_codepoint(ch, FORMAT_WHITE);

	// Write name and version information
	openrct2_write_full_version_info(ch, sizeof(buffer) - (ch - buffer));
	gfx_draw_string(dpi, buffer, COLOUR_BLACK, x + 5, y + 5 - 13);

	// Write platform information
	snprintf(ch, 256 - (ch - buffer), "%s (%s)", OPENRCT2_PLATFORM, OPENRCT2_ARCHITECTURE);
	gfx_draw_string(dpi, buffer, COLOUR_BLACK, x + 5, y + 5);
}


void game_handle_input();
void title_update()
{
	int i, numUpdates;
	screenshot_check();
	title_handle_keyboard_input();

	if (game_is_not_paused()) {
		title_update_showcase();

		if (gGameSpeed > 1) {
			numUpdates = 1 << (gGameSpeed - 1);
		} else {
			numUpdates = 1;
		}

		for (i = 0; i < numUpdates; i++) {
			game_logic_update();
		}
	}

	gInputFlags &= ~INPUT_FLAG_VIEWPORT_SCROLLING;

	window_map_tooltip_update_visibility();
	window_dispatch_update_all();

	gSavedAge++;

	// Input
	game_handle_input();
}

static uint8 *generate_random_script()
{
	int i, j;
	const int views = 16;

	util_srand((unsigned int)time(NULL));

	uint8 *script = malloc(views * 8 + 2);
	i = 0;
	script[i++] = TITLE_SCRIPT_LOAD;
	for (j = 0; j < views; j++) {
		script[i++] = TITLE_SCRIPT_LOCATION;
		script[i++] = 64 + (util_rand() % 128);
		script[i++] = 64 + (util_rand() % 128);

		int rotationCount = util_rand() % 4;
		if (rotationCount > 0) {
			script[i++] = TITLE_SCRIPT_ROTATE;
			script[i++] = rotationCount;
		}

		script[i++] = TITLE_SCRIPT_WAIT;
		script[i++] = 8 + (util_rand() % 6);
	}
	script[i] = TITLE_SCRIPT_RESTART;

	return script;
}

bool title_refresh_sequence()
{
	_scriptCurrentPreset = gCurrentPreviewTitleSequence;
	if (_loadedTitleSequenceId != _scriptCurrentPreset) {
		FreeTitleSequence(_loadedTitleSequence);

		const utf8 * path = title_sequence_manager_get_path(_scriptCurrentPreset);
		_loadedTitleSequence = LoadTitleSequence(path);
		_loadedTitleSequenceId = _scriptCurrentPreset;
	}
	TitleSequence *title = _loadedTitleSequence;

	bool hasLoad = false, hasInvalidSave = false, hasWait = false, hasRestart = false;
	for (int i = 0; i < title->NumCommands && !hasInvalidSave; i++) {
		if (title->Commands[i].Type == TITLE_SCRIPT_LOAD) {
			if (title->Commands[i].SaveIndex == 0xFF)
				hasInvalidSave = true;
			hasLoad = true;
		}
		else if (title->Commands[i].Type == TITLE_SCRIPT_LOADRCT1) {
			hasLoad = true;
		}
		else if (title->Commands[i].Type == TITLE_SCRIPT_LOADMM) {
			hasLoad = true;
		}
		else if (title->Commands[i].Type == TITLE_SCRIPT_WAIT && title->Commands[i].Seconds >= 4) {
			hasWait = true;
		}
		else if (title->Commands[i].Type == TITLE_SCRIPT_RESTART) {
			hasRestart = true;
			break;
		}
		else if (title->Commands[i].Type == TITLE_SCRIPT_END) {
			break;
		}
	}
	if (hasLoad && (hasWait || !hasRestart) && !hasInvalidSave) {
		char *src;
		uint8 *scriptPtr, *binaryScript;
		binaryScript = malloc(1024 * 8);
		scriptPtr = binaryScript;

		for (int i = 0; i < title->NumCommands; i++) {
			*scriptPtr++ = title->Commands[i].Type;
			switch (title->Commands[i].Type) {
			case TITLE_SCRIPT_LOADRCT1:
				*scriptPtr++ = title->Commands[i].SaveIndex;
				break;
			case TITLE_SCRIPT_LOAD:
				src = title->Saves[title->Commands[i].SaveIndex];
				do {
					*scriptPtr++ = *src++;
				} while (*(src - 1) != 0);
				break;
			case TITLE_SCRIPT_LOCATION:
				*scriptPtr++ = title->Commands[i].X;
				*scriptPtr++ = title->Commands[i].Y;
				break;
			case TITLE_SCRIPT_ROTATE:
				*scriptPtr++ = title->Commands[i].Rotations;
				break;
			case TITLE_SCRIPT_ZOOM:
				*scriptPtr++ = title->Commands[i].Zoom;
				break;
			case TITLE_SCRIPT_SPEED:
				*scriptPtr++ = title->Commands[i].Speed;
				break;
			case TITLE_SCRIPT_WAIT:
				*scriptPtr++ = title->Commands[i].Seconds;
				break;
			}
		}

		*scriptPtr++ = TITLE_SCRIPT_END;

		int scriptLength = (int)(scriptPtr - binaryScript);
		binaryScript = realloc(binaryScript, scriptLength);

		_scriptNoLoadsSinceRestart = 1;
		if (_loadedScript != _magicMountainScript)
			SafeFree(_loadedScript);
		_loadedScript = binaryScript;
		_currentScript = binaryScript;
		_scriptWaitCounter = 0;
		gTitleScriptCommand = -1;
		gTitleScriptSave = 0xFF;

		if (gScreenFlags == SCREEN_FLAGS_TITLE_DEMO) {
			title_update_showcase();
			gfx_invalidate_screen();
		}

		return true;
	}
	log_error("Failed to load title sequence, hasLoad: %i, hasWait4seconds: %i, hasRestart: %i, hasInvalidSave: %i", hasLoad, hasWait, hasRestart, hasInvalidSave);
	window_error_open(STR_ERR_FAILED_TO_LOAD_TITLE_SEQUENCE, (!hasWait && hasRestart) ? STR_TITLE_EDITOR_ERR_RESTART_REQUIRES_WAIT : STR_NONE);
	_scriptNoLoadsSinceRestart = 1;
	if (_loadedScript != _magicMountainScript)
		SafeFree(_loadedScript);
	_scriptCurrentPreset = 0;
	_loadedScript = (uint8*)_magicMountainScript;
	_currentScript = _magicMountainScript;
	_scriptWaitCounter = 0;
	gTitleScriptCommand = -1;
	gTitleScriptSave = 0xFF;
	gCurrentPreviewTitleSequence = 0;
	window_invalidate_by_class(WC_OPTIONS);
	window_invalidate_by_class(WC_TITLE_EDITOR);

	if (gScreenFlags == SCREEN_FLAGS_TITLE_DEMO) {
		title_update_showcase();
		gfx_invalidate_screen();
	}

	return false;
}

void title_skip_from_beginning()
{
	_scriptNoLoadsSinceRestart = 1;
	gTitleScriptCommand = -1;
	gTitleScriptSave = 0xFF;
	_currentScript = _loadedScript;
}
