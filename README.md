# Embedded Extended Universe

Contents of this repository:
 - the Program UPloader PUP

# PUP

Upload-over-serial system designed for high-speed transfers (as much as the words "high-speed" can
refer to UART). Currently features:
 - robust wire protocol
 - supports high baud rates (e.g. in the megabaud range)
 - supports LZMA2 + BCJ compression (via Tukaani's XZ-embedded)
 - supports both BIN and ELF formats

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

Steps:

 1. Edit `pup/config.ninja`
 2. Edit `rules.ninja`
 3. Run `ninja` from the project root

If everything ran successfully, you should have a `bin/pup` executable and a variety of
`bin/XYZ.bin` binaries. The latter can be loaded onto your device in whichever way is customary.

### XZ Support

PUP supports XZ! All you have to do is pass it a path to an XZ-compressed file. Note that there
_are_ specific compression requirements:

```shell
xz --arm -k --threads=1 -9 --check=crc32 --lzma2=dict=1Mi path/to/my/program.elf
bin/pup path/to/my/program.elf.xz
```

In my experience, this can shrink your program down at least a couple times.

Note that this works for both ELF and BIN files.

### ELF Support

A pointer to the `elf_boot_args` structure defined in `pup/protocol.h` will be passed to the
uploaded program in the 0th argument register (e.g. `r0` for ARM). This allows programs to access
their own ELF images for e.g. debugging reasons.
