#include "minisphere.h"
#include "api.h"
#include "color.h"
#include "vector.h"

#include "galileo.h"

static duk_ret_t js_GetDefaultShaderProgram (duk_context* ctx);
static duk_ret_t js_new_Group               (duk_context* ctx);
static duk_ret_t js_Group_finalize          (duk_context* ctx);
static duk_ret_t js_Group_get_angle         (duk_context* ctx);
static duk_ret_t js_Group_get_rotX          (duk_context* ctx);
static duk_ret_t js_Group_get_rotY          (duk_context* ctx);
static duk_ret_t js_Group_get_shader        (duk_context* ctx);
static duk_ret_t js_Group_get_x             (duk_context* ctx);
static duk_ret_t js_Group_get_y             (duk_context* ctx);
static duk_ret_t js_Group_set_angle         (duk_context* ctx);
static duk_ret_t js_Group_set_rotX          (duk_context* ctx);
static duk_ret_t js_Group_set_rotY          (duk_context* ctx);
static duk_ret_t js_Group_set_shader        (duk_context* ctx);
static duk_ret_t js_Group_set_x             (duk_context* ctx);
static duk_ret_t js_Group_set_y             (duk_context* ctx);
static duk_ret_t js_Group_draw              (duk_context* ctx);
static duk_ret_t js_new_ShaderProgram       (duk_context* ctx);
static duk_ret_t js_new_Shape               (duk_context* ctx);
static duk_ret_t js_Shape_finalize          (duk_context* ctx);
static duk_ret_t js_Shape_get_image         (duk_context* ctx);
static duk_ret_t js_Shape_set_image         (duk_context* ctx);
static duk_ret_t js_new_Vertex              (duk_context* ctx);

static void assign_default_uv  (shape_t* shape);
static void refresh_shape_vbuf (shape_t* shape);

struct shape
{
	unsigned int           refcount;
	image_t*               texture;
	shape_type_t           type;
	ALLEGRO_VERTEX*        sw_vbuf;
	ALLEGRO_VERTEX_BUFFER* vbuf;
	int                    max_vertices;
	int                    num_vertices;
	vertex_t               *vertices;
};

struct group
{
	unsigned int refcount;
	float        x, y, rot_x, rot_y;
	double       theta;
	vector_t*    shapes;
};

void
initialize_galileo(void)
{
	printf("Initializing Galileo\n");
}

void
shutdown_galileo(void)
{
	printf("Shutting down Galileo\n");
}

vertex_t
vertex(float x, float y, float u, float v, color_t color)
{
	vertex_t vertex;

	vertex.x = x; vertex.y = y;
	vertex.u = u; vertex.v = v;
	vertex.color = color;
	return vertex;
}

group_t*
new_group(void)
{
	group_t* group;

	if (!(group = calloc(1, sizeof(group_t))))
		goto on_error;
	group->shapes = new_vector(sizeof(shape_t*));
	return ref_group(group);

on_error:
	free(group);
	return NULL;
}

group_t*
ref_group(group_t* group)
{
	if (group != NULL)
		++group->refcount;
	return group;
}

void
free_group(group_t* group)
{
	shape_t** i_shape;
	
	iter_t iter;

	if (group == NULL || --group->refcount > 0)
		return;

	iter = iterate_vector(group->shapes);
	while (i_shape = next_vector_item(&iter))
		free_shape(*i_shape);
	free_vector(group->shapes);
	free(group);
}

shape_t*
get_group_shape(const group_t* group, int index)
{
	shape_t* shape;

	get_vector_item(group->shapes, index, &shape);
	return shape;
}

void
set_group_shape(group_t* group, int index, shape_t* shape)
{
	shape_t* old_shape;

	shape = ref_shape(shape);
	get_vector_item(group->shapes, index, &old_shape);
	set_vector_item(group->shapes, index, &shape);
	free_shape(old_shape);
}

bool
add_group_shape(group_t* group, shape_t* shape)
{
	shape = ref_shape(shape);
	push_back_vector(group->shapes, &shape);
	return true;
}

