/*
SKIN_TAGS.C

The tags of the bodies a player may play as besides the Master Chief
(player_skins.c): the Elites', the Flood combat forms' and the Hunter's.
Only the campaign's levels have them; The Maw (c40.map) has every one.
When a stock multiplayer map loads (scenario_tags_load), before its tags are
checked, their tags are copied from The Maw's into the tag cache after the
map's own, with every tag they need that the map has not got (a tag of the
same group and name as one of the map's is the map's), and the map's tag
table is copied after them with theirs added. The map's tag data then ends
after them, below its structure bsps, so a bsp's load does not clear them,
and tag_validate_tags checks them with the map's.

Their pointers are moved to where they are now by the tags' schemas
(tag_schema.h), which name every block, data, reference and tag index of a
group; a tag of a group without a schema, and the AI's tags (actor variants,
actors, dialogue), are not copied, and what names them names none. A model's
parts draw from vertex and index buffers in the map's tag header, which a
copied model's cannot be in: they are copied to buffers of their own after
the tags, which the check (tag_validate_set_extra_buffers) and the
rasterizer (skin_tags_loaded) take as the map's.

The bitmaps' pixels and the sounds' samples of the copied tags are in The
Maw's file, which is compressed: what of it they need is inflated once into
the skins' cache file (player_skins.cache in the saves' folder) with The
Maw's tag data, and read from there (skin_tags_read). The cache file is made
again when The Maw's checksum or this file's version (SKIN_PACK_VERSION)
changes.

Every machine of a network game adds the same tags at the same indices,
whatever their players chose: they are made only of the map's tags and The
Maw's, which every Xbox disc has. A machine without The Maw adds none, and
plays every player as the Master Chief (a host's Elite is no biped there,
and is not made: distributed_client_change_valid).
*/

#include "cseries.h"
#include "cache/cache_files.h"
#include "cache/physical_memory_map.h"
#include "scenario/scenario_definitions.h"
#include "bitmaps/bitmap_group.h"
#include "game/game_globals.h"
#include "units/unit_definitions.h"
#include "rasterizer/rasterizer_geometry.h"
#include "tag_files/tag_groups.h"

#include "tag_schema.h"
#include "player_skins.h"

#include "../../third_party/zlib/zlib_prefixed.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xtl.h>

/* the platform layer's (port/linux/src) */
void platform_log(char const *format, ...);
const char *platform_save_root(void);
void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size);

#ifdef HALO_ANDROID
void xgpu_static_buffers_add(const void *buffers, long count, unsigned long stride, int indices, const void *end);
void xgpu_static_buffers_remove(const void *buffers, long count, unsigned long stride, int indices);
#endif

/* ---------- constants */

enum
{
	/* the address a cache file's tags are linked to (the tag cache's) */
	SKIN_LINK_ADDRESS = 0x803A6000,
	CACHE_HEADER_BYTES = 0x800,
	CACHE_HEADER_SIGNATURE = 'head',
	CACHE_HEADER_VERSION = 5,
	TAG_HEADER_SIGNATURE = 'tags',
	/* a cache file's header type (cache_file_header's reserved60) */
	_cache_file_type_multiplayer = 1,
	/* a vertex or index buffer's header (a D3D resource: Common, Data, Lock) */
	BUFFER_SIZE = 12,
	MAXIMUM_WALK_DEPTH = 24,
	MAXIMUM_SKIN_TAGS = 4096,
	MAXIMUM_SKIN_RANGES = 8192,
	/* file ranges this close are read as one */
	RANGE_MERGE_GAP = 0x1000,
	INFLATE_IN_BYTES = 0x10000,
	INFLATE_OUT_BYTES = 0x40000,

	SKIN_PACK_VERSION = 1,

	REMAP_UNSEEN = -2,
	REMAP_PENDING = -3,
};

#define SKIN_DONOR_MAP "c40"
#define SKIN_PACK_FILE "player_skins.cache"
#define SKIN_PACK_MAGIC "OCESKINS"

/* the groups not copied: the AI's, and a level's own */
static unsigned long const skin_excluded_groups[] =
{
	'actv', 'actr', 'udlg', 'scnr', 'sbsp', 'matg', 'DeLa', 'Soul', 'ustr', 'hmt ', 'font', 0
};

/* the bipeds copied (player_skins.c's skins' bodies), whose tags bring the
rest (a Hunter's: its fuel rod cannon) */
static char const *const skin_root_bipeds[] =
{
	"characters\\elite\\elite",
	"characters\\elite\\elite special",
	"characters\\floodcombat_human\\floodcombat_human",
	"characters\\floodcombat elite\\floodcombat elite",
	"characters\\hunter\\hunter",
	NULL
};

/* rasterizer_geometry.c's rasterizer_vertex_type_sizes */
static short const skin_vertex_type_sizes[] =
{
	56, 32, 20, 8, 68, 32, 24, 36, 20, 16, 16, 8
};

/* ---------- structures */

/* cache_files.c's cache_file_tag_instance and cache_file_tag_header, with
addresses as numbers (The Maw's are not this process's) */
struct skin_instance
{
	unsigned long group_tag;
	unsigned long parent_group_tags[2];
	long tag_index;
	unsigned long name;
	unsigned long base_address;
	unsigned long unused[2];
};

struct skin_tag_header
{
	unsigned long instances;
	long scenario_tag_index;
	unsigned long checksum;
	long tag_count;
	long vertex_buffer_count;
	unsigned long vertex_buffers;
	long index_buffer_count;
	unsigned long index_buffers;
	unsigned long signature;
};

typedef char verify_skin_instance_size[sizeof(struct skin_instance) == 0x20 ? 1 : -1];
typedef char verify_skin_tag_header_size[sizeof(struct skin_tag_header) == 0x24 ? 1 : -1];

/* tag_schema_models.c's model_geometry_part */
struct skin_model_part
{
	unsigned long flags;
	short shader_index;
	char previous_part_index;
	char next_part_index;
	short centroid_primary_node_index;
	short centroid_secondary_node_index;
	real centroid_primary_node_weight;
	real centroid_secondary_node_weight;
	real_point3d centroid;
	struct tag_block uncompressed_vertices;
	struct tag_block compressed_vertices;
	struct tag_block triangles;
	struct triangle_buffer triangle_buffer;
	struct vertex_buffer vertex_buffer;
};

typedef char verify_skin_model_part_size[sizeof(struct skin_model_part) == 0x68 ? 1 : -1];

/* bytes of The Maw's file, in the skins' cache file */
struct skin_range
{
	unsigned long offset;
	unsigned long size;
	unsigned long pack_offset;
};

struct skin_pack_header
{
	char magic[8];
	long version;
	unsigned long checksum;
	long file_length;
	long tag_data_offset;
	long tag_data_size;
	long range_count;
	long range_table_offset;
	long tag_data_pack_offset;
};

