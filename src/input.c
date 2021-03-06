#include "minisphere.h"
#include "api.h"
#include "input.h"
#include "vector.h"

#define MAX_JOYSTICKS   (4)
#define MAX_JOY_BUTTONS (32)

struct key_queue
{
	int num_keys;
	int keys[255];
};

enum mouse_button
{
	MOUSE_BUTTON_LEFT,
	MOUSE_BUTTON_RIGHT,
	MOUSE_BUTTON_MIDDLE
};

enum mouse_wheel_event
{
	MOUSE_WHEEL_UP,
	MOUSE_WHEEL_DOWN
};

static duk_ret_t js_AreKeysLeft             (duk_context* ctx);
static duk_ret_t js_IsAnyKeyPressed         (duk_context* ctx);
static duk_ret_t js_IsJoystickButtonPressed (duk_context* ctx);
static duk_ret_t js_IsKeyPressed            (duk_context* ctx);
static duk_ret_t js_IsMouseButtonPressed    (duk_context* ctx);
static duk_ret_t js_GetJoystickAxis         (duk_context* ctx);
static duk_ret_t js_GetKey                  (duk_context* ctx);
static duk_ret_t js_GetKeyString            (duk_context* ctx);
static duk_ret_t js_GetNumMouseWheelEvents  (duk_context* ctx);
static duk_ret_t js_GetMouseWheelEvent      (duk_context* ctx);
static duk_ret_t js_GetMouseX               (duk_context* ctx);
static duk_ret_t js_GetMouseY               (duk_context* ctx);
static duk_ret_t js_GetNumJoysticks         (duk_context* ctx);
static duk_ret_t js_GetNumJoystickAxes      (duk_context* ctx);
static duk_ret_t js_GetNumJoystickButtons   (duk_context* ctx);
static duk_ret_t js_GetNumMouseWheelEvents  (duk_context* ctx);
static duk_ret_t js_GetPlayerKey            (duk_context* ctx);
static duk_ret_t js_GetToggleState          (duk_context* ctx);
static duk_ret_t js_SetMousePosition        (duk_context* ctx);
static duk_ret_t js_BindKey                 (duk_context* ctx);
static duk_ret_t js_BindJoystickButton      (duk_context* ctx);
static duk_ret_t js_ClearKeyQueue           (duk_context* ctx);
static duk_ret_t js_UnbindKey               (duk_context* ctx);
static duk_ret_t js_UnbindJoystickButton    (duk_context* ctx);

static void bind_button       (vector_t* bindings, int joy_index, int button, script_t* on_down_script, script_t* on_up_script);
static void bind_key          (vector_t* bindings, int keycode, script_t* on_down_script, script_t* on_up_script);
static void queue_key         (int keycode);
static void queue_wheel_event (int event);

static vector_t*            s_bound_buttons;
static vector_t*            s_bound_keys;
static vector_t*            s_bound_map_keys;
static ALLEGRO_EVENT_QUEUE* s_events;
static ALLEGRO_JOYSTICK*    s_joy_handles[MAX_JOYSTICKS];
static struct key_queue     s_key_queue;
static int                  s_last_wheel_pos = 0;
static int                  s_num_joysticks = 0;
static int                  s_num_wheel_events = 0;
static int                  s_wheel_queue[255];

struct bound_button
{
	int       joystick_id;
	int       button;
	bool      is_pressed;
	script_t* on_down_script;
	script_t* on_up_script;
};

struct bound_key
{
	int       keycode;
	bool      is_pressed;
	script_t* on_down_script;
	script_t* on_up_script;
};

void
initialize_input(void)
{
	int c_buttons = MAX_JOY_BUTTONS * MAX_JOYSTICKS;

	int i;

	printf("Initializing input\n");
	
	al_install_keyboard();
	al_install_mouse();
	al_install_joystick();
	s_events = al_create_event_queue();
	al_register_event_source(s_events, al_get_keyboard_event_source());
	al_register_event_source(s_events, al_get_mouse_event_source());
	al_register_event_source(s_events, al_get_joystick_event_source());

	// look for active joysticks
	s_num_joysticks = fmin(MAX_JOYSTICKS, al_get_num_joysticks());
	for (i = 0; i < MAX_JOYSTICKS; ++i)
		s_joy_handles[i] = i < s_num_joysticks ? al_get_joystick(i) : NULL;

	// create bound key vectors
	s_bound_buttons = new_vector(sizeof(struct bound_button));
	s_bound_keys = new_vector(sizeof(struct bound_key));
	s_bound_map_keys = new_vector(sizeof(struct bound_key));
}

void
shutdown_input(void)
{
	struct bound_button* i_button;
	struct bound_key*    i_key;
	
	iter_t iter;

	printf("Shutting down input\n");

	// free bound key scripts
	iter = iterate_vector(s_bound_buttons);
	while (i_button = next_vector_item(&iter)) {
		free_script(i_button->on_down_script);
		free_script(i_button->on_up_script);
	}
	iter = iterate_vector(s_bound_keys);
	while (i_key = next_vector_item(&iter)) {
		free_script(i_key->on_down_script);
		free_script(i_key->on_up_script);
	}
	iter = iterate_vector(s_bound_map_keys);
	while (i_key = next_vector_item(&iter)) {
		free_script(i_key->on_down_script);
		free_script(i_key->on_up_script);
	}
	free_vector(s_bound_buttons);
	free_vector(s_bound_keys);
	free_vector(s_bound_map_keys);

	// shut down Allegro input
	al_destroy_event_queue(s_events);
	al_uninstall_joystick();
	al_uninstall_mouse();
	al_uninstall_keyboard();
}

