# MX5000 Linux Monitor

System monitor for the Logitech MX5000 keyboard LCD on Linux.

The monitor displays system information on the keyboard LCD, refreshes
the display every five seconds, and supports page switching using the
keyboard's LCD buttons.

## Features

- Four information pages.
- Five-second refresh interval.
- Automatic discovery of the MX5000 input and HID raw devices.
- Separate control and read-only diagnostic scripts.

## Project status

Developed and tested on Linux Mint Cinnamon with a Logitech MX5000.
Compatibility with other distributions and configurations has not yet
been verified.

## Repository layout

- `src/` — monitor source code.
- `third_party/mx5000tools/` — upstream mx5000tools source.
- `patches/` — local changes to upstream sources.
- `scripts/` — control and diagnostic scripts.
- `config/` — desktop autostart entry.
- `docs/` — project documentation.

## Requirements

A compatible Logitech MX5000 keyboard, a C compiler, GNU Make,
the dependencies required by mx5000tools, and Linux input/HID access.

See `docs/MONITOR-README.md` and `docs/DEVELOPMENT.md` for existing
project notes.

## Third-party software

The upstream mx5000tools license is included in
`third_party/mx5000tools/COPYING`.

Review the upstream license and the project's licensing terms before
redistributing binaries or publishing a combined distribution.

## Development status

Installation automation and a clean end-to-end build from this
repository still need to be verified.
