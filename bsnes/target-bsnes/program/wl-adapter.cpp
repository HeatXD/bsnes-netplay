#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <vector>

#include <wl_sdk.h>
#include <gekkonet.h>

static std::vector<GekkoNetResult *> wlResults;

static void wlAdapterReset() {
    wlResults.clear();
}

static void wlAdapterSend(GekkoNetAddress *addr, const char *data, int len) {
    if(!addr || !addr->data || addr->size < 1) return;
    uint8_t to = *(uint8_t *)addr->data;
    wl_send(to, data, len);
}

static GekkoNetResult **wlAdapterReceive(int *out_count) {
    wlResults.clear();

    uint8_t from;
    uint8_t buf[1400];
    int len;
    while((len = wl_recv(&from, buf, sizeof(buf))) > 0) {
        //printf("[wl] recv from=%u len=%d\n", from, len);

        GekkoNetResult *result   = (GekkoNetResult *)malloc(sizeof(GekkoNetResult));
        uint8_t        *addr_buf = (uint8_t *)malloc(1);
        void           *data_buf = malloc(len);

        *addr_buf = from;
        memcpy(data_buf, buf, len);

        result->addr     = {addr_buf, 1};
        result->data     = data_buf;
        result->data_len = (unsigned int)len;

        wlResults.push_back(result);
    }

    *out_count = (int)wlResults.size();
    return wlResults.empty() ? nullptr : wlResults.data();
}

static void wlAdapterFree(void *ptr) {
    free(ptr);
}

static GekkoNetAdapter wlAdapterMake() {
    GekkoNetAdapter adapter;
    adapter.send_data    = wlAdapterSend;
    adapter.receive_data = wlAdapterReceive;
    adapter.free_data    = wlAdapterFree;
    return adapter;
}