bool
is_any_key_down(void)
{
	ALLEGRO_KEYBOARD_STATE kb_state;

	int i_key;

	al_get_keyboard_state(&kb_state);
	for (i_key = 0; i_key < ALLEGRO_KEY_MAX; ++i_key)
		if (al_key_down(&kb_state, i_key)) return true;
	return false;
}

bool
is_joy_button_down(int joy_index, int button)
{
	ALLEGRO_JOYSTICK_STATE joy_state;
	ALLEGRO_JOYSTICK*      joystick;

	if (!(joystick = s_joy_handles[joy_index]))
		return 0.0;
	al_get_joystick_state(joystick, &joy_state);
	return joy_state.button[button] > 0;
}

bool
is_key_down(int keycode)
{
	bool                   is_pressed;
	ALLEGRO_KEYBOARD_STATE kb_state;

	al_get_keyboard_state(&kb_state);
	switch (keycode) {
	case ALLEGRO_KEY_LSHIFT:
		is_pressed = al_key_down(&kb_state, ALLEGRO_KEY_LSHIFT)
			|| al_key_down(&kb_state, ALLEGRO_KEY_RSHIFT);
		break;
	case ALLEGRO_KEY_LCTRL:
		is_pressed = al_key_down(&kb_state, ALLEGRO_KEY_LCTRL)
			|| al_key_down(&kb_state, ALLEGRO_KEY_RCTRL);
		break;
	case ALLEGRO_KEY_ALT:
		is_pressed = al_key_down(&kb_state, ALLEGRO_KEY_ALT)
			|| al_key_down(&kb_state, ALLEGRO_KEY_ALTGR);
		break;
	default:
		is_pressed = al_key_down(&kb_state, keycode);
	}
	return is_pressed && kb_state.display == g_display;
}

float
get_joy_axis(int joy_index, int axis_index)
{
	ALLEGRO_JOYSTICK_STATE joy_state;
	ALLEGRO_JOYSTICK*      joystick;
	int                    n_stick_axes;
	int                    n_sticks;

	int i;

	if (!(joystick = s_joy_handles[joy_index]))
		return 0.0;
	al_get_joystick_state(joystick, &joy_state);
	n_sticks = al_get_joystick_num_sticks(joystick);
	for (i = 0; i < n_sticks; ++i) {
		n_stick_axes = al_get_joystick_num_axes(joystick, i);
		if (axis_index < n_stick_axes)
			return joy_state.stick[i].axis[axis_index];
		axis_index -= n_stick_axes;
	}
	return 0.0;
}

int
get_joy_axis_count(int joy_index)
{
	ALLEGRO_JOYSTICK* joystick;
	int               n_axes;
	int               n_sticks;

	int i;
	
	if (!(joystick = s_joy_handles[joy_index]))
		return 0;
	n_sticks = al_get_joystick_num_sticks(joystick);
	n_axes = 0;
	for (i = 0; i < n_sticks; ++i)
		n_axes += al_get_joystick_num_axes(joystick, i);
	return n_axes;
}

int
get_joy_button_count(int joy_index)
{
	ALLEGRO_JOYSTICK* joystick;

	if (!(joystick = s_joy_handles[joy_index]))
		return 0;
	return al_get_joystick_num_buttons(joystick);
}

void
clear_key_queue(void)
{
	s_key_queue.num_keys = 0;
}

void
update_bound_keys(bool use_map_keys)
{
	struct bound_button*   button;
	struct bound_key*      key;
	bool                   is_down;
	ALLEGRO_KEYBOARD_STATE kb_state;
	
	iter_t iter;

	// check bound keyboad keys
	al_get_keyboard_state(&kb_state);
	if (use_map_keys) {
		iter = iterate_vector(s_bound_map_keys);
		while (key = next_vector_item(&iter)) {
			is_down = al_key_down(&kb_state, key->keycode);
			if (is_down && !key->is_pressed)
				run_script(key->on_down_script, false);
			if (!is_down && key->is_pressed)
				run_script(key->on_up_script, false);
			key->is_pressed = is_down;
		}
	}
	iter = iterate_vector(s_bound_keys);
	while (key = next_vector_item(&iter)) {
		is_down = al_key_down(&kb_state, key->keycode);
		if (is_down && !key->is_pressed)
			run_script(key->on_down_script, false);
		if (!is_down && key->is_pressed)
			run_script(key->on_up_script, false);
		key->is_pressed = is_down;
	}

	// check bound joystick buttons
	iter = iterate_vector(s_bound_buttons);
	while (button = next_vector_item(&iter)) {
		is_down = is_joy_button_down(button->joystick_id, button->button);
		if (is_down && !button->is_pressed)
			run_script(button->on_down_script, false);
		if (!is_down && button->is_pressed)
			run_script(button->on_up_script, false);
		button->is_pressed = is_down;
	}
}

