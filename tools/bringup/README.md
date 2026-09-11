# Board bring-up probe

A standalone PlatformIO sketch for answering "are these pins right?" on a new round
QSPI AMOLED board, *before* trying to run the firmware on it. It has no dependency on
`../../src` — that is the point.

```bash
cd tools/bringup
$EDITOR src/main.cpp          # fill in the CONFIGURE block
pio run -t upload
pio device monitor -b 115200
```

This is the tool the ESP32-S3-Touch-AMOLED-1.43 port was brought up with. If you are
adding a third board, start here and follow the checklist in
[`../../docs/HARDWARE.md`](../../docs/HARDWARE.md).

## What it tells you

**1. The I2C pins, and what is on the bus.** Scans twice — once before panel init and
once after — and names the parts it recognises. If a device appears only in the second
scan, its reset line is tied to the panel reset, and the firmware must initialise the
display before touch. (This is exactly the case on the 1.43's FT3168, and it is why the
stock 1.75 image found *nothing* on that board: wrong pins and a touch chip held in reset.)

**2. The QSPI pins.** If the panel lights up and draws, they are right.

**3. The panel's column/row gaps, and the touch orientation.** A white ring is drawn that
should sit exactly on the rim, with coloured ticks on the outermost row and column. Tap
left/right of centre to nudge the image in X, above/below to nudge Y. Settle where the
ring is even all the way round and all four ticks are visible — those numbers are your
`LCD_COL_OFFSET` / `LCD_ROW_OFFSET`. Every value is echoed to serial, so you can read the
answer off the log rather than off the screen.

Touch coordinates are logged raw as you tap. If the drawn box does not land under your
fingertip, you need the mirror/swap flags in the board header.

## Three traps it is built to avoid

**Never call `gfx->begin()` twice.** Arduino_GFX has no teardown; the second call aborts
with `spi_bus_initialize(800): SPI bus already initialized`. That presents as a boot loop
showing a half-drawn screen, which looks convincingly like a hardware fault. This is why
the gap search shifts the image *in software* rather than rebuilding the panel object.

**These panels need 2-pixel-aligned address windows** (even start, odd end). Unaligned 1px
writes are silently dropped, so a test card built from 1px circles and crosshairs renders as
almost nothing — again, easily mistaken for a dead panel. The probe composes the whole frame
in PSRAM and blits it in one aligned full-screen window, so 1px detail is exact and a 1px
nudge really is 1px.

**Pick a test pattern that suits a round panel.** A full-screen rectangle only touches a
circle at four points, so it tells you almost nothing; and a thick ring cannot resolve a
shift smaller than its own thickness. The 1.43's gap was first misread as 0 for exactly that
reason before the nudge tool settled it at 6.

## Adapting it

The `CONFIGURE` block at the top of `src/main.cpp` carries the pins, panel size and QSPI
clock. Start at the vendor's clock rate and raise it only once the panel is working.

Touch reads assume the FocalTech register layout (count at `0x02`, 12-bit X/Y from `0x03`),
which covers FT3168/FT5x06. For a CST9217 or CST816 the scan and the panel phases still work
as-is; only the touch read in `loop()` needs swapping for that chip's protocol.
