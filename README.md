# PICFlipper: Flipper Zero ICSP tool for PIC18F67J60



This is an ICSP programmer for the PIC18F97J60 family of MCUs. I built this app because I was impatient and too cheap to order a PICkit5 programmer. And I wanted to see if it was even possible on a Flipper Zero. 

This app features:
 - Dump full flash binary image in `.bin` + Intel HEX
 - Dump live RAM
 - Write binary image with option to protect boot, with byte verification step at the end of flashing
 - Verify image burned on chip manually by selecting a source image file 
 - Pin connection check with wiring diagram (note that the PIC18 pin header that in the diagram is specific to the board I tested)
 - ufbt log serial console to monitor UART messages from the PIC18 in the CLI

Use at your own discretion. This app works great for me, but it may not for you. 
Worst case, you brick your device. That would suck.

While this tool should technically work with any PIC18F97J60 family of MCUs, I have only 
tested that it works on a PIC18F67J60 (64-pin variant).

## Usage

### Reading the binary image

Dumps program flash (0x000000–0x01FFFF, 128 KB) and the device ID over ICSP without
altering the chip. Two files are written to `/apps_data/picflipper/dumps`: a raw `.bin`
and an Intel HEX `.hex`, named `pic_<devid>_<timestamp>`. That same folder is where the
file browser starts when you pick a source image to write or verify.

Read-protected chips can't be dumped, flash reads back as all
`0x00`. The dump still saves, but the result screen flags it (`Saved (all 0x00:
code-protect?)`). A full write bulk-erases the chip, which clears protection but wipes the
contents.

### Dumping RAM

Snapshots the live data SRAM (GPR file registers, 0x0000–0x0F5F, ~3.9 KB) over ICSP, saved
as `picram_<devid>_<timestamp>.bin` (+ `.hex`) in the same `/apps_data/picflipper/dumps`
folder. ICSP entry resets the target, but the file registers survive the reset, so the
snapshot reflects state from just before entry.

### Writing images

Both modes program flash from a `.bin` picked in `/apps_data/picflipper/dumps`. Before
anything is erased, a confirmation screen shows the file name and the first 16 hex digits of
its SHA-256 (compare against `sha256sum` on your host) and requires a 5-second hold of OK.
Every write ends with a full read-back verify and reports the first mismatch address. A
write is refused before touching the chip if the device ID doesn't match, or if the image
would enable code protection (which would lock out future reads).

Full write bulk-erases the whole chip and programs all 128 KB (0x000000–0x01FFFF), including
the bootloader and Config Words. This is also how you restore a saved dump; bulk erase
clears code protection.

App data (keep boot) row-erases and reprograms only the application range
[0x000000–0x01E7FF], leaving the bootloader region [0x01E800–0x01FFFF] and Config Words
intact. The 0x01E800 boundary is derived from the firmware I tested on a PIC18F67J60 based board;
re-derive it — and keep it 1024-byte aligned — for a different layout. This mode still
reprograms the reset vector at 0x0000, so the image must keep it pointing at the bootloader.

### Verify image

Reads the chip back and compares it byte-for-byte against a `.bin` you select from SD, over
the file's length (capped at 128 KB). It never erases or writes. On a mismatch it reports
the first differing address — use it to confirm a flash, or check a chip against a known
image.

### Debug console

A passive monitor for the target's own serial output. Wire the PIC's debug TX to the
Flipper RX (header pin 14); the app reads it and forwards each line to the Flipper log.
Left/right change the baud (9600–115200).

Read the log on a host with `ufbt` (see [Build & install](#build--install) to install it).
Quit qFlipper first to release the serial connection then:

```sh
ufbt cli        # open the Flipper CLI
log info        # stream the log; Ctrl-C to stop
```

Use `log info`, not bare `log`: the default system log level is often `none` (silent).
Whatever the PIC prints on that UART then shows up in the stream, e.g. on the target:

```c
// PIC firmware, debug UART at the baud selected on the console screen
printf("boot ok, devid=%04X\r\n", devid);
```


### Wiring 

The pin numbers are specific to the PIC18F67J60 (64-pin). The MCU is self powered from the board, do not supply any voltage.

| Signal          | Flipper header            | PIC18F67J60 pin  |
|-----------------|---------------------------|------------------|
| MCLR/Vpp        | **PB3** (hdr pin 5)       | 7  (MCLR)        |
| PGC             | **PA7** (hdr pin 2)       | 42 (RB6/PGC)     |
| PGD             | **PA6** (hdr pin 3)       | 37 (RB7/PGD)     |
| GND             | **GND** (hdr pin 8/11/18) | any VSS          |
| UART (optional) | **RX** (hdr pin 14)       | 31 (RC6/TX1/CK1) |

The UART line is only for the debug console.

## Build & install

Requires [`ufbt`](https://github.com/flipperdevices/flipperzero-ufbt)
(`pipx install ufbt`).

```sh
ufbt              # build -> dist/picflipper.fap
ufbt launch       # build, install to /ext/apps/GPIO/, and run (quit qFlipper first)
```


## Implementation notes

- Protocol follows the [PIC18F97J60 Family Programming Specification](https://ww1.microchip.com/downloads/en/devicedoc/39688d.pdf)
  - Entry requires the 32-bit key `0x4D434850` ("MCHP"), shifted MSb-first:
    a brief MCLR pulse, ≥1 ms, then the key, then MCLR held. Commands and data
    are LSb-first.
- The 128 KB image is streamed to SD in 4 KB chunks.

## Project commentary
I used Claude Opus 4.8 for this project, again with a lot of handholding. I've never had to argue with anyone to keep working as much as I have with Claude. Like most projects, it began with an enthusiastic and optimistic Claude where anything is possible. But as soon as we hit a snag, Claude constantly gave me options like:
+ this is impossible and let's agree to call it quits here  
+ there is already a commercially available PIC programmer for purchase with official support, so why bother implementing one  
+ several more reasons why this is impossible...

I had to keep nudging it to investigate blind spots that I thought were obvious enough. At times, I had no idea wtf I was talking about because I'm not too familiar with the chip family, but I knew enough to look in the datasheet and call BS. I struggled with Claude through every bit of this to keep it moving and prevent it from stopping unless it hit a real block. In hindsight, this was mostly my fault. I should have planned better than just writing a simple MD file with specs and goal. I should have designed an iterative workflow that could programmatically keep the agents chugging along. I underestimated the complexity.

While a lot of these struggles are on me, there is an undeniable laziness to Claude that prefers to end quickly vs exploring other options or even just simply not suggest we quit every time. Maybe that's a good thing to not burn tokens on dead ends, but the biggest issue for me is the empathetic human tone it takes. I would be ok with stopping and getting a read on the blockers, but suggesting we quit is infuriating.

## License

[MIT](LICENSE) Copyright (c) 2026, BC (https://github.com/bciuca).

All source here is original work. It targets the Flipper Zero SDK and implements the PIC18F67J60 ICSP protocol per the public Microchip
spec [DS39688D](https://ww1.microchip.com/downloads/en/devicedoc/39688d.pdf). No third-party code is bundled.
