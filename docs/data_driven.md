# Data-driven game tables: a proposal

Status: proposal, 2026-11-08 (the todo item "Data driven: try to move
properties out of the executable and into data files as much as possible.
e.g. prices, opcode _displayed_ names, help text etc."). Nothing here is
built; it needs the decisions at the end.

## What is compiled in today

| Table | Where | Simulation state? |
|---|---|---|
| Matter prices | `game/matter.hpp` `MATTER_PRICE` | **yes**: the Ship turns matter into money |
| Terrain dig times and yields | `game/terrain.hpp` `TERRAIN_MINING` | **yes** |
| Terrain generator parameters and thresholds | `game/terrain.cpp` | **yes** (made on demand, every step) |
| Matter and terrain colours | `MATTER_COLOR_SRGB`, `TERRAIN_COLOR_SRGB` | no (look) |
| Opcode names (parse and print) | `game/opcode.hpp` `OPCODE_NAMES` | no, but they are the console and save-dump vocabulary |
| Opcode captions on tiles | `assets.json` (already data) | no |
| Key bindings, look settings | `assets/config/settings.json` (already data) | no |
| Command help text | in each `make_command` call | no |
| Opcode help text | does not exist yet | no |

## The constraint that decides the design

Lockstep multiplayer compares the World hash at every step, and a joiner
receives the World, not the rules. Any table the step reads must be
identical on every peer, or the first sale or dig desyncs. Today the step's
tables are compiled in, so `wire::build_id()` (checked in HELLO) already
guarantees agreement.

Moving a simulation table into a file breaks that guarantee unless the
file's content joins the check. There are two sound options:

1. **Ruleset hash in HELLO.** Hash the loaded rules files (canonical bytes)
   into a `ruleset_id` that HELLO carries beside `build_id`. The relay
   refuses a mismatch, as it refuses a different build. This is cheap, and
   modding means everyone installs the same files.
2. **Rules in the World.** The tables become World state, saved and hashed
   with everything else and donated to joiners. A tutorial or scenario could
   then change prices mid-game ("buying steel hurts"). This costs a save
   version bump, and every read goes through the World.

Presentation tables (colours, display names, help text) have no such
constraint: per-client files, freely editable.

## Recommendation

- **Now:** move *presentation* data to `assets/config/` (or a new
  `assets/data/`) as JSON, loaded at startup with the existing JSON parser
  that settings use. That covers opcode display names and one-line help,
  matter display names and colours, terrain colours, and command help
  overrides. This is also exactly what the context-help and hover-panel todo
  needs, so the two land together.
- **Simulation tables** (prices, dig times, generator parameters): keep them
  compiled in until a scenario needs to change them. The foreshadowed
  tutorial economy (buying a truck plus steel and concrete) suggests that
  will happen soon. Then take option 2, rules in the World, because prices
  that change during a game are already game state.
- **Format:** JSON, as settings and `assets.json` already are. One file per
  domain (`opcodes.json`, `matter.json`, `terrain.json`). Each record is
  keyed by the save-format short name (`ADD`, `COAL`, ...), never by numeric
  code, so the files survive renumbering.

## Decisions needed

1. Presentation data now, simulation data later (as recommended), or both
   now?
2. For simulation data: ruleset hash (option 1) or rules in the World
   (option 2)?
3. Location and format: `assets/config/*.json` beside settings, or a new
   `assets/data/`?
4. Who writes the opcode help text: Claude drafts from
   `game/docs/machine_language.md` for you to edit?
