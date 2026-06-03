# ESP32 Advanced OTA Hub (With Downgrade & CI/CD Support)

A commercial-style proof-of-concept for Over-The-Air (OTA) firmware updates on ESP32 devices.

## About The Project

This repository provides an advanced working example of how to implement remote firmware updates for ESP32 microcontrollers. Moving beyond basic single-file updates, this project demonstrates a robust pipeline where devices can dynamically fetch a list of available firmware versions, allowing the user to seamlessly **upgrade or downgrade** their device via a beautiful local web dashboard.

This project serves as a foundational template for prototyping reliable, commercial-grade remote update flows, complete with automated GitHub Actions CI/CD.

## Core Features

* **Multi-Version Firmware Selector:** The ESP32 parses an array of available versions from a cloud JSON manifest and populates a dynamic dropdown menu in the local web UI, allowing users to explicitly choose which version to flash.
* **Safe Downgrade Support:** Bypasses standard "upgrade-only" limitations, allowing users to safely roll back the ESP32 to older firmware versions for troubleshooting or recovery.
* **Automated CI/CD Pipeline:** Includes a GitHub Actions workflow that automatically compiles the C++ code, uniquely names the `.bin` artifact (e.g., `firmware_1.0.8.bin`), and dynamically appends the new version to the `manifest.json` whenever a Git tag is pushed.
* **Zero-Config Local Network Access (mDNS):** Eliminates the need for hardcoded Static IPs. Devices automatically broadcast a collision-proof local domain using their MAC address suffix (e.g., `http://otahub-A1B2.local`), making them instantly discoverable on local networks.
* **Real-Time Progress UI:** Uses background async polling to securely stream real-time flash progress to the web browser without triggering hardware cache panics.

## Getting Started

To get a local copy up and running, follow these steps.

### Prerequisites

* A C++ development environment configured for the ESP32 (such as PlatformIO or Arduino IDE).
* The [ESP32OTAPull Library](https://github.com/mikalhart/ESP32-OTA-Pull) installed in your environment.
* An OTA-compatible partition scheme selected in your IDE (e.g., "Default 4MB with spiffs").

### Initial Setup

1. Clone this repository to your local machine.
2. In your main code, ensure your baseline version is defined:

```cpp
#define CURRENT_VERSION "1.0.0"

```

3. Flash the initial code to your ESP32 via a physical USB cable.
4. Once booted, the device will host a setup network or connect to your local Wi-Fi. Access the device dashboard via its unique mDNS address (e.g., `http://otahub-XXXX.local`).

## The Automated Update Workflow (CI/CD)

This project utilizes GitHub Actions to completely automate the firmware release process. You do not need to manually edit the JSON file or upload binaries.

1. **Update Your Code:** Make your desired changes and update the `#define CURRENT_VERSION` macro in your C++ code (e.g., to `"1.0.0"`).
2. **Commit Your Changes:** Commit your code to the `main` branch.
3. **Tag and Push:** Create a Git tag starting with `v` and push it to GitHub. This tells the CI/CD pipeline that a new production release is ready.

```bash
git tag v1.0.0
git push origin v1.0.0

```

4. **Automation Takes Over:** * GitHub Actions will spin up a cloud server, compile your PlatformIO project, and generate `firmware_1.0.0.bin`.
* It modifies the `manifest.json` file in your repository, **appending** the new configuration to the top of the list so older versions remain available for downgrading.


5. **Device Pull:** Visit your device's local dashboard (`http://otahub-XXXX.local`). The UI will automatically fetch the updated manifest, populate the dropdown menu with the new version, and allow you to initiate the flash sequence.

### Example Manifest Structure

The automated pipeline manages a `manifest.json` file that looks like this, using the `Config` parameter as a specific version selector:

```json
{
  "Configurations": [
    {
      "Board": "ESP32_DEV",
      "Config": "target_v1.0.8",
      "Version": "1.0.8",
      "URL": "https://raw.githubusercontent.com/segestic/OTA-Demo-downgrade/main/firmware_1.0.8.bin"
    },
    {
      "Board": "ESP32_DEV",
      "Config": "target_v1.0.0",
      "Version": "1.0.0",
      "URL": "https://raw.githubusercontent.com/segestic/OTA-Demo-downgrade/main/firmware_1.0.0.bin"
    }
  ]
}

```

## Status & Error Codes

When the device checks for an update via the ESP32OTAPull library, it returns specific status codes to help with troubleshooting:

| Code | Constant | Description |
| --- | --- | --- |
| `0` | `UPDATE_OK` | The firmware was successfully downloaded and written to flash memory. |
| `-1` | `NO_UPDATE_AVAILABLE` | A matching profile was found, but the device is already running this version (and downgrades were not explicitly allowed). |
| `-2` | `NO_UPDATE_PROFILE_FOUND` | No configuration in the JSON matched the explicitly requested `Config` string or hardware profile. |
| `-3` | `UPDATE_AVAILABLE` | A new update is available on the server, but the device was explicitly instructed to only check, not download. |
| `1` | `HTTP_FAILED` | Failed to connect to the server or download the file (e.g., manifest URL is incorrect or server is down). |
| `2` | `WRITE_ERROR` | The file downloaded, but the device failed to write it to its internal flash memory. |
| `3` | `JSON_PROBLEM` | The manifest file could not be parsed. Check for formatting errors or trailing commas. |
| `4` | `OTA_UPDATE_FAIL` | A partition error occurred. Ensure your ESP32 is using an OTA-enabled partition scheme. |

## Acknowledgments

* [ESP32OTAPull Library](https://github.com/mikalhart/ESP32-OTA-Pull) - The core backend library handling the chunked binary downloads and JSON parsing.
