# MX5000 Monitor for Linux

A small Linux system monitor for the LCD and four buttons on the Logitech MX5000 keyboard. This is an early C prototype based on a working Linux Mint 22.3 setup.

## Current prototype

- Four pages: system overview, network, memory/storage, uptime/load.
- Refreshes approximately every five seconds.
- Discovers the keyboard's input and hidraw nodes using its USB/Bluetooth HID identity (`046d:b305`) rather than hard-coded `/dev/eventN` or `/dev/hidrawN` names.
- Listens for button event codes 696–699.
- Uses the existing `mx5000-tool` utility to draw the LCD. This first milestone intentionally does not reimplement the HID protocol.

## Dependencies

- A Linux system with a C compiler and `make`.
- `mx5000-tool` from `mx5000tools`, built and installed so it is available in `PATH` (or pass its path with `-t`).
- Read access to the MX5000 input event node and write access to its hidraw node. On many distributions, a narrowly scoped udev rule is preferable to adding the user to the broad `input` group.

## Build

```bash
make
```

## Run

```bash
./mx5000-monitor
```

If `mx5000-tool` is installed outside `PATH`:

```bash
./mx5000-monitor -t /usr/local/bin/mx5000-tool
```

Stop with `Ctrl+C`. Diagnostics go to standard error; the LCD utility's output is suppressed.

## Important prototype notes

- This is an initial prototype, not yet a polished release. Verify the displayed network counters and IP address on your own machine.
- Temperature depends on the thermal sensors exposed by the kernel under `/sys/class/thermal`.
- Button codes are the codes observed on the author's MX5000 setup and may need configuration on another kernel/device revision.
- Network interface IP discovery currently invokes the `ip` command through `popen()`. Ensure `iproute2` is installed and `ip` is available in `PATH`.
- Device permission setup and service/autostart integration will be documented after the first local test.

## Roadmap

- [ ] Test and refine on Linux Mint/Cinnamon.
- [ ] Remove remaining external command dependency for IP discovery.
- [ ] Add configuration file and optional page selection.
- [ ] Add safe, scoped udev rules and user service/autostart instructions.
- [ ] Add tests and CI build.
- [ ] Consider direct HID rendering after the wrapper-based version is stable.

## License

License has not yet been selected. Choose a license before publishing a public repository.