/* The Maw's tags, read into memory */
struct skin_donor
{
	byte *data;
	unsigned long size;
	struct skin_tag_header *header;
	struct skin_instance *instances;
	long count;
};

struct skin_import
{
	struct skin_donor donor;
	/* each of The Maw's tags (by absolute index): what its references are
	now (REMAP_UNSEEN, REMAP_PENDING while it is gathered, NONE, the map's
	tag's index or its copy's) */
	long *remap;
	/* The Maw's tags copied, in order */
	long order[MAXIMUM_SKIN_TAGS];
	long order_count;
	/* the map's tags (NULL while the skins' cache file is made: every tag
	is copied) */
	struct skin_instance *map_instances;
	long map_count;
	byte *map_data;
	unsigned long map_size;
	/* where the copies go */
	byte *out;
	unsigned long out_used;
	unsigned long out_limit;
	/* the file ranges of the tags gathered (the cache file's making) */
	struct skin_range *ranges;
	long range_count;
	/* the copied models' parts, and their buffers */
	long part_count;
	byte *vertex_headers;
	byte *index_headers;
	long vertex_header_count;
	long index_header_count;
	boolean copying;
	char const *failure;
};

static struct
{
	boolean imported;
	/* what the map's tags were as read (skin_tags_import_undo) */
	unsigned long original_instances;
	long original_count;
	long original_size;
	/* the copies' absolute indices */
	long first_index;
	long count;
	/* the copied models' buffers */
	byte *vertex_headers;
	byte *index_headers;
	long vertex_header_count;
	long index_header_count;
	boolean registered;
	/* the skins' cache file, open while its tags are */
	FILE *pack;
	struct skin_range *ranges;
	long range_count;
	long donor_file_length;
} skin_tags_globals;

/* ---------- private code */

static struct tag_schema_group const *skin_schema_group(
	unsigned long group_tag)
{
	struct tag_schema_group const *const *list;

	for (list = tag_schema_group_lists; *list; list++)
	{
		struct tag_schema_group const *group;

		for (group = *list; group->group_tag; group++)
		{
			if (group->group_tag == group_tag)
				return group;
		}
	}

	return NULL;
}

static boolean skin_group_excluded(
	unsigned long group_tag)
{
	long index;

	for (index = 0; skin_excluded_groups[index]; index++)
	{
		if (skin_excluded_groups[index] == group_tag)
			return TRUE;
	}

	return FALSE;
}

/* size bytes of The Maw's tags at its address, or NULL if they are not all
in them */
static byte *donor_get(
	struct skin_donor const *donor,
	unsigned long address,
	unsigned long size)
{
	unsigned long offset = address - SKIN_LINK_ADDRESS;

	if (address < SKIN_LINK_ADDRESS || offset > donor->size || size > donor->size - offset)
		return NULL;

	return donor->data + offset;
}

/* a string of The Maw's tags, or NULL if it does not end in them */
static char const *donor_string(
	struct skin_donor const *donor,
	unsigned long address)
{
	byte *bytes = donor_get(donor, address, 1);

	if (!bytes || !memchr(bytes, 0, donor->size - (address - SKIN_LINK_ADDRESS)))
		return NULL;

	return (char const *)bytes;
}

/* a tag's name in the map's tags read (not checked yet), or NULL */
static char const *map_name(
	struct skin_import const *import,
	long absolute_index)
{
	unsigned long address = import->map_instances[absolute_index].name;
	unsigned long offset = address - SKIN_LINK_ADDRESS;

	if (address < SKIN_LINK_ADDRESS || offset >= import->map_size ||
		!memchr(import->map_data + offset, 0, import->map_size - offset))
	{
		return NULL;
	}

	return (char const *)(import->map_data + offset);
}

static long map_tag_find(
	struct skin_import const *import,
	unsigned long group_tag,
	char const *name)
{
	long index;

	for (index = 0; index < import->map_count; index++)
	{
		struct skin_instance const *instance = &import->map_instances[index];
		char const *other;

		if (instance->group_tag != group_tag || !instance->base_address)
			continue;
		other = map_name(import, index);
		if (other && !_stricmp(other, name))
			return instance->tag_index;
	}

	return NONE;
}

static long donor_tag_find(
	struct skin_donor const *donor,
	unsigned long group_tag,
	char const *name)
{
	long index;

	for (index = 0; index < donor->count; index++)
	{
		char const *other;

		if (donor->instances[index].group_tag != group_tag)
			continue;
		other = donor_string(donor, donor->instances[index].name);
		if (other && !_stricmp(other, name))
			return index;
	}

	return NONE;
}

static void import_fail(
	struct skin_import *import,
	char const *why)
{
	if (!import->failure)
		import->failure = why;

	return;
}

/* the index a reference to one of The Maw's tags has now; while gathering,
a tag not seen yet is looked up in the map, else gathered */
static long import_reference(
	struct skin_import *import,
	long tag_index)
{
	long absolute_index = tag_index & 0xFFFF;
	struct skin_instance const *instance;
	long result;

	if (tag_index == NONE || absolute_index >= import->donor.count)
		return NONE;
	instance = &import->donor.instances[absolute_index];
	if (instance->tag_index != tag_index)
		return NONE;
	result = import->remap[absolute_index];
	if (result != REMAP_UNSEEN)
		return result == REMAP_PENDING ? tag_index : result;
	if (import->copying)
		return NONE;

	{
		struct tag_schema_group const *group = skin_schema_group(instance->group_tag);
		char const *name = donor_string(&import->donor, instance->name);

		if (!group || !group->definition || !instance->base_address || !name ||
			skin_group_excluded(instance->group_tag))
		{
			result = NONE;
		}
		else if (import->map_instances && (result = map_tag_find(import, instance->group_tag, name)) != NONE)
		{
			/* (the map's own) */
		}
		else if (import->order_count >= MAXIMUM_SKIN_TAGS)
		{
			import_fail(import, "too many tags");
			result = NONE;
		}
		else
		{
			import->order[import->order_count++] = absolute_index;
			import->remap[absolute_index] = REMAP_PENDING;
			return tag_index;
		}
	}
	import->remap[absolute_index] = result;

	return result;
}

static void import_range_add(
	struct skin_import *import,
	long offset,
	long size)
{
	if (!import->ranges || offset < 0 || size <= 0)
		return;
	if (import->range_count >= MAXIMUM_SKIN_RANGES)
	{
		import_fail(import, "too many file ranges");
		return;
	}
	import->ranges[import->range_count].offset = (unsigned long)offset;
	import->ranges[import->range_count].size = (unsigned long)size;
	import->ranges[import->range_count].pack_offset = 0;
	import->range_count++;

	return;
}