void
update_input(void)
{
	ALLEGRO_EVENT          event;
	ALLEGRO_MOUSE_STATE    mouse_state;

	// process Allegro input events
	while (al_get_next_event(s_events, &event)) {
		switch (event.type) {
		case ALLEGRO_EVENT_KEY_CHAR:
			switch (event.keyboard.keycode) {
			case ALLEGRO_KEY_ENTER:
				if (event.keyboard.modifiers & ALLEGRO_KEYMOD_ALT
				 || event.keyboard.modifiers & ALLEGRO_KEYMOD_ALTGR)
				{
					toggle_fullscreen();
				}
				else {
					queue_key(event.keyboard.keycode);
				}
				break;
			case ALLEGRO_KEY_F10:
				toggle_fullscreen();
				break;
			case ALLEGRO_KEY_F11:
				toggle_fps_display();
				break;
			case ALLEGRO_KEY_F12:
				take_screenshot();
				break;
			default:
				queue_key(event.keyboard.keycode);
				break;
			}
		}
	}
	
	// check whether mouse wheel moved since last update
	al_get_mouse_state(&mouse_state);
	if (mouse_state.z > s_last_wheel_pos)
		queue_wheel_event(MOUSE_WHEEL_UP);
	if (mouse_state.z < s_last_wheel_pos)
		queue_wheel_event(MOUSE_WHEEL_DOWN);
	s_last_wheel_pos = mouse_state.z;
}

static void
bind_button(vector_t* bindings, int joy_index, int button, script_t* on_down_script, script_t* on_up_script)
{
	bool                 is_new_entry = true;
	struct bound_button* bound;
	struct bound_button  new_binding;
	script_t*            old_down_script;
	script_t*            old_up_script;

	iter_t iter;

	new_binding.joystick_id = joy_index;
	new_binding.button = button;
	new_binding.is_pressed = false;
	new_binding.on_down_script = on_down_script;
	new_binding.on_up_script = on_up_script;
	iter = iterate_vector(bindings);
	while (bound = next_vector_item(&iter)) {
		if (bound->joystick_id == joy_index && bound->button == button) {
			bound->is_pressed = false;
			old_down_script = bound->on_down_script;
			old_up_script = bound->on_up_script;
			memcpy(bound, &new_binding, sizeof(struct bound_button));
			if (old_down_script != bound->on_down_script)
				free_script(old_down_script);
			if (old_up_script != bound->on_up_script)
				free_script(old_up_script);
			is_new_entry = false;
		}
	}
	if (is_new_entry)
		push_back_vector(bindings, &new_binding);
}

static void
bind_key(vector_t* bindings, int keycode, script_t* on_down_script, script_t* on_up_script)
{
	bool              is_new_key = true;
	struct bound_key* key;
	struct bound_key  new_binding;
	script_t*         old_down_script;
	script_t*         old_up_script;

	iter_t iter;

	new_binding.keycode = keycode;
	new_binding.is_pressed = false;
	new_binding.on_down_script = on_down_script;
	new_binding.on_up_script = on_up_script;
	iter = iterate_vector(bindings);
	while (key = next_vector_item(&iter)) {
		if (key->keycode == keycode) {
			key->is_pressed = false;
			old_down_script = key->on_down_script;
			old_up_script = key->on_up_script;
			memcpy(key, &new_binding, sizeof(struct bound_key));
			if (old_down_script != key->on_down_script)
				free_script(old_down_script);
			if (old_up_script != key->on_up_script)
				free_script(old_up_script);
			is_new_key = false;
		}
	}
	if (is_new_key)
		push_back_vector(bindings, &new_binding);
}

