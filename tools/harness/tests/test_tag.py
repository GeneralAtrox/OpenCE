"""Tag, You're It's kills: it passes on to the human it kills, and a player's body changes only where their role's
differs (port/linux/game/infection.c)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure


def generated(fault=None):
    network = read('port/linux/game/network_infection.h')
    enums = '\n'.join([enum_with(network, '_distributed_infection_waiting = 0'),
                       enum_with(network, '_distributed_infection_role_none = 0')]) + '\n'
    structures = '\n'.join([structure(network, 'distributed_infection_player'),
                            structure(network, 'distributed_infection_state')]) + '\n'
    infection = read('port/linux/game/infection.c')
    code = '\n'.join([function(infection, 'tag_respawn_in_role'), function(infection, 'infection_player_killed_player')])
    if fault:
        code = mutated(code, *fault)
    return (('enums.inc', enums), ('structures.inc', structures), ('under_test.inc', code))


CASES = ['tagged', 'not_tagged', 'client', 'no_hunter']


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('tag', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [
    (('killer->role = _distributed_infection_role_human;', ''), 'tagged'),
    (('\tunit_kill_no_statistics(player->unit_index);', ''), 'tagged'),
    (('? !player_skins_unit_is_body(player->unit_index) :', '? FALSE :'), 'no_hunter'),
    (('if (tag_enabled())', 'if (FALSE)'), 'not_tagged'),
    (('if (network_game_distributed_client() || infection.state.phase', 'if (infection.state.phase'), 'client'),
])
def test_negative_control(fault, case):
    status, output = run(build('tag', generated(fault)), case)
    assert status == CHECK_FAILED, output
