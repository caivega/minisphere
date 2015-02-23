#include "minisphere.h"
#include "api.h"
#include "person.h"

#include "map_engine.h"

struct map
{
	int        width, height;
	lstring_t* *scripts;
};

static bool change_map        (const char* filename);
static void render_map_engine (void);
static void update_map_engine (void);

static duk_ret_t js_MapEngine             (duk_context* ctx);
static duk_ret_t js_GetCurrentMap         (duk_context* ctx);
static duk_ret_t js_GetMapEngineFrameRate (duk_context* ctx);
static duk_ret_t js_SetMapEngineFrameRate (duk_context* ctx);
static duk_ret_t js_SetDefaultMapScript   (duk_context* ctx);
static duk_ret_t js_SetRenderScript       (duk_context* ctx);
static duk_ret_t js_SetUpdateScript       (duk_context* ctx);
static duk_ret_t js_IsMapEngineRunning    (duk_context* ctx);
static duk_ret_t js_AttachCamera          (duk_context* ctx);
static duk_ret_t js_AttachInput           (duk_context* ctx);
static duk_ret_t js_ExitMapEngine         (duk_context* ctx);
static duk_ret_t js_RenderMap             (duk_context* ctx);
static duk_ret_t js_UpdateMapEngine       (duk_context* ctx);

static person_t* s_camera_person = NULL;
static int       s_cam_x         = 0;
static int       s_cam_y         = 0;
static bool      s_exiting       = false;
static int       s_framerate     = 0;
static person_t* s_input_person  = NULL;
static map_t*    s_map           = NULL;
static char*     s_map_filename  = NULL;
static bool      s_running       = false;

#pragma pack(push, 1)
struct rmp_header
{
	char    signature[4];
	int16_t version;
	uint8_t type;
	int8_t  num_layers;
	uint8_t reserved_1;
	int16_t num_entities;
	int16_t start_x;
	int16_t start_y;
	int8_t  start_layer;
	int8_t  start_direction;
	int16_t num_strings;
	int16_t num_zones;
	uint8_t toric_map;
	uint8_t reserved[234];
};
#pragma pack(pop)

enum mapscript
{
	MAPSCRIPT_ON_ENTER,
	MAPSCRIPT_ON_LEAVE,
	MAPSCRIPT_ON_LEAVE_NORTH,
	MAPSCRIPT_ON_LEAVE_EAST,
	MAPSCRIPT_ON_LEAVE_SOUTH,
	MAPSCRIPT_ON_LEAVE_WEST
};

map_t*
load_map(const char* path)
{
	bool              failed = false;
	ALLEGRO_FILE*     file;
	map_t*            map;
	struct rmp_header rmp;
	lstring_t*        *scripts;

	int i;
	
	if ((map = calloc(1, sizeof(map_t))) == NULL) goto on_error;
	if ((file = al_fopen(path, "rb")) == NULL) goto on_error;
	if (al_fread(file, &rmp, sizeof(struct rmp_header)) != sizeof(struct rmp_header))
		goto on_error;
	if (memcmp(rmp.signature, ".rmp", 4) != 0) goto on_error;
	switch (rmp.version) {
	case 1:
		if ((scripts = calloc(rmp.num_strings, sizeof(lstring_t*))) == NULL)
			goto on_error;
		for (i = 0; i < rmp.num_strings; ++i)
			failed = ((scripts[i] = al_fread_lstring(file)) == NULL) || failed;
		if (failed) goto on_error;
		break;
	default:
		goto on_error;
		break;
	}
	map->scripts = scripts;
	return map;

on_error:
	if (file != NULL) al_fclose(file);
	if (scripts != NULL) {
		for (i = 0; i < rmp.num_strings; ++i) free_lstring(scripts[i]);
		free(scripts);
	}
	free(map);
	return NULL;
}