void
init_input_api(void)
{
	register_api_const(g_duk, "PLAYER_1", 0);
	register_api_const(g_duk, "PLAYER_2", 1);
	register_api_const(g_duk, "PLAYER_3", 2);
	register_api_const(g_duk, "PLAYER_4", 3);
	register_api_const(g_duk, "PLAYER_KEY_MENU", PLAYER_KEY_MENU);
	register_api_const(g_duk, "PLAYER_KEY_UP", PLAYER_KEY_UP);
	register_api_const(g_duk, "PLAYER_KEY_DOWN", PLAYER_KEY_DOWN);
	register_api_const(g_duk, "PLAYER_KEY_LEFT", PLAYER_KEY_LEFT);
	register_api_const(g_duk, "PLAYER_KEY_RIGHT", PLAYER_KEY_RIGHT);
	register_api_const(g_duk, "PLAYER_KEY_A", PLAYER_KEY_A);
	register_api_const(g_duk, "PLAYER_KEY_B", PLAYER_KEY_B);
	register_api_const(g_duk, "PLAYER_KEY_X", PLAYER_KEY_X);
	register_api_const(g_duk, "PLAYER_KEY_Y", PLAYER_KEY_Y);
	register_api_const(g_duk, "KEY_SHIFT", ALLEGRO_KEY_LSHIFT);
	register_api_const(g_duk, "KEY_CTRL", ALLEGRO_KEY_LCTRL);
	register_api_const(g_duk, "KEY_ALT", ALLEGRO_KEY_ALT);
	register_api_const(g_duk, "KEY_UP", ALLEGRO_KEY_UP);
	register_api_const(g_duk, "KEY_DOWN", ALLEGRO_KEY_DOWN);
	register_api_const(g_duk, "KEY_LEFT", ALLEGRO_KEY_LEFT);
	register_api_const(g_duk, "KEY_RIGHT", ALLEGRO_KEY_RIGHT);
	register_api_const(g_duk, "KEY_APOSTROPHE", ALLEGRO_KEY_QUOTE);
	register_api_const(g_duk, "KEY_BACKSLASH", ALLEGRO_KEY_BACKSLASH);
	register_api_const(g_duk, "KEY_BACKSPACE", ALLEGRO_KEY_BACKSPACE);
	register_api_const(g_duk, "KEY_CLOSEBRACE", ALLEGRO_KEY_CLOSEBRACE);
	register_api_const(g_duk, "KEY_CAPSLOCK", ALLEGRO_KEY_CAPSLOCK);
	register_api_const(g_duk, "KEY_COMMA", ALLEGRO_KEY_COMMA);
	register_api_const(g_duk, "KEY_DELETE", ALLEGRO_KEY_DELETE);
	register_api_const(g_duk, "KEY_END", ALLEGRO_KEY_END);
	register_api_const(g_duk, "KEY_ENTER", ALLEGRO_KEY_ENTER);
	register_api_const(g_duk, "KEY_EQUALS", ALLEGRO_KEY_EQUALS);
	register_api_const(g_duk, "KEY_ESCAPE", ALLEGRO_KEY_ESCAPE);
	register_api_const(g_duk, "KEY_HOME", ALLEGRO_KEY_HOME);
	register_api_const(g_duk, "KEY_INSERT", ALLEGRO_KEY_INSERT);
	register_api_const(g_duk, "KEY_MINUS", ALLEGRO_KEY_MINUS);
	register_api_const(g_duk, "KEY_NUMLOCK", ALLEGRO_KEY_NUMLOCK);
	register_api_const(g_duk, "KEY_OPENBRACE", ALLEGRO_KEY_OPENBRACE);
	register_api_const(g_duk, "KEY_PAGEDOWN", ALLEGRO_KEY_PGDN);
	register_api_const(g_duk, "KEY_PAGEUP", ALLEGRO_KEY_PGUP);
	register_api_const(g_duk, "KEY_PERIOD", ALLEGRO_KEY_FULLSTOP);
	register_api_const(g_duk, "KEY_SCROLLOCK", ALLEGRO_KEY_SCROLLLOCK);
	register_api_const(g_duk, "KEY_SEMICOLON", ALLEGRO_KEY_SEMICOLON);
	register_api_const(g_duk, "KEY_SPACE", ALLEGRO_KEY_SPACE);
	register_api_const(g_duk, "KEY_SLASH", ALLEGRO_KEY_SLASH);
	register_api_const(g_duk, "KEY_TAB", ALLEGRO_KEY_TAB);
	register_api_const(g_duk, "KEY_TILDE", ALLEGRO_KEY_TILDE);
	register_api_const(g_duk, "KEY_F1", ALLEGRO_KEY_F1);
	register_api_const(g_duk, "KEY_F2", ALLEGRO_KEY_F2);
	register_api_const(g_duk, "KEY_F3", ALLEGRO_KEY_F3);
	register_api_const(g_duk, "KEY_F4", ALLEGRO_KEY_F4);
	register_api_const(g_duk, "KEY_F5", ALLEGRO_KEY_F5);
	register_api_const(g_duk, "KEY_F6", ALLEGRO_KEY_F6);
	register_api_const(g_duk, "KEY_F7", ALLEGRO_KEY_F7);
	register_api_const(g_duk, "KEY_F8", ALLEGRO_KEY_F8);
	register_api_const(g_duk, "KEY_F9", ALLEGRO_KEY_F9);
	register_api_const(g_duk, "KEY_F10", ALLEGRO_KEY_F10);
	register_api_const(g_duk, "KEY_F11", ALLEGRO_KEY_F11);
	register_api_const(g_duk, "KEY_F12", ALLEGRO_KEY_F12);
	register_api_const(g_duk, "KEY_A", ALLEGRO_KEY_A);
	register_api_const(g_duk, "KEY_B", ALLEGRO_KEY_B);
	register_api_const(g_duk, "KEY_C", ALLEGRO_KEY_C);
	register_api_const(g_duk, "KEY_D", ALLEGRO_KEY_D);
	register_api_const(g_duk, "KEY_E", ALLEGRO_KEY_E);
	register_api_const(g_duk, "KEY_F", ALLEGRO_KEY_F);
	register_api_const(g_duk, "KEY_G", ALLEGRO_KEY_G);
	register_api_const(g_duk, "KEY_H", ALLEGRO_KEY_H);
	register_api_const(g_duk, "KEY_I", ALLEGRO_KEY_I);
	register_api_const(g_duk, "KEY_J", ALLEGRO_KEY_J);
	register_api_const(g_duk, "KEY_K", ALLEGRO_KEY_K);
	register_api_const(g_duk, "KEY_L", ALLEGRO_KEY_L);
	register_api_const(g_duk, "KEY_M", ALLEGRO_KEY_M);
	register_api_const(g_duk, "KEY_N", ALLEGRO_KEY_N);
	register_api_const(g_duk, "KEY_O", ALLEGRO_KEY_O);
	register_api_const(g_duk, "KEY_P", ALLEGRO_KEY_P);
	register_api_const(g_duk, "KEY_Q", ALLEGRO_KEY_Q);
	register_api_const(g_duk, "KEY_R", ALLEGRO_KEY_R);
	register_api_const(g_duk, "KEY_S", ALLEGRO_KEY_S);
	register_api_const(g_duk, "KEY_T", ALLEGRO_KEY_T);
	register_api_const(g_duk, "KEY_U", ALLEGRO_KEY_U);
	register_api_const(g_duk, "KEY_V", ALLEGRO_KEY_V);
	register_api_const(g_duk, "KEY_W", ALLEGRO_KEY_W);
	register_api_const(g_duk, "KEY_X", ALLEGRO_KEY_X);
	register_api_const(g_duk, "KEY_Y", ALLEGRO_KEY_Y);
	register_api_const(g_duk, "KEY_Z", ALLEGRO_KEY_Z);
	register_api_const(g_duk, "KEY_1", ALLEGRO_KEY_1);
	register_api_const(g_duk, "KEY_2", ALLEGRO_KEY_2);
	register_api_const(g_duk, "KEY_3", ALLEGRO_KEY_3);
	register_api_const(g_duk, "KEY_4", ALLEGRO_KEY_4);
	register_api_const(g_duk, "KEY_5", ALLEGRO_KEY_5);
	register_api_const(g_duk, "KEY_6", ALLEGRO_KEY_6);
	register_api_const(g_duk, "KEY_7", ALLEGRO_KEY_7);
	register_api_const(g_duk, "KEY_8", ALLEGRO_KEY_8);
	register_api_const(g_duk, "KEY_9", ALLEGRO_KEY_9);
	register_api_const(g_duk, "KEY_0", ALLEGRO_KEY_0);
	register_api_const(g_duk, "KEY_NUM_1", ALLEGRO_KEY_PAD_1);
	register_api_const(g_duk, "KEY_NUM_2", ALLEGRO_KEY_PAD_2);
	register_api_const(g_duk, "KEY_NUM_3", ALLEGRO_KEY_PAD_3);
	register_api_const(g_duk, "KEY_NUM_4", ALLEGRO_KEY_PAD_4);
	register_api_const(g_duk, "KEY_NUM_5", ALLEGRO_KEY_PAD_5);
	register_api_const(g_duk, "KEY_NUM_6", ALLEGRO_KEY_PAD_6);
	register_api_const(g_duk, "KEY_NUM_7", ALLEGRO_KEY_PAD_7);
	register_api_const(g_duk, "KEY_NUM_8", ALLEGRO_KEY_PAD_8);
	register_api_const(g_duk, "KEY_NUM_9", ALLEGRO_KEY_PAD_9);
	register_api_const(g_duk, "KEY_NUM_0", ALLEGRO_KEY_PAD_0);

	register_api_const(g_duk, "MOUSE_LEFT", MOUSE_BUTTON_LEFT);
	register_api_const(g_duk, "MOUSE_RIGHT", MOUSE_BUTTON_RIGHT);
	register_api_const(g_duk, "MOUSE_MIDDLE", MOUSE_BUTTON_MIDDLE);
	register_api_const(g_duk, "MOUSE_LEFT", MOUSE_BUTTON_LEFT);
	register_api_const(g_duk, "MOUSE_WHEEL_UP", MOUSE_WHEEL_UP);
	register_api_const(g_duk, "MOUSE_WHEEL_DOWN", MOUSE_WHEEL_DOWN);

	register_api_const(g_duk, "JOYSTICK_AXIS_X", 0);
	register_api_const(g_duk, "JOYSTICK_AXIS_Y", 1);
	register_api_const(g_duk, "JOYSTICK_AXIS_Z", 2);
	register_api_const(g_duk, "JOYSTICK_AXIS_R", 3);
	register_api_const(g_duk, "JOYSTICK_AXIS_U", 4);
	register_api_const(g_duk, "JOYSTICK_AXIS_V", 5);

	register_api_function(g_duk, NULL, "AreKeysLeft", js_AreKeysLeft);
	register_api_function(g_duk, NULL, "IsAnyKeyPressed", js_IsAnyKeyPressed);
	register_api_function(g_duk, NULL, "IsJoystickButtonPressed", js_IsJoystickButtonPressed);
	register_api_function(g_duk, NULL, "IsKeyPressed", js_IsKeyPressed);
	register_api_function(g_duk, NULL, "IsMouseButtonPressed", js_IsMouseButtonPressed);
	register_api_function(g_duk, NULL, "GetJoystickAxis", js_GetJoystickAxis);
	register_api_function(g_duk, NULL, "GetKey", js_GetKey);
	register_api_function(g_duk, NULL, "GetKeyString", js_GetKeyString);
	register_api_function(g_duk, NULL, "GetMouseWheelEvent", js_GetMouseWheelEvent);
	register_api_function(g_duk, NULL, "GetMouseX", js_GetMouseX);
	register_api_function(g_duk, NULL, "GetMouseY", js_GetMouseY);
	register_api_function(g_duk, NULL, "GetNumJoysticks", js_GetNumJoysticks);
	register_api_function(g_duk, NULL, "GetNumJoystickAxes", js_GetNumJoystickAxes);
	register_api_function(g_duk, NULL, "GetNumJoystickButtons", js_GetNumJoystickButtons);
	register_api_function(g_duk, NULL, "GetNumMouseWheelEvents", js_GetNumMouseWheelEvents);
	register_api_function(g_duk, NULL, "GetPlayerKey", js_GetPlayerKey);
	register_api_function(g_duk, NULL, "GetToggleState", js_GetToggleState);
	register_api_function(g_duk, NULL, "SetMousePosition", js_SetMousePosition);
	register_api_function(g_duk, NULL, "BindJoystickButton", js_BindJoystickButton);
	register_api_function(g_duk, NULL, "BindKey", js_BindKey);
	register_api_function(g_duk, NULL, "ClearKeyQueue", js_ClearKeyQueue);
	register_api_function(g_duk, NULL, "UnbindJoystickButton", js_UnbindJoystickButton);
	register_api_function(g_duk, NULL, "UnbindKey", js_UnbindKey);
}

