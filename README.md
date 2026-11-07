# RH850 Classical CAN Custom Transfer

Standalone, non-destructive communication test for R7F701684
(RH850/F1KM-S1). The PC workflow is **Custom Transfer**. Each enabled bus has an independent
protocol context,statistics and duplicate-response cache.
No App BIN, metadata, Flash erase or Flash write is used.

## Board and communication

| Item | Configuration |
| --- | --- |
| MCU | R7F701684, 100 pins, 1 MiB Code Flash |
| Clock | X1 16 MHz; CPU 120 MHz; CAN communication clock 40 MHz |
| Connector | CN6 / CN8;MCU CAN1 / CAN4 |
| CAN pins | CAN1 P10_7/P10_6 Alt4;CAN4 P0_10/P0_9 Alt5 |
| Transceiver | EVM CAN/CAN-FD transceivers;see board manuals |
| CAN mode / rate | Classical CAN, 500 kbit/s |
| Request / response | Standard data IDs 0x710 / 0x711, DLC8 |
| RX rule | Exact 0x710 / 0x7FF, data frames only |
| RX execution | FIFO1/4 shared EIINT23; per-port16-slot SPSC queues,15 usable |
| Diagnostics | UART0 TX P0_2, 115200 bit/s; optional logic-level output |
| RAM | LRAM FEDE0000..FEDF7FFF, retention RAM FEDF8000..FEDFFFFF |

The CAN interface uses an external transceiver and a shared signal ground.
Check bus termination at the physical ends of the test bus.
UART is optional, bounded, polled diagnostic output; UART CPU interrupts stay
masked. Use a compatible logic-level adapter, not an RS-232 voltage interface.

## EVM ports and buffer allocation

The EVM target is R7F701684 (100 pins,1 MiB Code Flash),X1=16MHz.
Default enabled ports are MCU CAN1 and CAN4. Set each ENABLE_PORT_CAN0..5
flag to0 or1 in `can_port_config.h`;at least one must be1.
Each project owns its local `can_port_config.h`. Apply compatible port and
communication settings to the Boot and App that will be installed together.

| MCU port | TX / RX routing | EVM connector |
| --- | --- | --- |
| CAN1 | P10_7 Alt4 / P10_6 Alt4 | Board CAN0,CN6 |
| CAN4 | P0_10 Alt5 / P0_9 Alt5 | Board CAN1,CN8 |

The board connector labels are not MCU channel numbers.
BLDC V2:S7_6/5 ON;S7_4/2 ON and S7_3/1 OFF.
ASK V3:J20 1-2/3-4 for CAN1,5-6/7-8 for CAN4.
Use the board manual's S4 termination settings;leave S4_3/4 OFF for separate
buses. Connect only the bus under test to the host,then repeat on the other
bus. The Tool bridge-port selector selects a host port,not a device port.

Each enabled MCU channel owns RX FIFO[channel] and a16-slot RAM queue
(15usable),with independent counters and source-channel reply routing.
TX uses buffer0 local to that channel (status index channel*32).
There are no RX Message Buffers or common TX/RX FIFOs.
FD RX allocation is1216bytes per port including hardware message headers;
default two ports use2432bytes,all six7296bytes,within the21504-byte shared
RX/FIFO budget (hardware manual24.10.1.5). Classical uses320bytes per port.
CPU RAM queues are separate from CAN message RAM.
Disabled ports allocate neither queues nor active filters.
CAN0/2/3/5 require board-level routing and compatible external transceivers.

