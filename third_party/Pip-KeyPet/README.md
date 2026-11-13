# Pip-KeyPet upstream source

This directory contains the upstream Pip-KeyPet files copied from
https://github.com/NovikovVisuals/Pip-KeyPet at commit
`37bc39da2104ea8445da8605d441d1ba1456ab96`.

The upstream project is licensed under MIT. The original license is included at
`LICENSE`; retain it with these source files.

## Imported source

- `ui/face.c` and `ui/face.h`: original procedural 3D ellipsoid eye renderer.
- `ui/pip.c` and `ui/pip.h`: original mood table, animation spring system,
  gaze, blink/wink, head pose, reactions and pet behavior.
- `ui/moods.h`: complete upstream mood identifiers.
- `ui/doodle.c` and `ui/doodle.h`: original procedural scene doodles and rain cloud.

## Integration in AI Companion

- `firmware/esp-idf/main/companion_face_renderer.c` is the upstream
  `ui/face.c` renderer with only its header include renamed to the local
  `companion_face_renderer.h`.
- The firmware animation loop uses the same paired-eye concepts: shared
  head pose, curved eye placement, far-eye foreshortening, 120 Hz substepped
  damped springs, shared gaze and synchronized blink.
- `AI_Companion_Emotional_Engine.html` uses one shared eye-size target per
  emotion, a shared pupil vector and a single three-axis head transform.
- `pip.c` is retained verbatim as the complete upstream reference engine.
  It is not compiled into the firmware as-is because it is the original
  LVGL application layer and depends on Pip-KeyPet's own pet, IMU, mic,
  theme, voice and UI modules. AI Companion keeps its own voice, memory,
  cloud, BLE, iPhone and device services; those services must not be replaced
  by Pip's unrelated application code.

This is an integration/port, not a claim that the two applications have
identical surrounding architecture.
