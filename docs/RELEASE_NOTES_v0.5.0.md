# GTA V Xbox 360 recompilation v0.5.0 (draft release text)

One download per computer, and one Android app. Each installs the game from **your own** discs
and Title Update 27 and recompiles it on your device. No game files are included.

| your device | download |
|---|---|
| Windows 10/11 x64 | `gtav-recomp-windows-x64.zip` |
| Linux x64 (glibc 2.35+, e.g. Ubuntu 22.04+) | `gtav-recomp-linux-x64.tar.gz` |
| Mac with Apple silicon, macOS 15+ | `gtav-recomp-macos-arm64.zip` |
| Android phone (arm64, Android 11+, Vulkan 1.1, Snapdragon 845 or newer) | `GTAV.apk` (also in every desktop download, folder `android/`) |

**Updating from 0.3.0:** set up the new version in a new folder, then copy your old `data/user`
folder into it (that holds your saves). Android: install the new APK over the old one and press
Install once more: the game files stay, the game is rebuilt on the phone.

## New

- **Android sets itself up, like the PC:** the app now carries the recompiler and compiler
  (codegen, clang, lld, XenosRecomp, dxc), so the phone copies the game files from your discs,
  recompiles the game and builds it by itself. No PC step and no game pack any more. Copy your
  Disc 1 and Disc 2 images and the title update to the phone, choose them in the app and press
  Install (the files are checked, then the build takes about 30-60 minutes; keep the phone plugged
  in). Shaders are translated on the phone while you play, as on the desktop, so the first minutes
  are slower. The app still contains no game code.
- **Faster everywhere, uncapped by default:** no frame cap and no vsync out of the box (the
  settings menu still has VSync and a 30/60 fps limit). The recompiled game code keeps more of the
  Xbox registers in host registers and inlines the register save/restore helpers (main thread
  4-14% less CPU per frame on Linux); the renderer only binds the textures the shaders sample and
  converts each vertex snapshot once; driving is uncapped too (the 30 fps vehicle limit is an
  option in the settings menu now).
- **PC keyboard and mouse layout**, following the PC game's defaults, with every action
  remappable (settings menu > Controls; controller buttons too).
- **Settings menu (#6):** F1 with keyboard and mouse (Esc otherwise, RB+Back on a controller):
  Resolution (render scale up to 2x), Fullscreen, Language (the game's 12 languages; speech stays
  English), Controls.
- **Fullscreen (#8):** F11 or Alt+Enter.
- **One-click bug reports** from the settings menu, and a crash report offered on the next start.
- **Android without the disc images (#19):** copy the `game`, `update` and `user` folders from your
  PC install to the phone by USB and press Install without choosing anything: about 16 GB on the
  phone instead of about 40.

## Fixed

- **WASTED / BUSTED screen stuck above 30 fps (#21):** the death screen now fades and you respawn
  at the hospital at any frame rate.
- **Mission Failed crash in the prologue on Windows (#3).**
- **Cutscene dialogue stopping partway (#3, #4):** speech plays to the end.
- **Keyboard didn't work (#11):** keyboard and mouse now reach the game on every desktop, including
  the Display Calibration screen (Enter = accept, Backspace = back).
- **Black world on the first launch** while shaders were still being built.
- **Antivirus warning on the Windows package (#18):** the profiler DLL is no longer shipped.
- **Linux:** guest `Sleep(0)` slept 100 µs instead of yielding (the update thread calls it ~1,500
  times a frame); several kernel-object lookups no longer take the global lock; long boot crawls
  from missing-file probes on slow disks are gone.
- macOS: the game fills Retina windows instead of a corner (#14), and a grey-world setting is off
  by default (#13). Thanks to the contributors of #13 and #14.

## Known issues

Shadows and anti-aliasing differ from the 360 (#8, #29); vehicle handling at exactly ~60 fps runs
faster than on the Xbox (#12: the settings menu's 30 fps vehicle option restores the Xbox feel);
the open issues on GitHub list the rest. Phones: S23 FE ~28 fps when cool; iPhone 13 mini ~30 fps
in the city.

## Checksums

_(SHA-256 of every asset, filled in at final assembly)_
