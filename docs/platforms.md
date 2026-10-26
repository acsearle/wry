# Platforms, build system, and where to exercise multiplayer

Status: an assessment, written 2026-10-26 from a survey of the tree as of
commit c5239a2.  The measurements were taken by grep and will drift;
re-verify before acting on them.  Of the order of work in section 3.5,
steps 1 (CMake with Ninja on the Mac) and 2 (the render-free host)
landed 2026-10-26; see sections 2.5 and 3.6.  Nothing else is decided or
started.

Three questions, asked together because they share an answer:

1. Where to exercise multiplayer: one process, two processes on one
   machine, or two machines on the LAN.
2. Whether to make a meta-build system (CMake) the source of truth and
   the Xcode project a product of it.
3. What the path to Windows and Linux looks like, and which alternatives
   to clang + SDL3 + Vulkan are real.

Hardware on hand: the M1 Max Mac (arm64), and a dual-boot Windows /
Ubuntu desktop with a GeForce 4090 on the same LAN (x86-64).

## 0. What the survey found

Four findings shape everything below.

- The multiplayer tests already cover two of the three topologies.  The
  relay_* and host_* unit tests run a server with two or three clients
  in one process over real AF_UNIX socket pairs and loopback TCP, under
  the Debug sanitizers.  tools/wry_net_smoke.py runs two headless
  processes over loopback TCP and ends with a planted desync diagnosed.
  Both run on real time (5 ms quantum, 200 ms handshake deadline,
  `eventually` polling).  Neither injects faults.
- Headless mode is not render-free.  WryDelegate's setUpHeadless creates
  a Metal device and the full scene chain and renders offscreen at
  1920 x 1080 RGBA16F.  Any non-Mac build, even a server-only one,
  starts by carving out a host loop that needs no GPU.
- Outside render/, the Apple-specific surface is small and sits behind
  seams that already exist (section 3.1).
- The simulation looks float-free.  The only floating point in game/ is
  in the host-side WorldState (zoom, frame delta).  Cross-architecture
  lockstep is therefore plausible rather than hopeful; the content hash
  is the test.

Sizes, as counted:

    .cpp  78 files  29.5k lines      .mm      17 files   7.2k lines
    .hpp 142 files  31.2k lines      .metal    1 file    2.1k lines

Toolchain on the Mac that day: Xcode 27.0, deployment target macOS 26,
C++23.  cmake and ninja not installed.  Homebrew has sdl3 (with its CMake
config) and libpng (already linked, by hand-wired /opt/homebrew paths in
the pbxproj).  No Vulkan SDK or MoltenVK.

## 1. Where to exercise multiplayer

### 1.1 What the application can see

Over TCP the application never sees loss or reordering.  It sees four
things: delay, delay variance, backpressure, and death.  Loss on a real
link shows up as a stall followed by a burst when retransmission catches
up.  Death comes as a clean close, or as a silent black hole where
nothing arrives and the send buffer slowly fills.

So a fake network that drops frames tests a failure that cannot happen
over this transport.  A fake that stalls, bursts, throttles, splits
writes at arbitrary byte offsets, and dies silently or loudly tests
exactly what can.  That is the fault vocabulary to build.

### 1.2 The ladder

With that vocabulary, an in-process fake is more stringent than a wired
LAN for the protocol logic: a wired LAN is sub-millisecond, lossless and
in order, and exercises nothing the loopback does not, while the fake
reaches interleavings the LAN never produces and makes every failure a
reproducible test.  It is not a superset.  Each rung adds a category the
previous one cannot reach.

    Topology                          Adds                                    Have
    --------------------------------  --------------------------------------  ----
    In-process, real socket pairs     relay state machine, kick reasons,      yes
                                      prefix contract, under TSan/ASan
    In-process + fault proxy          timers meeting delayed delivery:        no
                                      stall then burst, throttle into the
                                      bounds, silent death, split frames
    Two processes, one machine        process isolation, killing one side     yes
                                      mid-transfer, startup/shutdown order,
                                      port binding
    Two processes + dummynet on lo0   real TCP retransmission dynamics        no
                                      under configured delay and loss
    Two machines, wired               hardware asymmetry for keep-up-or-die,  no
                                      firewall prompts, interface binding;
                                      a tamer network than dummynet
    Two machines, one on WiFi         a genuinely hostile network for free,   no
                                      and the realistic deployment
    Two platforms                     cross-compiler and cross-architecture   no
                                      determinism
    Virtual clock, in-memory pipes    every interleaving from a seed,         no
                                      millions overnight