/* size bytes of the copies' space, aligned to alignment */
static byte *import_allocate(
	struct skin_import *import,
	unsigned long size,
	unsigned long alignment)
{
	unsigned long start = (import->out_used + alignment - 1) & ~(alignment - 1);

	if (start > import->out_limit || size > import->out_limit - start)
	{
		import_fail(import, "no room in the tag cache");
		return NULL;
	}
	import->out_used = start + size;

	return import->out + start;
}

/* a copied part's vertex or index buffer: its header after the copies'
others, and its bytes copied */
static void *import_buffer(
	struct skin_import *import,
	unsigned long header_address,
	unsigned long array_address,
	long array_count,
	unsigned long data_size,
	boolean vertices)
{
	byte *header = donor_get(&import->donor, header_address, BUFFER_SIZE);
	byte *copy_header;
	byte *data;
	byte *copy;
	unsigned long data_address;

	if (!header || header_address < array_address || (header_address - array_address) % BUFFER_SIZE ||
		(header_address - array_address) / BUFFER_SIZE >= (unsigned long)array_count)
	{
		return NULL;
	}
	memcpy(&data_address, header + 4, sizeof(data_address));
	data = donor_get(&import->donor, data_address, data_size);
	if (!data)
		return NULL;
	if (vertices)
	{
		if (import->vertex_header_count >= import->part_count)
			return NULL;
		copy_header = import->vertex_headers + import->vertex_header_count++ * BUFFER_SIZE;
	}
	else
	{
		if (import->index_header_count >= import->part_count)
			return NULL;
		copy_header = import->index_headers + import->index_header_count++ * BUFFER_SIZE;
	}
	copy = import_allocate(import, data_size ? data_size : 4, 16);
	if (!copy)
		return NULL;
	memcpy(copy_header, header, BUFFER_SIZE);
	memcpy(copy, data, data_size);
	data_address = (unsigned long)copy;
	memcpy(copy_header + 4, &data_address, sizeof(data_address));

	return copy_header;
}

static void import_part_buffers(
	struct skin_import *import,
	struct skin_model_part const *part,
	struct skin_model_part *copy)
{
	struct skin_tag_header const *header = import->donor.header;
	long vertex_size = part->vertex_buffer.type >= 0 &&
		part->vertex_buffer.type < (short)NUMBER_OF_ELEMENTS(skin_vertex_type_sizes) ?
		skin_vertex_type_sizes[part->vertex_buffer.type] : 0;
	long index_count = part->triangle_buffer.type == _triangle_buffer_type_triangles ? 3 * part->triangle_buffer.count :
		part->triangle_buffer.count > 0 ? part->triangle_buffer.count + 2 : 0;

	if (!copy)
	{
		import->part_count++;
		return;
	}
	copy->vertex_buffer.hardware_format = NULL;
	copy->triangle_buffer.hardware_format = NULL;
	if (!part->vertex_buffer.hardware_format || !part->triangle_buffer.hardware_format || !vertex_size ||
		part->vertex_buffer.count < 0 || part->vertex_buffer.count > 0x10000 || index_count < 0 ||
		index_count > 0x30000)
	{
		return;
	}
	copy->vertex_buffer.hardware_format = import_buffer(import, (unsigned long)part->vertex_buffer.hardware_format,
		header->vertex_buffers, header->vertex_buffer_count, (unsigned long)(part->vertex_buffer.count * vertex_size),
		TRUE);
	copy->triangle_buffer.hardware_format = import_buffer(import,
		(unsigned long)part->triangle_buffer.hardware_format, header->index_buffers, header->index_buffer_count,
		(unsigned long)(index_count * sizeof(word)), FALSE);
	if (!copy->vertex_buffer.hardware_format || !copy->triangle_buffer.hardware_format)
	{
		copy->vertex_buffer.hardware_format = NULL;
		copy->triangle_buffer.hardware_format = NULL;
	}

	return;
}

/* a bitmap's pixels, in The Maw's file (the cache file's making) */
static void import_bitmap_ranges(
	struct skin_import *import,
	byte const *root)
{
	struct bitmap_group const *group = (struct bitmap_group const *)root;
	long count = group->bitmaps.count;
	byte *bitmaps = count > 0 && count < 0x10000 ?
		donor_get(&import->donor, (unsigned long)group->bitmaps.address, (unsigned long)count * sizeof(struct bitmap_data)) :
		NULL;
	long index;

	for (index = 0; bitmaps && index < count; index++)
	{
		struct bitmap_data const *bitmap = (struct bitmap_data const *)(bitmaps + index * sizeof(struct bitmap_data));

		import_range_add(import, group->pixel_data.file_offset + bitmap->pixels_offset, bitmap->pixels_size);
	}

	return;
}

static void import_names(
	struct skin_import *import,
	long tag_index,
	struct tag_reference *copy);

