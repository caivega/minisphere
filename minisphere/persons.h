#include "map_engine.h"
#include "spriteset.h"

typedef struct person person_t;

extern void        init_person_api         (void);
extern person_t*   create_person           (const char* name, const char* sprite_file, bool is_persistent);
extern void        destroy_person          (person_t* person);
extern bool        is_person_obstructed_at (const person_t* person, float x, float y, person_t** out_obstructing_person, int* out_tile_index);
extern rect_t      get_person_base         (const person_t* person);
extern const char* get_person_name         (const person_t* person);
extern float       get_person_speed        (const person_t* person);
extern void        get_person_xy           (const person_t* person, float* out_x, float* out_y, bool normalize);
extern void        get_person_xyz          (const person_t* person, float* out_x, float* out_y, int* out_layer, bool normalize);
extern bool        set_person_script       (person_t* person, int type, const lstring_t* script);
extern void        set_person_speed        (person_t* person, float speed);
extern void        set_person_xyz          (person_t* person, int x, int y, int layer);
extern bool        call_person_script      (const person_t* person, int type, bool use_default);
extern void        command_person          (person_t* person, int command);
extern person_t*   find_person             (const char* name);
extern void        reset_persons           (map_t* map, bool keep_existing);
extern void        render_persons          (int layer, int cam_x, int cam_y);
extern void        talk_person             (const person_t* person);
extern void        update_persons          (void);

enum person_cmd
{
	COMMAND_WAIT,
	COMMAND_ANIMATE,
	COMMAND_FACE_NORTH,
	COMMAND_FACE_NORTHEAST,
	COMMAND_FACE_EAST,
	COMMAND_FACE_SOUTHEAST,
	COMMAND_FACE_SOUTH,
	COMMAND_FACE_SOUTHWEST,
	COMMAND_FACE_WEST,
	COMMAND_FACE_NORTHWEST,
	COMMAND_MOVE_NORTH,
	COMMAND_MOVE_NORTHEAST,
	COMMAND_MOVE_EAST,
	COMMAND_MOVE_SOUTHEAST,
	COMMAND_MOVE_SOUTH,
	COMMAND_MOVE_SOUTHWEST,
	COMMAND_MOVE_WEST,
	COMMAND_MOVE_NORTHWEST
};

enum person_script_type
{
	PERSON_SCRIPT_ON_CREATE,
	PERSON_SCRIPT_ON_DESTROY,
	PERSON_SCRIPT_ON_TOUCH,
	PERSON_SCRIPT_ON_TALK,
	PERSON_SCRIPT_GENERATOR,
	PERSON_SCRIPT_MAX
};