void
remove_group_shape(group_t* group, int index)
{
	remove_vector_item(group->shapes, index);
}

void
clear_group(group_t* group)
{
	shape_t** i_shape;
	
	iter_t iter;

	iter = iterate_vector(group->shapes);
	while (i_shape = next_vector_item(&iter))
		free_shape(*i_shape);
	clear_vector(group->shapes);
}

void
draw_group(const group_t* group)
{
	ALLEGRO_TRANSFORM matrix;
	ALLEGRO_TRANSFORM old_matrix;

	shape_t** i_shape;
	
	iter_t iter;

	al_copy_transform(&old_matrix, al_get_current_transform());
	al_identity_transform(&matrix);
	al_translate_transform(&matrix, group->rot_x, group->rot_y);
	al_rotate_transform(&matrix, group->theta);
	al_translate_transform(&matrix, group->x, group->y);
	al_scale_transform(&matrix, g_scale_x, g_scale_y);
	al_use_transform(&matrix);
	iter = iterate_vector(group->shapes);
	while (i_shape = next_vector_item(&iter))
		draw_shape(*i_shape);
	al_use_transform(&old_matrix);
}

shape_t*
new_shape(shape_type_t type, image_t* texture)
{
	shape_t* shape;
	
	if (!(shape = calloc(1, sizeof(shape_t))))
		goto on_error;
	shape->texture = ref_image(texture);
	shape->type = type;
	return ref_shape(shape);

on_error:
	free(shape);
	return NULL;
}

shape_t*
ref_shape(shape_t* shape)
{
	if (shape != NULL)
		++shape->refcount;
	return shape;
}

void
free_shape(shape_t* shape)
{
	if (shape == NULL || --shape->refcount > 0)
		return;
	free_image(shape->texture);
	if (shape->vbuf != NULL)
		al_destroy_vertex_buffer(shape->vbuf);
	free(shape->sw_vbuf);
	free(shape);
}

float_rect_t
get_shape_bounds(const shape_t* shape)
{
	float_rect_t bounds;

	int i;

	if (shape->num_vertices < 1)
		return new_float_rect(0.0, 0.0, 0.0, 0.0);
	bounds = new_float_rect(
		shape->vertices[0].x, shape->vertices[0].y,
		shape->vertices[0].x, shape->vertices[0].y);
	for (i = 1; i < shape->num_vertices; ++i) {
		bounds.x1 = fmin(shape->vertices[i].x, bounds.x1);
		bounds.y1 = fmin(shape->vertices[i].y, bounds.y1);
		bounds.x2 = fmax(shape->vertices[i].x, bounds.x2);
		bounds.y2 = fmax(shape->vertices[i].y, bounds.y2);
	}
	return bounds;
}

image_t*
get_shape_texture(const shape_t* shape)
{
	return shape->texture;
}

vertex_t
get_shape_vertex(shape_t* shape, int index)
{
	return shape->vertices[index];
}

void
set_shape_vertex(shape_t* shape, int index, vertex_t vertex)
{
	shape->vertices[index] = vertex;
}

void
set_shape_texture(shape_t* shape, image_t* texture)
{
	image_t* old_texture;
	
	old_texture = shape->texture;
	shape->texture = ref_image(texture);
	free_image(old_texture);
	refresh_shape_vbuf(shape);
}

bool
add_shape_vertex(shape_t* shape, vertex_t vertex)
{
	int      new_max;
	vertex_t *new_buffer;

	if (shape->num_vertices + 1 > shape->max_vertices) {
		new_max = (shape->num_vertices + 1) * 2;
		if (!(new_buffer = realloc(shape->vertices, new_max * sizeof(vertex_t))))
			return false;
		shape->vertices = new_buffer;
		shape->max_vertices = new_max;
	}
	++shape->num_vertices;
	shape->vertices[shape->num_vertices - 1] = vertex;
	return true;
}

void
remove_shape_vertex(shape_t* shape, int index)
{
	int i;

	--shape->num_vertices;
	for (i = index; i < shape->num_vertices; ++i)
		shape->vertices[i] = shape->vertices[i + 1];
}

