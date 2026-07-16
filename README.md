# Precision M4800 3dfx Edition Firmware

Custom coreboot and SeaBIOS firmware for the Dell Precision M4800 3dfx
Edition project.

This firmware targets a standard Precision M4800 motherboard fitted with a
custom 3dfx MXM graphics card in place of the usual NVIDIA or AMD adapter. No
motherboard hardware modifications are required.

The Precision M4800 provides both integrated- and discrete-GPU paths. This
project deliberately disables the Intel integrated GPU very early during
startup so the 3dfx MXM card can operate as the sole graphics adapter. Leaving
the iGPU enabled as a secondary adapter reserves resources that cause problems
under Windows 98.

The firmware also adds legacy operating-system compatibility and provides a
persistent BIOS-style setup utility for platform and graphics-card settings.

This is a machine-specific project. It is not intended as a universal Dell
Precision M4800 port or as an upstream-ready coreboot implementation. For a
general M4800 coreboot port, see the unmerged upstream change
[mb/dell: Add Dell Precision M4800 (Haswell)](https://review.coreboot.org/c/coreboot/+/79755).

## Hardware Target

| Component | Target |
| --- | --- |
| System | Dell Precision M4800 |
| CPU platform | Intel Haswell |
| Chipset | Intel Lynx Point |
| Embedded controller | Dell MEC5075 |
| Primary graphics | 3dfx Voodoo4 M4800 or Voodoo3 M3800 MXM card, including LVDS and eDP variants |
| Firmware payload | SeaBIOS |
| Flash size | 12 MiB |

The Intel integrated GPU is disabled and the MXM card is used as the primary
display adapter.

## Main Features

### Precision M4800 Board Port

- Dedicated `precision_m4800_3dfx` mainboard target.
- ACPI tables derived and adapted for this machine-specific firmware.
- VGA routing through the motherboard EC-controlled mux.

### Legacy Operating-System Support

- Fixed ACPI PCI resource layout for legacy operating systems.
- Legacy ACPI `Processor` objects for Windows XP SpeedStep compatibility.
- SeaBIOS keyboard lock-LED handling for DOS and Windows 98.
- Windows 98-oriented PCI IRQ routing with working level-triggered
  interrupts for PIC-mode operating systems.
- DOS sound (SBEMU) compatible HDA interrupt routing, verified with Doom.
- Mode-specific SATA ACPI objects matching the OEM firmware.
- mSATA support in IDE native mode, including under Windows 98.
- Added Lynx Point SATA support for selectable AHCI, IDE native, and IDE
  legacy modes.

### Platform Controls

- Configurable HDA controller state.
- Configurable CPU Turbo state.
- Configurable SATA mode and external VGA destination.
- Configurable mSATA controller (IDE native mode).
- Configurable boot order and boot-prompt delay.
- Editable RTC date and time.

### SATA Modes

| Mode | Intended use |
| --- | --- |
| AHCI | Recommended for NT-based Windows and Linux |
| IDE native | Recommended for Windows 98 and DOS; fully working under Windows XP |
| IDE legacy | Compatibility option using legacy IDE IRQ routing |

All three modes are usable under Windows XP.

## Project-Specific Changes

This section records the functional changes present on top of the base
coreboot revision. It is intended as a reference when updating, debugging, or
reimplementing the project.

| Area | Implementation |
| --- | --- |
| Mainboard port | Adds the `precision_m4800_3dfx` target with machine-specific GPIO, USB, ACPI, CMOS, romstage, and devicetree configuration. |
| Board ACPI | Provides machine-specific MEC EC, AC adapter, battery, power, platform, Super I/O, and OS-compatibility definitions. |
| Graphics selection | Disables the Intel iGPU before MRC, removes the Haswell integrated graphics build path, and leaves the MXM adapter as the sole graphics device. |
| PCI resources | Uses fixed ACPI PCI windows: non-prefetchable MMIO at `0xe0000000-0xefffffff` and prefetchable MMIO at `0xd8000000-0xdfffffff`, with fixed legacy I/O ranges. |
| CPU power management | Emits legacy ACPI `Processor` objects so Windows XP can bind its SpeedStep drivers and use the generated performance states. |
| SATA | Extends the Lynx Point SATA driver with AHCI, IDE native, and IDE legacy initialization selected from CMOS, with OEM-style fixed I/O BARs in IDE native mode, a separate interrupt pin per SATA function (required by Windows 98's ESDI_506.PDR), and the second SATA function (mSATA) exposed in IDE native mode with a setup option to hide it. |
| SATA ACPI | Generates mode-specific SATA ACPI objects at runtime, replicating Dell's per-mode SSDT swap: IDE channel and drive objects in IDE mode, AHCI port objects in AHCI mode. |
| GPIO relocation | Moves GPIOBASE from 0x480 to 0x1c00 (OEM location). The old window shadowed the ELCR trigger-mode registers and ISA DMA high page registers, forcing all PIC-routed interrupts to edge-triggered — the root cause of Windows 98 "delayed write failed" corruption and lost level-triggered interrupts under PIC-mode operating systems. |
| Legacy IRQ routing | Programs the OEM PIRQ routing table with coherent PCI_INTERRUPT_LINE hints for every function, including devices behind bridges. Dedicated interrupts for the disk controllers (IRQ 5/10 in PIC mode), HDA routed enabled at boot on IRQ 7 for DOS sound stacks (SBEMU), IRQ 5 kept otherwise device-free for Sound Blaster emulation, and AHCI-mode SATA on GSI 20 as required by Windows XP's AHCI stack. |
| FPU error reporting | Enables FERR#/IRQ 13 coprocessor error routing (OIC/GCS) like the OEM firmware, for DOS and Win9x-era software. |
| Audio | Supplies Realtek ALC292 verbs, adds early HDA disable support, and sends the Dell EC speaker-unmute command required for working audio. |
| Dell EC | Routes power-button events to the host OS and controls the dGPU VGA mux for either the laptop or docking-station connector. |
| SeaBIOS keyboard | Updates Caps Lock, Num Lock, and Scroll Lock LEDs directly so they work under DOS, Windows 98, and Windows XP. |
| SeaBIOS integration | Applies the project SeaBIOS patches automatically during the coreboot build and embeds the configurable boot-menu wait value. |
| Setup utility | Adds persistent CMOS controls, RTC editing, boot selection, system information, contextual help, and direct M3800/M4800 information, telemetry, and card settings. |
| Card recovery | Detects repeated `R` input at the SeaBIOS prompt, restores safe MXM-card defaults, and reboots. |
| Platform devices | Configures the USB 2.0/3.0 port maps, TPM 1.2, docking, Ethernet, optical drive, and HDD/ODD/mSATA port map, with CMOS defaults for the wireless radios. |
| Device policy | Disables the unused Intel ME interfaces, IDE-R, KT, and PCH thermal device in the board devicetree. |

## Hardware Compatibility

The following table records configurations tested with this firmware.

| Hardware or function | Windows 98 | Windows XP | Linux |
| :--- | :---: | :---: | :---: |
| USB 2.0 | Confirmed | Confirmed | Confirmed |
| USB 3.0 | X | Confirmed | Confirmed |
| SATA HDD/SSD in AHCI mode | X | Confirmed | Confirmed |
| SATA HDD/SSD in IDE native mode | Confirmed | Confirmed | Confirmed |
| SATA HDD/SSD in IDE legacy mode | Confirmed | Confirmed | Confirmed |
| SATA ODD | Confirmed | Confirmed | Confirmed |
| mSATA | Confirmed[^1] | Confirmed[^1] | Confirmed[^1] |
| HDA audio | Confirmed | Confirmed | Confirmed |
| Laptop headphone jack | Confirmed[^5] | Confirmed | Confirmed |
| Ethernet | Confirmed[^6] | Confirmed | Confirmed |
| Wi-Fi and Bluetooth | X | Confirmed | Confirmed |
| SD card reader | X | Confirmed | Confirmed |
| ExpressCard slot | Confirmed | Confirmed | Confirmed |
| Internal keyboard and touchpad | Confirmed | Confirmed | Confirmed |
| Keyboard lock LEDs | Confirmed | Confirmed | Confirmed |
| Media keys (volume up/down and mute) | Confirmed[^7] | Confirmed | Confirmed |
| RTC date and time | Confirmed | Confirmed | Confirmed |
| ACPI | Confirmed | Confirmed | Confirmed |
| AC adapter and battery state | Confirmed | Confirmed | Confirmed |
| Voodoo3 M3800 variants | Confirmed | Confirmed | Confirmed |
| Voodoo4 M4800 variants | Confirmed | Confirmed | Confirmed |
| Laptop VGA output | Confirmed | Confirmed | Confirmed |
| Laptop eSATA port | -[^4] | -[^4] | -[^4] |
| Docking-station VGA output | Confirmed | Confirmed | Confirmed |
| Docking-station USB 2.0 | Confirmed | Confirmed | Confirmed |
| Docking-station USB 3.0 | X | Confirmed | Confirmed |
| Docking-station PS/2 mouse | Confirmed | Confirmed | Confirmed |
| Docking-station PS/2 keyboard | Confirmed | Confirmed | Confirmed |
| Docking-station Ethernet | Confirmed[^6] | Confirmed | Confirmed |
| Docking-station power button | Confirmed | Confirmed | Confirmed |
| Docking-station DVI/DP | X | X | X |
| Docking-station COM port | X | X | X |
| Docking-station parallel port | X | X | X |
| Docking-station headphone jack | Confirmed[^5] | Confirmed | Confirmed |
| Docking-station eSATA | -[^4] | -[^4] | -[^4] |
| DisplayPort and HDMI output | X | X | X |
| Internal panel | Confirmed | Confirmed | Confirmed[^3] |
| 1920x1080 internal panel | Confirmed[^2] | Confirmed[^2] | Confirmed[^3] |
| S3 suspend/standby | X | X | X |

`-` means that support has not been confirmed for that operating system.
`X` means that the hardware is unavailable in this configuration or is not
supported by that operating system.

[^1]: mSATA works in AHCI mode and, through the second SATA controller, in
      IDE native mode. In IDE native mode it can be hidden with the mSATA
      Controller setup option.
[^2]: On Voodoo4 M4800 cards, the 1920x1080 internal panel requires a registry
      patch under Windows 98 and Windows XP due to a VSA-100 limitation. The
      patch is not required for Voodoo3 M3800 cards.
[^3]: The stock `xserver-xorg-video-tdfx` X11 driver programs the VSA PLLs
      incorrectly and can drive the VCO outside its valid range at any
      resolution. Linux testing uses a patched driver that corrects the PLL
      programming.
[^4]: eSATA likely works, but no eSATA device was available for testing.
[^5]: Under Windows 98, the laptop and docking-station headphone jacks
      currently require the [sdz-mods/WDMHDA](https://github.com/sdz-mods/WDMHDA)
      driver fork.
[^6]: Under Windows 98, the internal and docking-station Ethernet interfaces
      require the [sdz-mods/I217-LM_W98](https://github.com/sdz-mods/I217-LM_W98)
      driver.
[^7]: Under Windows 98, the volume up, volume down, and mute media keys require
      [sdz-mods/MKB98](https://github.com/sdz-mods/MKB98).

## Setup Utility

Press **Delete** at the SeaBIOS boot prompt to open the
**Precision M4800 3dfx Edition - Setup Utility**.

The setup utility contains:

| Page | Functions |
| --- | --- |
| Info | System, CPU, memory, 3dfx card, VBIOS, coreboot, and SeaBIOS information |
| Main | RTC date and time |
| Advanced | SATA mode, mSATA controller, HDA, CPU Turbo, and VGA mux routing |
| Boot | Boot order and boot-prompt delay |
| 3dfx | MXM card information, telemetry, and persistent card settings |
| Save & Exit | Save, discard, restore defaults, or reset the MXM card settings |

Platform settings are stored in CMOS and applied by coreboot on the next
boot.

### 3dfx MXM Control

The setup utility communicates with supported cards through GPIOs on the
Texas Instruments XIO2001 PCIe-to-PCI bridge.

Supported card families:

- M4800: Voodoo4 / Napalm x1
- M3800: Voodoo3 / Avenger

Displayed information includes:

- Card model, revision, and display type
- GPU model and framebuffer size
- VBIOS revision
- PCI bus speed
- GPU and SMC temperatures
- Fan speed

Configurable card settings include:

- Panel backlight
- VSA-100 core voltage
- Framebuffer size on M4800 cards
- VSA NT blank fix on M4800 cards

These values are stored on the MXM card rather than in laptop CMOS.

## VGA Routing

The external analog VGA output can be routed to:

- The laptop VGA connector
- The docking-station VGA connector

The routing is controlled through the Dell EC. The integrated-GPU route is
not used by this project.

## Boot Behavior

- **Escape** opens the SeaBIOS boot menu.
- **Delete** opens the setup utility.
- Repeatedly pressing **R** at the boot prompt resets the 3dfx MXM card to
  safe defaults and reboots. This can recover from card settings that prevent
  video output.
- The boot-prompt delay is configurable from 1 to 10 seconds.
- Boot priority can prefer HDD, optical drive, USB, or a coreboot payload.

## Known Limitations

- S3 suspend/standby is not supported.

## Building

Initialize the coreboot submodules:

```sh
git submodule update --init --checkout
```

Configure coreboot:

```sh
make menuconfig
```

Select:

```text
Mainboard vendor: Dell
Mainboard model: Precision M4800 3dfx Edition
Payload: SeaBIOS
```

Then build:

```sh
make
```

The resulting firmware image is:

```text
build/coreboot.rom
```

The SeaBIOS source is checked out during the build and the project patches in
`payloads/external/SeaBIOS/patches/` are applied automatically.

## Flashing Warning

Flashing firmware can render the laptop unbootable. Keep a known-good image
and an external SPI programmer available. Verify the flash layout and preserve
machine-specific regions such as the Intel Flash Descriptor, Management
Engine, and GbE data as required by the target system.

## Project History

This branch is based on coreboot commit:

```text
e5af2c6585 soc/intel/pantherlake: Remove implicit VBOOT_MUST_REQUEST_DISPLAY selection
```

The project branch is `m4800_3dfx`.

Two Dell MEC5035 changes were imported from coreboot Gerrit:

- [Route the power-button event to the host](https://review.coreboot.org/c/coreboot/+/84878)
- [Add the command required to unmute speakers](https://review.coreboot.org/c/coreboot/+/91120)

## Upstream Project

This repository is a fork of [coreboot](https://www.coreboot.org/), the open
source firmware project.

Upstream source and documentation:

- <https://review.coreboot.org/coreboot.git>
- <https://doc.coreboot.org/>
- <https://github.com/coreboot/coreboot>

## Licensing

coreboot is primarily licensed under the GNU General Public License version 2.
Individual files may use other compatible licenses and carry their own SPDX
identifiers. See `COPYING`, `AUTHORS`, and the headers of individual source
files for details.
