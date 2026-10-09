#!/usr/bin/env python3
"""Extract bounded coverage evidence from a CPU-qualified local replay trace.

No memory dumps or ROM bytes are emitted. Actor presence is reported separately
from transitions; it must not be described as exhaustive interaction coverage.
"""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import struct


def exact(stream, count):
    value = stream.read(count)
    if len(value) != count:
        raise ValueError('truncated trace')
    return value


def summarize(directory):
    metadata = [json.loads(s) for s in (directory / 'cases.jsonl').read_text().splitlines()]
    counters = Counter()
    actors, powers, forms, areas, routines, modes = set(), set(), set(), set(), set(), set()
    active_areas = set()
    moved, phase_changes = Counter(), Counter()
    states = defaultdict(set)
    case_rows = []
    with (directory / 'clips.bin').open('rb') as stream:
        h = struct.unpack('<8IQ', exact(stream, 40))
        magic, version, scene_size, case_size, frame_size, count, expected_frames, scope, fingerprint = h
        if (magic != 0x534d4231 or version != 2 or scope != 1 or frame_size != 2068
                or scene_size < 2048 or case_size != scene_size + 20
                or fingerprint != 0x6e01246e5d215cb3 or len(metadata) != count):
            raise ValueError('unqualified or incompatible trace')
        exact(stream, 32768)
        for index in range(count):
            case = exact(stream, case_size)
            n = struct.unpack_from('<I', case, scene_size)[0]
            if not metadata[index]['passed'] or n != metadata[index]['frames']:
                raise ValueError('case metadata mismatch')
            clip_actors, clip_powers, clip_modes = set(), set(), set()
            prev = memoryview(case)[:2048]
            frames = memoryview(exact(stream, n * frame_size))
            for t in range(n):
                start = t * frame_size
                ram = frames[start + 1:start + 2049]
                gameplay = (ram[0x770], ram[0x772]) == (1, 3)
                for k in range(6):
                    if 0 < ram[15 + k] < 128:
                        actor = ram[22 + k]
                        actors.add(actor)
                        clip_actors.add(actor)
                        states[actor].add(ram[30 + k])
                        if (gameplay and (prev[0x770], prev[0x772]) == (1, 3)
                                and 0 < prev[15 + k] < 128 and prev[22 + k] == actor):
                            position = (0x6e + k, 0x87 + k, 0xb6 + k, 0xcf + k)
                            moved[actor] += any(ram[v] != prev[v] for v in position)
                            phase_changes[actor] += ram[30 + k] != prev[30 + k]
                if ram[0x14] and ram[0x1b] == 46 and ram[0x39] < 4:
                    powers.add(ram[0x39])
                    clip_powers.add(ram[0x39])
                mode = (ram[0x770], ram[0x772])
                modes.add(mode)
                clip_modes.add(mode)
                forms.add(ram[0x756])
                areas.add(ram[0x750])
                if gameplay:
                    active_areas.add(ram[0x750] & 0x7f)
                routines.add(ram[0xe])
                counters['frames'] += 1
                counters['frames_outside_gameplay'] += mode != (1, 3)
                counters['nonidle_frame_boundaries'] += struct.unpack_from('<H', frames, start + 2056)[0] != 0x8057
                counters['game_mode_task_changes'] += mode != (prev[0x770], prev[0x772])
                counters['death_routine_entries'] += ram[0xe] == 11 and prev[0xe] != 11
                counters['player_form_changes'] += ram[0x756] != prev[0x756]
                counters['injury_timer_activations'] += ram[0x79e] != 0 and prev[0x79e] == 0
                counters['star_timer_activations'] += ram[0x79f] != 0 and prev[0x79f] == 0
                prev = ram
            row = dict(metadata[index])
            row.update(actor_ids=sorted(clip_actors), powerup_types=sorted(clip_powers),
                       game_modes_tasks=sorted(clip_modes))
            case_rows.append(row)
        if stream.read(1) or counters['frames'] != expected_frames:
            raise ValueError('trace count/trailer mismatch')
    result = dict(counters, cases=count, actor_ids=sorted(actors), powerup_types=sorted(powers),
                  player_forms=sorted(forms), raw_area_pointer_bytes=sorted(areas),
                  active_area_pointers=sorted(active_areas),
                  game_engine_routines=sorted(routines), game_modes_tasks=sorted(modes),
                  actor_state_values={str(k): sorted(v) for k, v in sorted(states.items())},
                  actor_position_change_frames={str(k): v for k, v in sorted(moved.items()) if v},
                  actor_state_change_frames={str(k): v for k, v in sorted(phase_changes.items()) if v},
                  coverage_is_exhaustive=False)
    (directory / 'coverage.json').write_text(json.dumps(result, indent=2) + '\n')
    (directory / 'case_coverage.jsonl').write_text(''.join(json.dumps(v) + '\n' for v in case_rows))
    print(json.dumps({'panel': str(directory), **counters}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('directories', nargs='+', type=Path)
    args = parser.parse_args()
    for directory in args.directories:
        summarize(directory)
