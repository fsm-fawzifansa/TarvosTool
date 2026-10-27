# TarvosTool

A macOS desktop tool for talking to **Würth Elektronik Tarvos-e** radio modules over a USB serial connection (FTDI VCP → UART). Use it to:

- send any of the module's documented commands from a **Command Builder**, with a live preview of the exact bytes on the wire,
- watch every transmitted and received frame in a decoded **Communication Log**,
- **chat** in plain text between two modules, and
- run **several modules at once**, one window per device.

It speaks the module's binary WE-ProWare serial protocol. Built with C++17 and wxWidgets.

> TarvosTool is an independent community project. It is not affiliated with or endorsed by Würth Elektronik.

---

## Requirements

| Requirement | Install |
|---|---|
| macOS | Apple Silicon or Intel |
| Xcode Command Line Tools | `xcode-select --install` |
| Homebrew | https://brew.sh |
| CMake + wxWidgets | `brew install cmake wxwidgets` |
| FTDI VCP driver (for FTDI-based boards) | https://ftdichip.com/drivers/vcp-drivers/ — reboot after installing |
| A Tarvos-e module or EV-kit | connected over USB |

---

## Build and run

```bash
git clone https://github.com/fsm-fawzifansa/TarvosTool.git
cd TarvosTool
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(sysctl -n hw.logicalcpu)
open TarvosTool.app
```

The build targets arm64 by default. For an Intel Mac:

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=x86_64
```

### VS Code

Open the folder and press **⌘⇧B** to configure and build. IntelliSense reads `compile_commands.json`, which is generated in the project root by the first build. If you see squiggles before that, run **C/C++: Reset IntelliSense Database** after building.

---

## Usage

### 1. Connect a module

1. Plug in the module and check that its port exists:
   ```bash
   ls /dev/cu.*
   ```
   Always use the `cu.*` node, not `tty.*`. The `tty.*` node waits for a carrier signal and will not work for host-initiated communication.
2. In TarvosTool click **Refresh**, pick the port, leave the baud rate at **115200** unless you changed it on the module, and click **Connect**.

The status bar shows the connection state and the TX/RX byte counters.

### 2. Send commands

Pick a command in the **Command Builder**. The parameter fields change to match it, and the exact frame that will be sent is previewed before you press **Send**. Values are hex bytes, for example `48 65 6C 6C 6F`.

Sent frames (`→ TX`) and received frames (`← RX`) appear in the log, each followed by a decoded description. For example, selecting `RESET_REQ` and pressing **Send** transmits `02 05 00 07`, and the module answers with its confirmation and a `RESET_IND` once it is ready again.

### 3. Chat between two modules

1. Connect each module in its own window (see below).
2. Click **Open Chat** in both windows.
3. In each chat window set **Dest Addr** (the other module's 2-byte address, hex) and **Channel** (hex, the same on both modules), and press **Set** next to each. The tool sends `SET_DESTADDR_REQ` and `SET_CHANNEL_REQ` for you.
4. Type a message and press **Enter**. It is sent with `DATA_REQ` to the configured destination, and appears in the other window as a message from "Remote". Messages can be up to 255 bytes of UTF-8.

### 4. Several modules at once

**File → New Window** (⌘N) opens another window with its own serial connection and protocol parser. Open one per module.

---

## Supported commands

Frame layout (WE-ProWare):

```
[STX=0x02] [CMD] [LEN] [PAYLOAD ...] [CS]
```

`LEN` is the payload length in bytes (1 byte). `CS` is the XOR of every preceding byte. A confirmation code is the request code `| 0x40`, and an indication code is the request code `| 0x80`.

| CMD  | Name              | Purpose                                      |
|------|-------------------|----------------------------------------------|
| 0x00 | DATA_REQ          | Send data to the configured destination      |
| 0x01 | DATAEX_REQ        | Send data with extended addressing           |
| 0x04 | SET_MODE_REQ      | Switch operating mode                        |
| 0x05 | RESET_REQ         | Software reset                               |
| 0x06 | SET_CHANNEL_REQ   | Set RF channel                               |
| 0x07 | SET_DESTNETID_REQ | Set destination network ID                   |
| 0x08 | SET_DESTADDR_REQ  | Set destination address                      |
| 0x09 | SET_REQ           | Write a non-volatile user setting            |
| 0x0A | GET_REQ           | Read a non-volatile user setting             |
| 0x0B | UNICAST_DATA_REQ  | Send data to a specific address              |
| 0x0D | RSSI_REQ          | Read the last received signal strength       |
| 0x0E | SHUTDOWN_REQ      | Shut the module down                         |
| 0x0F | STANDBY_REQ       | Enter standby                                |
| 0x10 | SET_PAPOWER_REQ   | Set PA output power                          |
| 0x12 | FACTORY_RESET_REQ | Restore defaults and reset                   |

Responses and events the tool decodes: `DATA_CNF` (0x40), `RESET_CNF` (0x45), `SET_CNF` (0x49), `GET_CNF` (0x4A), `FACTORY_RESET_CNF` (0x52), `DATAEX_IND` (0x81, incoming radio data followed by an RSSI byte), `RESET_IND` (0x85) and `STANDBY_IND` (0x8F).

Check the Tarvos-e manual for your firmware version for the exact meaning of each setting and parameter.

---

## Project structure

```
TarvosTool/
├── CMakeLists.txt
├── LICENSE
├── include/
│   ├── SerialPort.h         POSIX serial port (cu.* devices, 8N1)
│   ├── TarvosProtocol.h     frame builder, stateful parser, command constants
│   ├── MainFrame.h          main window: connection, log, command builder
│   └── ChatFrame.h          chat window
└── src/
    ├── main.cpp             wxApp entry point
    ├── SerialPort.cpp
    ├── TarvosProtocol.cpp
    ├── MainFrame.cpp
    └── ChatFrame.cpp
```

To add a command, add an entry to the command table at the top of `src/MainFrame.cpp` and, if you want a named constant, to `TarvosProtocol.h`.

---

## License

Released under the [MIT License](LICENSE). You are free to use, modify and distribute this software, including in your own projects, as long as you keep the copyright notice and license text, which credits **Fawzi Fansa** as the original author.