/* an element of The Maw's tags (source) and its copy (copy, or NULL while
gathering), by its definition: every block and data of it within The Maw's
tags, its blocks' elements walked, and copied, and its references and tag
indices the copies' */
static void import_element(
	struct skin_import *import,
	byte const *source,
	byte *copy,
	struct tag_schema_definition const *definition,
	short depth)
{
	struct tag_schema_field const *field;

	if (depth > MAXIMUM_WALK_DEPTH)
	{
		import_fail(import, "tags too deep");
		return;
	}
	for (field = definition->fields; field->type != _tag_schema_terminator && !import->failure; field++)
	{
		short index;

		for (index = 0; index < field->count && !import->failure; index++)
		{
			long offset = field->offset + index * field->size;
			byte const *address = source + offset;
			byte *target = copy ? copy + offset : NULL;

			switch (field->type)
			{
			case _tag_schema_struct:
				import_element(import, address, target, field->definition, depth);
				break;

			case _tag_schema_block:
			{
				struct tag_block const *block = (struct tag_block const *)address;
				struct tag_schema_definition const *element_definition = field->definition;
				long count = block->count;
				byte *elements;
				byte *copies = NULL;
				long element_index;

				if (count < 0)
				{
					import_fail(import, "a block's count is negative");
					break;
				}
				if (field->maximum > 0 && count > field->maximum)
					count = field->maximum;
				if (target)
				{
					((struct tag_block *)target)->count = count;
					((struct tag_block *)target)->address = NULL;
					((struct tag_block *)target)->definition = NULL;
				}
				if (!count)
					break;
				elements = (unsigned long)count <= 0x7FFFFFFF / (unsigned long)element_definition->size ?
					donor_get(&import->donor, (unsigned long)block->address, (unsigned long)count * element_definition->size) :
					NULL;
				if (!elements)
				{
					import_fail(import, "a block is outside the tags");
					break;
				}
				if (target)
				{
					copies = import_allocate(import, (unsigned long)count * element_definition->size, 4);
					if (!copies)
						break;
					memcpy(copies, elements, (unsigned long)count * element_definition->size);
					((struct tag_block *)target)->address = copies;
				}
				for (element_index = 0; element_index < count && !import->failure; element_index++)
				{
					import_element(import, elements + element_index * element_definition->size,
						copies ? copies + element_index * element_definition->size : NULL, element_definition, depth + 1);
				}
				break;
			}

			case _tag_schema_data:
			{
				struct tag_data const *data = (struct tag_data const *)address;
				long size = data->size;
				byte *bytes;

				if (size < 0)
				{
					import_fail(import, "a data's size is negative");
					break;
				}
				if (field->maximum > 0 && size > field->maximum)
					size = field->maximum;
				if (target)
				{
					((struct tag_data *)target)->size = size;
					((struct tag_data *)target)->address = NULL;
					((struct tag_data *)target)->definition = NULL;
				}
				if (!size)
					break;
				bytes = donor_get(&import->donor, (unsigned long)data->address, (unsigned long)size);
				if (!bytes)
				{
					import_fail(import, "a data is outside the tags");
					break;
				}
				if (target)
				{
					byte *bytes_copy = import_allocate(import, (unsigned long)size, 4);

					if (!bytes_copy)
						break;
					memcpy(bytes_copy, bytes, (unsigned long)size);
					((struct tag_data *)target)->address = bytes_copy;
				}
				break;
			}

			case _tag_schema_file_data:
			{
				struct tag_data const *data = (struct tag_data const *)address;

				if (target)
				{
					((struct tag_data *)target)->address = NULL;
					((struct tag_data *)target)->definition = NULL;
				}
				else
				{
					import_range_add(import, data->file_offset, data->size);
				}
				break;
			}

			case _tag_schema_reference:
			{
				struct tag_reference const *reference = (struct tag_reference const *)address;
				long tag_index = import_reference(import, reference->index);

				if (target)
					import_names(import, tag_index, (struct tag_reference *)target);
				break;
			}

			case _tag_schema_tag_index:
			{
				long tag_index;

				memcpy(&tag_index, address, sizeof(tag_index));
				tag_index = import_reference(import, tag_index);
				if (target)
					memcpy(target, &tag_index, sizeof(tag_index));
				break;
			}

			default:
				break;
			}
		}
	}

	if (!import->failure && definition->name && !strcmp(definition->name, "model_geometry_part"))
		import_part_buffers(import, (struct skin_model_part const *)source, (struct skin_model_part *)copy);

	return;
}

/* the instance table the copies are added to, as it is being built */
static struct skin_instance *import_instances;
static char const empty_name[] = "";

/* a copied reference's index and name: its tag's */
static void import_names(
	struct skin_import *import,
	long tag_index,
	struct tag_reference *copy)
{
	long absolute_index = tag_index & 0xFFFF;

	copy->index = tag_index;
	copy->name = (char *)empty_name;
	copy->name_length = 0;
	if (tag_index != NONE && import_instances && absolute_index < import->map_count + import->order_count)
	{
		copy->name = (char *)import_instances[absolute_index].name;
		copy->name_length = (long)strlen(copy->name);
		copy->group_tag = import_instances[absolute_index].group_tag;
	}

	return;
}

/* every tag the roots need, gathered in order (import->order) */
static void import_gather(
	struct skin_import *import)
{
	long index;
	long next;

	for (index = 0; index < import->donor.count; index++)
		import->remap[index] = REMAP_UNSEEN;
	for (index = 0; skin_root_bipeds[index] && !import->failure; index++)
	{
		long root = donor_tag_find(&import->donor, 'bipd', skin_root_bipeds[index]);

		if (root == NONE)
		{
			import_fail(import, "The Maw has not got a skin's biped");
			break;
		}
		import_reference(import, import->donor.instances[root].tag_index);
	}
	for (next = 0; next < import->order_count && !import->failure; next++)
	{
		struct skin_instance const *instance = &import->donor.instances[import->order[next]];
		struct tag_schema_group const *group = skin_schema_group(instance->group_tag);
		byte *root = donor_get(&import->donor, instance->base_address, (unsigned long)group->definition->size);

		if (!root)
		{
			import_fail(import, "a tag is outside the tags");
			break;
		}
		import_element(import, root, NULL, group->definition, 0);
		if (instance->group_tag == 'bitm' && import->ranges)
			import_bitmap_ranges(import, root);
	}

	return;
}

/* ---------- The Maw's file, and the skins' cache file */

static int range_compare(
	void const *a,
	void const *b)
{
	unsigned long first = ((struct skin_range const *)a)->offset;
	unsigned long second = ((struct skin_range const *)b)->offset;

	return first < second ? -1 : first > second ? 1 : 0;
}

/* the ranges in order, those that overlap or nearly meet made one */
static long ranges_merge(
	struct skin_range *ranges,
	long count)
{
	long merged = 0;
	long index;

	if (!count)
		return 0;
	qsort(ranges, (size_t)count, sizeof(*ranges), range_compare);
	for (index = 1; index < count; index++)
	{
		struct skin_range *last = &ranges[merged];
		unsigned long end = last->offset + last->size;

		if (ranges[index].offset <= end + RANGE_MERGE_GAP)
		{
			unsigned long other_end = ranges[index].offset + ranges[index].size;

			if (other_end > end)
				last->size = other_end - last->offset;
		}
		else
		{
			ranges[++merged] = ranges[index];
		}
	}

	return merged + 1;
}

