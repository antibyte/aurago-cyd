# Flash images for the AuraGo web flasher

AuraGo ships a copy under `internal/cyd/firmware/cyd/`. Refresh that folder
after a PlatformIO build if the web flasher should install a newer image.

Required files per variant (`cyd` or `cyd2usb`):

- `bootloader.bin`
- `partitions.bin`
- `boot_app0.bin`
- `firmware.bin`
- `version.txt`

Refresh after a PlatformIO build:

```bash
pio run -e cyd
# then copy .pio/build/cyd/{bootloader,partitions,firmware}.bin
# and the Arduino boot_app0.bin into firmware/cyd/
```
