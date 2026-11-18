# Backup, restore and flashing

Everyday flashing over Wi-Fi, build details and pitfalls: [dev-workflow.md](dev-workflow.md).

## Download mode
Needed if the USB port does not appear or keeps reconnecting.
1. Connect the watch over Micro-USB (a data cable).
2. Hold **BOOT**, short-press the crown, release BOOT after 2–3 s.
3. Check the port: `ls /dev/cu.usbmodem*` (macOS) or `ls /dev/ttyACM*` (Linux).

If that does not help (official LilyGO procedure): hold the crown until the port disappears, then while holding BOOT
press the crown for 1 s and release BOOT.

There is no reset button: after flashing, hold the crown 6 s (off) and 2 s (on).

## Full backup (before any flashing)
```sh
scripts/backup-flash.sh            # port auto-detected, file in ~/twatch-backups/
```
The script reads all 16 MB (`esptool --chip esp32s3 read-flash 0 0x1000000`), checks the size and writes a
`.sha256` next to it. esptool is taken from PATH or run through `uvx` without installing.

Check: the file is exactly `16777216` bytes and has a `.sha256`. Keep a copy off your laptop.

## Restore
```sh
scripts/restore-flash.sh ~/twatch-backups/twatchs3-backup-YYYYMMDD-HHMMSS.bin
```

## Official factory image (if you have no backup)
`LilyGoLib/firmware/twatchs3-sx1262-factory-20260919-0x0.bin` is written from address `0x0`:
```sh
curl -LO https://github.com/Xinyuan-LilyGO/LilyGoLib/raw/master/firmware/twatchs3-sx1262-factory-20260919-0x0.bin
scripts/restore-flash.sh twatchs3-sx1262-factory-20260919-0x0.bin
```
Pick the image matching your radio variant from the LilyGoLib `firmware/` folder.

## Flash this firmware
```sh
scripts/setup.sh        # once: Wi-Fi + OTA password
scripts/flash_usb.sh    # build + flash over USB
```