/* the bytes of The Maw's file in ranges (in order, apart), written to the
cache file at their pack offsets: inflated from its compressed body, or
read from it if it is not compressed */
static boolean donor_copy_ranges(
	FILE *raw,
	long raw_length,
	long file_length,
	struct skin_range const *ranges,
	long range_count,
	FILE *pack)
{
	byte *in;
	byte *out;
	z_stream stream;
	unsigned long position = CACHE_HEADER_BYTES;
	long range_index = 0;
	boolean success = FALSE;
	int result = Z_OK;

	in = malloc(INFLATE_IN_BYTES);
	out = malloc(INFLATE_OUT_BYTES);
	if (!in || !out)
	{
		free(in);
		free(out);
		return FALSE;
	}

	/* (a file as long as its header says is not compressed) */
	if (raw_length >= file_length)
	{
		for (range_index = 0; range_index < range_count; range_index++)
		{
			unsigned long done = 0;

			while (done < ranges[range_index].size)
			{
				unsigned long chunk = ranges[range_index].size - done;

				if (chunk > INFLATE_OUT_BYTES)
					chunk = INFLATE_OUT_BYTES;
				if (fseek(raw, (long)(ranges[range_index].offset + done), SEEK_SET) ||
					fread(out, 1, chunk, raw) != chunk ||
					fseek(pack, (long)(ranges[range_index].pack_offset + done), SEEK_SET) ||
					fwrite(out, 1, chunk, pack) != chunk)
				{
					goto done;
				}
				done += chunk;
			}
		}
		success = TRUE;
		goto done;
	}

	memset(&stream, 0, sizeof(stream));
	if (inflateInit(&stream) != Z_OK || fseek(raw, CACHE_HEADER_BYTES, SEEK_SET))
		goto done;
	while (range_index < range_count && result != Z_STREAM_END)
	{
		unsigned long produced;
		unsigned long chunk_start = position;

		if (!stream.avail_in)
		{
			size_t read = fread(in, 1, INFLATE_IN_BYTES, raw);

			if (!read)
				break;
			stream.next_in = in;
			stream.avail_in = (uInt)read;
		}
		stream.next_out = out;
		stream.avail_out = INFLATE_OUT_BYTES;
		result = inflate(&stream, Z_NO_FLUSH);
		if (result != Z_OK && result != Z_STREAM_END)
			break;
		produced = INFLATE_OUT_BYTES - stream.avail_out;
		position += produced;
		/* (the ranges this chunk has bytes of) */
		while (range_index < range_count)
		{
			struct skin_range const *range = &ranges[range_index];
			unsigned long start = range->offset > chunk_start ? range->offset : chunk_start;
			unsigned long end = range->offset + range->size < position ? range->offset + range->size : position;

			if (start < end)
			{
				if (fseek(pack, (long)(range->pack_offset + (start - range->offset)), SEEK_SET) ||
					fwrite(out + (start - chunk_start), 1, end - start, pack) != end - start)
				{
					inflateEnd(&stream);
					goto done;
				}
			}
			if (range->offset + range->size <= position)
				range_index++;
			else
				break;
		}
	}
	inflateEnd(&stream);
	success = range_index >= range_count;

done:
	free(in);
	free(out);

	return success;
}

/* The Maw's map file's header, from its file (raw_length bytes long) */
static boolean donor_header_read(
	FILE *raw,
	byte header[CACHE_HEADER_BYTES],
	long *raw_length)
{
	long signature;
	long version;
	long file_length;
	long tag_data_offset;
	long tag_data_size;

	if (fseek(raw, 0, SEEK_END) || (*raw_length = ftell(raw)) < CACHE_HEADER_BYTES ||
		fseek(raw, 0, SEEK_SET) || fread(header, 1, CACHE_HEADER_BYTES, raw) != CACHE_HEADER_BYTES)
	{
		return FALSE;
	}
	memcpy(&signature, header, 4);
	memcpy(&version, header + 4, 4);
	memcpy(&file_length, header + 8, 4);
	memcpy(&tag_data_offset, header + 0x10, 4);
	memcpy(&tag_data_size, header + 0x14, 4);

	return signature == CACHE_HEADER_SIGNATURE && version == CACHE_HEADER_VERSION &&
		file_length > CACHE_HEADER_BYTES && tag_data_size > (long)sizeof(struct skin_tag_header) &&
		tag_data_size <= TAG_CACHE_SIZE && tag_data_offset >= CACHE_HEADER_BYTES &&
		tag_data_offset <= file_length - tag_data_size;
}

/* The Maw's tags read (from the cache file) into donor, its header and
table checked */
static boolean donor_load(
	struct skin_donor *donor,
	FILE *pack,
	struct skin_pack_header const *header)
{
	struct skin_tag_header *tags;

	memset(donor, 0, sizeof(*donor));
	donor->size = (unsigned long)header->tag_data_size;
	donor->data = malloc(donor->size);
	if (!donor->data || fseek(pack, header->tag_data_pack_offset, SEEK_SET) ||
		fread(donor->data, 1, donor->size, pack) != donor->size)
	{
		free(donor->data);
		donor->data = NULL;
		return FALSE;
	}
	tags = (struct skin_tag_header *)donor->data;
	donor->header = tags;
	if (tags->signature != TAG_HEADER_SIGNATURE || tags->tag_count <= 0 || tags->tag_count > 0xFFFF)
		return FALSE;
	donor->instances = (struct skin_instance *)donor_get(donor, tags->instances,
		(unsigned long)tags->tag_count * sizeof(struct skin_instance));
	donor->count = tags->tag_count;
	if (!donor->instances || tags->vertex_buffer_count < 0 || tags->index_buffer_count < 0)
		return FALSE;

	return TRUE;
}

static void pack_path(
	char *path,
	size_t size,
	char const *suffix)
{
	snprintf(path, size, "%s/%s%s", platform_save_root(), SKIN_PACK_FILE, suffix);

	return;
}

/* the skins' cache file made from The Maw's file (whose header is
donor_header): its tag data, and the bytes the skins' tags read of the rest */
static boolean pack_make(
	FILE *raw,
	long raw_length,
	byte const *donor_header,
	struct skin_pack_header *header)
{
	char path[512];
	char temporary_path[520];
	FILE *pack;
	struct skin_range tag_range;
	struct skin_import *import = NULL;
	boolean success = FALSE;
	long index;
	unsigned long pack_offset;

	pack_path(path, sizeof(path), "");
	pack_path(temporary_path, sizeof(temporary_path), ".new");
	pack = fopen(temporary_path, "w+b");
	if (!pack)
		return FALSE;

	memset(header, 0, sizeof(*header));
	memcpy(header->magic, "unmade..", 8);
	header->version = SKIN_PACK_VERSION;
	memcpy(&header->checksum, donor_header + 0x64, 4);
	memcpy(&header->file_length, donor_header + 8, 4);
	memcpy(&header->tag_data_offset, donor_header + 0x10, 4);
	memcpy(&header->tag_data_size, donor_header + 0x14, 4);
	header->tag_data_pack_offset = sizeof(*header);
	if (fwrite(header, sizeof(*header), 1, pack) != 1)
		goto done;

	/* the tag data, first */
	tag_range.offset = (unsigned long)header->tag_data_offset;
	tag_range.size = (unsigned long)header->tag_data_size;
	tag_range.pack_offset = (unsigned long)header->tag_data_pack_offset;
	if (!donor_copy_ranges(raw, raw_length, header->file_length, &tag_range, 1, pack))
		goto done;

