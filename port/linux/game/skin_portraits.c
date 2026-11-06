/* port: ADD PLAYER's portraits. Each quarter's picture of its player (the
Xbox's Master Chief in their color) is their skin's body instead: the
biped's model, in an idle animation, in their color (or their Elite rank's),
lit by hand and drawn in the picture's box after the menus
(menu_functions.c's menu_functions_join4_picture notes it while the menus
are drawn; render.c's render_frame draws the notes). The menus' map has the
bodies and Master Chief's biped (skin_tags.c); a body it has not leaves the
stock picture. */

#include "cseries.h"
#include "math/real_math.h"
#include "models/model_animation_definitions.h"
#include "models/model_definitions.h"
#include "models/models.h"
#include "objects/object_definitions.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "render/render_cameras.h"
#include "tag_files/tag_groups.h"
#include <xtl.h>
#include "rasterizer/xbox/rasterizer_xbox.h"

#include <math.h>
#include <string.h>

enum
{
	/* the quarters' pictures */
	MAXIMUM_SKIN_PORTRAITS = 4,
	/* models.c's _render_model_no_planar_fog_bit */
	SKIN_PORTRAIT_NO_PLANAR_FOG = 1 << 2,
	/* an animation's frames a second */
	SKIN_PORTRAIT_FRAMES_PER_SECOND = 30,
	/* the rasterizer's _rasterizer_target_render_primary */
	SKIN_PORTRAIT_TARGET_PRIMARY = 0,
};

struct skin_portrait
{
	rectangle2d bounds;
	long biped;
	real_rgb_color color;
};

static struct
{
	/* the frame the notes are for (render.frame_index, as the menus drew) */
	long frame_index;
	short count;
	struct skin_portrait portraits[MAXIMUM_SKIN_PORTRAITS];
} skin_portraits;

/* the body's biped drawn in the box this frame, in the color; FALSE when
the map has not the biped, or its model or animations */
unsigned char skin_portraits_note(
	rectangle2d const *bounds,
	char const *biped_name,
	real_rgb_color const *color)
{
	struct object_definition *definition;
	struct skin_portrait *portrait;
	long biped;

	if (!biped_name || (biped = tag_loaded('bipd', biped_name)) == NONE)
		return FALSE;
	definition = object_definition_get(biped);
	if (definition->object.model.index == NONE || definition->object.animation_graph.index == NONE)
		return FALSE;
	if (skin_portraits.frame_index != render.frame_index)
	{
		skin_portraits.frame_index = render.frame_index;
		skin_portraits.count = 0;
	}
	if (skin_portraits.count >= MAXIMUM_SKIN_PORTRAITS)
		return FALSE;
	portrait = &skin_portraits.portraits[skin_portraits.count++];
	portrait->bounds = *bounds;
	portrait->biped = biped;
	portrait->color = *color;

	return TRUE;
}

/* the graph's standing idle with a frame for each of the model's nodes
(else any idle), or NULL */
static struct animation const *skin_portrait_animation(
	long animation_graph_index,
	struct model const *model)
{
	struct animation_graph *graph = animation_graph_definition_get(animation_graph_index);
	short pass;
	short index;

	for (pass = 0; pass < 2; pass++)
	{
		for (index = 0; index < graph->animations.count; index++)
		{
			struct animation const *animation =
				TAG_BLOCK_GET_ELEMENT(&graph->animations, index, struct animation);

			if (animation->node_count != model->nodes.count || animation->frame_count <= 0 ||
				!strstr(animation->name, "idle") || (pass == 0 && !strstr(animation->name, "stand")))
			{
				continue;
			}
			return animation;
		}
	}

	return NULL;
}

