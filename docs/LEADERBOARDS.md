# Native leaderboards and EOS

The port implements the native statistics enumerator, reads and writes, and an
EOS Stats/Leaderboards provider. This is an integration under validation, not a
claim that every original ranking/filter has been reproduced in production.

The original service schema is read from the user's local XEX metadata. Gzip
length/CRC and UTF-16 are checked before reading view IDs, column IDs and
aggregation rules. No extracted catalog, names, images or service XML is included
in the repository or installer. The local title currently supplies 17 views,
131 columns and 97 numeric per-view statistics.

## Product configuration

Starting the game writes `eos/leaderboard-setup.csv` beneath that installation's
user-data directory. This private, locally generated file lists the EOS board ID,
stat name, aggregation, primary score and reset category. Keep it out of Git and
public packages.

For the deployment used by `eos/config.ini`:

1. Enable Stats ingestion/query and Leaderboards query in the player client policy.
2. Create each catalog stat in **Epic Online Services → Stats**, preserving its
   aggregation exactly. `Last` becomes `LATEST`; SUM inputs are increments supplied
   by the native game, not cumulative totals invented by the port.
3. In **Leaderboards**, create each board ID, selecting the stat marked `Primary`.
   Ordinary boards have no end date. Their start must precede legitimate scores.
4. Weekly views require an explicit seven-day period. A new period can use the
   board ID followed by `_YYYYMMDD`. The runtime chooses the newest active matching
   period, checks its backing stat/aggregation, and applies its time bounds to
   secondary columns too. It rejects an unlimited period for a weekly view.

EOS does not create or repeat leaderboard periods through its client SDK. Future
weekly periods need administrative scheduling. The runtime refreshes definitions
every minute, so portal changes do not require reinstalling the game.

See Epic's [leaderboard setup guide](https://dev.epicgames.com/docs/epic-online-services/player-and-game-data/leaderboards-interface/leaderboards-guide/set-up-leaderboards).

## Behavior and validation limits

- The game keeps its original leaderboard screen. By-rank and centered-XUID
  enumerations are translated to EOS queries; the nested result layout preserves
  big-endian pointers, 64-bit rating and requested columns.
- Only the signed-in player's PUID can receive a native stat write. Host-supplied
  remote XUIDs are never used to impersonate other accounts. Submission from each
  participant at the end of a real match still needs verification.
- Native 64-bit values outside EOS's signed 32-bit stat range are rejected rather
  than truncated. Failed or ambiguous submissions are not automatically replayed,
  because duplicating SUM increments would corrupt rankings.
- An EOS ingestion success confirms request acceptance, not a score's appearance.
  Validation must obtain a real score through normal gameplay and read it back.
  No fabricated score is submitted by unattended tests.
- EOS's cached rank window limits pagination. Centering on a player or filtering
  requested XUIDs works within that window. Full original friend-filter behavior,
  players outside that window, weekly rollover and end-of-round scores are not
  yet validated. Xbox Live's historic rankings are not imported.
- Missing definitions and permissions are logged explicitly. A native empty/not
  ranked screen alone does not establish that the cloud ranking is operational.

CTest exercises a synthetic authored compressed schema, CRC/size rejection,
native enumeration layout/bounds, signed values and write decoding without game
assets or cloud score changes. The developer schema test also accepts a private
`title-service-schema.bin` export for opt-in local inspection.

On 2026-10-30 the private product was configured with all 97 native stats and
17 boards. The two weekly boards cover 2026-10-26 through 2026-11-02. A rebuilt
game authenticated, recognized **17/17** definitions and successfully read an
empty multiplayer ranking from its original menu. This proves definition/query
integration; scoring, friend filtering and a later weekly rollover still need
the gameplay checks above.
