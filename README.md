# SEUMvita

Workspace for porting a Windows Unity game to PS Vita (VitaSDK + vitaGL).

## Toolchain (headless Linux)

```bash
scripts/setup-toolchain.sh
export VITASDK=/usr/local/vitasdk PATH=$VITASDK/bin:$PATH
```

Installs, into `$HOME/vita` and `/usr/local/vitasdk`:

| Piece | What it's for |
|---|---|
| VitaSDK (GCC 15.2) + ~108 vdpm packages (vitaGL, SDL2/3, OpenAL, ffmpeg, ...) | Compiling `eboot.bin` / VPKs |
| [psp2-tooling](https://github.com/Grarak/psp2-tooling) `retail-cp.skprx` | Makes a retail 3.65 Vita behave like a DevKit over USB |
| 3.65 DevKit `os0:kd` modules ([psvita-elfs `365-TOOL`](https://github.com/LiEnby/psvita-elfs/tree/365-TOOL/os0/kd)), ported to `*-retail.skprx` | DECI4p: TTY, launch/kill, host0:, core dumps, DTrace, Razor, screenshots |
| [vita-agent-use](https://github.com/shoui520/vita-agent-use) plugins + Python client | Agent control over Wi-Fi: upload, install VPK, launch, screenshot, input, crash events |

Known toolchain quirks handled by the script:
- `vdpm/install-all.sh` is broken against the current vdpm binary; the script uses `vdpm install` directly.
- `openssl` (1.0.2) is skipped: it conflicts with `openssl-1.1.1`, which `curl` already pulls in.
- vita-agent-use is built `MinSizeRel`: with `Release`, `vita-elf-create` fails on `vita_agent_shell` ("segment 1 overlaps").
- vitaGL now needs `stdc++` and `pthread` at link time.

## Porting plan

See [docs/PORTING_PLAN.md](docs/PORTING_PLAN.md). The numbers in it come from
`tools/asset_inventory.py` and `tools/level_budget.py` (`pip install UnityPy`,
then run each with `gamedata/Seum_Data` as the argument).

## Sample

`samples/hello-vitagl` is a smoke test (SDL2 input + vitaGL clear loop, title ID `SEUM00001`, built with `-na` so the profilers can map symbols):

```bash
cmake -S samples/hello-vitagl -B samples/hello-vitagl/build && cmake --build samples/hello-vitagl/build
```

## Installing on the Vita (one-time)

Needs a 3.65 Vita with Ensō + taiHEN (retail-cp patches 3.65 offsets; tested on PCH-2000) and VitaShell.

1. Copy `vita-deploy/ur0/tai/*` to `ur0:tai/` (VitaShell FTP or USB).
2. Merge `vita-deploy/config.txt.ADD-THESE-LINES` into `ur0:tai/config.txt`. `retail-cp` goes above other USB plugins.
3. Reboot. The first boot arms dev mode and restarts once by itself.
4. On the PC: `python3 vita-agent-use/client/vita_agent.py bootstrap`, then `session pair` and tap OK on the Vita.

Uninstall retail-cp with `psp2-tooling/devkit/retail_vita.sh uninstall`. Deleting the plugin alone does not clear the debug bootparams.
