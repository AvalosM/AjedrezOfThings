# AjedrezOfThings

Arcade chess between kiwi boards. Built for the **Ghost in the HAL** hardware hackathon
(FCEyN · UBA, 23–24 October 2026).

## Event material

- [opening.pdf](opening.pdf): opening talk (schedule, theme, judging criteria)
- [Setup.pdf](Setup.pdf): dev environment setup and the board (photo + schematic)

Organizers' example firmware for the board: https://github.com/SanFlan/kiwi-board-example

## Toolchain setup

ESP-IDF, release v6.1, target ESP32-S3:

```bash
git clone -b release/v6.1 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32s3 && . ./export.sh
```

In a project folder:

```bash
idf.py set-target esp32s3
idf.py menuconfig   # flash size: 16MB
idf.py flash monitor   # Ctrl+] to exit
```

If the board won't flash over USB: hold RESET + BOOT, release RESET, release BOOT, then flash.