The one-address-space caveat cuts the other way: in-process tests share
the GC heap, the pool and every static, so a bug where two clients
accidentally share state through a registry is invisible there, and
only the two-process smoke sees it.

Scenarios the proxy rung reaches and nothing today does:

- A client's stream stalls for a couple of seconds and arrives as a
  burst.  Is it kicked for silence first?  Does it apply its bounded
  share per frame and catch up?  What if the stall straddles a hash
  comparison window?
- A save transfer to a joiner is throttled below the relay's outbound
  bound (multiplayer.md open item 12): the joiner is kicked as a slow
  consumer while behaving perfectly.
- A peer dies silently during the handshake, versus after promotion.
  Both should end in a LEAVE, by different timers.

### 1.3 How to build the proxy rung

Transport is concrete over file descriptors, and transport, relay and
host never touch kevent directly (they use recv_some, send_some,
sleep_for, cancelable_after and with_deadline only).  So do not fake
Transport.  Put a shuttle between two socket pairs: a coroutine that
forwards bytes under a policy (delay; hold then release; cap bytes per
tick; split at random offsets; stop forwarding without closing; close).
It slots under the existing relay tests, touches no production code,
and exercises the real reactor and codec.

Its weakness is real time.  The suite already leans on short real
deadlines that the sanitizers stretch; expect to loosen a few.

### 1.4 The deterministic rung

This is the FoundationDB style of testing.  The architecture is unusually
well shaped for it: the relay is one sequential task over one inbox, and
every wait goes through the reactor's five operations.  But
steady_clock::now() is read directly at about forty sites in the reactor
and a dozen in the transport, so it means a second reactor implementation
plus a clock indirection, not a toggle.  Not first.  Funnelling clock
reads through one accessor as hygiene now keeps it cheap later.

### 1.5 Two machines

They earn their keep on three things only: hardware asymmetry
(keep-up-or-die has never run against a genuinely slower peer); the
network, if one side is on WiFi; and cross-platform determinism once a
second platform exists, which nothing on one machine can give.

Clock skew should be moot: the protocol exchanges a quantum and relative
timers, and no wall-clock timestamps were found.  One blocker before even
a Mac-to-Mac run: core/main.mm chdirs to a hardcoded assets path under
/Users/antony.

### 1.6 Order

1. The fault proxy under the in-process relay tests.
2. A dummynet profile for wry_net_smoke.py (pf + dnctl still ship in
   recent macOS and shape loopback; verify on 26.  netem on Linux).
3. Two machines when the Linux build exists.
4. The virtual clock only if bugs keep escaping 1 and 2.

## 2. CMake as the source of truth

### 2.1 Gains, in the order they would be felt

- Worktrees can build and run the suite.  Today a worktree has no
  project, so work there can only syntax-check.
- Faster command-line iteration: Ninja's no-op and incremental builds,
  and a compile_commands.json for clangd and clang-tidy.
- Configuration as code.  Release carrying ThreadSanitizer because the
  scheme's Launch action says so becomes a preset flag one can read.
  libpng's hand-wired Homebrew paths become one find_package line.
  Sanitizer, GPU validation and frame-capture settings become target
  properties (XCODE_SCHEME_*) rather than hidden scheme state.
- A target split.  One app target currently carries the GUI, headless
  mode and the tests.  A core library, a headless executable, an app
  bundle and a test runner is the natural CMake shape, and is how a
  Linux server-only build falls out without forking the source.