void
init_map_engine_api(duk_context* ctx)
{
	register_api_func(ctx, NULL, "MapEngine", &js_MapEngine);
	register_api_func(ctx, NULL, "GetCurrentMap", &js_GetCurrentMap);
	register_api_func(ctx, NULL, "GetMapEngineFrameRate", &js_GetMapEngineFrameRate);
	register_api_func(ctx, NULL, "SetMapEngineFrameRate", &js_SetMapEngineFrameRate);
	register_api_func(ctx, NULL, "SetDefaultMapScript", &js_SetDefaultMapScript);
	register_api_func(ctx, NULL, "SetRenderScript", &js_SetRenderScript);
	register_api_func(ctx, NULL, "SetUpdateScript", &js_SetUpdateScript);
	register_api_func(ctx, NULL, "IsMapEngineRunning", &js_IsMapEngineRunning);
	register_api_func(ctx, NULL, "AttachCamera", &js_AttachCamera);
	register_api_func(ctx, NULL, "AttachInput", &js_AttachInput);
	register_api_func(ctx, NULL, "ExitMapEngine", &js_ExitMapEngine);
	register_api_func(ctx, NULL, "RenderMap", &js_RenderMap);
	register_api_func(ctx, NULL, "UpdateMapEngine", &js_UpdateMapEngine);

	// Map script types
	register_api_const(ctx, "SCRIPT_ON_ENTER_MAP", MAPSCRIPT_ON_ENTER);
	register_api_const(ctx, "SCRIPT_ON_LEAVE_MAP", MAPSCRIPT_ON_LEAVE);
	register_api_const(ctx, "SCRIPT_ON_LEAVE_MAP_NORTH", MAPSCRIPT_ON_LEAVE_NORTH);
	register_api_const(ctx, "SCRIPT_ON_LEAVE_MAP_EAST", MAPSCRIPT_ON_LEAVE_EAST);
	register_api_const(ctx, "SCRIPT_ON_LEAVE_MAP_SOUTH", MAPSCRIPT_ON_LEAVE_SOUTH);
	register_api_const(ctx, "SCRIPT_ON_LEAVE_MAP_WEST", MAPSCRIPT_ON_LEAVE_WEST);

	// initialize subcomponent APIs (persons, etc.)
	init_person_api();
}

static bool
change_map(const char* filename)
{
	map_t* map;
	char*  path;

	path = get_asset_path(filename, "maps", false);
	map = load_map(path);
	free(path);
	free(s_map_filename); s_map_filename = NULL;
	if (map != NULL) {
		s_map = map;
		s_map_filename = strdup(filename);
		
		// run default map entry script
		duk_push_global_stash(g_duktape);
		duk_get_prop_string(g_duktape, -1, "map_def_enter_script");
		if (duk_is_callable(g_duktape, -1)) {
			duk_call(g_duktape, 0); duk_pop(g_duktape);
		}
		duk_pop(g_duktape);
		
		// run map entry script
		duk_compile_lstring(g_duktape, 0x0, s_map->scripts[3]->buffer, s_map->scripts[3]->length);
		duk_call(g_duktape, 0); duk_pop(g_duktape);
		
		return true;
	}
	else {
		return false;
	}
}

static void
render_map_engine(void)
{
	al_draw_text(g_sys_font, al_map_rgb(255, 255, 255), 160, 114, ALLEGRO_ALIGN_CENTER, s_map_filename);
	render_persons(0, 0);
	duk_push_global_stash(g_duktape);
	duk_get_prop_string(g_duktape, -1, "render_script");
	if (duk_is_callable(g_duktape, -1)) duk_call(g_duktape, 0);
	duk_pop_2(g_duktape);
}

static void
update_map_engine(void)
{
	ALLEGRO_KEYBOARD_STATE kb_state;
	float                  x, y;
	
	update_persons();
	
	// check for player input
	if (s_input_person != NULL) {
		al_get_keyboard_state(&kb_state);
		if (al_key_down(&kb_state, ALLEGRO_KEY_UP)) {
			command_person(s_input_person, COMMAND_FACE_NORTH);
			command_person(s_input_person, COMMAND_MOVE_NORTH);
		}
		else if (al_key_down(&kb_state, ALLEGRO_KEY_RIGHT)) {
			command_person(s_input_person, COMMAND_FACE_EAST);
			command_person(s_input_person, COMMAND_MOVE_EAST);
		}
		else if (al_key_down(&kb_state, ALLEGRO_KEY_DOWN)) {
			command_person(s_input_person, COMMAND_FACE_SOUTH);
			command_person(s_input_person, COMMAND_MOVE_SOUTH);
		}
		else if (al_key_down(&kb_state, ALLEGRO_KEY_LEFT)) {
			command_person(s_input_person, COMMAND_FACE_WEST);
			command_person(s_input_person, COMMAND_MOVE_WEST);
		}
	}
	
	// update camera
	if (s_camera_person != NULL) {
		get_person_xy(s_camera_person, &x, &y, s_map->width, s_map->height);
		s_cam_x = x;
		s_cam_y = y;
	}

	// run update script
	duk_push_global_stash(g_duktape);
	duk_get_prop_string(g_duktape, -1, "update_script");
	if (duk_is_callable(g_duktape, -1)) duk_call(g_duktape, 0);
	duk_pop_2(g_duktape);
}

static duk_ret_t
js_MapEngine(duk_context* ctx)
{
	const char* filename;
	
	filename = duk_to_string(ctx, 0);
	s_running = true;
	s_exiting = false;
	al_clear_to_color(al_map_rgba(0, 0, 0, 255));
	s_framerate = duk_to_int(ctx, 1);
	change_map(filename);
	while (!s_exiting) {
		if (!begin_frame(s_framerate)) duk_error(ctx, DUK_ERR_ERROR, "!exit");
		update_map_engine();
		render_map_engine();
	}
	s_running = false;
	return 0;
}