static void
queue_key(int keycode)
{
	int key_index;

	if (s_key_queue.num_keys < 255) {
		key_index = s_key_queue.num_keys;
		++s_key_queue.num_keys;
		s_key_queue.keys[key_index] = keycode;
	}
}

static void
queue_wheel_event(int event)
{
	if (s_num_wheel_events < 255) {
		s_wheel_queue[s_num_wheel_events] = event;
		++s_num_wheel_events;
	}
}

static duk_ret_t
js_AreKeysLeft(duk_context* ctx)
{
	duk_push_boolean(ctx, s_key_queue.num_keys > 0);
	return 1;
}

static duk_ret_t
js_IsAnyKeyPressed(duk_context* ctx)
{
	duk_push_boolean(ctx, is_any_key_down());
	return 1;
}

static duk_ret_t
js_IsJoystickButtonPressed(duk_context* ctx)
{
	int joy_index = duk_require_int(ctx, 0);
	int button = duk_require_int(ctx, 1);
	
	duk_push_boolean(ctx, is_joy_button_down(joy_index, button));
	return 1;
}

static duk_ret_t
js_IsKeyPressed(duk_context* ctx)
{
	int keycode = duk_require_int(ctx, 0);

	duk_push_boolean(ctx, is_key_down(keycode));
	return 1;
}

