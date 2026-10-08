# OpenHome LAN — TO-DO

## Current work

- [x] Windows LAN editing with explicit Save, Discard, and close protection.
- [x] DSi staged box changes and verified hub-to-card delivery.
- [x] Switch staged bank edits with explicit Save, Discard, and close protection.
- [x] Windows release check, checksum-verified installer download, and installer handoff.
- [ ] Publish and test the Switch 0.3.8 build on hardware.
- [ ] Add a Settings toggle for Windows update checks and a visible manual update result.
- [ ] Show hub delivery phases: uploaded, waiting for DS, delivered to card.

## Reliability and layout

- [ ] One Activity page for uploads, delivery, conflicts, backups, and retry failures.
- [ ] A device dashboard with Windows, Switch, DSi, and hub presence plus last confirmed sync.
- [ ] An offline-copy badge on saves that cannot currently return to their source device.
- [ ] A conflict screen: keep card, keep hub, compare metadata, or preserve both backups.
- [ ] Restore timeline with automatic pre-save snapshots.

## A unique local Pokémon archive

- [ ] Journey timeline per Pokémon: origin, games visited, dates, ribbons, milestones, and player notes.
- [ ] “Where can this Pokémon go?” planner that shows compatible saves and conversion trade-offs before moving it.
- [ ] Collection goals across local games: living dex, regional dex, ribbon master, shiny dex, events, and custom goals.
- [ ] Safe clone detector that groups likely duplicates without deleting anything.
- [ ] Local-first trainer/family archive with tags, favourites, stories, and no cloud account requirement.
- [ ] QR pairing for Windows, DS/DSi, and Switch clients so setup avoids repeated IP/password entry.

## Later devices and formats

- [ ] DSi single-tap sync launcher that returns to TWiLight Menu++ after syncing changed saves.
- [ ] R4/DSPico-aware save-map presets.
- [ ] Emulator watchers for Ryujinx and other local emulators with explicit conflict handling.
- [ ] Read-only support for more save formats before enabling write support after round-trip validation.

## Non-negotiable rules

- Never overwrite a card or save silently.
- Keep checksummed backups before writes.
- Explain each device’s sync state on one screen.
- Keep the hub LAN-first and useful without internet access.
