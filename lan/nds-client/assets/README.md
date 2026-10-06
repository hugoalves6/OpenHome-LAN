# Launcher artwork

OpenHome DS v0.4.5 offers a connection choice when Wi-Fi is unavailable
or after three consecutive hub connection failures: A retries the saved network;
B continues offline and pauses automatic retries. Press L to reconnect later.
Offline mode preserves local saves; hub sync and boxes require a connection.

The icon and covers are encoded from the user-supplied `icon-nds.png` artwork.
These files retain the original design, resized for the launcher formats.

`icon.bmp` is a 32x32, uncompressed 4bpp BMP with palette entry zero reserved
for the exterior transparency. White inside the logo remains opaque.
It is embedded in OpenHomeMini.nds by ndstool.

`pico-cover.bmp` is an indexed 128x96 BMP; the logo fits in the visible
106x96 region. Install as `/_pico/covers/user/OpenHomeMini.nds.bmp`.

`twilight-boxart.png` is a 128x115 PNG. Install as
`/_nds/TWiLightMenu/boxart/OpenHomeMini.nds.png` and enable box art in settings.

If you rename the application, rename the cover to match its full filename.
Reopen the launcher after updating. Existing configuration is preserved.

Format references:
- https://github.com/LNH-team/pico-launcher/blob/develop/docs/Customization.md
- https://wiki.ds-homebrew.com/twilightmenu/how-to-get-box-art
