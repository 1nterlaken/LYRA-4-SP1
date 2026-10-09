# Building LYRA-4

Zephyr **v4.3.1** and Zephyr SDK **0.17.4**, pinned in `west.yml`. The workspace path must not contain spaces, and this repository must be cloned into the workspace as a folder named `lyra4-sp1`.

```sh
# workspace/  contains: this clone (as lyra4-sp1), zephyr/, zephyr-sdk-0.17.4/
export ZEPHYR_BASE=$PWD/zephyr ZEPHYR_TOOLCHAIN_VARIANT=zephyr ZEPHYR_SDK_INSTALL_DIR=$PWD/zephyr-sdk-0.17.4
west build -p always -d build-lyra -b stem_player lyra4-sp1/firmware -- \
  -DBOARD_ROOT=$PWD/lyra4-sp1 "-DEXTRA_CONF_FILE=$PWD/lyra4-sp1/firmware/lyra.conf"
python3 -I lyra4-sp1/tools/ci/check_image.py --repo lyra4-sp1 build-lyra
```

Output: `build-lyra/zephyr/lyra4-sp1.bin` (this is what you flash). Do not flash an image that fails `check_image.py`.

All patches in `zephyr-patches/` must be applied to the Zephyr checkout first (`git apply` each).

## Variants
- Default: `firmware/lyra.conf`.

## Console
The USB serial console keeps a stored log in RAM and replays it when a terminal connects.
