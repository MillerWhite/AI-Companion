# Pip-KeyPet upstream source

This directory contains source copied from the MIT-licensed upstream project:
https://github.com/NovikovVisuals/Pip-KeyPet
Upstream revision: `37bc39da2104ea8445da8605d441d1ba1456ab96`.

The original MIT license is retained at `LICENSE`. Keep the copyright and
license notice with redistributed copies and substantial portions.

## Imported modules

- `ui/face.c`, `ui/face.h`: procedural shaded ellipsoid eye renderer.
- `ui/pip.c`, `ui/pip.h`, `ui/moods.h`: eye animation, mood shapes,
  gaze, blink/wink, head pose, reactions and interaction logic.
- `ui/doodle.c`, `ui/doodle.h`: procedural scene effects.
- `ui/apps.c`: clock, badge, Quick QR, notifications, music, remote,
  camera, timer, noise meter, games and other app screens.
- `ui/phone.c`, `ui/phone.h`: iPhone notifications/media UI and computer
  remote UI contracts.
- `ui/shell.c`, `ui/shell.h`: app navigation and hardware-button routing.
- `ui/theme.c`, `ui/theme.h`: visual themes, typography and UI primitives.
- `ui/profile.c`, `ui/profile.h`: badge identity and four Quick QR links.
- `ui/voice.c`, `ui/voice.h`: procedural nonverbal voice synthesis.
- `ui/imu.c`, `ui/imu.h`: shake, tilt, face-down and step classifier.
- `ui/mic.c`, `ui/mic.h`: sound level and blow detection.
- `ui/maze.c`, `ui/maze.h`: maze game physics.
- `docs/index.html`: upstream browser control panel reference.

## AI Companion port boundary

These are retained as upstream source/reference modules. They are not all
compiled into AI Companion's production firmware yet. The upstream LVGL app
stack has its own board glue, fonts, application shell, BLE host and pet-state
model. Directly replacing AI Companion's `main.c` or build manifest with
Pip's would break the existing Waveshare setup, owner memory/personality,
cloud speech path, AI Companion USB protocol and service modules.

The production eye renderer at
`firmware/esp-idf/main/companion_face_renderer.c` is already based on the
upstream procedural `ui/face.c`. AI Companion's current firmware continues to
own its display, audio, Wi-Fi, BLE, iPhone, USB, QR and cloud services.

## Explicit exclusions

Pip's food/energy/fun/love depletion, feeding, care timers and punishment for
being left alone are not part of AI Companion. The upstream `pet.c` and
`pet.h` are intentionally not imported into the production firmware. The
friend's reactions are driven by interaction, sensors, owner-configured
personality and AI events instead.

## Status

This is a source import and integration foundation, not a claim that every
upstream app has already been ported to the device. Production integration
must adapt the app/phone/shell contracts to AI Companion's existing modules
and must be verified with CI and the physical Waveshare board.
