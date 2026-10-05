# OpenHome LAN fork

Windows fork of [andrewbenington/OpenHome](https://github.com/andrewbenington/OpenHome), based on commit `a61afa518e0c0ce4766cadefb940e1b5360b7be6` (1.18.0).

This adds **LAN console saves** to the Home page. Connect to the hub, keep OpenHome DS **v0.4.0 or later** on the console's home/save-sync screen, and open a synchronized DS save. Box edits save automatically after a short delay. Destructive releases retain OpenHome's explicit confirmation. Windows waits for the console to acknowledge the edited checksum before reporting success.

The DSi app must be running and awake; playing a game, leaving the app, or losing Wi-Fi locks editing. Presence is checked by a heartbeat (5-second idle polling, 25-second expiry), so abrupt disconnections can take up to 25 seconds to be detected. Settings and box browsing on the DSi do not currently run background sync. Return to its home screen for PC editing. DS client v0.4.3 retries failed connections after 5, 5, 5, 5, 10, 10, 10, 20, 20, then 30 seconds, keeping 30 seconds thereafter; a successful connection resets the schedule. These are waits between attempts; an individual connection attempt may also take time.

The first release opens **one live remote save at a time**, without other local saves open. Transfers to the Windows HOME bank use the existing editor. The bank belongs to this Windows installation; this does not introduce a shared Switch/Windows HOME bank.

## Safety and delivery

- Hub-side exclusive edit lease, refreshed while Windows is connected. Stale saves and expired/changed console sessions are rejected.
- Existing Switch uploads, FTP uploads, and native DS box moves cannot overwrite a save with an active Windows lease or pending delivery.
- Hub edits keep verified prior bytes under `.backups`; console downloads retain the previous card file and verify SHA256 before acknowledgment.
- A durable pending-delivery receipt survives hub restarts. A Pi upload is not proof of card delivery.
- If delivery times out, the editor retains its draft and the hub keeps the pending file. Reconnect the console, wait for synchronization, then reopen the remote save. A simultaneous card change requires explicit conflict resolution on the DSi.
- HOME-bank persistence and remote card writes are not a distributed atomic transaction. Do not close either application during a save; errors require inspection before repeating a cross-bank move.
- The LAN fork has its own application/configuration directories and installer identity. It does not migrate an existing OpenHome bank automatically.

## Hub

Python 3.11+; no Python dependencies are required for the server. Choose a password in your environment and keep the service bound to your private LAN address:

```powershell
$env:OHNX_PASSWORD = 'YOUR_HUB_PASSWORD'
python lan/server/lan_hub.py --bind 192.168.1.125 --port 8321 --data D:/OpenHomeData
```

Linux uses the same arguments and `OHNX_PASSWORD` environment variable. The existing OpenHomeNX HTTP endpoints are retained. New authenticated routes are `/ohnx/live/status` and POST `/ohnx/live/{pulse,acquire,renew,release,commit}`. Remote commits accept validated raw 512 KiB Gen 4/5 DS saves.

## DS client

The matching GPL-3.0 native client source is in `lan/nds-client`. Build with devkitARM, libnds, dswifi, libfat, and `make`. Install `OpenHomeMini.nds` and configure `/openhome-mini.ini` using the example, with a unique card ID and mappings to the actual card saves. Preserve your existing profile when updating an installed client. No credential or real save is shipped in this fork.

For the in-app updater, publish a manifest at `updates/openhome-mini.json` containing `version` and the binary's `sha256`, and place the matching binary at `updates/OpenHomeMini.nds`. Existing deployed clients require the binary to remain below 1 MiB. Native artwork/resources come from OpenHomeNX; see `lan/nds-client/vendor/port-assets.md` for attribution.

## Build and checks

Use the upstream Windows build prerequisites (Node 24+, pnpm, Rust, wasm-pack, MSVC build tools). Run `pnpm install`, `pnpm typecheck`, and `pnpm tauri build`. The resulting app is **OpenHome LAN**.

```powershell
python -m unittest discover -s lan/tests -q
pnpm exec vitest run src/core/lan
```

Tests use disposable fixtures: offline locking, exclusive leases, conflict checks, checksum acknowledgment, durable pending delivery, and Windows-to-hub HeartGold byte preservation. Actual DSi-to-Windows delivery must also be checked on hardware.
