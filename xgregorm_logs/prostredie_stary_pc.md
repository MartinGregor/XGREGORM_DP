# Prostredie starého počítača (mimo gitu aj mimo ns-3-dev)

Zapísané 2026-11-02 pri prenose. Dopĺňa `xgregorm_STAV_PRACE.md`, časť 3.

- Ubuntu 24.04.5 LTS vo WSL2, gcc 13.3.0, CMake 3.28.3, Python 3.12.3.
- `~/.bashrc`: `export SUMO_HOME=/usr/local/share/sumo`.
- **SUMO `v1_18_0`** zo zdrojákov (`~/deps/sumo`, https://github.com/eclipse-sumo/sumo.git): `CMAKE_BUILD_TYPE=Release`,
  `CMAKE_INSTALL_PREFIX=/usr/local`, zapnuté Python a Java väzby. Python balíky `traci`, `sumolib`, `libsumo`, `libtraci` 1.18.0 sú z tejto inštalácie.
- **gRPC `v1.60.0`** zo zdrojákov (`~/deps/grpc`, inštaluje `sandbox_builder.sh install-dependencies`) do `/usr/local`, vrátane protobuf, absl
  a zlib. `src/carla/CMakeLists.txt` ho vyžaduje (`find_package(gRPC CONFIG REQUIRED)`), bez neho `./ns3 configure` zlyhá, aj keď sa CARLA nepoužíva.
- Binárky ns-3 berú `libz.so.1` z `/usr/local/lib`.

## Skrátenie inštalácie

`xgregorm_usr_local.tar.gz` v tom istom GitHub Release je celý `/usr/local` starého počítača (SUMO aj gRPC, skompilované).
Použiť len na Ubuntu 24.04, inak skompilovať zo zdrojákov:

```bash
sudo tar -xzf xgregorm_usr_local.tar.gz -C /
sudo ldconfig
echo 'export SUMO_HOME=/usr/local/share/sumo' >> ~/.bashrc
sudo apt install -y $(cat xgregorm_logs/apt_balicky.txt)   # systémové knižnice, ktoré SUMO, gRPC a ns-3 potrebujú
sumo --version     # má vypísať 1.18.0
```
