/*
 * wl_sdk.h — Weyvelength SDK public API
 *
 * 1. Call wl_init() with the bridge port and player ID from the Weyvelength client.
 * 2. Each game tick, call wl_recv() in a loop until it returns 0 to drain all
 *    pending inbound packets.
 * 3. Call wl_send() to transmit to a peer immediately.
 * 4. Call wl_shutdown() on exit.
 *
 * All functions are thread-safe.
 * Link against wl_sdk.dll.lib (Windows) or libwl_sdk.so (Linux).
 */

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

#ifdef _WIN32
#define WL_API __declspec(dllimport)
#else
#define WL_API
#endif

    /* Initialise. Pass --wl-udp-port and --wl-player-id from argv. Returns 0 on success. */
    WL_API int wl_init(int bridge_port, uint8_t local_player_id);

    /* Release all resources. */
    WL_API void wl_shutdown(void);

    /* Last error string. Valid until the next SDK call. */
    WL_API const char *wl_last_error(void);

    /* This player's ID (0 = not yet initialised). */
    WL_API uint8_t wl_local_player_id(void);

    /*
     * Receive one pending inbound packet into buf.
     * Returns bytes written (> 0), 0 if no packet is available, or -1 on error.
     * Call in a loop each game tick until it returns 0.
     */
    WL_API int wl_recv(uint8_t *from_player_id, void *buf, int buf_len);

    /* Send data_len bytes to to_player_id immediately. Returns 0 on success. */
    WL_API int wl_send(uint8_t to_player_id, const void *data, int data_len);

#ifdef __cplusplus
}
#endif
