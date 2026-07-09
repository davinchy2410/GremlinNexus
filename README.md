
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
* **Advanced Asynchronous Macros:** Create complex sequences of keyboard presses, mouse clicks, and vJoy inputs with custom delays using a fluid drag-and-drop editor. Runs on a non-blocking engine to keep latency at zero.
* **Dynamic Rotary Sequences:** Turn simple rotary encoders into state machines. Map a single dial to seamlessly cycle through custom sequences of keystrokes, audio cues, or macros.
* **Hardware Toggle & Latching Switch Support:** Perfect for VIRPIL control panels. Easily wrap and convert physical two-position latching switches into momentary pulses recognized by modern space simulators.
* **Tempo Actions (Short/Long/Double Press):** Map multiple functions to a single physical button. Hold a button to switch internal logic modes, double-tap to trigger a macro, or short-press for a standard input.
* **Steal Binding (Recursive Exclusivity):** Never worry about accidental double-binds again. The engine recursively scans your profile (even inside nested Macros or Toggles) and automatically unbinds old overlapping assignments.
* **vJoy Auditor:** A built-in visual "minesweeper" that scans your active profile and displays a real-time grid of all free and occupied vJoy buttons, ensuring you never run out of logical slots blindly.
* **Star Citizen Native Integration:** Features a dedicated integration module utilizing native Star Citizen terminology (such as Strafe and Decoupled) for frictionless configuration.
* **Modern Telemetry UI:** A fully responsive, dark-themed (Catppuccin-inspired) QML interface that prevents visual fatigue, featuring smart layout constraints and scalable tab navigation for massive hardware setups.

## 📸 Screenshots


* <img width="982" height="587" alt="Animation 3" src="https://github.com/user-attachments/assets/0aaacf98-59c7-467b-8b57-c14a33ea6a3f" />
* <img width="982" height="587" alt="Animation 2" src="https://github.com/user-attachments/assets/b5114756-22a9-470a-8a87-979a23c033c9" />
* <img width="1915" height="1021" alt="profiles" src="https://github.com/user-attachments/assets/e68a07a1-a5db-46e3-ab04-cafd5f63d42a" />

* <img width="1897" height="1018" alt="divide tester" src="https://github.com/user-attachments/assets/ae184b44-6187-4856-8103-f7cbdddfc609" /><img width="982" height="587" alt="Animation" src="https://github.com/user-attachments/assets/47c645e3-d245-4741-8d9a-8c9bab356765" />
* <img width="1909" height="1015" alt="settings " src="https://github.com/user-attachments/assets/ea8941e5-aac2-477d-89c7-292706c8300d" />
* <img width="1048" height="796" alt="pwa editor" src="https://github.com/user-attachments/assets/21d1c425-fef4-4acd-b78c-9d4b56f03080" />
* <img width="437" height="448" alt="joy bindings" src="https://github.com/user-attachments/assets/0b4c90aa-8b3c-4e2f-ba06-5facd9216217" />
* <img width="435" height="375" alt="button bindings " src="https://github.com/user-attachments/assets/fbebd315-1453-498d-9a38-fcaed8a7d136" />
* <img width="416" height="489" alt="macro editor" src="https://github.com/user-attachments/assets/e0010ea4-e37a-4c3d-8c2e-d8849d78ead2" />
* <img width="1903" height="1022" alt="starcitizen " src="https://github.com/user-attachments/assets/f219b03a-d710-43cb-95dd-27006abcc9d3" />







## 🛠️ Installation & Usage

For regular users, there is no need to compile the source code. 

1. Download the latest precompiled `setup.exe` from the [Releases](https://github.com/davinchy2410/GremlinNexus/releases) page.
2. Run the installer and follow the on-screen instructions to complete the installation.
3. Launch `GremlinNexus.exe`.
4. Connect your devices, create a new profile, and start routing!

## 💻 Building from Source

If you wish to contribute or build the project yourself:

### Prerequisites
* CMake (3.16+)
* Ninja Build System
* Qt 6.x (with QML/QtQuick modules)
* MSVC or MinGW compiler

### Build Steps

```bash
git clone [https://github.com/davinchy2410/GremlinNexus.git](https://github.com/davinchy2410/GremlinNexus.git)
cd GremlinNexus
cmake -B build -G Ninja
cmake --build build
```

## 🌍 Internationalization (i18n)
The application is built with a strict English core and fully supports dynamic language switching via Qt's `QTranslator`. Current supported translations include English and Español.

## 👨‍💻 Author & Credits
Developed and maintained by **Darian Zuain**.

Special thanks to the flight sim and Star Citizen communities for the ongoing inspiration for better peripheral management tools.

## 📄 License
This project is licensed under the GNU General Public License v3.0 (GPLv3) - see the `LICENSE` file for details. 

This ensures that Gremlin Nexus and any derivative works will always remain free and open-source software.