void
draw_shape(const shape_t* shape)
{
	ALLEGRO_BITMAP* bitmap;
	int             draw_mode;

	if (shape->type == SHAPE_AUTO)
		draw_mode = shape->num_vertices == 1 ? ALLEGRO_PRIM_POINT_LIST
		: shape->num_vertices == 2 ? ALLEGRO_PRIM_LINE_LIST
		: shape->num_vertices == 4 ? ALLEGRO_PRIM_TRIANGLE_FAN
		: ALLEGRO_PRIM_TRIANGLE_STRIP;
	else
		draw_mode = shape->type == SHAPE_LINE_LIST ? ALLEGRO_PRIM_LINE_LIST
		: shape->type == SHAPE_TRIANGLE_LIST ? ALLEGRO_PRIM_TRIANGLE_LIST
		: shape->type == SHAPE_TRIANGLE_STRIP ? ALLEGRO_PRIM_TRIANGLE_STRIP
		: shape->type == SHAPE_TRIANGLE_FAN ? ALLEGRO_PRIM_TRIANGLE_FAN
		: ALLEGRO_PRIM_POINT_LIST;
	bitmap = shape->texture != NULL ? get_image_bitmap(shape->texture) : NULL;
	if (shape->vbuf != NULL)
		al_draw_vertex_buffer(shape->vbuf, bitmap, 0, shape->num_vertices, draw_mode);
	else
		al_draw_prim(shape->sw_vbuf, NULL, bitmap, 0, shape->num_vertices, draw_mode);
}

static void
assign_default_uv(shape_t* shape)
{
	// this assigns default UV coordinates to a shape's vertices. note that clockwise
	// winding from top left is assumed; if the shape is wound any other way, the
	// texture will be rotated accordingly. if this is not what you want, explicit U/V
	// coordinates should be supplied.
	
	double phi;

	int i;

	for (i = 0; i < shape->num_vertices; ++i) {
		// circumscribe the UV coordinate space.
		// the circumcircle is rotated 135 degrees counterclockwise, which ensures
		// that the top-left corner of a clockwise quad is mapped to (0,0).
		phi = 2 * M_PI * i / shape->num_vertices - M_PI_4 * 3;
		shape->vertices[i].u = cos(phi) * M_SQRT1_2 + 0.5;
		shape->vertices[i].v = sin(phi) * M_SQRT1_2 + 0.5;
	}
}

static void
refresh_shape_vbuf(shape_t* shape)
{
	ALLEGRO_BITMAP* bitmap;
	ALLEGRO_VERTEX* vertices = NULL;
	int             w_texture, h_texture;

	int i;
	
	if (shape->vbuf != NULL)
		al_destroy_vertex_buffer(shape->vbuf);
	free(shape->sw_vbuf); shape->sw_vbuf = NULL;
	bitmap = shape->texture != NULL ? get_image_bitmap(shape->texture) : NULL;
	w_texture = bitmap != NULL ? al_get_bitmap_width(bitmap) : 0;
	h_texture = bitmap != NULL ? al_get_bitmap_height(bitmap) : 0;
	if (shape->vbuf = al_create_vertex_buffer(NULL, NULL, shape->num_vertices, ALLEGRO_PRIM_BUFFER_STATIC))
		vertices = al_lock_vertex_buffer(shape->vbuf, 0, shape->num_vertices, ALLEGRO_LOCK_WRITEONLY);
	if (vertices == NULL) {
		if (!(shape->sw_vbuf = malloc(shape->num_vertices * sizeof(ALLEGRO_VERTEX))))
			return;
		vertices = shape->sw_vbuf;
	}
	for (i = 0; i < shape->num_vertices; ++i) {
		vertices[i].x = shape->vertices[i].x;
		vertices[i].y = shape->vertices[i].y;
		vertices[i].z = 0;
		vertices[i].color = nativecolor(shape->vertices[i].color);
		vertices[i].u = shape->vertices[i].u * w_texture;
		vertices[i].v = shape->vertices[i].v * h_texture;
	}
	if (vertices != shape->sw_vbuf)
		al_unlock_vertex_buffer(shape->vbuf);
	else if (shape->vbuf != NULL)
		al_destroy_vertex_buffer(shape->vbuf);
}