static duk_ret_t
js_IsMouseButtonPressed(duk_context* ctx)
{
	int button = duk_require_int(ctx, 0);

	int                 button_id;
	ALLEGRO_MOUSE_STATE mouse_state;

	button_id = button == MOUSE_BUTTON_RIGHT ? 2
		: button == MOUSE_BUTTON_MIDDLE ? 3
		: 1;
	al_get_mouse_state(&mouse_state);
	duk_push_boolean(ctx, mouse_state.display == g_display && al_mouse_button_down(&mouse_state, button_id));
	return 1;
}

static duk_ret_t
js_GetJoystickAxis(duk_context* ctx)
{
	int joy_index = duk_require_int(ctx, 0);
	int axis_index = duk_require_int(ctx, 1);

	duk_push_number(ctx, get_joy_axis(joy_index, axis_index));
	return 1;
}

static duk_ret_t
js_GetKey(duk_context* ctx)
{
	int keycode;

	while (s_key_queue.num_keys == 0) {
		do_events();
	}
	keycode = s_key_queue.keys[0];
	--s_key_queue.num_keys;
	memmove(s_key_queue.keys, &s_key_queue.keys[1], sizeof(int) * s_key_queue.num_keys);
	duk_push_int(ctx, keycode);
	return 1;
}

