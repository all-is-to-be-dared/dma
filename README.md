# Embedded Extended Universe

Contents of this repository:
 - the Program UPloader PUP

# PUP

Upload-over-serial system designed for high-speed transfers (as much as the words "high-speed" can
refer to UART). Currently features:
 - robust wire protocol
 - supports high baud rates (e.g. in the megabaud range)
 - supports LZMA2 + BCJ compression (via Tukaani's XZ-embedded)

PUP consists of two components, a host-side uploader (the eponymous `pup`), and a device-side
downloader.

Device support:
 - BCM2835

Host support:
 - macOS
 - Linux

### Building

macOS:
```shell
$ brew install xz ninja picocom
```

Debian/Ubuntu:
```shell
$ sudo apt install xz-utils ninja-build picocom
```

Note: if you see a message about arm-none-eabi-gcc not supported `-std=c23`, you may need to
      upgrade your arm-none-eabi-gcc.

 1. Edit `pup/config.ninja` to point at your `xzutils` installation.
 2. Edit `rules.ninja` to point to the correct toolchains
 3. Run `ninja` from the project root

If everything ran successfully, you should have a `bin/pup` executable and a variety of
`bin/XYZ.bin` binaries. The latter can be loaded onto your device in whichever way is customary.
