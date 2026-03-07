# WL SDK Issue: Tokio Worker Thread Starvation

## Problem

When integrating the wl SDK with GekkoNet as a custom adapter in a game loop,
the SDK stops sending and receiving packets entirely, causing GekkoNet to time
out and disconnect all peers after ~5 seconds.

### Root Cause

`wl_init()` creates a multi-threaded Tokio runtime with **one worker thread**
that handles all I/O (both the recv_task and send_task). In a game that uses
a tight main loop with no frame-rate limiter active, the calling thread
monopolizes the CPU and the OS never schedules the Tokio worker thread. With
no worker thread time, `send_task` never drains its channel and `recv_task`
never reads from the socket, so zero packets flow in either direction.

This is specific to the wl adapter because GekkoNet's built-in UDP adapter
uses a blocking OS thread for I/O which the OS schedules independently. The
wl SDK's Tokio async tasks are cooperative and depend on getting CPU time.

### Reproduction

Conditions that trigger the bug:
1. Game loop runs without emulator->run() executing each iteration (e.g.
   during GekkoNet's initial handshake phase, before any GekkoAdvanceEvent
   is fired — no emulator frames = no audio output = no audio buffer blocking
   = loop spins at CPU speed).
2. wl_init() was called on the same thread as the game loop.
3. Tokio runtime has only 1 worker thread.

### Evidence

Adding `printf` statements anywhere in the send/receive path fixes the issue
because printf is a blocking syscall that causes the OS to context-switch,
giving the Tokio worker thread time to run. A `usleep(500ms)` before starting
GekkoNet does NOT fix it because the starvation happens continuously during
the game loop, not just at startup. A `usleep(1)` inside the game loop's
netplay polling function DOES fix it for the same reason as printf.

## Workaround (caller side)

Call `usleep(1)` (or equivalent yield) once per game loop iteration while
the wl adapter is active. This is enough to trigger an OS context switch.

```cpp
// In the game loop, before gekko_network_poll():
if(wl_active) usleep(1);
```

## Suggested SDK Fix

The SDK should not depend on the caller yielding. Options in order of preference:

### Option A: Increase Tokio worker threads (minimal change)

In `core.rs`, change the runtime builder from 1 worker thread to at least 2,
so the recv_task and send_task each have a dedicated thread:

```rust
// Before:
tokio::runtime::Builder::new_multi_thread()
    .worker_threads(1)
    ...

// After:
tokio::runtime::Builder::new_multi_thread()
    .worker_threads(2)
    ...
```

This ensures I/O tasks always have a thread available even if the Tokio
scheduler is under pressure from other tasks.

### Option B: Use a dedicated OS thread (most robust)

Spawn the recv loop and send loop as plain `std::thread` threads with blocking
I/O (`socket.recv_from` blocking). This is completely independent of any
async runtime scheduling and cannot be starved by the caller's event loop.
This is what GekkoNet's built-in adapter does.

### Option C: Expose a wl_poll() function (polling model)

Add a `wl_poll()` function that the caller invokes each game loop tick to
process pending sends and receives on the caller's thread. This gives the
caller explicit control and works even in single-threaded environments. The
recv callback would be fired synchronously from within `wl_poll()`.