void
init_galileo_api(void)
{
	// Galileo core API constants
	register_api_const(g_duk, "SHAPE_AUTO", SHAPE_AUTO);
	register_api_const(g_duk, "SHAPE_POINT_LIST", SHAPE_POINT_LIST);
	register_api_const(g_duk, "SHAPE_LINE_LIST", SHAPE_LINE_LIST);
	register_api_const(g_duk, "SHAPE_TRIANGLE_LIST", SHAPE_TRIANGLE_LIST);
	register_api_const(g_duk, "SHAPE_TRIANGLE_STRIP", SHAPE_TRIANGLE_STRIP);
	register_api_const(g_duk, "SHAPE_TRIANGLE_FAN", SHAPE_TRIANGLE_FAN);

	// Vertex object
	register_api_ctor(g_duk, "Vertex", js_new_Vertex, NULL);
	
	// Shape object
	register_api_ctor(g_duk, "Shape", js_new_Shape, js_Shape_finalize);
	register_api_prop(g_duk, "Shape", "image", js_Shape_get_image, js_Shape_set_image);

	// ShaderProgram object
	register_api_function(g_duk, NULL, "GetDefaultShaderProgram", js_GetDefaultShaderProgram);
	register_api_ctor(g_duk, "ShaderProgram", js_new_ShaderProgram, NULL);
	
	// Group object
	register_api_ctor(g_duk, "Group", js_new_Group, js_Group_finalize);
	register_api_prop(g_duk, "Group", "angle", js_Group_get_angle, js_Group_set_angle);
	register_api_prop(g_duk, "Group", "rotX", js_Group_get_rotX, js_Group_set_rotX);
	register_api_prop(g_duk, "Group", "rotY", js_Group_get_rotY, js_Group_set_rotY);
	register_api_prop(g_duk, "Group", "shader", js_Group_get_shader, js_Group_set_shader);
	register_api_prop(g_duk, "Group", "x", js_Group_get_x, js_Group_set_x);
	register_api_prop(g_duk, "Group", "y", js_Group_get_y, js_Group_set_y);
	register_api_function(g_duk, "Group", "draw", js_Group_draw);
}

static duk_ret_t
js_new_Group(duk_context* ctx)
{
	duk_require_object_coercible(ctx, 0);
	if (!duk_is_array(ctx, 0))
		duk_error_ni(ctx, -1, DUK_ERR_TYPE_ERROR, "Shape(): First argument must be an array");
	duk_require_sphere_obj(ctx, 1, "ShaderProgram");

	size_t    num_shapes;
	group_t*  group;
	shape_t*  shape;

	duk_uarridx_t i;

	if (!(group = new_group()))
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "Group(): Failed to create group object");
	num_shapes = duk_get_length(ctx, 0);
	for (i = 0; i < num_shapes; ++i) {
		duk_get_prop_index(ctx, 0, i);
		shape = duk_require_sphere_obj(ctx, -1, "Shape");
		if (!add_group_shape(group, shape))
			duk_error_ni(ctx, -1, DUK_ERR_ERROR, "Group(): Shape list allocation failure");
	}
	duk_push_sphere_obj(ctx, "Group", group);
	return 1;
}

static duk_ret_t
js_Group_finalize(duk_context* ctx)
{
	group_t* group;

	group = duk_require_sphere_obj(ctx, 0, "Group");
	free_group(group);
	return 0;
}

static
js_Group_get_angle(duk_context* ctx)
{
	group_t* group;

	duk_push_this(ctx);
	group = duk_require_sphere_obj(ctx, -1, "Group");
	duk_pop(ctx);
	duk_push_number(ctx, group->theta);
	return 1;
}

