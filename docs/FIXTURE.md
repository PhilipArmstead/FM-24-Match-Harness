Editing a fixture file
===

A fixture file is the JSON that `fm24ctl get-next-fixture <file>.json` writes.
It holds everything the match engine needs to play one match: both squads with
every player's attributes, both managers' tactics, the officials, the venue
and the seed. You can edit it and play it with `fm24ctl run <file>.json`.

This page explains what is in the file, what you can change safely, and what
the values mean. Some of it is still unknown. Where something hasn't been
tested, this page says so.

To inspect a fixture visually, run `fm24ctl <file>.json` (or `<file>.bin`).
This opens a read-only Windows viewer, not a match run. JSON is read offline;
binary inspection requires FM24 running and idle. The viewer shows stored
tactics and player wellbeing; it does not guess AI starting assignments,
weather meanings or league/cup type codes. Its date/time comes from the
export's advisory `source` annotation, converted to `YYYY-MM-DD HH:MM`
(accounting for leap years), not a decoded date field. Binary dates remain
unknown when no date annotation is available. See the
README's [viewer notes](../README.md#view-a-fixture) for the limits.

# The rules

- **Change values, not structure.** You can change any number or string. Don't
  add, remove or reorder fields. The file is a list of every value the game
  wrote, in order, and the encoder writes them back in that order. A missing
  or extra field shifts everything after it and the game can't read the
  result.
- **Edit the number, not the annotation.** Only the value keys (`u8`, `u16`,
  `u32`, `u64`, `str`, `hex`, `u8map`) are read back. Everything else is a
  note for you and is ignored when the file is played: `name`, `player`,
  `flags`, `role` (and `member`, `site`, `p` in a `--full` export). Changing a
  `flags` list does nothing. Change the number next to it.
- **Stay in range.** A `u8` is 0-255, `u16` 0-65535, `u32` 0-4294967295,
  `u64` 0-18446744073709551615. The encoder refuses anything else, and
  negative numbers.
- **Keep every `u8map` entry, in order.** The keys are labels only; the values
  are written in the order they appear.
- **Strings can change length.** A `str` is stored with its length, which the
  encoder recalculates.
- **Your edits survive every seed.** For each seed, `run` has the game build
  the fixture again and then puts your edits back (see the README). A field
  counts as edited when it differs from what the game builds with the file's
  own seed. If the game's build for some seed has a different structure where
  you edited (for example another line-up), that seed fails and `harness.log`
  names the field. `run --keep-build` skips the rebuild and plays your file as
  it is, changing only the seed.

To check an edit, run `fm24ctl encode <file>.json <file>.bin`. It fails with
the field number if a value is invalid.

# Finding your way around

The file has a header and a `fields` array:

```json
{
  "format": "fm24-match-config/1",
  "game_build": 1744804480,
  "engine_version": "24.2.0.0",
  "source": "Aston Villa vs Bristol City, 2023 day 186 19:30, seed 42",
  "fields": [
    {"name": "match", "u16": 2057},
    {"u8": 24},
    ...
  ]
}
```

