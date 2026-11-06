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
own. A stock multiplayer map has the skins' bipeds from Two Betrayals' tags
(skin_tags.c), and so does a campaign level in co-op, besides its own; a
Custom Edition map has none.

What a skin's body cannot do (it is a campaign character's):
- the first-person arms are the Master Chief's, always (the globals');
- an Elite holds the human weapons as it holds a plasma rifle (an assault
  rifle, shotgun, sniper rifle, rocket launcher or flamethrower) or a pistol
  (the oddball, the flag), as it has no animations of its own for
  them; a Flood form holds a sniper rifle and the flag as an assault rifle,
  and the oddball as a pistol;
- a Hunter has only its fuel rod cannon, and picks up nothing else; the
  cannon never runs out, as an actor's, and a player's pauses to reload
  after HUNTER_SHOTS_PER_RELOAD shots; it strikes with its shield (its
  biped's melee damage, as the cannon has none, in an unarmed blow's
  time, with its body's melee animation); it must be held 1.25 seconds
  to charge each shot, as an actor's is;
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
#include "units/units.h"
#include "items/weapons.h"
#include "items/weapon_definitions.h"
#include "tag_files/tag_groups.h"

#include "network_coop.h"
#include "player_skins.h"

#include <stdio.h>
#include <string.h>

const char *config_string(const char *name);
int config_write(char const *name, char const *value);
void platform_log(char const *format, ...);

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

	/* a player Hunter's cannon: the shots between reloads, and a reload's
	ticks */
	HUNTER_SHOTS_PER_RELOAD = 5,
	HUNTER_RELOAD_TICKS = 5 * TICKS_PER_SECOND / 2,
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

/* their names in the menus (tools/port_settings.py's SKINS) */
static wchar_t const *const player_skin_names[NUMBER_OF_PLAYER_SKINS] =
{
	L"MASTER CHIEF", L"ELITE", L"ELITE MINOR", L"ELITE MAJOR", L"ELITE SPEC OPS", L"ELITE ZEALOT",
	L"STEALTH ELITE", L"FLOOD HUMAN", L"FLOOD ELITE", L"HUNTER",
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
	/* the shots each body's weapons have fired, in this map */
	long shots[NUMBER_OF_SKIN_BODIES];
	/* and the blows they have struck */
	long melees[NUMBER_OF_SKIN_BODIES];
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

/* a weapon a skin's body was given with no rounds in a magazine (the
level's own weapons, made for an actor, which needs none): the magazine is
filled, as a player's is */
static void skin_weapons_fill(
	long unit_index)
{
	struct unit_datum *unit = unit_get(unit_index);
	short slot;

	for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
	{
		long weapon_index = unit->unit.weapon_object_indices[slot];
		struct weapon_datum *weapon;
		struct weapon_definition *definition;
		short magazine_index;

		if (weapon_index == NONE)
			continue;
		weapon = weapon_get(weapon_index);
		definition = weapon_definition_get(weapon->definition_index);
		for (magazine_index = 0; magazine_index < definition->weapon.magazines.count && magazine_index < 2; magazine_index++)
		{
			struct weapon_magazine_definition *magazine = TAG_BLOCK_GET_ELEMENT(&definition->weapon.magazines,
				magazine_index, struct weapon_magazine_definition);

			if (weapon->weapon.magazines[magazine_index].rounds_total + weapon->weapon.magazines[magazine_index].rounds_loaded > 0 ||
				magazine->rounds_loaded_maximum + magazine->rounds_total_maximum <= 0)
				continue;
			weapon->weapon.magazines[magazine_index].rounds_loaded = magazine->rounds_loaded_maximum;
			weapon->weapon.magazines[magazine_index].rounds_total = magazine->rounds_total_maximum;
			platform_log("player skins: a weapon's magazine %d was empty: filled with %d and %d rounds", (int)magazine_index,
				(int)magazine->rounds_loaded_maximum, (int)magazine->rounds_total_maximum);
		}
	}

	return;
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

wchar_t const *player_skins_name(
	short skin)
{
	return player_skin_names[skin >= 0 && skin < NUMBER_OF_PLAYER_SKINS ? skin : _player_skin_chief];
}

short player_skins_local_step(
	long local_player_index,
	short direction)
{
	short skin = player_skins_local_choice(local_player_index);
	char name[32];

	if (local_player_index < 0 || local_player_index >= MAXIMUM_SKIN_LOCAL_PLAYERS)
		return skin;
	skin = (short)((skin + direction % NUMBER_OF_PLAYER_SKINS + NUMBER_OF_PLAYER_SKINS) % NUMBER_OF_PLAYER_SKINS);
	snprintf(name, sizeof(name), "multiplayer.skin_%ld", local_player_index + 1);
	if (config_write(name, player_skin_definitions[skin].word))
		platform_log("player skins: player %ld chose %s", local_player_index + 1, player_skin_definitions[skin].word);
	else
		platform_log("player skins: player %ld's choice of %s could not be saved", local_player_index + 1,
			player_skin_definitions[skin].word);

	return player_skins_local_choice(local_player_index);
}

short player_skins_valid(
	short skin)
{
	return skin > _player_skin_chief && skin < NUMBER_OF_PLAYER_SKINS ? skin : _player_skin_chief;
}

/* port: (menu_functions.c's ADD PLAYER portraits, skin_portraits.c) */
char const *player_skins_portrait_biped(
	short skin,
	real_rgb_color *rank_color,
	unsigned char *ranked)
{
	struct player_skin_definition const *definition = &player_skin_definitions[player_skins_valid(skin)];

	*ranked = definition->color.red >= 0.f;
	if (*ranked)
		*rank_color = definition->color;

	return definition->body == _skin_body_chief ? PLAYER_SKINS_PORTRAIT_CHIEF : skin_body_bipeds[definition->body];
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

void player_skins_unit_spawned(
	long player_index,
	long unit_index)
{
	short skin;
	short body;

	if (player_index == NONE || unit_index == NONE || !player_skins_globals.loaded)
		return;
	skin = player_skin_of(player_index);
	body = skin_body_of_definition(unit_get(unit_index)->definition_index);
	if (skin == _player_skin_chief && body == NONE)
		return;
	if (body != NONE)
		skin_weapons_fill(unit_index);
	platform_log("player skins: player %ld (%s) is %s", DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index),
		player_skin_definitions[skin].word, body == NONE ? "the map's own unit" : skin_body_bipeds[body]);

	return;
}

/* the skin body unit_index is, if a player's, else NONE */
static short player_skin_body_of_unit(
	long unit_index)
{
	struct unit_datum *unit;

	if (unit_index == NONE || !player_skins_globals.loaded)
		return NONE;
	unit = unit_get(unit_index);
	if (unit->unit.player_index == NONE)
		return NONE;

	return skin_body_of_definition(unit->definition_index);
}

boolean player_skins_unit_is_body(
	long unit_index)
{
	return player_skin_body_of_unit(unit_index) != NONE;
}

boolean player_skins_weapon_fire(
	long weapon_index,
	long unit_index)
{
	short body = player_skin_body_of_unit(unit_index);
	struct weapon_datum *weapon;
	struct weapon_definition *definition;

	if (body == NONE)
		return TRUE;
	weapon = weapon_get(weapon_index);
	definition = weapon_definition_get(weapon->definition_index);
	/* the Hunter's cannon, an actor's, has a magazine of no rounds, which
	never empties: its rounds_total counts the shots since the last reload
	and its state_timer the reload's ticks, which weapon_update counts down
	(the magazine's state stays idle) */
	if (body == _skin_body_hunter && definition->weapon.magazines.count > 0 &&
		TAG_BLOCK_GET_ELEMENT(&definition->weapon.magazines, 0, struct weapon_magazine_definition)->rounds_loaded_maximum <= 0)
	{
		struct weapon_magazine *magazine = &weapon->weapon.magazines[0];

		if (magazine->state_timer > 0)
			return FALSE;
		magazine->rounds_total++;
		if (magazine->rounds_total >= HUNTER_SHOTS_PER_RELOAD)
		{
			magazine->rounds_total = 0;
			magazine->state_timer = HUNTER_RELOAD_TICKS;
			platform_log("player skins: a Hunter reloads after %d shots (%ld fired)", (int)HUNTER_SHOTS_PER_RELOAD,
				player_skins_globals.shots[body] + 1);
		}
	}
	player_skins_globals.shots[body]++;
	if (player_skins_globals.shots[body] == 1 || player_skins_globals.shots[body] % 25 == 0)
	{
		platform_log("player skins: %s has fired %ld shots", skin_body_bipeds[body],
			player_skins_globals.shots[body]);
	}

	return TRUE;
}

void player_skins_unit_melee(
	long unit_index)
{
	short body = player_skin_body_of_unit(unit_index);

	if (body == NONE)
		return;
	player_skins_globals.melees[body]++;
	if (player_skins_globals.melees[body] == 1 || player_skins_globals.melees[body] % 10 == 0)
	{
		platform_log("player skins: %s has struck %ld blows", skin_body_bipeds[body],
			player_skins_globals.melees[body]);
	}

	return;
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
	{
		player_skins_globals.bipeds[body] = NONE;
		player_skins_globals.shots[body] = 0;
		player_skins_globals.melees[body] = 0;
	}
	player_skins_globals.loaded = FALSE;

	return;
}
