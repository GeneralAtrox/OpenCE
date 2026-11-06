/*
PHYSICAL_MEMORY_MAP.H

header included in hcex build.
*/

#ifndef __PHYSICAL_MEMORY_MAP_H
#define __PHYSICAL_MEMORY_MAP_H
#pragma once

/* ---------- constants */

/* port: the tag cache's size, which the loader checks a map's tag data and
structure bsps against (cache_files.c) */
#define TAG_CACHE_SIZE 0x1600000

/* port: the skins' tags' second place (port/linux/game/skin_tags.c), where
the Xbox's game state was (0x80061000, 0x345000 bytes, up to the tag cache:
the native builds' is above it, halo_port_capacity.h). Tags copied into a
map whose tag cache has no room for them go here, below its tags, so that
with them they are one run of memory (port/linux/game/tag_validate.c). The
same size on every machine: the bodies a map has room for must match in a
network game */
#define SKIN_TAGS_BASE_ADDRESS 0x80061000
#define SKIN_TAGS_SIZE 0x345000

/* ---------- macros */

/* ---------- structures */

/* ---------- prototypes/PHYSICAL_MEMORY_MAP.C */

void physical_memory_allocate(void);
void physical_memory_verify(void);
void physical_memory_free(void);

void *physical_memory_get_game_state_base_address(void);
void *physical_memory_get_tag_cache_base_address(void);
void *physical_memory_get_texture_cache_base_address(void);
void *physical_memory_get_sound_cache_base_address(void);
/* port: SKIN_TAGS_BASE_ADDRESS if its memory could be had, else NULL */
void *physical_memory_get_skin_tags_base_address(void);

/* ---------- globals */

/* ---------- public code */

#endif // __PHYSICAL_MEMORY_MAP_H