Only `format` and the fields' values are needed to play the file. The one
other header key that is read is `away_team`, which only a file made by
`replace-away` has (see [Changing the opponent](#changing-the-opponent)). The
rest of the header is information. `game_build` is the PE timestamp of the
`fm.exe` that wrote it: 1744804480 (Steam) or 1744804949 (Epic Games).

Each field has exactly one value key:

| Key                       | Meaning                                                        |
|---------------------------|----------------------------------------------------------------|
| `u8`, `u16`, `u32`, `u64` | an unsigned whole number of 1, 2, 4 or 8 bytes                 |
| `str`                     | text (UTF-8)                                                   |
| `hex`                     | raw bytes, two hex digits per byte                             |
| `u8map`                   | a player's 69 ratings, one byte each (see [Ratings](#ratings)) |

And some notes:

| Key      | Meaning                                                                                        |
|----------|------------------------------------------------------------------------------------------------|
| `name`   | where you are: set on the first field of each block, and on every field whose meaning is known |
| `player` | the player's name, on the first field of each player block                                     |
| `flags`  | the names of the bits set in a bit-set value (tactics)                                         |
| `role`   | `"seed"` on the seed field                                                                     |

## Research keys (`--full`)

`fm24ctl get-next-fixture <file>.json --full` also writes keys that say where
each value comes from in the game. They make the file about 3.5 times as
large, change nothing when it is played, and are mainly useful for working
out what unnamed fields mean:

| Key                       | Meaning                                                                                                                                                                                                                  |
|---------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `member`                  | the game object member the value came from, e.g. `MATCH_PLAYER_DATA+0x108`. `(computed)` is a value the game calculated while saving, `(constant)` a fixed byte, `fn_<address>+0xNN` a member of an object not named yet |
| `site`                    | the addresses (in `fm.exe`) of the code that wrote the value and of its caller                                                                                                                                           |
| `p`                       | an index into the header's `paths` list: the chain of calls that led to the write                                                                                                                                        |
| `bytes`, `paths` (header) | the size of the match data the file encodes to, and the call chains `p` refers to                                                                                                                                        |

A few places below refer to a `member`. To find those fields, export with
`--full`. Fields are in the same order in both kinds of export, so a field's
position in `fields` is the same in both.

A field without a `name` belongs to the block named above it. Search the file
for a `name` to jump to a block. The blocks, in file order:

| Block                         | What it is                                                                                             |
|-------------------------------|--------------------------------------------------------------------------------------------------------|
| `match`                       | the match header. The seed is a few fields in                                                          |
| `official[0..3]`, `referee`   | the match officials                                                                                    |
| `venue`                       | the stadium, attendance and weather                                                                    |
| `fixture`, `fixture_extra`    | the competition and fixture details                                                                    |
| `home.team`, `home.team_info` | the home club                                                                                          |
| `home.manager`                | the home manager, including `home.manager.tactic`                                                      |
| `home.staff[0..2]`            | other staff, each optional (`home.has_staff[1]`, `home.has_staff[2]`)                                  |
| `home.player_count`           | how many players the squad has (don't change it)                                                       |
| `home.player[0..]`            | the players, one block each                                                                            |
| `home.team_extra`             | a default tactic and other data, including one more staff member's attributes and person record        |
| `home.backroom[0..]`          | backroom staff (`home.backroom_count`), each with [attributes](#staff-attributes) and a default tactic |
| `away.*`                      | the same for the away side                                                                             |

Every manager, backroom entry and `team_extra` carries a tactic. **Only the
managers' tactics are the real ones.** The others are defaults. Edit
`home.manager.tactic` and `away.manager.tactic`.

# The seed

The seed is the field with `"role": "seed"`, near the top:

```json
{"u32": 42, "role": "seed"}
```

You don't need to edit it. `fm24ctl run <file> 1-100` plays the file once per
seed. With no seeds given, `run` plays this one.

# Players

## Who is who

Each player block starts with a field carrying the player's name:

```json
{"name": "home.player[0]", "player": "Emiliano Martínez", "u8": 255}
```

Further in are `home.player[0].person_uid` (the player's unique ID in the
game's database) and `home.player[0].ratings`.

For the side you manage, the first 11 players are the starting line-up, in the
order of the tactic's formation slots: `player[0]` plays `slot[0]`, and so on
(see [Formation](#formation-positions-roles-and-duties)). The rest are the
substitutes. This held in the dumps checked so far. The AI side's players are in
another order.

You can't swap players in or out, or move a player to another slot: players
write different numbers of fields, so that changes the structure. Pick the
line-up in the game and export again. You *can* make a player play like
another by copying ratings (below).

## Ratings

`ratings` is a `u8map` of 69 values: 15 position ratings, then 54 attributes.

```json
"name": "home.player[0].ratings",
"u8map": {"pos_gk": 20, "pos_sw": 1, ..., "crossing": 5, "dribbling": 15, "finishing": 15, ...}
```

**Position ratings** (`pos_gk` to `pos_wbr`) are the player's familiarity
with each position, on the game's 1-20 scale:

`pos_gk`, `pos_sw` (sweeper), `pos_dl`, `pos_dc`, `pos_dr`, `pos_dm`,
`pos_ml`, `pos_mc`, `pos_mr`, `pos_aml`, `pos_amc`, `pos_amr`, `pos_st`,
`pos_wbl`, `pos_wbr`.

Whether the engine uses them during a match hasn't been tested.

**Attributes** are on a 1-100 scale, five times the value the game shows. A
player with 15 Dribbling has `"dribbling": 75`. The game shows
`(value + 2) / 5`, rounded down, from 1 to 20:

| Shown in game | Stored values |
|---------------|---------------|
| 1             | 1-7           |
| 2             | 8-12          |
| 10            | 48-52         |
| 15            | 73-77         |
| 20            | 98-100        |

The game's own data has values between the steps too (about one attribute in
eight in a typical file isn't a multiple of 5). Use multiples of 5 to set a
value as the game would show it. Whether the engine tells, say, 74 and 76
apart hasn't been tested.

The attributes, in file order:

| Group                  | Keys                                                                                                                                        |
|------------------------|---------------------------------------------------------------------------------------------------------------------------------------------|
| Technical              | `crossing`, `dribbling`, `finishing`, `heading`, `long_shots`, `marking`, `off_the_ball`, `passing`, `penalty_taking`, `tackling`, `vision` |
| Goalkeeping            | `handling`, `aerial_reach`, `command_of_area`, `communication`, `kicking`, `throwing`                                                       |
| Mental and goalkeeping | `anticipation`, `decisions`, `one_on_ones`, `positioning`, `reflexes`                                                                       |
| Technical              | `first_touch`, `technique`                                                                                                                  |
| Feet                   | `left_foot`, `right_foot`                                                                                                                   |
| Mental                 | `flair`                                                                                                                                     |
| Set pieces             | `corners`                                                                                                                                   |
| Mental                 | `teamwork`, `work_rate`                                                                                                                     |
| Set pieces             | `long_throws`                                                                                                                               |
| Goalkeeping            | `eccentricity`, `rushing_out`, `punching`                                                                                                   |
| Physical               | `acceleration`                                                                                                                              |
| Set pieces             | `free_kicks`                                                                                                                                |
| Physical               | `strength`, `stamina`, `pace`, `jumping_reach`                                                                                              |
| Mental                 | `leadership`                                                                                                                                |
| Hidden                 | `dirtiness`                                                                                                                                 |
| Physical               | `balance`                                                                                                                                   |
| Mental                 | `bravery`                                                                                                                                   |
| Hidden                 | `consistency`                                                                                                                               |
| Mental                 | `aggression`                                                                                                                                |
| Physical               | `agility`                                                                                                                                   |
| Hidden                 | `important_matches`, `injury_proneness`, `versatility`                                                                                      |
| Physical               | `natural_fitness`                                                                                                                           |
| Mental                 | `determination`, `composure`, `concentration`                                                                                               |

`left_foot` and `right_foot` are the player's footedness on the same scale.
The hidden attributes are ones the game doesn't show normally. For
`dirtiness` and `injury_proneness`, higher is worse.

**The engine plays from these attributes.** Edits take effect as they are,
with no other changes needed. In one fixture (Aston Villa against Bristol
City, all Villa players changed together), pace and acceleration mattered most
by far, then dribbling, balance, leadership, agility, jumping reach and
strength; a lower dirtiness helped. Most others made no difference that 300
matches could detect.

## Values derived from ratings

After the ratings come values the game calculates from them when it builds
the match: `ability` (the player's current ability), `leadership_value`, and a
list of `role_rating[<role>@<position>]` entries (how well the player suits
each role, with `.role` and `.position`).

**Leave them alone, and don't bother updating them after editing ratings.** In
a test, changing these values made no difference to results, while changing
the attributes did. The game uses them to pick its team, not to play the
match.

# Values copied from the game's database

When the game builds a match, it copies some values from its database: from
each person's record (players, staff and officials), each player's record and
each team's record. These fields are named in the file. In one fixture, each
was checked against the database for all 47 players and every manager, staff
member and backroom entry, and matched every time.

The referee is the exception: the referee block has the same fields, but its
personality values differ from the referee's database record (pressure 12
against 10). The game seems to take referees' values from somewhere else.

Whether the engine reads any of them during a match hasn't been tested. As
with the ratings, editing a field changes only the match, not your save.

## Players

In every `<side>.player[i]` block:

| Name                             | Type  | Meaning                                                                                                       | Database (player record) |
|----------------------------------|-------|---------------------------------------------------------------------------------------------------------------|--------------------------|
| `.condition`                     | `u16` | physical condition: 0-10000, where 10000 is 100%                                                              | `+0x1F8`                 |
| `.sharpness[0]`, `.sharpness[1]` | `u16` | match sharpness, same scale. The game writes the same value twice                                             | `+0x1F4`                 |
| `.fatigue`                       | `u16` | fatigue (FMRTE's "jadedness"), stored as a negative number: 0 is fresh, 65394 is -142 (65536 minus the value) | `+0x1F6`                 |
| `.home_reputation`               | `u16` | reputation in the player's home nation, 0-10000                                                               | `+0x1FA`                 |
| `.current_reputation`            | `u16` | current reputation, 0-10000                                                                                   | `+0x1FC`                 |
| `.world_reputation`              | `u16` | world reputation, 0-10000                                                                                     | `+0x1FE`                 |
| `.current_ability`               | `u16` | current ability (CA), 1-200                                                                                   | `+0x200`                 |
| `.morale`                        | `u8`  | morale: 11 "fairly good", 12 "quite good", 13 "good" (the other levels aren't mapped yet)                     | `+0x25F`                 |
| `.squad_number`                  | `u8`  | shirt number. A player whose contract has no number (255) gets one for the match                              | contract `+0x55`         |
| `.last_transfer_fee`             | `u32` | the fee of the player's last transfer, in pounds (0 for a free transfer or academy player)                    | `+0x1E0`                 |
| `.traits`                        | `u64` | player traits (preferred moves), one bit per trait; the `flags` note names them. See [Traits](#traits)        | person record `+0xF0`    |
| `.preferred_position`            | `u8`  | where the player prefers to be when he shares a position with teammates; see the table below                  | `+0x266`                 |

`.preferred_position` codes, as the game's position tooltip words them. Codes
1, 4 and 6 were checked in the game; the others come from FM-Player-Rater's
notes:

| Value | Preference                                                                                     |
|-------|------------------------------------------------------------------------------------------------|
| 0     | none (the most common)                                                                         |
| 1     | "on the right in a two-man pairing" (Tielemans)                                                |
| 2     | probably on the left in a two-man pairing                                                      |
| 3     | probably on the right in a two-man pairing or in the middle in a three-man grouping            |
| 4     | "on the left in a two-man pairing or in the middle in a three-man grouping" (Sanson)           |
| 5     | probably in the middle                                                                         |
| 6     | "on the right in a two-man pairing or on the right or middle in a three-man grouping" (Bailey) |

`.ability` (see [above](#values-derived-from-ratings)) is current ability
adjusted for the match, e.g. 158 for a player whose `.current_ability` is
157.

Not in the file: potential ability, date of birth (only `.age`),
international goals, wages, bonuses, contract dates, asking price, injury
risk and relationships.

### Traits

`.traits` is a bit set (see [Bit sets](#bit-sets)). FMRTE's preferred-moves
list, read across the rows (left, right, left, ...), is in bit order. The
names checked against six players' trait lists in the game are in **bold**.
The rest follow from that order.

| Bit | Value               | Trait                                                  |
|-----|---------------------|--------------------------------------------------------|
| 0   | 1                   | runs with ball down left                               |
| 1   | 2                   | runs with ball down right                              |
| 2   | 4                   | runs with ball through centre                          |
| 3   | 8                   | gets into opposition area                              |
| 4   | 16                  | **moves into channels**                                |
| 5   | 32                  | **gets forward whenever possible**                     |
| 6   | 64                  | plays short simple passes                              |
| 7   | 128                 | **tries killer balls often**                           |
| 8   | 256                 | **shoots from distance**                               |
| 9   | 512                 | shoots with power                                      |
| 10  | 1024                | places shots                                           |
| 11  | 2048                | **curls ball**                                         |
| 12  | 4096                | likes to round keeper                                  |
| 13  | 8192                | **likes to try to beat offside trap**                  |
| 14  | 16384               | **uses outside of foot**                               |
| 15  | 32768               | marks opponent tightly                                 |
| 16  | 65536               | **winds up opponents**                                 |
| 17  | 131072              | **argues with officials**                              |
| 18  | 262144              | plays with back to goal                                |
| 19  | 524288              | comes deep to get ball                                 |
| 20  | 1048576             | **plays one-twos**                                     |
| 21  | 2097152             | likes to lob keeper                                    |
| 22  | 4194304             | dictates tempo                                         |
| 23  | 8388608             | attempts overhead kicks                                |
| 24  | 16777216            | looks for pass rather than attempting to score         |
| 25  | 33554432            | plays no through balls                                 |
| 26  | 67108864            | stops play                                             |
| 27  | 134217728           | **knocks ball past opponent**                          |
| 28  | 268435456           | moves ball to right foot before dribble attempt        |
| 29  | 536870912           | **moves ball to left foot before dribble attempt**     |
| 30  | 1073741824          | dwells on ball                                         |
| 31  | 2147483648          | arrives late in opponents area                         |
| 32  | 4294967296          | **tries to play way out of trouble**                   |
| 33  | 8589934592          | stays back at all times                                |
| 34  | 17179869184         | avoids using weaker foot                               |
| 35  | 34359738368         | **tries tricks**                                       |
| 36  | 68719476736         | tries long range free kicks                            |
| 37  | 137438953472        | dives into tackles                                     |
| 38  | 274877906944        | does not dive into tackles                             |
| 39  | 549755813888        | **cuts inside from both wings** (FMRTE: "cuts inside") |
| 40  | 1099511627776       | hugs line                                              |
| 41  | 2199023255552       | gets crowd going                                       |
| 42  | 4398046511104       | **tries first time shots**                             |
| 43  | 8796093022208       | tries long range passes                                |
| 44  | 17592186044416      | **likes ball played into feet**                        |
| 45  | 35184372088832      | hits free kick with power                              |
| 46  | 70368744177664      | likes to beat man repeatedly                           |
| 47  | 140737488355328     | likes to switch ball to other flank                    |
| 48  | 281474976710656     | will retire at top                                     |
| 49  | 562949953421312     | will play football as long as possible                 |
| 50  | 1125899906842624    | possesses long flat throw                              |
| 51  | 2251799813685248    | **runs with ball often**                               |
| 52  | 4503599627370496    | runs with ball rarely                                  |
| 53  | 9007199254740992    | attempts to develop weaker foot                        |
| 54  | 18014398509481984   | static target man                                      |
| 55  | 36028797018963968   | uses long throw to start counter attack                |
| 56  | 72057594037927936   | refrains from taking long shots                        |
| 57  | 144115188075855872  | cuts inside from left wing                             |
| 58  | 288230376151711744  | **cuts inside from right wing**                        |
| 59  | 576460752303423488  | crosses early                                          |
| 60  | 1152921504606846976 | brings ball out of defence                             |
| 61  | 2305843009213693952 | plays ball with feet                                   |

Whether the engine reads traits during a match hasn't been tested.

## Every person

Every player, manager, staff member, backroom entry and official has these.
Except for the referee (above), their values come from the person record:

| Name                           | Type  | Meaning                                                    | Database (person record)                                                        |
|--------------------------------|-------|------------------------------------------------------------|---------------------------------------------------------------------------------|
| `.person_uid`                  | `u32` | the person's unique ID                                     | `+0x0C`                                                                         |
| `.nationality`                 | `u32` | the unique ID of the person's nation, e.g. 765 for England | `+0x70` (a pointer to the nation; its unique ID is at `+0x0C`)                  |
| `.personality.adaptability`    | `u8`  | 1-20                                                       | `+0x78`                                                                         |
| `.personality.ambition`        | `u8`  | 1-20                                                       | `+0x79`                                                                         |
| `.personality.loyalty`         | `u8`  | 1-20                                                       | `+0x7A`                                                                         |
| `.personality.pressure`        | `u8`  | 1-20                                                       | `+0x7B`                                                                         |
| `.personality.professionalism` | `u8`  | 1-20                                                       | `+0x7C`                                                                         |
| `.personality.sportsmanship`   | `u8`  | 1-20                                                       | `+0x7D`                                                                         |
| `.personality.temperament`     | `u8`  | 1-20                                                       | `+0x7E`                                                                         |
| `.age`                         | `u8`  | age in years on the match date                             | from the date of birth at `+0x44`                                               |
| `.caps`                        | `u32` | international caps (senior)                                | matches the game's player screen for 3 players; no single database offset found |
| `.height`                      | `u32` | height in metres, as a 32-bit float (below)                | player record `+0x14E` (`u16`, centimetres)                                     |
| `.weight`                      | `u8`  | weight in kilograms                                        | player record `+0x14C`                                                          |

Height and weight come from the player record, so only players have their
real values. Everyone else has a default, usually 1.82 m and 75 kg.

`.height` holds the raw bits of a float: Ollie Watkins' 180 cm is
`1072064102`, i.e. 1.8. To convert in Python:

```python
import struct
metres = struct.unpack('<f', struct.pack('<I', field['u32']))[0]  # 1072064102 -> 1.8
field['u32'] = struct.unpack('<I', struct.pack('<f', 1.85))[0]     # set 1.85 m
```

Controversy (`+0x7F`) isn't copied. A player block's `.person_uid` is
written in a different place from other people's, so a player block has it
only once.

## Staff attributes

The manager, every backroom entry and `team_extra` carry a staff member's
attributes. `home.staff[0..2]` don't: they have only a person record. Each
attribute is a `u8` named `<block>.attributes.<attribute>`, followed by the
staff member's reputation and ability:

```json
{"name": "home.backroom[5].attributes.tactical_attacking", "u8": 12},
...
{"name": "home.backroom[5].attributes.coaching_set_pieces", "u8": 40},
{"name": "home.backroom[5].home_reputation", "u16": 6250},
{"name": "home.backroom[5].current_reputation", "u16": 6500},
{"name": "home.backroom[5].world_reputation", "u16": 5250},
{"name": "home.backroom[5].current_ability", "u16": 130},
{"name": "home.backroom[5].potential_ability", "u16": 140},
```

Reputations are 0-10000 and abilities 1-200. The 52 attributes are a copy of
the staff record that starts `0xE0` bytes before the person record in the
game's memory (`MATCH_STAFF+0x60` to `+0x93`), in this order. The first 27
are on the game's 1-20 scale. The other 25 are on the 1-100 scale that the
game shows as `(value + 2) / 5`, like a player's ratings: a Motivating of 16
is stored as 80.

| #  | Scale | Name                                            | #  | Scale | Name                             |
|----|-------|-------------------------------------------------|----|-------|----------------------------------|
| 0  | 1-20  | `tactical_attacking`                            | 26 | 1-20  | `unknown_26`                     |
| 1  | 1-20  | `business`                                      | 27 | 1-100 | `coaching_gk_shot_stopping`      |
| 2  | 1-20  | `unknown_2`                                     | 28 | 1-100 | `man_management_or_judging_ca_a` |
| 3  | 1-20  | `tactical_directness`                           | 29 | 1-100 | `judging_pa`                     |
| 4  | 1-20  | `determination_or_discipline`                   | 30 | 1-100 | `man_management_or_judging_ca_b` |
| 5  | 1-20  | `tactical_free_roles`                           | 31 | 1-100 | `motivating`                     |
| 6  | 1-20  | `interference`                                  | 32 | 1-100 | `physiotherapy`                  |
| 7  | 1-20  | `tactical_marking`                              | 33 | 1-100 | `tactical_knowledge`             |
| 8  | 1-20  | `tactical_offside`                              | 34 | 1-100 | `coaching_attacking`             |
| 9  | 1-20  | `patience`                                      | 35 | 1-100 | `coaching_defending`             |
| 10 | 1-20  | `pressing_or_hardness_of_training_a`            | 36 | 1-100 | `coaching_fitness`               |
| 11 | 1-20  | `resources`                                     | 37 | 1-100 | `coaching_mental`                |
| 12 | 1-20  | `working_with_youngsters`                       | 38 | 1-100 | `coaching_technical`             |
| 13 | 1-20  | `unknown_13` (74, 66 and 100 in the files seen) | 39 | 1-100 | `coaching_tactical`              |
| 14 | 1-20  | `buying_players`                                | 40 | 1-100 | `coaching_dirtiness_allowance`   |
| 15 | 1-20  | `mind_games`                                    | 41 | 1-100 | `coaching_gk_handling`           |
| 16 | 1-20  | `tactical_sitting_back`                         | 42 | 1-100 | `coaching_gk_distribution`       |
| 17 | 1-20  | `tactical_use_of_playmaker`                     | 43 | 1-100 | `coaching_versatility`           |
| 18 | 1-20  | `tactical_use_of_substitutions`                 | 44 | 1-100 | `judging_player_data`            |
| 19 | 1-20  | `tactical_depth`                                | 45 | 1-100 | `judging_team_data`              |
| 20 | 1-20  | `tactical_flamboyancy`                          | 46 | 1-100 | `presenting_data`                |
| 21 | 1-20  | `tactical_flexibility`                          | 47 | 1-100 | `sports_science`                 |
| 22 | 1-20  | `pressing_or_hardness_of_training_b`            | 48 | 1-100 | `unknown_48`                     |
| 23 | 1-20  | `squad_rotation`                                | 49 | 1-100 | `negotiating`                    |
| 24 | 1-20  | `tactical_tempo`                                | 50 | 1-100 | `judging_staff_ability`          |
| 25 | 1-20  | `tactical_width`                                | 51 | 1-100 | `coaching_set_pieces`            |

The names come from matching two staff members' FMRTE profiles (Pako
Ayestarán and Josep Gombau) against their values. The `tactical_*` ones are
FMRTE's "tactical attributes" (the staff member's preferred style), not the
coaching ones. The two profiles had the same values for man management and
judging CA, and for pressing and hardness of training, so which of each
`_a`/`_b` pair is which isn't known. Only one of determination and level of
discipline is stored here. FMRTE's coaching attribute "player" isn't stored.

**Only the manager's attributes reach the engine.** In one fixture (Aston
Villa against Bristol City, seeds 1-50), maxing every known attribute of the
manager changed every seed's match and turned Villa's 46 wins and 4 draws
into 50 wins. Maxing those of any backroom entry or `team_extra`, alone or
all 20 together, played every seed exactly as before. That includes the
assistant manager, who makes the substitutions under `run`.

Maxing the manager's attributes one at a time in the same fixture (seeds
1-20, baseline 18 wins and 2 draws, 3.70-1.50 on average) changed matches for
five of them:

| Attribute                        | From | To  | Seeds changed | Result                              |
|----------------------------------|------|-----|---------------|-------------------------------------|
| `motivating`                     | 80   | 100 | 20 of 20      | 20 wins, 3.80-0.85                  |
| `man_management_or_judging_ca_b` | 50   | 100 | 20 of 20      | 15 wins, 4 draws, 1 loss, 3.35-1.35 |
| `working_with_youngsters`        | 12   | 20  | 14 of 20      | 18 wins, 2 draws, 3.75-1.50         |
| `determination_or_discipline`    | 16   | 20  | 8 of 20       | 18 wins, 2 draws, 3.50-1.30         |
| `tactical_attacking`             | 10   | 20  | 3 of 20       | 18 wins, 2 draws, 3.55-1.35         |

"Seeds changed" counts seeds whose final random-number state differed, so it
shows that the engine read the value, not that results improved. The other
41 known attributes changed nothing; `coaching_fitness` and
`coaching_mental` were already at their maximum and weren't tested. Twenty
seeds are too few to say how much each one helps. Judging CA is a scouting
attribute, so `_b`, which the engine reads, is probably man management, but
that hasn't been confirmed in the game.

## Teams

| Name                     | Type  | Meaning                        | Database (team record) |
|--------------------------|-------|--------------------------------|------------------------|
| `<side>.team.reputation` | `u16` | the team's reputation, 0-10000 | `+0xA8`                |

## Where these are in a `--full` export

In a `--full` export these fields carry a `member`. The table maps each one
to its database field. A player record starts `0x278` bytes before its person
record (`0x368` for a player who is also staff).

| `member`                          | Name                             | Database field                                        |
|-----------------------------------|----------------------------------|-------------------------------------------------------|
| `MATCH_PLAYER_DATA+0x4a`          | `.condition`                     | player `+0x1F8`                                       |
| `MATCH_PLAYER_DATA+0x4c`, `+0x4e` | `.sharpness[0]`, `.sharpness[1]` | player `+0x1F4`                                       |
| `MATCH_PLAYER_DATA+0x50`          | `.fatigue`                       | player `+0x1F6`                                       |
| `MATCH_PLAYER_DATA+0x56`          | `.home_reputation`               | player `+0x1FA`                                       |
| `MATCH_PLAYER_DATA+0x58`          | `.current_reputation`            | player `+0x1FC`                                       |
| `MATCH_PLAYER_DATA+0x5a`          | `.world_reputation`              | player `+0x1FE`                                       |
| `MATCH_PLAYER_DATA+0x5c`          | `.current_ability`               | player `+0x200`                                       |
| `MATCH_PLAYER_DATA+0x74`          | `.morale`                        | player `+0x25F` (constants.h calls it happiness)      |
| `MATCH_PLAYER_DATA+0x68`          | `.last_transfer_fee`             | player `+0x1E0`                                       |
| `MATCH_PLAYER_DATA+0xf4`          | `.preferred_position`            | player `+0x266`                                       |
| `MATCH_PLAYER_DATA+0x49`          | `.squad_number`                  | contract `+0x55`                                      |
| `MATCH_PLAYER_DATA+0x100`         | `.traits`                        | person `+0xF0`                                        |
| `fn_1a7fdc0+0x48`                 | `.caps`                          | not found                                             |
| `fn_1a7fdc0+0x69`                 | `.age`                           | person `+0x44` (date of birth)                        |
| `MATCH_PLAYER_DATA+0x108`         | `.ratings`                       | player `+0x208` to `+0x24C` (69 bytes)                |
| `fn_1a7fdc0+0x40`                 | `.person_uid`                    | person `+0x0C`                                        |
| `fn_1a7fdc0+0x54`                 | `.nationality`                   | person `+0x70`, then nation `+0x0C`                   |
| `fn_1a7fdc0+0x62` to `+0x68`      | `.personality.*`                 | person `+0x78` to `+0x7E` (order above)               |
| `(computed)` at site `1a8004e`    | `.height`                        | player `+0x14E`, divided by 100 and stored as a float |
| none, at site `1a80064`           | `.weight`                        | player `+0x14C`                                       |
| `MATCH_TEAM_BASE+0x112`           | `.team.reputation`               | team `+0xA8`                                          |

Some other members match a database field that has no name yet (for the
players; `MATCH_PLAYER_DATA+0x52` in 43 of 47):

| `member`                 | Database field                      |
|--------------------------|-------------------------------------|
| `MATCH_PLAYER_DATA+0x52` | player `+0xFC`                      |
| `MATCH_PLAYER_DATA+0x54` | player `+0x16D`                     |
| `fn_1a7fdc0+0x61`        | person `+0x169`                     |
| `fn_19ba6e0+0x30`        | person `+0x18` (`u64`)              |
| `fn_19ba6e0+0x38`        | person `+0x0C`: the unique ID again |
| `fn_19ba6e0+0x45`        | person `+0x91`                      |
| `fn_5305f90+0x4`, `+0x8` | person `+0x92`                      |

# Tactics

Each manager's tactic is in `<side>.manager.tactic`:

| Part               | What it is                                          |
|--------------------|-----------------------------------------------------|
| `takers`           | captains and set-piece takers                       |
| `part_4240`        | opposition instructions                             |
| `plan[0]`          | **the tactic in use**                               |
| `plan[1]`          | a second tactic (a default 4-4-2 in the files seen) |
| `set_pieces`       | set-piece routines                                  |
| `formation[0..25]` | a copy of each slot's position. Leave it alone      |

**What the engine reads.** For the side you manage, the engine reads
`plan[0]`'s mentality and team instructions. For the side the game's AI
manages, it reads only the mentality: changing the AI side's team
instructions, or a player's role, made no difference to any match. The AI side
seems to pick its own tactic when the match starts.

The tests so far have only covered mentality and team instructions. Whether
roles, duties, player instructions, set pieces and opposition instructions
change results hasn't been measured yet, though they are stored the same way.

## Mentality

`<side>.manager.tactic.plan[0].mentality` (`u8`):

| Value | Mentality      |
|-------|----------------|
| 1     | very defensive |
| 2     | defensive      |
| 3     | cautious       |
| 4     | balanced       |
| 5     | positive       |
| 6     | attacking      |
| 7     | very attacking |

## Bit sets

Team instructions, player instructions, positions and roles are **bit sets**:
one number in which each option is one bit. The number is the sum of the
values of the options that are on. The `flags` note lists them by name, and
`bit N` for a bit nobody has identified yet.

To turn an option **on**, add its value; to turn it **off**, subtract it. Only
add a value whose bit isn't already set (check `flags`), and only subtract one
that is.

Example: `team_instructions[0]` is `269225000`, and its `flags` are "passing
directness: shorter", "pass into space", "crosses: low", "run at defence",
"focus play: down the left", "focus play: down the right" and "goalkeeper:
roll it out". That's 8 + 32 + 1024 + 2048 + 262144 + 524288 + 268435456 =
269225000. To turn "run at defence" off, subtract 2048: `269222952`.

In Python, `value | (1 << bit)` sets a bit and `value & ~(1 << bit)` clears
it.

**Options with levels** use one bit per level, and the middle (default) level
uses none. Set at most one bit of such a group. Passing directness "shorter"
is 8. To make it "much shorter", subtract 8 and add 16.

## Team instructions

`<side>.manager.tactic.plan[0].team_instructions[0]`, `[1]` and `[2]` are three
`u32` bit sets. In the table, *word* is which of the three, and *value* is what
the option adds.

| Word | Bit | Value      | Option                                                                                  |
|------|-----|------------|-----------------------------------------------------------------------------------------|
| 0    | 1   | 2          | passing directness: much more direct                                                    |
| 0    | 2   | 4          | passing directness: more direct                                                         |
| 0    | 3   | 8          | passing directness: shorter                                                             |
| 0    | 4   | 16         | passing directness: much shorter                                                        |
| 0    | 5   | 32         | pass into space                                                                         |
| 0    | 6   | 64         | play out of defence                                                                     |
| 0    | 7   | 128        | hit early crosses                                                                       |
| 0    | 8   | 256        | crosses: floated                                                                        |
| 0    | 9   | 512        | crosses: whipped                                                                        |
| 0    | 10  | 1024       | crosses: low                                                                            |
| 0    | 11  | 2048       | run at defence                                                                          |
| 0    | 12  | 4096       | dribble less                                                                            |
| 0    | 13  | 8192       | shoot on sight                                                                          |
| 0    | 14  | 16384      | work ball into box                                                                      |
| 0    | 15  | 32768      | play for set pieces                                                                     |
| 0    | 16  | 65536      | be more expressive                                                                      |
| 0    | 17  | 131072     | be more disciplined                                                                     |
| 0    | 18  | 262144     | focus play: down the left                                                               |
| 0    | 19  | 524288     | focus play: down the right                                                              |
| 0    | 20  | 1048576    | focus play: through the middle                                                          |
| 0    | 23  | 8388608    | overlap left                                                                            |
| 0    | 24  | 16777216   | overlap right                                                                           |
| 0    | 26  | 67108864   | underlap left                                                                           |
| 0    | 27  | 134217728  | underlap right                                                                          |
| 0    | 28  | 268435456  | goalkeeper: roll it out                                                                 |
| 0    | 29  | 536870912  | goalkeeper: throw it long                                                               |
| 0    | 30  | 1073741824 | goalkeeper: take short kicks                                                            |
| 0    | 31  | 2147483648 | goalkeeper: take long kicks                                                             |
| 1    | 0   | 1          | distribute to centre-backs                                                              |
| 1    | 1   | 2          | distribute to full-backs                                                                |
| 1    | 2   | 4          | distribute to flanks                                                                    |
| 1    | 3   | 8          | distribute over opposition defence                                                      |
| 1    | 4   | 16         | distribute to playmaker                                                                 |
| 1    | 5   | 32         | distribute to target forward                                                            |
| 1    | 7   | 128        | goalkeeper: distribute quickly                                                          |
| 1    | 8   | 256        | goalkeeper: slow pace down                                                              |
| 1    | 9   | 512        | tempo: much higher                                                                      |
| 1    | 10  | 1024       | tempo: higher                                                                           |
| 1    | 11  | 2048       | tempo: lower                                                                            |
| 1    | 12  | 4096       | tempo: much lower                                                                       |
| 1    | 13  | 8192       | attacking width: wide                                                                   |
| 1    | 14  | 16384      | attacking width: fairly wide                                                            |
| 1    | 15  | 32768      | attacking width: fairly narrow                                                          |
| 1    | 16  | 65536      | attacking width: narrow                                                                 |
| 1    | 17  | 131072     | when possession won: counter                                                            |
| 1    | 18  | 262144     | when possession won: hold shape                                                         |
| 1    | 21  | 2097152    | defensive line: much higher                                                             |
| 1    | 22  | 4194304    | defensive line: higher                                                                  |
| 1    | 23  | 8388608    | defensive line: lower                                                                   |
| 1    | 24  | 16777216   | defensive line: much lower                                                              |
| 1    | 26  | 67108864   | line of engagement: high press                                                          |
| 1    | 27  | 134217728  | line of engagement: mid block                                                           |
| 1    | 28  | 268435456  | line of engagement: low block                                                           |
| 1    | 30  | 1073741824 | trigger press: much more often                                                          |
| 1    | 31  | 2147483648 | trigger press: more often                                                               |
| 2    | 0   | 1          | trigger press: less often                                                               |
| 2    | 1   | 2          | trigger press: much less often                                                          |
| 2    | 2   | 4          | prevent short goalkeeper distribution                                                   |
| 2    | 3   | 8          | when possession lost: counter-press                                                     |
| 2    | 4   | 16         | when possession lost: regroup                                                           |
| 2    | 5   | 32         | tackling: get stuck in                                                                  |
| 2    | 6   | 64         | tackling: stay on feet                                                                  |
| 2    | 8   | 256        | legacy, probably "use offside trap". FM24 can't change it and the engine never reads it |
| 2    | 9   | 512        | step up more                                                                            |
| 2    | 10  | 1024       | drop off more                                                                           |
| 2    | 11  | 2048       | time wasting: often                                                                     |
| 2    | 12  | 4096       | time wasting: sometimes                                                                 |
| 2    | 13  | 8192       | trap inside                                                                             |
| 2    | 14  | 16384      | trap outside                                                                            |
| 2    | 15  | 32768      | stop crosses                                                                            |
| 2    | 16  | 65536      | invite crosses                                                                          |

Groups where only one option can be on, as in the game: each "passing
directness", "crosses", "tempo", "attacking width", "defensive line", "line
of engagement", "trigger press" (across words 1 and 2), "time wasting",
"tackling" and "when possession ..." group; the goalkeeper's distribution type
(0:28-31) and target (1:0-5); "goalkeeper: distribute quickly / slow pace
down"; "step up / drop off"; "trap inside / outside"; "stop / invite
crosses". "Focus play" down the left and down the right can both be on (both
flanks). The game itself never sets two options of one group, so what the
engine does with such a combination is unknown.

In one test (Villa against Bristol City), clearing all of Villa's team
instructions cost about one goal of goal difference per match. No single
instruction made a clear difference alone; the largest were "defensive line:
much higher", "line of engagement: high press" and "trigger press: much more
often".

## Formation: positions, roles and duties

`plan[0]` has 11 formation slots, `slot[0]` to `slot[10]`. Each has:

- `.slot[i].position` (`u32` bit set): where the slot is on the pitch;
- `.slot[i].role` (`u64` bit set): the role and duty;
- `.slot[i].player_instructions[...]`: the player instructions (next
  section).

For your side, `slot[i]` is played by `player[i]`.

**Position**: one position bit, plus a side bit when the formation has two of
that position (two centre-backs, two strikers):

| Bit | Value   | Position                |
|-----|---------|-------------------------|
| 0   | 1       | GK                      |
| 2   | 4       | DR                      |
| 3   | 8       | DL                      |
| 4   | 16      | DC                      |
| 5   | 32      | WBR                     |
| 6   | 64      | WBL                     |
| 7   | 128     | DM                      |
| 8   | 256     | MR                      |
| 9   | 512     | ML                      |
| 10  | 1024    | MC                      |
| 11  | 2048    | AMR                     |
| 12  | 4096    | AML                     |
| 13  | 8192    | AMC                     |
| 14  | 16384   | ST                      |
| 20  | 1048576 | the left one of a pair  |
| 21  | 2097152 | the right one of a pair |

For example, the right-hand of two centre-backs is 16 + 2097152 = `2097168`,
and FM's "DMCL" is DM + left = 128 + 1048576 = `1048704`. This order differs
from the order of the position ratings.

Changing positions moves players around the pitch, but this hasn't been
tested in the engine, and the game would normally change roles and
instructions with them. `formation[i]` holds a copy of each slot's position.

**Role and duty**: one role bit plus one duty bit:

| Bit | Value       | Duty      |
|-----|-------------|-----------|
| 21  | 2097152     | defend    |
| 22  | 4194304     | support   |
| 23  | 8388608     | attack    |
| 34  | 17179869184 | automatic |

| Bit | Value            | Role                        |
|-----|------------------|-----------------------------|
| 0   | 1                | goalkeeper                  |
| 1   | 2                | central_defender            |
| 2   | 4                | full_back                   |
| 3   | 8                | wing_back                   |
| 4   | 16               | defensive_midfielder        |
| 5   | 32               | central_midfielder          |
| 6   | 64               | wide_midfielder             |
| 7   | 128              | winger                      |
| 9   | 512              | attacking_midfielder        |
| 10  | 1024             | deep_lying_forward          |
| 11  | 2048             | advanced_forward            |
| 12  | 4096             | sweeper_keeper              |
| 14  | 16384            | libero                      |
| 15  | 32768            | deep_lying_playmaker        |
| 16  | 65536            | box_to_box_midfielder       |
| 17  | 131072           | advanced_playmaker          |
| 18  | 262144           | target_man (target forward) |
| 19  | 524288           | poacher                     |
| 20  | 1048576          | complete_forward            |
| 24  | 16777216         | ball_playing_defender       |
| 27  | 134217728        | inside_forward              |
| 28  | 268435456        | ball_winning_midfielder     |
| 29  | 536870912        | no_nonsense_centre_back     |
| 30  | 1073741824       | defensive_winger            |
| 31  | 2147483648       | pressing_forward            |
| 32  | 4294967296       | trequartista                |
| 33  | 8589934592       | anchor_man                  |
| 35  | 34359738368      | half_back                   |
| 36  | 68719476736      | no_nonsense_full_back       |
| 37  | 137438953472     | enganche                    |
| 38  | 274877906944     | complete_wing_back          |
| 39  | 549755813888     | regista                     |
| 40  | 1099511627776    | false_nine                  |
| 41  | 2199023255552    | shadow_striker              |
| 42  | 4398046511104    | wide_target_man             |
| 43  | 8796093022208    | wide_playmaker              |
| 44  | 17592186044416   | inverted_wing_back          |
| 45  | 35184372088832   | raumdeuter                  |
| 46  | 70368744177664   | roaming_playmaker           |
| 47  | 140737488355328  | mezzala                     |
| 48  | 281474976710656  | carrilero                   |
| 49  | 562949953421312  | inverted_winger             |
| 50  | 1125899906842624 | segundo_volante             |
| 51  | 2251799813685248 | wide_centre_back            |
| 52  | 4503599627370496 | inverted_full_back          |

Example: a sweeper keeper on defend is 4096 + 2097152 = `2101248`; a
pressing forward on support is 2147483648 + 4194304 = `2151677952`.

Pick a duty the role has in the game (a sweeper keeper has defend, support
and attack; a poacher only attack). The game never builds other combinations,
so how the engine treats them is unknown.

## Player instructions

Each slot has a list of instruction sets, one per role the slot has had
(often 6-8):

```json
"name": "home.manager.tactic.plan[0].slot[1].player_instructions[central_defender]", "u64": 2
"name": "home.manager.tactic.plan[0].slot[1].player_instructions[central_defender].bits[0]", "flags": ["dribble more", "tackle harder"], "u32": 2147491840
"name": "home.manager.tactic.plan[0].slot[1].player_instructions[central_defender].bits[1]", "flags": [], "u32": 0
...
"name": "home.manager.tactic.plan[0].slot[1].player_instructions[ball_playing_defender].bits[0]", ...
```

The `u64` names the role (its role bit). `bits[0]` and `bits[1]` are the
instructions. When you set an instruction in the game, it is stored in
**every** role's set of that slot, so it survives a role change. To match
that, make the same edit in all of the slot's sets. Which set the engine reads
(probably the one for the slot's current role) hasn't been tested.

| Word | Bit | Value      | Instruction                                                                 |
|------|-----|------------|-----------------------------------------------------------------------------|
| 0    | 1   | 2          | unknown (seen once; perhaps "shorter passing", which otherwise sets no bit) |
| 0    | 2   | 4          | more direct passing                                                         |
| 0    | 3   | 8          | take more risks                                                             |
| 0    | 4   | 16         | take fewer risks                                                            |
| 0    | 5   | 32         | cross more often                                                            |
| 0    | 6   | 64         | cross less often                                                            |
| 0    | 7   | 128        | cross from deep                                                             |
| 0    | 8   | 256        | cross from byline                                                           |
| 0    | 9   | 512        | cross to near post                                                          |
| 0    | 10  | 1024       | cross to far post                                                           |
| 0    | 11  | 2048       | cross to centre                                                             |
| 0    | 12  | 4096       | cross to target forward                                                     |
| 0    | 13  | 8192       | dribble more                                                                |
| 0    | 14  | 16384      | dribble less                                                                |
| 0    | 15  | 32768      | run wide with ball                                                          |
| 0    | 16  | 65536      | cut inside with ball                                                        |
| 0    | 18  | 262144     | shoot more often                                                            |
| 0    | 19  | 524288     | shoot less often                                                            |
| 0    | 22  | 4194304    | get further forward                                                         |
| 0    | 23  | 8388608    | hold position                                                               |
| 0    | 24  | 16777216   | roam from position                                                          |
| 0    | 25  | 33554432   | stay wider                                                                  |
| 0    | 26  | 67108864   | sit narrower                                                                |
| 0    | 27  | 134217728  | move into channels                                                          |
| 0    | 30  | 1073741824 | trigger press less often (the only option besides balanced)                 |
| 0    | 31  | 2147483648 | tackle harder                                                               |
| 1    | 0   | 1          | ease off tackles                                                            |
| 1    | 1   | 2          | mark tighter                                                                |
| 1    | 2   | 4          | mark a specific player                                                      |
| 1    | 3   | 8          | mark a specific position                                                    |

Unnamed bits (e.g. `bit 29`) appear in some role sets the game made itself.
Leave them as they are.

**Marking:** with "mark a specific player" on, a field shortly after `bits[1]`
(member `fn_1a0fca0+0xc`) holds the opponent's index, and 255 means nobody.
With "mark a specific position" on, the set's last `u32` is the position, as a
position bit set (see the position table).

## Opposition instructions

`<side>.manager.tactic.part_4240` holds one record per opponent: four `u32`
values followed by six `u8`. In the one case mapped (marking Bristol City's
goalkeeper), four consecutive fields changed, most likely these four `u32`:

| Field               | Values                            |
|---------------------|-----------------------------------|
| 1st: tight marking  | 0 none, 1 never, 2 always         |
| 2nd: trigger press  | 0 none, 1 never, 2 always         |
| 3rd: tackling       | 0 none, 1 normal, 2 easy, 3 hard  |
| 4th: show onto foot | 0 none, 1 left, 2 right, 3 weaker |

Which record belongs to which opponent isn't mapped yet.

## Set pieces and takers

These are only partly mapped:

- `set_pieces`: each routine (corners, free kicks, throw-ins, ...) has a
  delivery target in its `fn_<address>+0x148` member and, where the routine
  has one, a delivery type in `+0x150` (0 inswinger, 1 outswinger).
  - Corners: 0 mixed, 1 short, 2 near post, 3 far post, 4 edge of area, 5
    centre.
  - Wide free kicks: 0 mixed, 1 short, 3 near post, 4 centre, 5 far post, 6
    best header.
  - Which `fn_<address>` is which routine isn't recorded yet.
  - Throw-ins in attack: `TACTIC_SET_PIECES+0x1`, 0 retain possession, 1 work
    the line.
- `takers`: short lists of players first (the first list is the captains;
  then penalty, free-kick and corner takers), then ranked lists of the whole
  squad that the game rebuilds when takers change. Players are identified by
  a database index, which each player block writes just before its
  `person_uid`.

# Substitutions and the manager's flags

The first field of `<side>.manager` is the manager's flag word (`u32`). For
the side you manage, `run` resets it to `0x21306` (135942) before kick-off:
the value the game uses at kick-off when you choose no Match Plan. Your
assistant then makes substitutions, as when you let the game simulate a
match. AI-managed sides are left as they are. Use `run --human-control` to
play the file's own value instead; a side that the flags say is under human
control makes no changes during the match, not even for injuries.

# Officials, venue and weather

These are drawn from the seed when the game builds the match: the officials'
ratings for the day, the attendance, the weather and a random number per
player. `run` takes them from the game's build for each seed, unless you
edited them. If you edit one, your value is used for every seed.

Their fields are mostly unnamed: look in the `official[0..3]`, `referee` and
`venue` blocks. Names such as the stadium are `str` fields you can edit.

# Changing the opponent

You can't swap teams by editing the file: another squad writes a different
number of fields. Have the game build the match against another team instead:

```
fm24ctl replace-away next-fixture.json 677 luton.json
fm24ctl replace-away next-fixture.json "Luton Town" luton.json
```

The team is its unique ID in the game's database, or its club's short name as
the game shows it ("Man City", not "Manchester City"). Case doesn't matter, and
part of a name will do if no club's name matches in full. A name picks the
club's first team. If the name fits more than one club, the command lists them
with their IDs; use an ID then.

The game builds the new side, picking the squad and tactic as it would for a
real match. Your own side keeps the line-up and tactic you picked in the game.
Your edits to the rest of the file are kept, as `run` keeps them. Edits to the
old away team are dropped, because those players aren't in the match any more.
The result line says how many edits were kept and dropped.

The output has `"away_team": <ID>` in its header. `run` reads it and builds
the new opponent for every seed, so seeds work as for any other file. Don't
remove it. Run `replace-away` again on the output to change the opponent again.

Limits:

- The input must still be the human manager's next fixture, as for `run`. Only
  the away team can be replaced, and only when it is the opponent: if you
  manage the away side, the command refuses.
- The match stays a match of the original competition, venue and date. Only
  the team changes; the game doesn't check that the new team could play in
  it.
- Some of your side's tactic depends on the opponent, such as the opposition
  instructions (`part_4240`), whose records are per opposing player. The game
  rebuilds these for the new opponent. An edit there that can't be placed in
  the new build fails the command and names the field in `harness.log`.
- While building, the harness briefly puts the new team into the game's own
  fixture and the human's records of the match, then restores them. Run it
  only while the game is idle, as for every other command.

# Editing with a script

Read the file, find fields by `name`, change the values and write it back
(UTF-8, no BOM):

```python
import json

with open('next-fixture.json', encoding='utf-8') as f:
    fixture = json.load(f)
fields = {field['name']: field for field in fixture['fields'] if 'name' in field}

# Mentality: attacking.
fields['home.manager.tactic.plan[0].mentality']['u8'] = 6

# Team instructions: turn "run at defence" (word 0, bit 11) off.
field = fields['home.manager.tactic.plan[0].team_instructions[0]']
field['u32'] &= ~(1 << 11)

# Every home player: pace +2 in game (+10 stored), capped at 100.
for name, field in fields.items():
    if name.startswith('home.player[') and name.endswith('.ratings'):
        field['u8map']['pace'] = min(100, field['u8map']['pace'] + 10)

with open('edited.json', 'w', encoding='utf-8') as f:
    json.dump(fixture, f, ensure_ascii=False)
```

The `flags` notes go stale when you change a value this way; that does no
harm. Then `fm24ctl run edited.json 1-200`.

A few block names repeat (`home.manager` names both its first field and a
later one), and the dictionary keeps the last field with each name. Every
name used above is unique.

# Not known yet

- What most of the unnamed fields are. In a `--full` export many carry a
  `member`, which says where the value lives in the game but not what it
  means.
- Whether the engine reads the position ratings, roles, duties, player
  instructions, set pieces and opposition instructions (not tested yet).
- Whether the engine reads the [values copied from the
  database](#values-copied-from-the-games-database) (condition, sharpness,
  personality, ...).
- Player instruction bit 0:1, and the other unnamed bits.
- The [staff attributes](#staff-attributes) named `unknown_*`, which of each
  `_a`/`_b` pair is which, and how much each of the manager's attributes the
  engine reads changes results.
- Which set-piece routine is which, which takers list is which set piece, and
  which opposition record is which opponent.

Found something? Please
[open an issue](https://github.com/PhilipArmstead/FM-24-Match-Harness/issues).
