# OpenCE: 16-player split screen and player skins

<p align="center">
  <img src="port/assets/icon/opence-icon.png" alt="OpenCE" width="128">
</p>

This is a fork of [OpenCommunityEdition/OpenCE](https://github.com/OpenCommunityEdition/OpenCE), the native port of Halo: Combat Evolved's Xbox code to Windows, Linux, Android and the web. Everything OpenCE does is still here. Its README covers downloads, settings, multiplayer and mods.

The work is on the `split16` branch.

## What this fork adds

- **16-player split screen.** Up to 16 local players on one computer in network games. The views are laid out in an even grid. Each view has its own HUD and fog, and controllers 5 to 16 can remap their buttons.
- **16-player co-op campaign.** Multiplayer > CO-OP CAMPAIGN hosts network co-op for up to 16 players, with every level open at any difficulty.
- **Player skins.** Each player chooses a body for multiplayer and co-op. The choice is synced to every machine and to each split-screen view.
  - Master Chief in each armour colour. This is the default.
  - Elites in each colour, or by rank: minor, major, spec ops, zealot and stealth.
  - Flood combat forms (human and Elite).
  - The Hunter. Its fuel rod cannon never runs out, charges before each shot, reloads after five, and it melees with its shield.
- **Choose a skin while joining.** On the ADD PLAYER screen, each player's picture is a 3D render of the body they chose, X changes it, and Y makes a new profile. A controller with no free profile gets one made for it. Profiles another player holds, and the game's unusable defaults, are hidden.
- **A pause menu that fits.** In co-op split screen, each player's pause menu is drawn whole inside their own view.

The skin bodies are copied at load time from your own Two Betrayals map (`c40.map`). A stock multiplayer map has them all. A co-op campaign level gets the ones it lacks, in memory of their own below the level's tag cache, so every body plays on every level. Custom Edition maps always play Master Chief.

Details and settings (`multiplayer.skin_1` to `skin_16`) are in [port/linux/README.md](port/linux/README.md).

## Playing it

You need your own Halo: Combat Evolved Xbox disc image (`.iso` or `.xiso`). No game data is included. At first start, the game asks for the disc image and extracts its maps.

There are no release downloads for this fork. To build it, install Python, ninja and clang, then run:

```sh
python configure.py --release
ninja windows   # or: ninja linux
```

See [OpenCE's README](https://github.com/OpenCommunityEdition/OpenCE#readme) for each platform's requirements.

## Credits and licence

This fork is built on OpenCE, by Jonas Volman (@bnunu), MrBruh (@cybersecurity) and [its contributors](https://github.com/OpenCommunityEdition/OpenCE/graphs/contributors). OpenCE comes from the decompilation of [bnunu/halo-1](https://github.com/bnunu/halo-1) and [punpckhdq/halo](https://github.com/punpckhdq/halo). The artists of the high-res redraws are listed in [AUTHORS.md](AUTHORS.md).

Licensed `AGPL-3.0-or-later`, as OpenCE is: see [LICENSE](LICENSE). Third-party code, fonts and art keep their own licences.

OpenCE is an unofficial fan project. It is not endorsed by or affiliated with Microsoft, Xbox Game Studios, Halo Studios, Activision or Bungie. Halo and Halo: Combat Evolved are trademarks of Microsoft Corporation.
