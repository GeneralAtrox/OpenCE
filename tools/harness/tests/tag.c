#include "harness.h"
#include "enums.inc"
/* Tag, You're It's kills (port/linux/game/infection.c): it passes on to the
human it kills, who is it, and it is a human again; the old it dies (with no
death counted) to spawn again in a human's body, where the map has the
Hunter's; any other death changes no one. */
typedef unsigned short word;
#define VALID_INDEX(index, count) ((index) >= 0 && (index) < (count))
#define HALO_PORT_MAXIMUM_NETWORK_PLAYERS 4
#define INFECTION_MAXIMUM_SCORE 1000000
#define DATUM_INDEX_TO_ABSOLUTE_INDEX(index) ((index) & 0xFFFF)
#define SET_FLAG(flags, bit, value) ((value) ? ((flags) |= FLAG(bit)) : ((flags) &= ~FLAG(bit)))
#include "structures.inc"

/* the world: four players, player n's unit 10 + n, a Hunter's biped 500 and
the map's own 400 */
enum { _object_dead_bit = 0, HUNTER = 500, MAP_UNIT = 400 };
struct player_datum { long unit_index; long dead_unit_index; boolean quit_out_of_game; };
struct unit_datum { long definition_index; struct { long damage_flags; } object; };
static struct
{
 struct distributed_infection_state state;
 long equipped_unit[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
 long killed_unit[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
} infection;
static struct player_datum players[4];
static struct unit_datum units[4];
static boolean client, tag, hunter_loaded;
static long reconciled, killed[4], kills;
static boolean network_game_distributed_client(void){return client;}
static boolean tag_enabled(void){return tag;}
static struct distributed_infection_player *infection_player(long index)
{return VALID_INDEX(index,4)?&infection.state.players[index]:NULL;}
static struct player_datum *player_get(long index){return &players[index];}
static struct unit_datum *unit_try_and_get(long index){return index>=10&&index<14?&units[index-10]:NULL;}
static boolean tag_player_is_it(long index){return infection.state.players[index].role==_distributed_infection_role_zombie;}
static void infection_reconcile_player(long index){reconciled|=1<<index;}
/* (player_skins.c's: it a Hunter where the map has one, the map's own unit
NONE) */
static long player_skins_unit_definition(long index,long definition)
{return tag_player_is_it(index)&&hunter_loaded?HUNTER:definition;}
static boolean player_skins_unit_is_body(long unit){return units[unit-10].definition_index==HUNTER;}
static void unit_kill_no_statistics(long unit){killed[kills++]=unit;}
#define platform_log(...) ((void)0)
#include "under_test.inc"

/* a round with player `it` it, as a Hunter where the map has one */
static void round_with_it(int it,boolean hunter)
{
 memset(&infection,0,sizeof(infection));
 infection.state.phase=_distributed_infection_active;
 for(int player=0;player<4;player++)
 {
  players[player].unit_index=10+player;players[player].dead_unit_index=NONE;players[player].quit_out_of_game=FALSE;
  units[player].definition_index=player==it&&hunter?HUNTER:MAP_UNIT;units[player].object.damage_flags=0;
  infection.state.players[player].identity=player+1;
  infection.state.players[player].role=player==it?_distributed_infection_role_zombie:_distributed_infection_role_human;
  infection.killed_unit[player]=NONE;infection.equipped_unit[player]=NONE;
 }
 reconciled=0;kills=0;client=FALSE;tag=TRUE;hunter_loaded=hunter;
}

static void dies(long player){SET_FLAG(units[player].object.damage_flags,_object_dead_bit,TRUE);}

int main(int argc,char **argv)
{
 const char *case_name=argc>1?argv[1]:"";
 CASE("tagged") {
  round_with_it(1,TRUE);dies(2);
  infection_player_killed_player(1,11,2,FALSE);
  CHECK(infection.state.players[2].role==_distributed_infection_role_zombie,"the human killed is it");
  CHECK(infection.state.players[1].role==_distributed_infection_role_human,"it is a human again");
  CHECK(infection.state.players[1].score==1,"it scores the kill");
  CHECK(kills==1&&killed[0]==11,"the old it's Hunter dies, to spawn again as a human: %ld", kills);
  CHECK((reconciled&6)==6&&infection.state.revision>0,"both players' teams, and the state sent again");
  CHECK(infection.killed_unit[2]==12,"the death counted once");
  infection_player_killed_player(1,11,2,FALSE);
  CHECK(infection.state.players[1].role==_distributed_infection_role_human&&kills==1,"a death reported twice is one");
 } else CASE("not_tagged") {
  round_with_it(1,TRUE);dies(2);
  infection_player_killed_player(3,13,2,TRUE);
  CHECK(infection.state.players[2].role==_distributed_infection_role_human&&infection.state.players[1].role==
   _distributed_infection_role_zombie,"a human killed by a human stays one");
  round_with_it(1,TRUE);dies(2);
  infection_player_killed_player(NONE,NONE,2,FALSE);
  CHECK(infection.state.players[2].role==_distributed_infection_role_human,"a human killed by the world stays one");
  round_with_it(1,TRUE);dies(2);
  infection_player_killed_player(2,12,2,FALSE);
  CHECK(infection.state.players[2].role==_distributed_infection_role_human,"a human's suicide changes no one");
  round_with_it(1,TRUE);dies(1);
  infection_player_killed_player(3,13,1,FALSE);
  CHECK(infection.state.players[1].role==_distributed_infection_role_zombie&&infection.state.players[3].score==1,
   "it killed by a human stays it, and the human scores");
  round_with_it(1,TRUE);dies(1);
  infection_player_killed_player(1,11,1,FALSE);
  CHECK(infection.state.players[1].role==_distributed_infection_role_zombie,"it killing itself stays it");
  CHECK(kills==0,"no one else dies");
 } else CASE("client") {
  round_with_it(1,TRUE);dies(2);client=TRUE;
  infection_player_killed_player(1,11,2,FALSE);
  CHECK(infection.state.players[2].role==_distributed_infection_role_human&&kills==0,"only the host decides");
 } else CASE("no_hunter") {
  /* (a map without the Hunter's tags: it plays the map's own unit, so the
  old it keeps its body, alive) */
  round_with_it(1,FALSE);dies(2);
  infection_player_killed_player(1,11,2,FALSE);
  CHECK(infection.state.players[2].role==_distributed_infection_role_zombie,"the human killed is it");
  CHECK(kills==0,"the old it lives on");
 } else return 2;
 return 0;
}