- The platform foundation: per-platform source lists, find_package for
  SDL3 and Vulkan, FetchContent for the small Vulkan helpers, a CI
  matrix later.

### 2.2 Costs

- The generated Xcode project is worse than the hand-made one: explicit
  file lists needing a reconfigure on every added file (unless globbing
  with CONFIGURE_DEPENDS, which the CMake documentation frowns on and
  which works fine for one developer); ALL_BUILD and ZERO_CHECK targets;
  build settings in CMake rather than the Xcode settings pane.
- A lag risk with a brand-new Xcode (27).  CMake's Xcode generator
  tracks Xcode closely but new-release quirks last a few weeks.  The
  Ninja path has no such dependency.
- Shader compilation is handled twice: the Xcode generator hands .metal
  files to Xcode; under Ninja a custom command runs the Metal compiler
  and library linker.  That is needed anyway to build from a shell.

The debugger, Instruments and Metal frame capture do not care who
generated the project, and attach to a Ninja-built bundle too.

### 2.3 Alternatives

Meson: nicer language, weak Xcode backend, thinner game ecosystem.
premake / xmake: popular in games, but SDL3 and Vulkan discovery is on
you.  Bazel: a different lifestyle.  zig build: interesting mainly as a
cross-compiler.  CMake is the boring right answer because SDL3, the
Vulkan SDK, the Vulkan memory allocator, SPIRV-Cross, shaderc, libpng
and Tracy all ship it, and clang-cl + Ninja + CMake is the standard
Windows setup.

### 2.4 Shape

CMake is the source of truth with two consumers: Ninja for daily and
worktree builds, and a generated Xcode project in an out-of-tree
directory for debugging and profiling.  Keep the hand-made project until
the generated one has earned trust, then delete it.

### 2.5 What landed (2026-10-26)

CMakeLists.txt at the repository root, CMakePresets.json beside it, and
cmake/Info.plist.in.  Membership is folder-based (CONFIGURE_DEPENDS
globs over the six module folders, matching the Xcode project's
synchronized groups), and the flags reproduce what the scheme passes,
captured from an xcodebuild log: C++23 without extensions, the scheme's
warning set, -O0 with DEBUG=1 and libc++'s debug hardening in Debug,
-Os with NDEBUG=1 in Release, Address + Undefined sanitizers with the
scheme's exclusions, the Metal compiler's fast-math flags with line
tables and recorded sources in Debug.  libpng, FreeType and SQLite come
from find_package instead of hard-wired paths.  The Xcode toolchain and
SDK are pinned in the presets, so DEVELOPER_DIR need not be set.

    cmake --preset mac-debug && cmake --build --preset mac-debug
    ctest --preset mac-debug                 # client --test-only
    build/mac-debug/client.app/Contents/MacOS/client --test-only FILTER