	/* then what the skins' tags read of the rest */
	import = malloc(sizeof(*import));
	if (!import)
		goto done;
	memset(import, 0, sizeof(*import));
	if (!donor_load(&import->donor, pack, header))
		goto done;
	import->remap = malloc((size_t)import->donor.count * sizeof(long));
	import->ranges = malloc(MAXIMUM_SKIN_RANGES * sizeof(struct skin_range));
	if (!import->remap || !import->ranges)
		goto done;
	import_gather(import);
	if (import->failure)
	{
		platform_log("player skins: The Maw's tags cannot be used: %s\n", import->failure);
		goto done;
	}
	header->range_count = ranges_merge(import->ranges, import->range_count);
	pack_offset = (unsigned long)(header->tag_data_pack_offset + header->tag_data_size);
	for (index = 0; index < header->range_count; index++)
	{
		struct skin_range *range = &import->ranges[index];

		if (range->offset >= (unsigned long)header->file_length)
		{
			range->size = 0;
		}
		else if (range->size > (unsigned long)header->file_length - range->offset)
		{
			range->size = (unsigned long)header->file_length - range->offset;
		}
		range->pack_offset = pack_offset;
		pack_offset += range->size;
	}
	if (!donor_copy_ranges(raw, raw_length, header->file_length, import->ranges, header->range_count, pack))
		goto done;
	header->range_table_offset = (long)pack_offset;
	memcpy(header->magic, SKIN_PACK_MAGIC, 8);
	if (fseek(pack, header->range_table_offset, SEEK_SET) ||
		fwrite(import->ranges, sizeof(struct skin_range), (size_t)header->range_count, pack) !=
			(size_t)header->range_count ||
		fseek(pack, 0, SEEK_SET) || fwrite(header, sizeof(*header), 1, pack) != 1 || fflush(pack))
	{
		goto done;
	}
	platform_log("player skins: made %s (%ld tags, %ld file ranges, %lu bytes)\n", path, import->order_count,
		header->range_count, pack_offset + header->range_count * (unsigned long)sizeof(struct skin_range));
	success = TRUE;

done:
	if (import)
	{
		free(import->donor.data);
		free(import->remap);
		free(import->ranges);
		free(import);
	}
	fclose(pack);
	if (success)
	{
		remove(path);
		success = rename(temporary_path, path) == 0;
	}
	if (!success)
		remove(temporary_path);

	return success;
}

/* the skins' cache file, open, made (again) if it is not The Maw's of now;
NULL if The Maw cannot be read */
static FILE *pack_open(
	struct skin_pack_header *header)
{
	char donor_xbox_path[256];
	char donor_path[512];
	char path[512];
	byte donor_header[CACHE_HEADER_BYTES];
	long raw_length;
	FILE *raw;
	FILE *pack;
	unsigned long checksum;

	snprintf(donor_xbox_path, sizeof(donor_xbox_path), "%s%s.map", cache_files_map_directory(), SKIN_DONOR_MAP);
	platform_translate_path(donor_xbox_path, donor_path, sizeof(donor_path));
	raw = fopen(donor_path, "rb");
	if (!raw)
	{
		platform_log("player skins: no %s.map, so no Elite, Flood or Hunter (%s)\n", SKIN_DONOR_MAP, donor_path);
		return NULL;
	}
	if (!donor_header_read(raw, donor_header, &raw_length))
	{
		platform_log("player skins: %s is not a map this game plays\n", donor_path);
		fclose(raw);
		return NULL;
	}
	memcpy(&checksum, donor_header + 0x64, 4);

	pack_path(path, sizeof(path), "");
	pack = fopen(path, "rb");
	if (pack)
	{
		if (fread(header, sizeof(*header), 1, pack) == 1 && !memcmp(header->magic, SKIN_PACK_MAGIC, 8) &&
			header->version == SKIN_PACK_VERSION && header->checksum == checksum &&
			!memcmp(&header->file_length, donor_header + 8, 4) &&
			!memcmp(&header->tag_data_offset, donor_header + 0x10, 4) &&
			!memcmp(&header->tag_data_size, donor_header + 0x14, 4) &&
			header->range_count >= 0 && header->range_count <= MAXIMUM_SKIN_RANGES &&
			header->tag_data_pack_offset == (long)sizeof(*header))
		{
			fclose(raw);
			return pack;
		}
		fclose(pack);
	}

	platform_log("player skins: making %s from %s\n", path, donor_path);
	if (!pack_make(raw, raw_length, donor_header, header))
	{
		platform_log("player skins: could not make %s\n", path);
		fclose(raw);
		return NULL;
	}
	fclose(raw);
	pack = fopen(path, "rb");
	if (pack && fread(header, sizeof(*header), 1, pack) != 1)
	{
		fclose(pack);
		pack = NULL;
	}

	return pack;
}

/* the lowest address a structure bsp of the map's loads at (the end of the
tag cache if none): the copies end below it */
static unsigned long map_structure_bsp_limit(
	struct skin_import const *import,
	struct skin_tag_header const *header)
{
	unsigned long limit = SKIN_LINK_ADDRESS + TAG_CACHE_SIZE;
	long absolute_index = header->scenario_tag_index & 0xFFFF;
	unsigned long scenario_address;
	unsigned long offset;
	struct scenario const *scenario;
	long index;

	if (absolute_index >= import->map_count)
		return 0;
	scenario_address = import->map_instances[absolute_index].base_address;
	offset = scenario_address - SKIN_LINK_ADDRESS;
	if (scenario_address < SKIN_LINK_ADDRESS || offset > import->map_size ||
		sizeof(struct scenario) > import->map_size - offset)
	{
		return 0;
	}
	scenario = (struct scenario const *)(import->map_data + offset);
	if (scenario->structure_bsp_references.count < 0 || scenario->structure_bsp_references.count > 0x100)
		return 0;
	for (index = 0; index < scenario->structure_bsp_references.count; index++)
	{
		unsigned long address = (unsigned long)scenario->structure_bsp_references.address +
			index * sizeof(struct scenario_structure_bsp_reference);
		struct scenario_structure_bsp_reference const *reference;

		offset = address - SKIN_LINK_ADDRESS;
		if (address < SKIN_LINK_ADDRESS || offset > import->map_size ||
			sizeof(*reference) > import->map_size - offset)
		{
			return 0;
		}
		reference = (struct scenario_structure_bsp_reference const *)(import->map_data + offset);
		if ((unsigned long)reference->base_address < limit)
			limit = (unsigned long)reference->base_address;
	}

	return limit;
}

/* ---------- public code */

