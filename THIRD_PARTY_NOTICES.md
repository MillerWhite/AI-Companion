AI Companion incorporates a modified procedural voice synthesizer derived from Pip-KeyPet.

Upstream project: https://github.com/NovikovVisuals/Pip-KeyPet
Upstream files: ui/voice.c and ui/voice.h
Copyright (c) 2026 David Novikov
License: MIT

The upstream license permits use, modification, distribution, sublicensing, and sale, provided this notice and the copyright notice are retained. The synthesizer has been adapted for AI Companion's nonverbal-only sound cues.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

Additional adapted upstream module:
- Pip-KeyPet `ui/imu.c` and `ui/imu.h`: the pure-C motion classifier in `companion_motion.c` / `companion_motion.h`, with renamed types and event identifiers. Copyright (c) 2026 David Novikov; MIT License (full license text above).