static
js_Group_set_angle(duk_context* ctx)
{
	group_t* group;
	double theta = duk_require_number(ctx, 0);

	duk_push_this(ctx);
	group = duk_require_sphere_obj(ctx, -1, "Group");
	duk_pop(ctx);
	group->theta = theta;
	return 0;
}

static duk_ret_t
js_Group_get_shader(duk_context* ctx)
{
	duk_push_sphere_obj(ctx, "ShaderProgram", NULL);
	return 1;
}

static duk_ret_t
js_Group_set_shader(duk_context* ctx)
{
	return 0;
}

static
js_Group_get_rotX(duk_context* ctx)
{
	group_t* group;

	duk_push_this(ctx);
	group = duk_require_sphere_obj(ctx, -1, "Group");
	duk_pop(ctx);
	duk_push_number(ctx, group->rot_x);
	return 1;
}

static
js_Group_set_rotX(duk_context* ctx)
{
	group_t* group;
	double value = duk_require_number(ctx, 0);

	duk_push_this(ctx);
	group = duk_require_sphere_obj(ctx, -1, "Group");
	duk_pop(ctx);
	group->rot_x = value;
	return 0;
}

static
js_Group_get_rotY(duk_context* ctx)
{
	group_t* group;

	duk_push_this(ctx);
	group = duk_require_sphere_obj(ctx, -1, "Group");
	duk_pop(ctx);
	duk_push_number(ctx, group->rot_y);
	return 1;
}

static
js_Group_set_rotY(duk_context* ctx)
{
	group_t* group;
	double value = duk_require_number(ctx, 0);

	duk_push_this(ctx);
	group = duk_require_sphere_obj(ctx, -1, "Group");
	duk_pop(ctx);
	group->rot_y = value;
	return 0;
}

static
js_Group_get_x(duk_context* ctx)
{
	group_t* group;

	duk_push_this(ctx);
	group = duk_require_sphere_obj(ctx, -1, "Group");
	duk_pop(ctx);
	duk_push_number(ctx, group->x);
	return 1;
}

static
js_Group_set_x(duk_context* ctx)
{
	group_t* group;
	double value = duk_require_number(ctx, 0);

	duk_push_this(ctx);
	group = duk_require_sphere_obj(ctx, -1, "Group");
	duk_pop(ctx);
	group->x = value;
	return 0;
}

static
js_Group_get_y(duk_context* ctx)
{
	group_t* group;

	duk_push_this(ctx);
	group = duk_require_sphere_obj(ctx, -1, "Group");
	duk_pop(ctx);
	duk_push_number(ctx, group->y);
	return 1;
}

static
js_Group_set_y(duk_context* ctx)
{
	group_t* group;
	double value = duk_require_number(ctx, 0);

	duk_push_this(ctx);
	group = duk_require_sphere_obj(ctx, -1, "Group");
	duk_pop(ctx);
	group->y = value;
	return 0;
}

static duk_ret_t
js_Group_draw(duk_context* ctx)
{
	group_t* group;

	duk_push_this(ctx);
	group = duk_require_sphere_obj(ctx, -1, "Group");
	duk_pop(ctx);
	draw_group(group);
	return 0;
}

static duk_ret_t
js_GetDefaultShaderProgram(duk_context* ctx)
{
	duk_push_sphere_obj(ctx, "ShaderProgram", NULL);
	return 1;
}

static duk_ret_t
js_new_ShaderProgram(duk_context* ctx)
{
	return 0;
}

