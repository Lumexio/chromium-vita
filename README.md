# chromium-vita

Detailed implementation plan for a PS Vita homebrew web browser project.

## Goal
Build a practical, installable VPK browser for PS Vita (3.60/3.65 homebrew), with the most modern web compatibility feasible on Vita hardware.

---

## 1) Feasibility analysis

### Hardware and OS constraints
- **CPU**: Quad-core ARM Cortex-A9-class, low clock for modern browser engines.
- **RAM**: Limited system memory for large page processes/JIT-heavy runtimes.
- **GPU**: Vita GPU is not directly compatible with Chromium’s mainstream GPU backends.
- **Storage**: Homebrew storage is limited; browser assets, cache, and cert bundles must be compact.
- **OS/runtime**: PS Vita homebrew environment lacks Linux/Android/Windows assumptions Chromium expects.

### Practical expectation
- A **full upstream Chromium port is not realistic** as an MVP due to memory, platform, sandbox, and maintenance complexity.
- A realistic target is a **modern-ish, limited browser**:
  - HTTPS/TLS support
  - Basic JS-capable browsing
  - Lightweight tabs (1–3)
  - No heavy extensions/media DRM/service-worker-heavy features in MVP

### Minimum viable feature set
- URL bar + navigation controls
- Single tab initially, optional 2–3 tab cap
- TLS 1.2+ with updated cert store
- Basic HTML/CSS/JS rendering
- Download handling (small files), bookmarks, history

---

## 2) Architecture and components

### Chromium component feasibility
- **Blink + V8 + Chromium multi-process model**: too heavy for initial Vita delivery.
- **Network stack**: can be reused conceptually, but direct Chromium net integration is high effort.
- **Sandbox**: likely disabled/replaced in homebrew context.

### Recommended architecture (phased)
1. **MVP engine path (recommended)**: Use a lighter embeddable engine first (e.g., WebKit/WPE, NetSurf, or a minimal existing engine) behind a Vita-native shell.
2. **Chromium-inspired shell**: Keep module boundaries similar to Chromium-style browser architecture:
   - UI/input layer
   - Navigation/session layer
   - Renderer abstraction
   - Network/TLS abstraction
3. **Future Chromium experiments**: Isolate and prototype individual Chromium subsystems only after MVP works on hardware.

### Rendering approach
- **Primary**: software rendering with careful region invalidation (lowest platform risk).
- **Optional acceleration**: lightweight GPU blit/composition for final frame upload where possible.
- Reason: direct Chromium GPU backend porting is high risk early.

---

## 3) Toolchain and build system

### Required tooling
- **VitaSDK** (preferred)
- `cmake`, `ninja`, `clang` toolchain from VitaSDK flow
- Vita libraries (vita2d / vitasdk networking/input/libs as required)
- Scripted CI build for reproducibility

### Cross-compile strategy
- Use VitaSDK toolchain files in CMake.
- Keep browser core as portable C/C++ modules with a thin Vita platform layer.
- Build third-party dependencies as static libs where feasible to simplify packaging.

### Build configuration priorities
- Optimize for memory and size:
  - Disable debug symbols in release builds
  - Prefer `-Os`/size-aware settings for release
  - Disable non-essential optional features
- Gate expensive features behind compile flags.

### Build and run locally (same as CI)
1. Install prerequisites:
   - VitaSDK (and set `VITASDK`)
   - `cmake`
   - `ninja`
   - `python3`

2. From the repository root:
   ```bash
   cd /path/to/chromium-vita
   python3 scripts/gen_assets.py
   cmake -B build \
     -DCMAKE_BUILD_TYPE=Release \
     -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
     -G Ninja
   cmake --build build --parallel
   ```

3. Generated installable package:
   ```bash
   build/chromium-vita.vpk
   ```

4. Copy the `.vpk` to your Vita (VitaShell USB/FTP) and install it from VitaShell.

---

## 4) Porting strategy (milestones)

1. **Milestone 0: Skeleton app**
   - VPK app boots, renders UI shell, handles input, exits cleanly.
2. **Milestone 1: Networking/TLS foundation**
   - HTTP/HTTPS requests, cert validation, error surfaces.
3. **Milestone 2: Rendering integration**
   - Embed chosen engine and render first pages.
4. **Milestone 3: Navigation UX**
   - Address bar, back/forward/reload/stop, basic history.
5. **Milestone 4: Stability/perf pass**
   - Memory budgets, watchdogs, crash recovery, cache sizing.
6. **Milestone 5: Packaging and release**
   - Final VPK metadata/assets, user docs, install/update path.

### Platform abstraction handling
- Create `platform/vita/*` for input, timing, sockets/TLS glue, storage paths, and frame presentation.
- Keep engine-facing interfaces platform-neutral for future portability.

### TLS/SSL strategy
- Use a maintained TLS library compatible with Vita homebrew environment.
- Bundle/update CA cert store in app data.
- Provide clear UI for certificate/hostname errors.

---

## 5) UI/UX on Vita

### Input mapping
- Left stick / D-pad: cursor and focus navigation
- Touchscreen: direct pointer interaction
- Buttons:
  - `X`: activate/select
  - `O`: back/cancel
  - Triggers: tab switch / scroll modifiers

### On-screen keyboard
- Integrate Vita IME for URL/search input.
- Provide compact input overlay with recent URL suggestions.

### Memory-safe UI approach
- Avoid large retained UI trees.
- Use fixed-size pools/ring buffers for history/log panels where practical.
- Cap number of tabs and page cache aggressively.

---

## 6) VPK packaging

### Required structure (high level)
- `eboot.bin`
- `sce_sys/param.sfo`
- `sce_sys/pic0.png`
- `sce_sys/icon0.png`
- `sce_sys/livearea/contents/*` (LiveArea assets/templates)
- App resources (fonts, certs, default config) in predictable internal paths

### Metadata guidance
- TitleID: unique homebrew-safe ID format (avoid collisions)
- `param.sfo`: clear app name, version, and category
- Version assets and metadata with release tags

---

## 7) Testing and debugging

### Workflow
- Use emulator for quick iteration where possible.
- Validate every milestone on **real Vita hardware** for memory/input/rendering correctness.

### Debugging
- Structured logging with log levels and ring-buffer persistence.
- Crash handling + last-action breadcrumbs.
- Maintain reproducible test pages:
  - static HTML baseline
  - JS-heavy sample
  - TLS edge-case sample

---

## 8) Risks and alternatives

### Major blockers
- Chromium-scale memory/process model mismatch with Vita
- GPU backend incompatibility
- Ongoing security patch burden for browser engines

### Mitigation
- Start with lightweight engine MVP.
- Keep strict feature gating and hard memory budgets.
- Automate dependency/security update checks.

### Alternatives
- **WebKit-based embedded port**: better modern compatibility than very small engines, lower risk than full Chromium.
- **NetSurf**: much lighter, but limited modern JS compatibility.
- **Kiosk mode target**: if full browsing is unrealistic, support a defined set of sites well.

---

## Local tests

- Run host-side unit tests for tooling scripts:
  - `python3 -m unittest discover -s tests -v`

---

## Suggested default project context
- Target device: **PS Vita (3.60/3.65 homebrew)**
- Preferred SDK: **VitaSDK**
- Experience level assumption for this plan: **intermediate**
- Primary goal: **minimal modern browsing first, then incremental expansion**
- Performance target: **responsive UI with stable page interaction over raw FPS claims**