static duk_ret_t
js_GetKeyString(duk_context* ctx)
{
	int n_args = duk_get_top(ctx);
	int keycode = duk_require_int(ctx, 0);
	bool shift = n_args >= 2 ? duk_require_boolean(ctx, 1) : false;

	switch (keycode) {
	case ALLEGRO_KEY_A: duk_push_string(ctx, shift ? "A" : "a"); break;
	case ALLEGRO_KEY_B: duk_push_string(ctx, shift ? "B" : "b"); break;
	case ALLEGRO_KEY_C: duk_push_string(ctx, shift ? "C" : "c"); break;
	case ALLEGRO_KEY_D: duk_push_string(ctx, shift ? "D" : "d"); break;
	case ALLEGRO_KEY_E: duk_push_string(ctx, shift ? "E" : "e"); break;
	case ALLEGRO_KEY_F: duk_push_string(ctx, shift ? "F" : "f"); break;
	case ALLEGRO_KEY_G: duk_push_string(ctx, shift ? "G" : "g"); break;
	case ALLEGRO_KEY_H: duk_push_string(ctx, shift ? "H" : "h"); break;
	case ALLEGRO_KEY_I: duk_push_string(ctx, shift ? "I" : "i"); break;
	case ALLEGRO_KEY_J: duk_push_string(ctx, shift ? "J" : "j"); break;
	case ALLEGRO_KEY_K: duk_push_string(ctx, shift ? "K" : "k"); break;
	case ALLEGRO_KEY_L: duk_push_string(ctx, shift ? "L" : "l"); break;
	case ALLEGRO_KEY_M: duk_push_string(ctx, shift ? "M" : "m"); break;
	case ALLEGRO_KEY_N: duk_push_string(ctx, shift ? "N" : "n"); break;
	case ALLEGRO_KEY_O: duk_push_string(ctx, shift ? "O" : "o"); break;
	case ALLEGRO_KEY_P: duk_push_string(ctx, shift ? "P" : "p"); break;
	case ALLEGRO_KEY_Q: duk_push_string(ctx, shift ? "Q" : "q"); break;
	case ALLEGRO_KEY_R: duk_push_string(ctx, shift ? "R" : "r"); break;
	case ALLEGRO_KEY_S: duk_push_string(ctx, shift ? "S" : "s"); break;
	case ALLEGRO_KEY_T: duk_push_string(ctx, shift ? "T" : "t"); break;
	case ALLEGRO_KEY_U: duk_push_string(ctx, shift ? "U" : "u"); break;
	case ALLEGRO_KEY_V: duk_push_string(ctx, shift ? "V" : "v"); break;
	case ALLEGRO_KEY_W: duk_push_string(ctx, shift ? "W" : "w"); break;
	case ALLEGRO_KEY_X: duk_push_string(ctx, shift ? "X" : "x"); break;
	case ALLEGRO_KEY_Y: duk_push_string(ctx, shift ? "Y" : "y"); break;
	case ALLEGRO_KEY_Z: duk_push_string(ctx, shift ? "Z" : "z"); break;
	case ALLEGRO_KEY_1: duk_push_string(ctx, shift ? "!" : "1"); break;
	case ALLEGRO_KEY_2: duk_push_string(ctx, shift ? "@" : "2"); break;
	case ALLEGRO_KEY_3: duk_push_string(ctx, shift ? "#" : "3"); break;
	case ALLEGRO_KEY_4: duk_push_string(ctx, shift ? "$" : "4"); break;
	case ALLEGRO_KEY_5: duk_push_string(ctx, shift ? "%" : "5"); break;
	case ALLEGRO_KEY_6: duk_push_string(ctx, shift ? "^" : "6"); break;
	case ALLEGRO_KEY_7: duk_push_string(ctx, shift ? "&" : "7"); break;
	case ALLEGRO_KEY_8: duk_push_string(ctx, shift ? "*" : "8"); break;
	case ALLEGRO_KEY_9: duk_push_string(ctx, shift ? "(" : "9"); break;
	case ALLEGRO_KEY_0: duk_push_string(ctx, shift ? ")" : "0"); break;
	case ALLEGRO_KEY_BACKSLASH: duk_push_string(ctx, shift ? "|" : "\\"); break;
	case ALLEGRO_KEY_FULLSTOP: duk_push_string(ctx, shift ? ">" : "."); break;
	case ALLEGRO_KEY_CLOSEBRACE: duk_push_string(ctx, shift ? "}" : "]"); break;
	case ALLEGRO_KEY_COMMA: duk_push_string(ctx, shift ? "<" : ","); break;
	case ALLEGRO_KEY_EQUALS: duk_push_string(ctx, shift ? "+" : "="); break;
	case ALLEGRO_KEY_MINUS: duk_push_string(ctx, shift ? "_" : "-"); break;
	case ALLEGRO_KEY_QUOTE: duk_push_string(ctx, shift ? "\"" : "'"); break;
	case ALLEGRO_KEY_OPENBRACE: duk_push_string(ctx, shift ? "{" : "["); break;
	case ALLEGRO_KEY_SEMICOLON: duk_push_string(ctx, shift ? ":" : ";"); break;
	case ALLEGRO_KEY_SLASH: duk_push_string(ctx, shift ? "?" : "/"); break;
	case ALLEGRO_KEY_SPACE: duk_push_string(ctx, " "); break;
	case ALLEGRO_KEY_TAB: duk_push_string(ctx, "\t"); break;
	case ALLEGRO_KEY_TILDE: duk_push_string(ctx, shift ? "~" : "`"); break;
	default:
		duk_push_string(ctx, "");
	}
	return 1;
}

static duk_ret_t
js_GetMouseWheelEvent(duk_context* ctx)
{
	int event;

	int i;
	
	while (s_num_wheel_events == 0) {
		do_events();
	}
	event = s_wheel_queue[0];
	--s_num_wheel_events;
	for (i = 0; i < s_num_wheel_events; ++i) s_wheel_queue[i] = s_wheel_queue[i + 1];
	duk_push_int(ctx, event);
	return 1;
}

static duk_ret_t
js_GetMouseX(duk_context* ctx)
{
	ALLEGRO_MOUSE_STATE mouse_state;
	
	al_get_mouse_state(&mouse_state);
	duk_push_int(ctx, mouse_state.x / g_scale_x);
	return 1;
}

static duk_ret_t
js_GetMouseY(duk_context* ctx)
{
	ALLEGRO_MOUSE_STATE mouse_state;

	al_get_mouse_state(&mouse_state);
	duk_push_int(ctx, mouse_state.y / g_scale_y);
	return 1;
}

static duk_ret_t
js_GetNumJoysticks(duk_context* ctx)
{
	duk_push_int(ctx, s_num_joysticks);
	return 1;
}

