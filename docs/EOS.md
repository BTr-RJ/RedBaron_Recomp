# EOS online integration

Epic Online Services supplies lobby discovery and P2P transport, with relay fallback. A host runs gameplay. Players use the original in-game match menus; the launcher asks only for an online player name. No separate room-service address is needed.

## Player flow

- **Public Player Match:** create a public session, search Custom Match, or use Quick Game. Multiple hosts may advertise simultaneously.
- **Quick Game:** searches compatible public competitive sessions across modes and chooses the lowest measured reachable ping. Version, content, category and capacity checks remain enforced. Full/private/unreachable sessions are not selected. Co-op uses its own two-player category.
- **Private Match:** creates an invite-only EOS lobby. RT in a supported hangar/lobby opens official Epic friends. Public sessions also support invitations; public search does not require an invitation.
- **Overlay:** Shift+F3 opens it after Epic authentication. RT is intercepted in the hangar/lobby, not as a weapon input during flight. A pre-search Player Match party is not yet a published gameplay session; create/find a match before gameplay invitations.
- **Name:** the launcher name applies to the local profile. Friends retain their Epic identities.

## Authentication and service setup

Epic Account Services provides login, friends and presence; EOS Connect supplies gameplay product-user identity. Basic Profile, Friends and Presence must be enabled and the same GameClient linked to the application. The official overlay/redistributable is included in the installation workflow.

**Public-access limitation:** brand review / authorization for arbitrary external Epic accounts has not been verified. Until approved, login or social features may be restricted to organization members and authorized test accounts. Code cannot bypass Epic access checks. Offline play remains available after authentication failure.

Local configuration is `eos/config.ini` beside the game. A release may distribute the player GameClient configuration required by EOS; that client must require a signed-in user and have only player permissions. Never put TrustedServer or administrative credentials in a game. Do not post local configuration or token-bearing logs in issues.

See Epic's [client policy guide](https://dev.epicgames.com/docs/epic-online-services/eos-fundamentals/client-and-client-policy/client-policy-guide) and [Account Services](https://dev.epicgames.com/docs/epic-account-services).

## Achievements

Local title metadata supplies twelve native achievements. The deployment recognizes those definitions and the port synchronizes only native IDs. Three unrelated Epic samples remain in the service and may appear in its overlay; the game ignores them. Extracted descriptions/icons are not committed. Real gameplay unlock/upload/restore needs community validation; unattended tests never grant artificial achievements.

## Validation and limits

Two Windows PCs on separate Internet connections reached a lobby and flight. A later session lasted roughly four and a half minutes before a confirmed manual exit. Separate transport tests exercised direct/forced-relay traffic and multiple identities. These do not prove eight real games, complete rounds, every mode or invite acceptance between real accounts.

Private/public/co-op session creation, native Quick Game handling, overlay opening and catalog queries have been exercised. Invite acceptance, presence, join/leave edge cases, long sessions and cloud score/achievement updates need public testing. No zero-latency or complete Xbox Live parity claim is made. See [Leaderboards](LEADERBOARDS.md).

Use **Update > Open logs folder** for reports. Include mode, public/private, host/client and error text. Remove tokens/personal details; do not attach game files or `eos/config.ini`.