Presets: mac-debug (Address + Undefined, the scheme's default), mac-tsan
(Thread + Undefined), mac-release (no sanitizers), mac-xcode (the
generated project under build/mac-xcode, configured but not yet lived
in).  A clean Debug build takes about twenty seconds of wall clock on
the M1 Max.  Verified: the suite passes from the Ninja binary and from
the xcodebuild binary with the identical test list, and both compilers
emit the same warning kinds.

Known differences from the Xcode product: the bundle is ad-hoc signed
(Xcode signs with the development identity), it carries no copy of the
docs (the synchronized groups copy every .md into Resources, which
nothing reads), the sanitizer runtime is referenced from the toolchain
rather than copied into Contents/Frameworks, and Release's libc++
hardening mode is left at the library default where Xcode's choice has
not been checked.

## 3. The path to Windows and Linux

### 3.1 The measured surface, by layer

    Layer          Today                      Measured                         Replacement
    -------------  -------------------------  -------------------------------  --------------------------
    Compiler       Apple clang, C++23         [[clang::musttail]] x6 in the    clang on Linux,
                                              coroutine trampolines;           clang-cl on Windows
                                              __int128 trie keys and save
                                              tags; ext_vector_type simd;
                                              __builtin_{popcount,ctz,clz}g;
                                              _Float16 vectors
    Window, input, AppKit, AVAudioEngine,     17 .mm files, but gui.mm and     SDL3
    audio, paths   hardcoded assets dir       font.mm are all but free of
                                              Objective-C; WryAudio.mm is
                                              109 lines; GameController is
                                              linked but unused
    Graphics       Metal, bind-and-draw       2.1k lines MSL, ~35 entry        Vulkan; or SDL3 GPU;
                   style                      points, 1 mesh pipeline (text),  or WebGPU native
                                              1 compute kernel; MPS Gaussian
                                              blur + image add; no argument
                                              buffers, heaps or ICBs
    Reactor and    kqueue (READ, WRITE,       transport, relay and host        epoll + timerfd +
    sockets        TIMER, USER), socketpair,  never touch kevent               eventfd; WSAPoll or IOCP
                   AF_UNIX control socket
    Threads and    os_sync_wait_on_address,   ext/atomic.hpp, ext/mutex.hpp,   futex / WaitOnAddress,
    atomics        os_unfair_lock, one        one dispatch_async_f in          SRWLOCK, an own
                   Dispatch hop, pthread      ext/coroutine.cpp, pthread       blocking pool
                   names, mach time           names in gc / pool / reactor /
                                              bag, mach time in debug / test

Apple simd: about 16 types and 35 functions used across 33 files (25 of
them in render/).  clang's ext_vector_type exists on every clang target,
so a same-named header is a few hundred lines; MSVC or GCC would need a
class library instead.

### 3.2 Compilers

Clang everywhere.  MSVC has no musttail (the coroutine resume and destroy
cascades would recurse on the stack), no __int128, and no vector
extensions.  GCC is friendlier: musttail since 15, the generic builtins
since 14, only the simd header needs a second path.  Neither is a port
requirement; both are later hygiene, GCC the more valuable because a
second front end finds a different class of bugs.  clang-cl integrates
with the Visual Studio debugger and uses the Microsoft ABI and standard
library.

### 3.3 Graphics: the only multi-week item and the one real choice

The renderer is a conventional Metal 2 design: small constants pushed per
draw, a few bound textures, instanced draws, universal pixel formats
(RGBA16F, RGBA8 and sRGB, Depth32F, R32F, RG16F).  The Vulkan tax is the
plumbing before the first triangle (instance, device, swapchain,
synchronisation, memory allocation, descriptors), not the features.
Mesh shaders map to VK_EXT_mesh_shader, which the 4090 supports fully.
The two MPS kernels become compute shaders, wanted anyway for control
over the Gaussian.  The planned Metal 4 migration
(client/client/render/docs/metal4_migration.md) deepens the Metal path.
So the honest shape is two backends: Metal first-class on the Mac,
Vulkan on the other two, behind the per-header boundary render/ already
keeps.

Alternatives:

- Wine, in either direction.  Wine and Proton run Windows binaries on
  Linux and macOS.  Nothing runs a macOS binary anywhere else (Darling
  has no Metal).  Wine only helps once a Windows build exists.
- Direct3D 12 + Proton for Linux.  The commercial pattern: ship Windows,
  the Steam Deck runs it through vkd3d-proton.  Best Windows tooling,
  spares a native Linux build for end users.  But a third API family,
  and nothing can be debugged through a translation layer; for running
  natively on the Ubuntu box it loses to Vulkan.
- MoltenVK.  One Vulkan backend for all three, the Mac served through
  translation.  No mesh shader support as far as known, which pushes
  GPU text onto the CPU atlas path, and it argues against Metal 4.
  Worth installing even under the two-backend plan: the Vulkan backend
  could be developed and debugged on the Mac without rebooting the PC.
  Ships in the LunarG SDK.
- SDL3 GPU.  A thin layer over Metal, Vulkan and D3D12 designed for one
  backend on three platforms, shaders compiled offline per target.
  D3D11-era feature tier: no mesh shaders.  Matches what the renderer
  actually uses almost exactly, minus the text mesh shader and the two
  blur kernels.  Lowest total work if atlas text everywhere is
  acceptable, or a simpler second backend than raw Vulkan if Metal
  stays.  Young; still moves a little.
- WebGPU native (Dawn, wgpu).  Same tier as SDL GPU, bigger build, an
  API designed around Metal's concepts.  Same mesh shader loss.
- OpenGL 4.6 on Windows and Linux.  Least code, rock-solid NVIDIA
  drivers, but a global-state model that fits Metal-shaped code worse
  than Vulkan does, and dead on the Mac.  Pass.
- Shader source.  Hand-porting the MSL to GLSL is a few days at this
  size, but two sources then drift.  Slang compiles one source to
  SPIR-V and MSL and is now hosted by Khronos.  Hand-port if the Vulkan
  backend is an experiment; Slang if two backends are the long-term
  plan.

### 3.4 Determinism across machines

libm and the standard libraries differ between Apple, glibc and
Microsoft, and arm64 contracts multiply-adds where baseline x86-64
cannot.  None of it reaches a float-free simulation.  The rule to keep:
no float ever enters World.  The content hash is the test that it has
not.

### 3.5 Order of work

Each step is useful on its own.

1. CMake + Ninja on the Mac: same binary, suite green both ways.
   LANDED 2026-10-26 (section 2.5).
2. Carve the render-free host: GuiContext, WorldState, Host/Relay and
   the control socket with no device or scene chain.  The one design
   change everything else leans on.
   LANDED 2026-10-26 (section 3.6).
3. Linux headless on the PC: epoll reactor; wait-on-address, lock,
   Dispatch and thread-name shims; the simd header; a configurable
   assets path.  libpng is already portable.  Payoff before any
   graphics work: relay and bot clients on the Ubuntu box for LAN
   testing, Linux CI, rr for the collector, a second kernel and libc++
   under the suite, and the first cross-architecture run of the hash.
4. Vulkan + SDL3 on Linux, the friendliest place to develop a Vulkan
   backend: native RenderDoc and Nsight, no Winsock yet.
5. Windows: the same backend plus the Winsock reactor, clang-cl, a
   replacement for socketpair (Windows has AF_UNIX stream sockets but
   no socketpair), and path handling.

### 3.6 What landed (2026-10-26): the render-free host

`--headless` is now render/headless_host.{hpp,cpp}: no NSApplication,
window, device or scene chain.  It drives GuiContext and WorldState
directly, performing the host's per-frame duties as WryDelegate does
minus the drawing; the main menu's buttons moved into
render/main_menu.{hpp,cpp} so the Metal scene and the headless host
share them; `GuiContext::wait_for_input` idles on the control socket
and the session's inbox at once (a when_any over non-consuming queue
waits); the launch options moved from the delegate into
`GuiContext::configure_from_arguments`.  The former headless mode, the
Metal scenes rendering to an offscreen drawable, is `--offscreen`, which
the screenshot smoke uses; the network smoke runs two headless
processes.  core/docs/commands.md section 9 describes both.

What is still Apple in the headless path, for step 3: main.mm itself
(the entry point is Objective-C++ only because the GUI branch is), the
reactor, the ext shims, the simd header, libpng's build wiring, and the
hardcoded assets path.  WorldState still carries GUI state (the palette,
the camera uniforms, the map pipeline) and builds the world map in the
background; harmless headless, and a later split if it ever matters.

Rough sizes: the shims are days each, the SDL3 layer about a week,
Windows a few days on top of Linux, the Vulkan backend the one
multi-week piece.  Semi-mechanical is the right word for everything
except step 2 and the graphics choice.

Hardware notes.  Dual boot means one PC operating system live at a time;
WSL2 in mirrored networking mode lets the Linux headless build run
beside the Windows client on the same box, reachable from the Mac.  A
Linux VM or container on the Mac gives Linux without the PC, but arm64
Linux; the PC gives x86-64, which is the architecture that actually
tests determinism.
