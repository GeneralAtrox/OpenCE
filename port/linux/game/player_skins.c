/*
PLAYER_SKINS.C

The body each player plays as in multiplayer and network co-op: the Master
Chief (in the player's armor color), an Elite of any rank (minor, major,
spec ops, zealot or stealth, or one in the player's color), a Flood combat
form (a human's or an Elite's) or a Hunter. Each machine's players choose
theirs in the Network Setup settings (multiplayer.skin_1 to skin_16, one for
each local player), and the choice travels in the network player's
icon_index, which the Xbox game sent but never used: the host keeps what a
machine says if it is one of player_skin, else the Master Chief.

A player's body is its skin's biped where the map has it, else the map's
own. A stock multiplayer map has the skins' bipeds from The Maw's tags
(skin_tags.c); a campaign level in co-op has only its own (an Elite where
the level has Elites, a Flood form where it has Flood, a Hunter where it has
Hunters), and a Custom Edition map none.

What a skin's body cannot do (it is a campaign character's):
- the first-person arms are the Master Chief's, always (the globals');
- an Elite holds the human weapons as it holds a plasma rifle (an assault
  rifle, shotgun, sniper rifle, rocket launcher or flamethrower) or a pistol
  (the oddball, the flag), as it has no animations of its own for
  them; a Flood form holds a sniper rifle and the flag as an assault rifle,
  and the oddball as a pistol;
- a Hunter has only its fuel rod cannon, and picks up nothing else;
- the Flood and the Hunter cannot drive or ride a vehicle (their animations
  have no seats in one), nor the Flood throw a grenade;
- a stealth Elite is not camouflaged (it is as any player is, with the
  powerup);
- in a team game every body has its team's color; the Elites' ranks show
  theirs only in a game without teams.
*/

#include "cseries.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "tag_files/tag_groups.h"

#include "network_coop.h"
#include "player_skins.h"

#include <stdio.h>
#include <string.h>

const char *config_string(const char *name);

/* ---------- constants */

enum
{
	_skin_body_chief = 0,
	_skin_body_elite,
	_skin_body_elite_stealth,
	_skin_body_flood_human,
	_skin_body_flood_elite,
	_skin_body_hunter,
	NUMBER_OF_SKIN_BODIES,

	MAXIMUM_SKIN_LOCAL_PLAYERS = 16,
};

struct player_skin_definition
{
	char const *word;
	short body;
	/* an Elite's rank's armor color (its actor variant's change color A),
	in a game without teams; a negative red for the player's own */
	real_rgb_color color;
};

static struct player_skin_definition const player_skin_definitions[NUMBER_OF_PLAYER_SKINS] =
{
	{ "chief", _skin_body_chief, { -1.f, 0.f, 0.f } },
	{ "elite", _skin_body_elite, { -1.f, 0.f, 0.f } },
	{ "elite_minor", _skin_body_elite, { 0.31f, 0.30f, 0.53f } },
	{ "elite_major", _skin_body_elite, { 0.25f, 0.08f, 0.09f } },
	/* (the spec ops' armor is black: their tags are only in two levels,
	and have no color of their own) */
	{ "elite_spec_ops", _skin_body_elite, { 0.06f, 0.06f, 0.07f } },
	{ "elite_zealot", _skin_body_elite, { 0.29f, 0.24f, 0.05f } },
	{ "elite_stealth", _skin_body_elite_stealth, { -1.f, 0.f, 0.f } },
	{ "flood_human", _skin_body_flood_human, { -1.f, 0.f, 0.f } },
	{ "flood_elite", _skin_body_flood_elite, { -1.f, 0.f, 0.f } },
	{ "hunter", _skin_body_hunter, { -1.f, 0.f, 0.f } },
};

static char const *const skin_body_bipeds[NUMBER_OF_SKIN_BODIES] =
{
	NULL,
	"characters\\elite\\elite",
	"characters\\elite\\elite special",
	"characters\\floodcombat_human\\floodcombat_human",
	"characters\\floodcombat elite\\floodcombat elite",
	"characters\\hunter\\hunter",
};

/* a weapon a body has no animations for, held as one it has */
struct skin_weapon_label
{
	short body;
	char const *label;
	char const *held_as;
};

