# Development notes

This repository starts by preserving the known-good `mx5000-tool` LCD rendering path. The C process owns the refresh loop and button input, while the utility handles the vendor-specific HID report protocol.

## Suggested test sequence

1. Build with `make` and review compiler warnings.
2. Run from a terminal so discovery and permission errors are visible.
3. Confirm all four buttons switch pages.
4. Unplug/reconnect the keyboard and confirm the process recovers.
5. Compare RAM/CPU usage with the Bash/Python implementation using `ps -o pid,rss,%cpu,cmd`.

Do not run the old and new monitors simultaneously: both would write to the same LCD.