- [BLDC V2 manual](https://www.renesas.com/en/document/mat/rh850f1km-s1-bldc-starter-kit-v2-users-manual)
- [ASK V3 manual](https://www.renesas.com/en/document/mat/rh850f1km-s1-starter-kit-v3-user-manual-hardware)

## Build and artifact

Open `Sample_Project_RH850_S1_CAN_Transfer.mtpj` in CS+ CC and rebuild with
CC-RH V2.07.00. The selected device is R7F701684.

The programmer image is:

`DefaultBuild/Sample_Project_RH850_S1_CAN_Transfer.mot`

It starts at address zero and contains the standalone runtime.
It is not a Boot-only image and is not a Tool CAN Upgrade BIN.

From the companion `simple_renesas_isp_tool` checkout, the PowerShell entry is:

```powershell
.\scripts\build_rh850_can_transfer.ps1 -DeviceProject ..\Sample_Project_RH850_S1_CAN_Transfer -Prepare
.\scripts\test_rh850_transfer.ps1 -DeviceProject ..\Sample_Project_RH850_S1_CAN_Transfer
```

`-DeviceProject` is the relative or absolute location of this checkout.
`-Prepare` reconciles only owned source registrations and the generated vector
projection, preserving the SC sources and backing up the MTPJ under Tool tmp.
Run it after SC regeneration. It excludes the copied demo runtime and per-slot
dummy ISR source without deleting those files.
No Python runtime is used to build firmware. Native model tests additionally
use the existing Visual Studio C/C++ developer tools.

The local CS+ post-build hook `tools/verify_transfer.ps1` checks the actual
MOT/map,all512 vector destinations and RAM/stack below the debugger reservation.
It is carried with this standalone project;normal Rebuild needs no Tool checkout.

## Source map

| File | Responsibility |
| --- | --- |
| `can_transfer_config.h` | IDs, target 0x8501, diagnostic trace option |
| `can_transfer_protocol.c/.h` | Portable C90 command decoding, CRC8, duplicate cache and counters |
| `rh850_can.c/.h` | Classical wrapper over the shared multi-port IRQ/queue/TX core |
| `custom_func.c` | Main-loop protocol dispatch and UART trace; OSTM0 tick |
| `hal/can/can_service.c/.h`, `rs_canfd*` | Shared v4 queues,TX events,validation and register driver |
| `hal/can/can_config.c`, `hal/can/can_board.c` | Exact protocol filters and EVM684/16MHz pin/clock/INTC adapter |
| `generated_projection/device_vectors.c` | Complete 512-entry INTBP table; unused entries share one handler |
| `boot.asm` / `cstart.asm` | Reset, ECC RAM initialization, INTBP and C runtime |

Only the RX interrupt consumes the hardware FIFO. Main-loop work consumes RAM
and performs protocol decoding, logging and TX. A full queue drops the newest
frame and increments diagnostics. The ISR drains at most 16 frames per entry;
a full batch requests another interrupt, so reception has no polling fallback.
The standalone table maps CAN RX to channel23 and the 1-ms OSTM0 tick to84.

The service/driver match the standalone MultiCHv4 core;this project retains its
own EVM settings and Custom Transfer protocol. No demo console/accept-all rules
or v4 startup/vector table is imported. TX is successful only after its matching
DONE event,not after enqueue. Timeout requests one abort;an unconfirmed result
latches that bus's TX fault without restarting/replaying automatically.
Channel stop/recover and tag cancellation are foreground C APIs,not new Tool
commands. Settle the protocol transaction and save diagnostics before explicit
control. Other channels and their queues remain independent. Hardware error
flags are retained,not cleared by background status reads or CLEAR_STATS.

## Tool operation

1. Program the standalone MOT with the debugger/programmer.
2. Connect CANH, CANL and signal ground to the MCU CAN host.
3. In Tool, use `Open + Probe Host`, select the bridge CAN port, then
   Classical CAN / 500K / Custom Transfer.
4. Select `Discover Test Target`; check target0x8501 and label `R7F70168`.
5. Select `Transfer Test`; compare the Tool log with device UART TRACE if used.

Custom Transfer v1 has an eight-character label. `R7F70168` is that short label,
not the complete MCU part number. The full part is listed in the board table.

| Statistic | Meaning |
| --- | --- |
| 0 / 1 | Protocol requests / successfully completed transmissions |
| 2 | Observed hardware-lost events plus software queue drops, since clear |
| 3 | TX failures |
| 4 / 5 / 6 | Protocol errors including invalid frames / CRC errors / duplicates |
| 7 / 8 / 9 | Main-loop observed warning / passive / bus-off entry transitions |
| 10 | Last initialization/TX outcome; values follow `rs_canfd_result_t`. RX lost/invalid observations are reported separately |
| 11 | Low 16 bits of the selected MCU channel ERFL; decode using the RH850 hardware manual |

Counters saturate at65535. GET_STATS is read-only. CLEAR_STATS resets protocol
counters; it does not reset CAN or clear peripheral error flags. A lost-event
counter is not an exact count of every frame lost in hardware, and polling
error-state transitions does not capture states shorter than a loop interval.

## Verification and references

Offline CC-RH rebuild, actual MOT/vector guards, C90 protocol / production PC
client tests and actual IRQ/queue adapter models are provided.
The models simulate the peripheral/API boundary; they do not establish physical
CAN timing, transceiver operation or hardware interrupt acceptance.

- [Tool operation and shared protocol](https://released.github.io/slide_simple_renesas_isp_tool/#custom-transfer)
- [Custom Transfer byte format](https://github.com/released/simple_renesas_isp_tool/blob/main/docs/F13_TRANSFER_PROTOCOL.md)
- [EVM hardware](https://www.renesas.com/en/document/mat/rh850f1km-s1-starter-kit-v3-user-manual-hardware)
- [RH850/F1KM hardware manual](https://www.renesas.com/en/document/mah/rh850f1km-rh850f1kh-users-manual-hardware)

## User code entry

`custom_func.c` provides `hardware_init()` for initialization and `loop()` for
foreground service. Add application initialization and foreground work through
these callbacks, retaining the existing communication service calls.
The selected main/startup adapter owns entry and handoff. Generated peripheral
files remain under `src/`; retain the project's MTPJ source selection when using
Code Generator or Smart Configurator, then rebuild to validate the image and vectors.

## Project layout and standalone build

This project includes its own source files, configuration headers and build tools.
A sibling Boot, App or Transfer checkout is not required to compile it.
Keep the project directory name aligned with its `.mtpj` filename; remove a ZIP download suffix when necessary.
Build with CS+ Rebuild, or run the local PowerShell entry:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
```

| Location | Responsibility |
| --- | --- |
| `custom_func.c` | Project initialization in `hardware_init()` and recurring work in `loop()`; runtime orchestration stays here |
| `hal/can` | CAN service, controller driver and CAN adapter, when used by this project |
| `hal/device` | Device, clock, peripheral and CPU handoff APIs |
| `hal/image` | Metadata parser/profile, image validation and software CRC |
| `hal/flash` | Flash access/geometry APIs, when required by the selected image |
| `hal/isp` | Update/Transfer protocol and transport processing APIs |
| Root configuration headers | Local MCU/Flash, handoff and communication contract |
| `tools` and root PowerShell helpers | Local metadata/IRQ generation and linked-image checks |
| `src` | Code Generator or Smart Configurator peripheral sources |

Only the sources registered in this project's CS+ configuration are linked.
Generated main/vector implementations must not become a second active entry.
Retain the `custom_func.c` callbacks and the selected startup adapter after code generation.

Boot and App each carry a local contract. Keep their MCU, Flash layout, image format,
IRQ ABI, handoff and communication settings compatible when installing a pair.
App releases are edited in `app_image_config.h` and `app_irq_handlers.h` in the App project.
Rebuild the selected App to regenerate its metadata, vector table, update BIN and timing BINs.
The App build does not read or rewrite a Boot checkout.

## Diagnostic text output

The project-local `retarget_config.h` selects the formatter at compile time:

```c
#define TINY_PRINTF_FULL 1 /* 1: Full C99-format mode; 0: Current mode. */
```

Rebuild after changing the mode. Full is the default and supports C99 integer,
string/character, pointer, `f/F/e/E/g/G/a/A`, `n`, `hh/h/l/ll/j/z/t/L`, flags,
numeric/dynamic width and precision. Current retains the existing format subset,
finite rounding and attempted-character counts. Both retain streamed precision
and the project's finite UART0 wait; ISP data uses its independent interface.

```c
#include "retarget.h"
/* Foreground, after UART0 initialization. Use types matching each format. */
tiny_printf("counter=%lu status=0x%08lX\r\n", counter, status);
tiny_printf("voltage=%*.*f size=%zu\r\n", 12, 3, voltage, size);
```

Full uses fixed C locale and nearest/ties-to-even rounding. Its exact decimal
conversion does not delegate to the vendor `printf`. Double/long-double precision
follows the selected CC-RH `-Xdbl_size` setting (both types must match); extra
printed decimals do not add floating-point precision. `%lc`/`%ls` accept C-locale
ASCII wide characters; unrepresentable characters return a negative result.
Ordinary `%s` copies encoded bytes. `%p` uses hexadecimal `0x...`, including
`0x0` for null; pointer notation is implementation-defined by the C standard.

Full returns the count accepted by UART, or a negative result on format/count/
encoding errors or UART timeout. This is not receiver/wire acknowledgement.
`SendChar()` returns the accepted unsigned byte or -1 on its existing wait bound.
Current mode ignores the hook error and retains attempted-output counts.
Format in foreground code, not in an ISR or Flash P/E path.

Format examples and the mode table are described in the
[RH850 Log guide](https://released.github.io/slide_boot_loader_for_RH850/app-development.html#rh850-runtime-log).
