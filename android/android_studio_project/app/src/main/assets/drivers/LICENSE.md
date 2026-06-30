# Bundled GPU driver — license & attribution

`turnip.zip` in this directory is an adrenotools driver package (a `meta.json`
plus a Vulkan ICD `.so`) that xenia-thor installs automatically on first run so
the emulator uses the validated Mesa Turnip driver out-of-the-box on the AYN
Thor (Adreno 740). Users can still override or update it from the in-app
**GPU drivers** screen (`GpuDriverManagerActivity`).

## What is bundled

* **Driver:** Mesa **Turnip** — the open-source Vulkan driver for Qualcomm
  Adreno GPUs (part of the [Mesa 3D Graphics Library](https://www.mesa3d.org/)).
* **Build:** `Turnip v26.0.0 - R8` (reports Vulkan 1.4.335), `vulkan.ad07xx.so`.
* **Packaged by:** the community
  [K11MCH1/AdrenoToolsDrivers](https://github.com/K11MCH1/AdrenoToolsDrivers)
  project (meta.json author: "KIMCHI"). Redistributed unmodified.

The `.so` is shipped verbatim; xenia-thor does not modify the Mesa binary.

## License

Mesa is licensed under the **MIT (Expat) License**. The bundled Turnip binary is
covered by that license (individual Mesa components carry their own copyright
notices held by their respective authors — e.g. Red Hat, Google, Valve, and the
freedreno/Turnip contributors).

```
Copyright (C) the Mesa 3D Graphics Library contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

Full Mesa licensing details: <https://docs.mesa3d.org/license.html>.
