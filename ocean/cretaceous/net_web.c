// The web build has no networking: browsers cannot open the UDP sockets ENet
// needs, so tools/build_web.sh compiles this file in place of net.c,
// netclient.c and netsim.c. Hosting and joining fail at once (the title menu
// hides them on web) and every per-frame call is a no-op. Native builds never
// compile it.
#ifdef __EMSCRIPTEN__
#include "net.h"
#include <string.h>

CretNet* cret_net_host(int port, int first_slot) { (void)port; (void)first_slot; return NULL; }
void cret_net_server_poll(CretNet* n, CretSim* s, CretInput* inputs, bool* remote) { (void)n; (void)s; (void)inputs; (void)remote; }
void cret_net_server_broadcast(CretNet* n, const CretSim* s) { (void)n; (void)s; }
void cret_net_server_stats(const CretNet* n, CretNetServerStats* st) { (void)n; memset(st, 0, sizeof *st); }
CretNet* cret_net_join(const char* address, int port, float timeout_s) { (void)address; (void)port; (void)timeout_s; return NULL; }
CretNet* cret_net_join_start(const char* address, int port, const CretNetsimCfg* netsim, uint32_t seed) {
    (void)address; (void)port; (void)netsim; (void)seed; return NULL;
}
bool cret_net_client_welcomed(const CretNet* n, CretConfig* cfg, int* slot) { (void)n; (void)cfg; (void)slot; return false; }
void cret_net_client_tick(CretNet* n, CretSim* s, const CretInput* in) { (void)n; (void)s; (void)in; }
int cret_net_client_request(CretNet* n, int species) { (void)n; (void)species; return 0; }
bool cret_net_client_spawn_reply(CretNet* n, CretSpawnReply* out) { (void)n; (void)out; return false; }
void cret_net_client_profile(CretNet* n, const char* name) { (void)n; (void)name; }
void cret_net_client_unlock(CretNet* n, int choice) { (void)n; (void)choice; }
bool cret_net_client_poll(CretNet* n, CretSim* s) { (void)n; (void)s; return false; }
void cret_net_client_draw(CretNet* n, const CretSim* s, CretSim* out, float alpha, float dt) { (void)n; (void)alpha; (void)dt; if (out && s) memcpy(out, s, sizeof *out); }
void cret_net_pump(CretNet* n) { (void)n; }
void cret_net_client_stats(const CretNet* n, CretNetStats* st) { (void)n; memset(st, 0, sizeof *st); }
void cret_net_client_stats_reset(CretNet* n) { (void)n; }
void cret_net_close(CretNet* n) { (void)n; }
float cret_net_hist_pct(const uint32_t* hist, int bins, float p) { (void)hist; (void)bins; (void)p; return 0; }
#endif