static duk_ret_t
js_new_Shape(duk_context* ctx)
{
	int n_args = duk_get_top(ctx);
	duk_require_object_coercible(ctx, 0);
	if (!duk_is_array(ctx, 0))
		duk_error_ni(ctx, -1, DUK_ERR_TYPE_ERROR, "Shape(): First argument must be an array");
	image_t* texture = duk_is_null(ctx, 1) ? NULL : duk_require_sphere_obj(ctx, 1, "Image");
	shape_type_t type = n_args >= 3 ? duk_require_int(ctx, 2) : SHAPE_AUTO;

	bool      is_missing_uv = false;
	size_t    num_vertices;
	shape_t*  shape;
	duk_idx_t stack_idx;
	vertex_t  vertex;

	duk_uarridx_t i;

	if (type < 0 || type >= SHAPE_MAX)
		duk_error_ni(ctx, -1, DUK_ERR_RANGE_ERROR, "Shape(): Invalid shape type constant");
	if (!(shape = new_shape(type, texture)))
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "Shape(): Failed to create shape object");
	num_vertices = duk_get_length(ctx, 0);
	for (i = 0; i < num_vertices; ++i) {
		duk_get_prop_index(ctx, 0, i); stack_idx = duk_normalize_index(ctx, -1);
		vertex.x = duk_get_prop_string(ctx, stack_idx, "x") ? duk_require_number(ctx, -1) : 0.0f;
		vertex.y = duk_get_prop_string(ctx, stack_idx, "y") ? duk_require_number(ctx, -1) : 0.0f;
		if (duk_get_prop_string(ctx, stack_idx, "u"))
			vertex.u = duk_require_number(ctx, -1);
		else
			is_missing_uv = true;
		if (duk_get_prop_string(ctx, stack_idx, "v"))
			vertex.v = duk_require_number(ctx, -1);
		else
			is_missing_uv = true;
		vertex.color = duk_get_prop_string(ctx, stack_idx, "color")
			? duk_require_sphere_color(ctx, -1)
			: rgba(255, 255, 255, 255);
		duk_pop_n(ctx, 6);
		if (!add_shape_vertex(shape, vertex))
			duk_error_ni(ctx, -1, DUK_ERR_ERROR, "Shape(): Vertex list allocation failure");
	}
	if (is_missing_uv)
		assign_default_uv(shape);
	refresh_shape_vbuf(shape);
	duk_push_sphere_obj(ctx, "Shape", shape);
	return 1;
}

static duk_ret_t
js_Shape_finalize(duk_context* ctx)
{
	shape_t* shape;

	shape = duk_require_sphere_obj(ctx, 0, "Shape");
	free_shape(shape);
	return 0;
}

static duk_ret_t
js_Shape_get_image(duk_context* ctx)
{
	shape_t* shape;

	duk_push_this(ctx);
	shape = duk_require_sphere_obj(ctx, -1, "Shape");
	duk_pop(ctx);
	duk_push_sphere_obj(ctx, "Shape", ref_image(get_shape_texture(shape)));
	return 1;
}

static duk_ret_t
js_Shape_set_image(duk_context* ctx)
{
	shape_t* shape;
	image_t* texture = duk_require_sphere_obj(ctx, 0, "Image");

	duk_push_this(ctx);
	shape = duk_require_sphere_obj(ctx, -1, "Shape");
	duk_pop(ctx);
	set_shape_texture(shape, texture);
	return 0;
}

static duk_ret_t
js_new_Vertex(duk_context* ctx)
{
	int n_args = duk_get_top(ctx);
	bool has_color = n_args >= 3;
	bool has_uv = n_args >= 4;
	float x = duk_require_number(ctx, 0);
	float y = duk_require_number(ctx, 1);
	color_t color = has_color ? duk_require_sphere_color(ctx, 2) : rgba(255, 255, 255, 255);
	float u = has_uv ? duk_require_number(ctx, 3) : 0.0;
	float v = has_uv ? duk_require_number(ctx, 4) : 0.0;
	
	duk_push_this(ctx);
	duk_push_number(ctx, x); duk_put_prop_string(ctx, -2, "x");
	duk_push_number(ctx, y); duk_put_prop_string(ctx, -2, "y");
	if (has_color) {
		duk_push_sphere_color(ctx, color);
		duk_put_prop_string(ctx, -2, "color");
	}
	if (has_uv) {
		duk_push_number(ctx, u);
		duk_push_number(ctx, v);
		duk_put_prop_string(ctx, -3, "v");
		duk_put_prop_string(ctx, -2, "u");
	}
	duk_pop(ctx);
	return 0;
}
