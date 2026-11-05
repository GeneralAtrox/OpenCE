/*
PLAYER_SKINS.H

The bodies a player may play as (player_skins.c), and the Elite, Flood and
Hunter tags a stock multiplayer map is given for them (skin_tags.c).
*/

#ifndef __PLAYER_SKINS_H
#define __PLAYER_SKINS_H
#pragma once

/* ---------- constants */

enum player_skin
{
	_player_skin_chief = 0,
	_player_skin_elite,
	_player_skin_elite_minor,
	_player_skin_elite_major,
	_player_skin_elite_spec_ops,
	_player_skin_elite_zealot,
	_player_skin_elite_stealth,
	_player_skin_flood_human,
	_player_skin_flood_elite,
	_player_skin_hunter,
	NUMBER_OF_PLAYER_SKINS
};

/* ---------- prototypes/PLAYER_SKINS.C */

/* the skin a machine's local player (0 to 15) has chosen
(multiplayer.skin_<n>), as a network player's icon_index carries it */
short player_skins_local_choice(long local_player_index);
/* a skin a network player says it has, as the host takes it: one of
player_skin, else the Master Chief's */
short player_skins_valid(short skin);
/* the unit definition a player spawns as: its skin's biped, if this map
has it, else default_definition_index */
long player_skins_unit_definition(long player_index, long default_definition_index);
/* the change color of a player whose skin has its own (an Elite's rank, in
a game without teams) */
void player_skins_change_color(long player_index, real_rgb_color *change_color);
/* the weapon label a unit's animation graph is searched for: a skin's
body holds a weapon it has no animations for as one it has */
char const *player_skins_weapon_label(long definition_index, char const *weapon_label);
/* a player's unit was made (the host's) or attached (a client's): logged,
if it is a skin's body or the player chose one */
void player_skins_unit_spawned(long player_index, long unit_index);
/* a weapon held by unit_index is about to fire a shot: FALSE while a
player Hunter's cannon reloads; else the shot is counted (and logged) if
the unit is a player's skin body */
boolean player_skins_weapon_fire(long weapon_index, long unit_index);
/* whether unit_index is a player's skin body */
boolean player_skins_unit_is_body(long unit_index);
/* a player's unit strikes a blow: counted and logged if a skin's body */
void player_skins_unit_melee(long unit_index);
/* the map's tags have loaded or are going */
void player_skins_tags_loaded(void);
void player_skins_tags_unloaded(void);

/* ---------- prototypes/SKIN_TAGS.C */

/* the tags at tag_header, *tag_data_size bytes of the stock map map_name
(of the cache file type header_type) just read into the tag cache: before
they are checked, the skins' tags are added to them from The Maw's, and
*tag_data_size grows over them. FALSE if none were added */
boolean skin_tags_import(void *tag_header, long *tag_data_size, short header_type, char const *map_name);
/* the tags' check refused them: they are as they were read */
void skin_tags_import_undo(void *tag_header, long *tag_data_size);
/* the largest map file a tag's bitmap pixels or sound samples may be in:
file_length, or The Maw's if its tags were added */
long skin_tags_file_length(long file_length);
/* the tags were checked: the added models' vertex and index buffers are
registered (as the map's are, tags_header_register_vertex_and_index_buffers) */
void skin_tags_loaded(void);
/* the map's tags are going */
void skin_tags_unloaded(void);
/* a read of a bitmap's pixels or a sound's samples of an added tag
(cache_file_read): served from the skins' cache file, at once. FALSE for
any other tag's */
boolean skin_tags_read(long tag_index, long offset, long size, void *buffer);
/* whether the tags of the map loaded include the skins' (then every
machine playing it has the same tags, at the same indices) */
boolean skin_tags_imported(void);

#endif // __PLAYER_SKINS_H
