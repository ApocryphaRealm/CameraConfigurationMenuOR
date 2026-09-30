# CCM - Camera Configuration Menu (Oblivion Remastered)

A free third-person camera with per-state framing and smoothing, configured on its own page in the Apocrypha Menu
Framework. An OBSE64 plugin. **Work in progress - test builds, see `CHANGELOG.md`.**

**Requires:** OBSE64 and the Apocrypha Menu Framework (Oblivion Remastered) 1.0.4 or later.

* Plan, probes and results: `D:\Claude output\4. plans\Camera Configuration Menu (CCM)\` (`PLAN.md`, `probes\RESULTS.md`).
* Build: `xmake f -p windows -a x64 -m releasedbg` then `xmake build CameraConfigurationMenu` (from PowerShell, not Git
  Bash - xmake picks MinGW there). `git submodule update --init --recursive` first.
* Shipped data is generated: `python tools/gen.py` (INI from the settings table, English strings from `TR()`).

Credits: the free-camera behaviour and styles are reimplemented from **Player Camera - Stop Looking at My Back** by
**igroshev1990** (Nexus oblivionremastered/mods/2998), under its permission to modify and convert with credit; no file
of it is included. The settings layout was informed by **SmoothCam** by **mwilsnd**; no SmoothCam code, text or asset
is included. The conversation camera's hold on the speaker follows the lock-on of **Ultimate Combat** by **Kramer7046**
(Nexus oblivionremastered/mods/5439) - its target socket, turn rates and arm hold - under its permission to modify with
credit; no file of it is included. GPL-3.0-or-later.