boolean skin_tags_import(
	void *tag_header,
	long *tag_data_size,
	short header_type,
	char const *map_name_text)
{
	struct skin_tag_header *header = tag_header;
	struct skin_pack_header pack_header;
	struct skin_import *import = NULL;
	FILE *pack = NULL;
	unsigned long start;
	unsigned long limit;
	long index;
	boolean success = FALSE;

	/* (the last map's buffers were in the tags this map's replaced:
	scenario_tags_unload deregistered them) */
	if (skin_tags_globals.pack)
		fclose(skin_tags_globals.pack);
	free(skin_tags_globals.ranges);
	memset(&skin_tags_globals, 0, sizeof(skin_tags_globals));
	tag_validate_set_extra_buffers(NULL, 0, NULL, 0);
	if (header_type != _cache_file_type_multiplayer || !map_name_text || !_stricmp(map_name_text, SKIN_DONOR_MAP))
		return FALSE;
	/* (the header is not checked yet: its table must be in the tags) */
	if (*tag_data_size < (long)sizeof(*header) || *tag_data_size > TAG_CACHE_SIZE ||
		header->signature != TAG_HEADER_SIGNATURE || header->tag_count <= 0 || header->tag_count > 0xFFFF ||
		header->instances < SKIN_LINK_ADDRESS || header->instances - SKIN_LINK_ADDRESS > (unsigned long)*tag_data_size ||
		(unsigned long)header->tag_count * sizeof(struct skin_instance) >
			(unsigned long)*tag_data_size - (header->instances - SKIN_LINK_ADDRESS))
	{
		return FALSE;
	}

	import = malloc(sizeof(*import));
	if (!import)
		return FALSE;
	memset(import, 0, sizeof(*import));
	import->map_data = (byte *)tag_header;
	import->map_size = (unsigned long)*tag_data_size;
	import->map_count = header->tag_count;
	import->map_instances = (struct skin_instance *)(import->map_data + (header->instances - SKIN_LINK_ADDRESS));

	/* (a map that has the skins' bipeds plays its own) */
	if (map_tag_find(import, 'bipd', skin_root_bipeds[0]) != NONE)
		goto done;

	start = (SKIN_LINK_ADDRESS + import->map_size + 15) & ~15UL;
	limit = map_structure_bsp_limit(import, header);
	if (limit <= start)
	{
		platform_log("player skins: no room in %s's tag cache\n", map_name_text);
		goto done;
	}

	pack = pack_open(&pack_header);
	if (!pack)
		goto done;
	if (!donor_load(&import->donor, pack, &pack_header))
	{
		platform_log("player skins: the skins' cache file cannot be read\n");
		goto done;
	}
	import->remap = malloc((size_t)import->donor.count * sizeof(long));
	if (!import->remap)
		goto done;

	/* the tags to copy, and their models' parts */
	import_gather(import);
	if (import->failure)
	{
		platform_log("player skins: The Maw's tags cannot be used: %s\n", import->failure);
		goto done;
	}

	/* where they go: the table, the buffers' headers, then the tags */
	import->out = (byte *)start;
	import->out_limit = limit - start;
	{
		long new_count = import->map_count + import->order_count;
		long first_salt = 0;
		struct skin_instance *instances;

		if (new_count > 0xFFFF)
		{
			platform_log("player skins: too many tags\n");
			goto done;
		}
		instances = (struct skin_instance *)import_allocate(import, (unsigned long)new_count * sizeof(*instances), 4);
		import->vertex_headers = import_allocate(import, (unsigned long)(import->part_count + 1) * BUFFER_SIZE, 4);
		import->index_headers = import_allocate(import, (unsigned long)(import->part_count + 1) * BUFFER_SIZE, 4);
		if (import->failure)
		{
			platform_log("player skins: %s\n", import->failure);
			goto done;
		}
		memcpy(instances, import->map_instances, (size_t)import->map_count * sizeof(*instances));
		for (index = 0; index < import->map_count; index++)
		{
			long salt = (unsigned long)instances[index].tag_index >> 16;

			if (salt > first_salt)
				first_salt = salt;
		}
		first_salt++;
		for (index = 0; index < import->order_count; index++)
		{
			struct skin_instance const *source = &import->donor.instances[import->order[index]];
			struct skin_instance *instance = &instances[import->map_count + index];
			struct tag_schema_group const *group = skin_schema_group(source->group_tag);
			char const *name = donor_string(&import->donor, source->name);
			char *name_copy = import_allocate(import, (unsigned long)strlen(name) + 1, 1);

			if (!name_copy)
				break;
			strcpy(name_copy, name);
			memset(instance, 0, sizeof(*instance));
			instance->group_tag = source->group_tag;
			instance->parent_group_tags[0] = group->parent_group_tags[0];
			instance->parent_group_tags[1] = group->parent_group_tags[1];
			instance->tag_index = (long)((((unsigned long)(first_salt + index) & 0xFFFF) << 16) |
				(unsigned long)(import->map_count + index));
			instance->name = (unsigned long)name_copy;
			import->remap[import->order[index]] = instance->tag_index;
		}
		import_instances = instances;
		import->copying = TRUE;

		/* the tags, copied */
		for (index = 0; index < import->order_count && !import->failure; index++)
		{
			struct skin_instance const *source = &import->donor.instances[import->order[index]];
			struct tag_schema_group const *group = skin_schema_group(source->group_tag);
			byte *root = donor_get(&import->donor, source->base_address, (unsigned long)group->definition->size);
			byte *copy = import_allocate(import, (unsigned long)group->definition->size, 4);

			if (!root || !copy)
			{
				import_fail(import, "a tag cannot be copied");
				break;
			}
			memcpy(copy, root, (unsigned long)group->definition->size);
			import_element(import, root, copy, group->definition, 0);
			instances[import->map_count + index].base_address = (unsigned long)copy;
		}
		import_instances = NULL;
		if (import->failure)
		{
			platform_log("player skins: The Maw's tags could not be copied: %s\n", import->failure);
			goto done;
		}

		skin_tags_globals.original_instances = header->instances;
		skin_tags_globals.original_count = header->tag_count;
		skin_tags_globals.original_size = *tag_data_size;
		skin_tags_globals.first_index = import->map_count;
		skin_tags_globals.count = import->order_count;
		skin_tags_globals.vertex_headers = import->vertex_headers;
		skin_tags_globals.index_headers = import->index_headers;
		skin_tags_globals.vertex_header_count = import->vertex_header_count;
		skin_tags_globals.index_header_count = import->index_header_count;
		header->instances = (unsigned long)instances;
		header->tag_count = new_count;
		*tag_data_size = (long)((start + import->out_used + 15) & ~15UL) - SKIN_LINK_ADDRESS;
	}

	/* their bitmaps' and sounds' bytes, from the cache file */
	skin_tags_globals.ranges = malloc((size_t)(pack_header.range_count + 1) * sizeof(struct skin_range));
	if (!skin_tags_globals.ranges || fseek(pack, pack_header.range_table_offset, SEEK_SET) ||
		fread(skin_tags_globals.ranges, sizeof(struct skin_range), (size_t)pack_header.range_count, pack) !=
			(size_t)pack_header.range_count)
	{
		platform_log("player skins: the skins' cache file cannot be read\n");
		skin_tags_import_undo(tag_header, tag_data_size);
		goto done;
	}
	skin_tags_globals.range_count = pack_header.range_count;
	skin_tags_globals.donor_file_length = pack_header.file_length;
	skin_tags_globals.pack = pack;
	pack = NULL;
	skin_tags_globals.imported = TRUE;
	tag_validate_set_extra_buffers(skin_tags_globals.vertex_headers, skin_tags_globals.vertex_header_count,
		skin_tags_globals.index_headers, skin_tags_globals.index_header_count);
	platform_log("player skins: %ld tags of The Maw's added to %s (%lu bytes, %ld models' parts)\n",
		skin_tags_globals.count, map_name_text, import->out_used, import->part_count);
	success = TRUE;

done:
	if (pack)
		fclose(pack);
	if (import)
	{
		free(import->donor.data);
		free(import->remap);
		free(import);
	}

	return success;
}

