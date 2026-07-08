Markdown
# Gremlin Nexus 🚀

![C++](https://img.shields.io/badge/C++-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white)
![Qt](https://img.shields.io/badge/Qt-41CD52?style=for-the-badge&logo=qt&logoColor=white)
![vJoy](https://img.shields.io/badge/vJoy-Integration-blue?style=for-the-badge)
![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)

**Gremlin Nexus** is an advanced, high-performance input router and profile manager designed specifically for complex space simulators like Star Citizen. 

Built for enthusiasts running heavy HOSAS (Hands-On Stick And Stick) setups—such as VKB Gladiator EVOs and VIRPIL Control Panels—Gremlin Nexus takes raw hardware inputs and routes them through virtual joysticks (vJoy) with unparalleled logic, exclusivity, and customization.

---

## 🛰️ Core Features

* **Intelligent vJoy Routing:** Seamlessly map multiple physical devices into consolidated virtual vJoy outputs.
* **Tempo Actions (Short/Long/Double Press):** Map multiple functions to a single physical button. Hold a button to switch internal logic modes, double-tap to trigger a macro, or short-press for a standard input.
* **Steal Binding (Recursive Exclusivity):** Never worry about accidental double-binds again. The engine recursively scans your profile (even inside nested Macros or Toggles) and automatically unbinds old overlapping assignments.
* **vJoy Auditor:** A built-in visual "minesweeper" that scans your active profile and displays a real-time grid of all free and occupied vJoy buttons, ensuring you never run out of logical slots blindly.
* **Star Citizen Native Integration:** Features a dedicated integration module utilizing native Star Citizen terminology (Strafe, Decoupled, Power Triangle) for frictionless configuration.
* **Modern Telemetry UI:** A fully responsive, dark-themed (Catppuccin-inspired) QML interface that prevents visual fatigue, featuring smart layout constraints and scalable tab navigation for massive hardware setups.

## 📸 Screenshots

*(Replace these placeholders with actual screenshots of your polished UI)*

* `[Image: Main Profile Editor showing the Axis/Button routing]`
* `[Image: vJoy Auditor Popup in action]`
* `[Image: Tempo Action Picker showing Mode Switch integration]`
* `[Image: Star Citizen Integration Module]`

## 🛠️ Installation & Usage

For regular users, there is no need to compile the source code. 

1.  Download the latest pre-compiled `.zip` from the [Releases](../../releases) page.
2.  Extract the archive and run `GremlinNexus.exe`.
3.  Plug in your gear, create a new profile, and start routing!

## 💻 Building from Source

If you wish to contribute or build the project yourself:

### Prerequisites
* CMake (3.16+)
* Ninja Build System
* Qt 6.x (with QML/QtQuick modules)
* MSVC or MinGW compiler

### Build Steps
```bash
git clone [https://github.com/yourusername/GremlinNexus.git](https://github.com/yourusername/GremlinNexus.git)
cd GremlinNexus
cmake -B build -G Ninja
cmake --build build
🌍 Internationalization (i18n)
The application is built with a strict English core and fully supports dynamic language switching via Qt's QTranslator. Current supported translations include English and Español.

👨‍💻 Author & Credits
Developed and maintained by Darian Zuain.

Special thanks to the flight sim and Star Citizen communities for the ongoing inspiration for better peripheral management tools.

📄 License
This project is licensed under the GNU General Public License v3.0 (GPLv3) - see the LICENSE file for details.

This ensures that Gremlin Nexus and any derivative works will always remain free and open-source software.