static void skin_portrait_render(
	struct skin_portrait const *portrait)
{
	struct object_definition *definition = object_definition_get(portrait->biped);
	long model_index = definition->object.model.index;
	struct model *model = model_definition_get(model_index);
	struct animation const *animation;
	real_matrix4x3 node_matrices[MAXIMUM_NODES_PER_MODEL];
	real_orientation orientations[MAXIMUM_NODES_PER_MODEL];
	real_rgb_color change_colors[4];
	real function_values[16];
	struct render_lighting lighting;
	struct render_camera camera;
	struct rasterizer_window_begin_parameters parameters;
	real_point3d origin = { 0.f, 0.f, 0.f };
	real_vector3d forward = { 1.f, 0.f, 0.f };
	real_vector3d up = { 0.f, 0.f, 1.f };
	real_point3d lowest, highest, center;
	real reach = 0.f;
	real width = (real)(portrait->bounds.x1 - portrait->bounds.x0);
	real height = (real)(portrait->bounds.y1 - portrait->bounds.y0);
	real tangent;
	real distance;
	real half_height;
	/* seen from the front and a little to its left (an idle stands turned
	to its right, its weapon side) */
	real yaw = -1.1f;
	short node_count = model->nodes.count;
	short index;

	if (node_count <= 0 || node_count > MAXIMUM_NODES_PER_MODEL || width <= 0.f || height <= 0.f)
		return;
	animation = skin_portrait_animation(definition->object.animation_graph.index, model);
	if (animation)
	{
		short frame = (short)((system_milliseconds() * SKIN_PORTRAIT_FRAMES_PER_SECOND / 1000) %
			(unsigned long)animation->frame_count);

		animation_get_node_orientations(model, animation, frame, orientations);
		model_node_matrices_from_orientations(model, node_matrices, orientations, &origin, &forward, &up);
	}
	else
	{
		model_get_node_matrices(model, node_matrices, &origin, &forward, &up);
	}

	/* (framed by its nodes: the feet's are at the ankles, the head's below
	its top) */
	lowest = highest = node_matrices[0].position;
	for (index = 0; index < node_count; index++)
	{
		real_point3d const *position = &node_matrices[index].position;
		real across = (real)sqrt(position->x * position->x + position->y * position->y);

		lowest.x = MIN(lowest.x, position->x);
		lowest.y = MIN(lowest.y, position->y);
		lowest.z = MIN(lowest.z, position->z);
		highest.x = MAX(highest.x, position->x);
		highest.y = MAX(highest.y, position->y);
		highest.z = MAX(highest.z, position->z);
		reach = MAX(reach, across);
	}
	half_height = (highest.z - lowest.z) * 0.5f;
	lowest.z -= half_height * 0.12f;
	highest.z += half_height * 0.2f;
	half_height = (highest.z - lowest.z) * 0.5f;
	center.x = (lowest.x + highest.x) * 0.5f;
	center.y = (lowest.y + highest.y) * 0.5f;
	center.z = (lowest.z + highest.z) * 0.5f;

	memset(&camera, 0, sizeof(camera));
	camera.vertical_field_of_view = 0.6f;
	tangent = (real)tan(camera.vertical_field_of_view * 0.5f);
	distance = MAX(half_height * 1.08f / tangent, reach * 1.1f / (tangent * width / height)) + reach * 0.5f;
	camera.forward.i = -(real)cos(yaw);
	camera.forward.j = -(real)sin(yaw);
	camera.forward.k = 0.f;
	camera.up.k = 1.f;
	camera.position.x = center.x - camera.forward.i * distance;
	camera.position.y = center.y - camera.forward.j * distance;
	camera.position.z = center.z;
	camera.viewport_bounds = portrait->bounds;
	camera.window_bounds = portrait->bounds;
	camera.z_near = 0.01f;
	camera.z_far = 100.f;

	memset(&parameters, 0, sizeof(parameters));
	parameters.camera = camera;
	render_camera_build_frustum(&parameters.camera, NULL, &parameters.frustum, TRUE);
	parameters.rasterizer_target = SKIN_PORTRAIT_TARGET_PRIMARY;
	/* (a window of its own: the transparent geometry and lights begun and
	ended with it) */
	parameters.window_index = 0;
	parameters.suppress_clear = TRUE;
	rasterizer_window_begin(&parameters);
	/* (the menus' 3D scene's depth, not its picture) */
	IDirect3DDevice8_Clear(global_d3d_device, 0, NULL, D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 1.0f, 0);
	render.camera = parameters.camera;
	render.frustum = parameters.frustum;
	memset(&render.fog, 0, sizeof(render.fog));