void skin_tags_import_undo(
	void *tag_header,
	long *tag_data_size)
{
	struct skin_tag_header *header = tag_header;

	if (!skin_tags_globals.original_count)
		return;
	header->instances = skin_tags_globals.original_instances;
	header->tag_count = skin_tags_globals.original_count;
	*tag_data_size = skin_tags_globals.original_size;
	skin_tags_unloaded();

	return;
}

long skin_tags_file_length(
	long file_length)
{
	return skin_tags_globals.imported && skin_tags_globals.donor_file_length > file_length ?
		skin_tags_globals.donor_file_length :
		file_length;
}

void skin_tags_loaded(
	void)
{
	struct skin_tag_header buffers;

	if (!skin_tags_globals.imported || skin_tags_globals.registered)
		return;
	/* (registered as the map's are: tags_header_register_vertex_and_index_buffers
	reads only the buffers of the header it is given) */
	memset(&buffers, 0, sizeof(buffers));
	buffers.vertex_buffer_count = skin_tags_globals.vertex_header_count;
	buffers.vertex_buffers = (unsigned long)skin_tags_globals.vertex_headers;
	buffers.index_buffer_count = skin_tags_globals.index_header_count;
	buffers.index_buffers = (unsigned long)skin_tags_globals.index_headers;
	buffers.signature = TAG_HEADER_SIGNATURE;
	tags_header_register_vertex_and_index_buffers((struct cache_file_tag_header *)&buffers);
#ifdef HALO_ANDROID
	xgpu_static_buffers_add(skin_tags_globals.vertex_headers, skin_tags_globals.vertex_header_count, BUFFER_SIZE, 0,
		(byte const *)SKIN_LINK_ADDRESS + TAG_CACHE_SIZE);
	xgpu_static_buffers_add(skin_tags_globals.index_headers, skin_tags_globals.index_header_count, BUFFER_SIZE, 1,
		(byte const *)SKIN_LINK_ADDRESS + TAG_CACHE_SIZE);
#endif
	skin_tags_globals.registered = TRUE;
	tag_validate_set_extra_buffers(NULL, 0, NULL, 0);

	/* the copied bipeds' HUDs: the multiplayer unit's (the campaign's
	characters have none, and a player's meters are its unit's HUD's) */
	{
		long globals_index = tag_loaded('matg', "globals\\globals");
		struct game_globals *globals = globals_index != NONE ? tag_get('matg', globals_index) : NULL;
		struct game_globals_multiplayer_information *multiplayer = globals && globals->multiplayer_information.count > 0 ?
			TAG_BLOCK_GET_ELEMENT(&globals->multiplayer_information, 0, struct game_globals_multiplayer_information) :
			NULL;
		struct unit_definition *player_unit = multiplayer && multiplayer->unit.index != NONE ?
			tag_get('unit', multiplayer->unit.index) : NULL;
		long index;

		for (index = 0; player_unit && index < skin_tags_globals.count; index++)
		{
			struct cache_file_tag_instance *instance = &global_tag_instances[skin_tags_globals.first_index + index];

			if (instance->group_tag == 'bipd')
			{
				struct unit_definition *unit = tag_get('bipd', instance->tag_index);

				unit->unit.huds = player_unit->unit.huds;
			}
		}
	}

	return;
}

void skin_tags_unloaded(
	void)
{
	if (skin_tags_globals.registered)
	{
		struct skin_tag_header buffers;

		memset(&buffers, 0, sizeof(buffers));
		buffers.vertex_buffer_count = skin_tags_globals.vertex_header_count;
		buffers.vertex_buffers = (unsigned long)skin_tags_globals.vertex_headers;
		buffers.index_buffer_count = skin_tags_globals.index_header_count;
		buffers.index_buffers = (unsigned long)skin_tags_globals.index_headers;
		tags_header_deregister_vertex_and_index_buffers((struct cache_file_tag_header *)&buffers);
#ifdef HALO_ANDROID
		xgpu_static_buffers_remove(skin_tags_globals.vertex_headers, skin_tags_globals.vertex_header_count, BUFFER_SIZE,
			0);
		xgpu_static_buffers_remove(skin_tags_globals.index_headers, skin_tags_globals.index_header_count, BUFFER_SIZE, 1);
#endif
	}
	if (skin_tags_globals.pack)
		fclose(skin_tags_globals.pack);
	free(skin_tags_globals.ranges);
	memset(&skin_tags_globals, 0, sizeof(skin_tags_globals));
	tag_validate_set_extra_buffers(NULL, 0, NULL, 0);

	return;
}

boolean skin_tags_read(
	long tag_index,
	long offset,
	long size,
	void *buffer)
{
	long absolute_index = tag_index & 0xFFFF;
	long index;

	if (!skin_tags_globals.imported || tag_index == NONE || absolute_index < skin_tags_globals.first_index ||
		absolute_index >= skin_tags_globals.first_index + skin_tags_globals.count)
	{
		return FALSE;
	}
	if (size <= 0 || offset < 0)
		return TRUE;
	memset(buffer, 0, (size_t)size);
	for (index = 0; index < skin_tags_globals.range_count; index++)
	{
		struct skin_range const *range = &skin_tags_globals.ranges[index];
		unsigned long start = (unsigned long)offset > range->offset ? (unsigned long)offset : range->offset;
		unsigned long end = (unsigned long)offset + (unsigned long)size;

		if (range->offset + range->size < end)
			end = range->offset + range->size;
		if (start >= end)
			continue;
		if (fseek(skin_tags_globals.pack, (long)(range->pack_offset + (start - range->offset)), SEEK_SET) ||
			fread((byte *)buffer + (start - (unsigned long)offset), 1, end - start, skin_tags_globals.pack) !=
				end - start)
		{
			platform_log("player skins: could not read %ld bytes at %08lx\n", size, (unsigned long)offset);
		}
	}

	return TRUE;
}

boolean skin_tags_imported(
	void)
{
	return skin_tags_globals.imported;
}