static struct skin_weapon_label const skin_weapon_labels[] =
{
	{ _skin_body_elite, "ar", "pr" },
	{ _skin_body_elite, "sg", "pr" },
	{ _skin_body_elite, "sr", "pr" },
	{ _skin_body_elite, "rl", "pr" },
	{ _skin_body_elite, "ft", "pr" },
	{ _skin_body_elite, "b", "hp" },
	{ _skin_body_elite, "f", "hp" },
	{ _skin_body_elite_stealth, "ar", "pr" },
	{ _skin_body_elite_stealth, "sg", "pr" },
	{ _skin_body_elite_stealth, "sr", "pr" },
	{ _skin_body_elite_stealth, "rl", "pr" },
	{ _skin_body_elite_stealth, "ft", "pr" },
	{ _skin_body_elite_stealth, "b", "hp" },
	{ _skin_body_elite_stealth, "f", "hp" },
	{ _skin_body_flood_human, "sr", "ar" },
	{ _skin_body_flood_human, "f", "ar" },
	{ _skin_body_flood_human, "b", "hp" },
	{ _skin_body_flood_elite, "sr", "ar" },
	{ _skin_body_flood_elite, "f", "ar" },
	{ _skin_body_flood_elite, "b", "hp" },
	{ _skin_body_hunter, "b", "fr" },
	{ _skin_body_hunter, "f", "fr" },
	{ NONE, NULL, NULL }
};

/* ---------- globals */

static struct
{
	/* each body's biped in the map loaded, or NONE */
	long bipeds[NUMBER_OF_SKIN_BODIES];
	boolean loaded;
} player_skins_globals;

/* ---------- private code */

static short skin_body_of_definition(
	long definition_index)
{
	short body;

	if (!player_skins_globals.loaded || definition_index == NONE)
		return NONE;
	for (body = _skin_body_chief + 1; body < NUMBER_OF_SKIN_BODIES; body++)
	{
		if (player_skins_globals.bipeds[body] == definition_index)
			return body;
	}

	return NONE;
}

static boolean player_skins_apply(
	void)
{
	return game_engine_running() || network_coop_active();
}

static short player_skin_of(
	long player_index)
{
	struct player_datum *player;

	if (player_index == NONE)
		return _player_skin_chief;
	player = player_get(player_index);

	return player_skins_valid(player->network_player_data.icon_index);
}

/* ---------- public code */

short player_skins_local_choice(
	long local_player_index)
{
	char name[32];
	char const *word;
	short skin;

	if (local_player_index < 0 || local_player_index >= MAXIMUM_SKIN_LOCAL_PLAYERS)
		return _player_skin_chief;
	snprintf(name, sizeof(name), "multiplayer.skin_%ld", local_player_index + 1);
	word = config_string(name);
	for (skin = 0; word && skin < NUMBER_OF_PLAYER_SKINS; skin++)
	{
		if (!strcmp(word, player_skin_definitions[skin].word))
			return skin;
	}

	return _player_skin_chief;
}

short player_skins_valid(
	short skin)
{
	return skin > _player_skin_chief && skin < NUMBER_OF_PLAYER_SKINS ? skin : _player_skin_chief;
}

long player_skins_unit_definition(
	long player_index,
	long default_definition_index)
{
	short skin;
	long biped;

	if (!player_skins_apply() || !player_skins_globals.loaded)
		return default_definition_index;
	skin = player_skin_of(player_index);
	biped = player_skins_globals.bipeds[player_skin_definitions[skin].body];

	return biped != NONE ? biped : default_definition_index;
}

void player_skins_change_color(
	long player_index,
	real_rgb_color *change_color)
{
	struct player_skin_definition const *definition;

	if (!player_skins_apply() || !player_skins_globals.loaded || !game_engine_running() || game_engine_has_teams())
		return;
	definition = &player_skin_definitions[player_skin_of(player_index)];
	if (definition->color.red < 0.f || player_skins_globals.bipeds[definition->body] == NONE)
		return;
	*change_color = definition->color;

	return;
}

char const *player_skins_weapon_label(
	long definition_index,
	char const *weapon_label)
{
	short body = skin_body_of_definition(definition_index);
	long index;

	if (body == NONE || !weapon_label)
		return weapon_label;
	for (index = 0; skin_weapon_labels[index].label; index++)
	{
		if (skin_weapon_labels[index].body == body && !_stricmp(skin_weapon_labels[index].label, weapon_label))
			return skin_weapon_labels[index].held_as;
	}

	return weapon_label;
}

void player_skins_tags_loaded(
	void)
{
	short body;

	player_skins_globals.bipeds[_skin_body_chief] = NONE;
	for (body = _skin_body_chief + 1; body < NUMBER_OF_SKIN_BODIES; body++)
		player_skins_globals.bipeds[body] = tag_loaded('bipd', skin_body_bipeds[body]);
	player_skins_globals.loaded = TRUE;

	return;
}

void player_skins_tags_unloaded(
	void)
{
	short body;

	for (body = 0; body < NUMBER_OF_SKIN_BODIES; body++)
		player_skins_globals.bipeds[body] = NONE;
	player_skins_globals.loaded = FALSE;

	return;
}
