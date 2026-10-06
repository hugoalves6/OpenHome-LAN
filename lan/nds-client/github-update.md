# Direct GitHub app updates (v0.5.1)

The DS app itself fetches this fixed, public manifest over HTTPS:
`https://raw.githubusercontent.com/hugoalves6/OpenHome-LAN/lan-live-dsi/lan/nds-client/update.json`.
The manifest contains a strict `major.minor.patch` version, SHA-256 and the
immutable GitHub release URL for `OpenHomeMini.nds`. Only newer versions install.

Downloads stream to `/OpenHomeMini.nds.new`, verify SHA-256, then use the existing
backup/rename installer. The old binary remains available if downloading or
verification fails. Confirm installation with A and relaunch the app afterward.
Saves and `openhome-mini.ini` are not replaced. No GitHub account/token is needed.

Startup only checks GitHub and shows a persistent notice if a newer release is
available. It never starts an install or opens an installation prompt. Open
App updates to select Check for updates or Install update, then confirm with A.
During installation, save sync is disabled and the card's hub presence is
withdrawn to lock PC editing. Failure/cancellation resumes polling. After success,
the app displays "Press START to close the app" and "Then open OpenHome DS again";
sync and navigation remain disabled until closing/relaunching.

Once initialized, the manual App updates
action works when the hub is offline, provided Wi-Fi has internet access.
Hub reconnection retries do not repeatedly check GitHub. An offline update check
fails without changing the installed app. The console clock must be correct.

## HTTPS implementation and first-run setup

BearSSL 0.6 (MIT, `vendor/bearssl-0.6/LICENSE.txt`) implements TLS 1.2 with
hostname, certificate chain and certificate date validation. The compiled CA
anchors are generated from the Mozilla bundle downloaded from curl.se; the
original dated PEM bundle is included. No validation bypass or HTTP downgrade
is used. HTTPS redirects are limited to the fixed repository's release-download
path and GitHub's release asset hosts. Hub authentication headers are never sent
to GitHub. Body size is capped at 1 MiB, header parsing is bounded, and both
Content-Length and chunked responses are supported. B cancels network waits.

The DS has no exposed secure OS random device. On its first successful trusted
LAN hub login, the app derives private updater state from the server-generated
128-bit random session token. It saves `/openhome-updater.seed`, rotating it
before each TLS connection and separating next-state and handshake derivations.
There is no rand()/clock-only fallback. Initial provisioning uses the same
trusted LAN HTTP channel as the previous app updater: it is not resistant to
an attacker already intercepting that initial LAN connection. After provisioning,
the hub is not needed for updates. Never publish, share or clone the seed file.
If it is missing, connect to the trusted hub once or use `provision_updater.py`
on a PC to create a fresh per-card cryptographic seed. No seed is bundled.

The existing icon data is losslessly compressed from 665600 to 216573 bytes
to leave room for TLS within the old updater's 1 MiB limit. Each sprite is
decoded into a shared aligned buffer immediately before its DMA copy.

## Publishing an update

1. Bump `APP_VERSION`, build with devkitARM and verify the binary is <= 1 MiB.
2. Commit the source; publish a GitHub release with `OpenHomeMini.nds` attached.
3. Upload the completed asset before updating `update.json` on `lan-live-dsi`.
4. Set `version`, lowercase `sha256` and the immutable release `url`.
5. Keep earlier release assets intact for rollback/manual recovery.

The Pi's old update endpoint serves the current bootstrap for consoles migrating from v0.4.x.
Future releases are fetched directly by the native app; no Pi update poller runs.

Host tests exercise the actual TLS client against public GitHub releases,
redirects, checksum rejection, untrusted certificates, wrong hostnames and URL
restrictions. Real DSi TLS speed and the complete installation flow still need
hardware verification.