	change_colors[0] = portrait->color;
	/* (the others as a placed object's: white, or its first permutation's
	middle; objects.c's object_choose_random_change_colors) */
	for (index = 1; index < 4; index++)
	{
		change_colors[index].red = change_colors[index].green = change_colors[index].blue = 1.f;
		if (index < definition->object.change_colors.count)
		{
			struct object_change_color_definition const *change =
				TAG_BLOCK_GET_ELEMENT(&definition->object.change_colors, index, struct object_change_color_definition);

			if (change->permutations.count > 0)
			{
				struct object_change_color_permutation const *permutation =
					TAG_BLOCK_GET_ELEMENT(&change->permutations, 0, struct object_change_color_permutation);

				change_colors[index].red = (permutation->color_lower_bound.red + permutation->color_upper_bound.red) * 0.5f;
				change_colors[index].green =
					(permutation->color_lower_bound.green + permutation->color_upper_bound.green) * 0.5f;
				change_colors[index].blue =
					(permutation->color_lower_bound.blue + permutation->color_upper_bound.blue) * 0.5f;
			}
		}
	}
	memset(function_values, 0, sizeof(function_values));
	memset(&lighting, 0, sizeof(lighting));
	lighting.ambient_color.red = 0.4f;
	lighting.ambient_color.green = 0.4f;
	lighting.ambient_color.blue = 0.45f;
	lighting.distant_light_count = 2;
	lighting.distant_lights[0].color.red = 1.f;
	lighting.distant_lights[0].color.green = 0.96f;
	lighting.distant_lights[0].color.blue = 0.9f;
	lighting.distant_lights[0].direction.i = -(real)cos(yaw + 0.6f);
	lighting.distant_lights[0].direction.j = -(real)sin(yaw + 0.6f);
	lighting.distant_lights[0].direction.k = -0.7f;
	normalize3d(&lighting.distant_lights[0].direction);
	lighting.distant_lights[1].color.red = 0.25f;
	lighting.distant_lights[1].color.green = 0.3f;
	lighting.distant_lights[1].color.blue = 0.4f;
	lighting.distant_lights[1].direction.i = (real)cos(yaw - 0.8f);
	lighting.distant_lights[1].direction.j = (real)sin(yaw - 0.8f);
	lighting.distant_lights[1].direction.k = 0.2f;
	normalize3d(&lighting.distant_lights[1].direction);
	/* (a shiny surface is drawn toward its reflection, tinted by this: none
	is black) */
	lighting.reflection_tint_color.alpha = 0.75f;
	lighting.reflection_tint_color.red = 0.9f;
	lighting.reflection_tint_color.green = 0.9f;
	lighting.reflection_tint_color.blue = 0.9f;
	lighting.shadow_vector.k = -1.f;

	rasterizer_models_begin(TRUE);
	render_model(model_index, 10000.f, node_matrices, NULL, change_colors, function_values, &lighting, &center,
		half_height, NULL, 0, 0, SKIN_PORTRAIT_NO_PLANAR_FOG);
	rasterizer_models_end();
	/* (its transparent parts, as a view draws them: the first pass sorts) */
	rasterizer_transparent_geometry_draw(TRUE);
	rasterizer_transparent_geometry_draw(FALSE);
	rasterizer_transparent_geometry_stop();
	rasterizer_window_end();

	return;
}

/* (render.c's render_frame, after the views and their menus) */
void skin_portraits_render(
	void)
{
	struct render_camera saved_camera;
	struct render_frustum saved_frustum;
	struct render_fog saved_fog;
	struct rasterizer_window_begin_parameters saved_window;
	short index;

	if (skin_portraits.frame_index != render.frame_index || !skin_portraits.count)
		return;
	saved_camera = render.camera;
	saved_frustum = render.frustum;
	saved_fog = render.fog;
	saved_window = global_window_parameters;
	for (index = 0; index < skin_portraits.count; index++)
		skin_portrait_render(&skin_portraits.portraits[index]);
	render.camera = saved_camera;
	render.frustum = saved_frustum;
	render.fog = saved_fog;
	global_window_parameters = saved_window;
	skin_portraits.count = 0;

	return;
}