static duk_ret_t
js_GetCurrentMap(duk_context* ctx)
{
	if (s_running) {
		duk_push_string(ctx, s_map_filename);
		return 1;
	}
	else {
		duk_error(ctx, DUK_ERR_ERROR, "GetCurrentMap(): Operation requires map engine to be running");
	}
}

static duk_ret_t
js_GetMapEngineFrameRate(duk_context* ctx)
{
	duk_push_int(ctx, s_framerate);
	return 1;
}

static duk_ret_t
js_SetMapEngineFrameRate(duk_context* ctx)
{
	s_framerate = duk_to_int(ctx, 0);
	return 0;
}

static duk_ret_t
js_SetDefaultMapScript(duk_context* ctx)
{
	const char* script;
	size_t      script_size;
	const char* script_name;
	int         script_type;

	script_type = duk_require_int(ctx, 0);
	script = duk_require_lstring(ctx, 1, &script_size);
	script_name = (script_type == MAPSCRIPT_ON_ENTER) ? "map_def_enter_script"
		: (script_type == MAPSCRIPT_ON_LEAVE) ? "map_def_leave_script"
		: (script_type == MAPSCRIPT_ON_LEAVE_NORTH) ? "map_def_leave_north_script"
		: (script_type == MAPSCRIPT_ON_LEAVE_EAST) ? "map_def_leave_east_script"
		: (script_type == MAPSCRIPT_ON_LEAVE_SOUTH) ? "map_def_leave_south_script"
		: (script_type == MAPSCRIPT_ON_LEAVE_WEST) ? "map_def_leave_west_script"
		: NULL;
	if (script_name != NULL) {
		duk_push_global_stash(ctx);
		duk_push_string(ctx, "[def-mapscript]");
		duk_compile_lstring_filename(ctx, DUK_COMPILE_EVAL, script, script_size);
		duk_put_prop_string(ctx, -2, script_name);
		duk_pop(ctx);
		return 0;
	}
	else {
		duk_error(ctx, DUK_ERR_ERROR, "SetDefaultMapScript(): Invalid map script constant");
	}
}

static duk_ret_t
js_SetRenderScript(duk_context* ctx)
{
	const char* script;
	size_t      script_size;

	script = duk_require_lstring(ctx, 0, &script_size);
	duk_push_global_stash(ctx);
	duk_push_string(ctx, "[renderscript]");
	duk_compile_lstring_filename(ctx, DUK_COMPILE_EVAL, script, script_size);
	duk_put_prop_string(ctx, -2, "render_script");
	duk_pop(ctx);
	return 0;
}

static duk_ret_t
js_SetUpdateScript(duk_context* ctx)
{
	const char* script;
	size_t      script_size;

	script = duk_require_lstring(ctx, 0, &script_size);
	duk_push_global_stash(ctx);
	duk_push_string(ctx, "[updatescript]");
	duk_compile_lstring_filename(ctx, DUK_COMPILE_EVAL, script, script_size);
	duk_put_prop_string(ctx, -2, "update_script");
	duk_pop(ctx);
	return 0;
}

static duk_ret_t
js_IsMapEngineRunning(duk_context* ctx)
{
	duk_push_boolean(ctx, s_running);
	return 1;
}

static duk_ret_t
js_AttachCamera(duk_context* ctx)
{
	const char* name;
	person_t*   person;

	name = duk_to_string(ctx, 0);
	if ((person = find_person(name)) != NULL) {
		s_camera_person = person;
	}
	else {
		duk_error(ctx, DUK_ERR_REFERENCE_ERROR, "AttachCamera(): Person '%s' doesn't exist", name);
	}
	return 0;
}

static duk_ret_t
js_AttachInput(duk_context* ctx)
{
	const char* name;
	person_t*   person;

	name = duk_to_string(ctx, 0);
	if ((person = find_person(name)) != NULL) {
		s_input_person = person;
	}
	else {
		duk_error(ctx, DUK_ERR_REFERENCE_ERROR, "AttachInput(): Person '%s' doesn't exist", name);
	}
	return 0;
}

static duk_ret_t
js_ExitMapEngine(duk_context* ctx)
{
	if (s_running) {
		s_exiting = true;
		return 0;
	}
	else {
		duk_error(ctx, DUK_ERR_ERROR, "ExitMapEngine(): Operation requires map engine to be running");
	}
}

static duk_ret_t
js_RenderMap(duk_context* ctx)
{
	if (s_running) {
		render_map_engine();
		return 0;
	}
	else {
		duk_error(ctx, DUK_ERR_ERROR, "RenderMap(): Operation requires map engine to be running");
	}
}

static duk_ret_t
js_UpdateMapEngine(duk_context* ctx)
{
	if (s_running) {
		update_map_engine();
		return 0;
	}
	else {
		duk_error(ctx, DUK_ERR_ERROR, "UpdateMapEngine(): Operation requires map engine to be running");
	}
}