static duk_ret_t
js_GetNumJoystickAxes(duk_context* ctx)
{
	int joy_index = duk_require_int(ctx, 0);
	
	duk_push_int(ctx, get_joy_axis_count(joy_index));
	return 1;
}

static duk_ret_t
js_GetNumJoystickButtons(duk_context* ctx)
{
	int joy_index = duk_require_int(ctx, 0);

	duk_push_int(ctx, get_joy_button_count(joy_index));
	return 1;
}

static duk_ret_t
js_GetNumMouseWheelEvents(duk_context* ctx)
{
	duk_push_int(ctx, s_num_wheel_events);
	return 1;
}

static duk_ret_t
js_GetPlayerKey(duk_context* ctx)
{
	int key_type;
	int player;

	player = duk_require_int(ctx, 0);
	key_type = duk_require_int(ctx, 1);
	switch (key_type) {
	case PLAYER_KEY_MENU: duk_push_int(ctx, ALLEGRO_KEY_ESCAPE); break;
	case PLAYER_KEY_UP: duk_push_int(ctx, ALLEGRO_KEY_UP); break;
	case PLAYER_KEY_DOWN: duk_push_int(ctx, ALLEGRO_KEY_DOWN); break;
	case PLAYER_KEY_LEFT: duk_push_int(ctx, ALLEGRO_KEY_LEFT); break;
	case PLAYER_KEY_RIGHT: duk_push_int(ctx, ALLEGRO_KEY_RIGHT); break;
	case PLAYER_KEY_A: duk_push_int(ctx, ALLEGRO_KEY_Z); break;
	case PLAYER_KEY_B: duk_push_int(ctx, ALLEGRO_KEY_X); break;
	case PLAYER_KEY_X: duk_push_int(ctx, ALLEGRO_KEY_C); break;
	case PLAYER_KEY_Y: duk_push_int(ctx, ALLEGRO_KEY_V); break;
	}
	return 1;
}

static duk_ret_t
js_GetToggleState(duk_context* ctx)
{
	duk_push_false(ctx);
	return 1;
}

static duk_ret_t
js_SetMousePosition(duk_context* ctx)
{
	int x = duk_require_int(ctx, 0);
	int y = duk_require_int(ctx, 1);
	
	al_set_mouse_xy(g_display, x * g_scale_x, y * g_scale_y);
	return 0;
}

static duk_ret_t
js_BindJoystickButton(duk_context* ctx)
{
	int joy_index = duk_require_int(ctx, 0);
	int button = duk_require_int(ctx, 1);
	script_t* on_down_script = duk_require_sphere_script(ctx, 2, "[button-down script]");
	script_t* on_up_script = duk_require_sphere_script(ctx, 3, "[button-up script]");

	if (joy_index < 0 || joy_index >= MAX_JOYSTICKS)
		duk_error_ni(ctx, -1, DUK_ERR_RANGE_ERROR, "BindJoystickButton(): Joystick index out of range (%i)", joy_index);
	if (button < 0 || button >= MAX_JOY_BUTTONS)
		duk_error_ni(ctx, -1, DUK_ERR_RANGE_ERROR, "BindJoystickButton(): Button index out of range (%i)", button);
	bind_button(s_bound_buttons, joy_index, button, on_down_script, on_up_script);
	return 0;
}

static duk_ret_t
js_BindKey(duk_context* ctx)
{
	int keycode = duk_require_int(ctx, 0);
	script_t* on_down_script = duk_require_sphere_script(ctx, 1, "[key-down script]");
	script_t* on_up_script = duk_require_sphere_script(ctx, 2, "[key-up script]");

	if (keycode < 0 || keycode >= ALLEGRO_KEY_MAX)
		duk_error_ni(ctx, -1, DUK_ERR_RANGE_ERROR, "BindKey(): Invalid key constant");
	bind_key(s_bound_map_keys, keycode, on_down_script, on_up_script);
	return 0;
}

static duk_ret_t
js_ClearKeyQueue(duk_context* ctx)
{
	clear_key_queue();
	return 0;
}

static duk_ret_t
js_UnbindJoystickButton(duk_context* ctx)
{
	int joy_index = duk_require_int(ctx, 0);
	int button = duk_require_int(ctx, 1);

	if (joy_index < 0 || joy_index >= MAX_JOYSTICKS)
		duk_error_ni(ctx, -1, DUK_ERR_RANGE_ERROR, "BindJoystickButton(): Joystick index out of range (%i)", joy_index);
	if (button < 0 || button >= MAX_JOY_BUTTONS)
		duk_error_ni(ctx, -1, DUK_ERR_RANGE_ERROR, "BindJoystickButton(): Button index out of range (%i)", button);
	bind_button(s_bound_buttons, joy_index, button, NULL, NULL);
	return 0;
}

static duk_ret_t
js_UnbindKey(duk_context* ctx)
{
	int keycode = duk_require_int(ctx, 0);

	if (keycode < 0 || keycode >= ALLEGRO_KEY_MAX)
		duk_error_ni(ctx, -1, DUK_ERR_RANGE_ERROR, "UnbindKey(): Invalid key constant");
	bind_key(s_bound_map_keys, keycode, NULL, NULL);
	return 0;
}
