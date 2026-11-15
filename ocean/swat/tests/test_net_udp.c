// Two-process real-UDP network test (make net-udp-test, NETWORK.md).
//
// The parent forks a server process that runs the production ENet server
// (net.c) on a motel session at 60 Hz wall time. The parent is the client
// (net.c's client: deltas, redundant inputs, interpolation, shared-controller
// prediction) and also runs an in-process impairment shim: a UDP relay
// thread between the client's ENet socket and the server's, which delays,
// jitters, drops and duplicates datagrams with a seeded generator. Every
// byte crosses real localhost UDP sockets and real ENet.
//
// The server writes its truth (actor positions per tick, its own position
// after each executed command of the client) to a file; after both finish the
// client compares what it presented with it: the same remote/local/latency
// metrics as the simulated-link harness (tests/test_netcond.c). Wall-clock
// scheduling makes the numbers vary slightly from run to run (the loss and
// delay draws are seeded), so the limits are looser than the harness's.
#include "net.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define TICK_MS (1000.0/60.0)
#define FRAME_MS (1000.0/120.0)
#define RUN_MS 14000.0
#define WARMUP_MS 2500.0
#define MAX_TICKS 2400
#define MAX_SEQ 2400

static double now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return t.tv_sec*1000.0+t.tv_nsec/1e6;
}
static void sleep_until(double t) {
    double d=t-now_ms(); if(d<=0) return;
    struct timespec s={(time_t)(d/1000),(long)(fmod(d,1000)*1e6)}; nanosleep(&s,NULL);
}

// Same scripted local player as the harness's motel loop.
static SwatInput script(uint32_t n) {
    SwatInput in=swat_neutral_input();
    uint32_t p=n%300;
    if(p<60) in.forward=1;
    else if(p<100) { in.strafe=1; in.forward=.4f; }
    else if(p<125) {}
    else if(p<185) in.forward=-1;
    else if(p<215) { in.forward=1; in.gait=SWAT_SPRINT; }
    else if(p<240) {}
    else if(p<270) { in.strafe=(n/7)%2 ? 1 : -1; }
    else in.strafe=-1;
    in.yaw_delta=.012f*sinf((float)n/35.0f);
    return in;
}

// ---------------------------------------------------------------------------
// Truth shared through a file
// ---------------------------------------------------------------------------
typedef struct Truth {
    int ticks; uint32_t max_seq,starved,merged;
    double tick_ms[MAX_TICKS];                 // wall time each tick was stepped
    b3Pos actor[MAX_TICKS][SWAT_MAX_ACTORS];
    bool present[MAX_TICKS][SWAT_MAX_ACTORS];
    b3Pos seq[MAX_SEQ]; double exec_ms[MAX_SEQ];
} Truth;
static Truth truth;

static void server_process(int port,const char* path) {
    static SwatSim sim; static SwatNetServer server;
    SwatConfig config=swat_default_config(); config.mission=SWAT_MOTEL; swat_config_human(&config);
    config.max_ticks=100000; config.hostile_fire=false; config.randomize=false;
    swat_sim_init(&sim,config,42);
    if(!swat_server_open(&server,&sim,port,false)) { fprintf(stderr,"server: %s\n",server.error); _exit(2); }
    memset(&truth,0,sizeof(truth));
    double start=now_ms(),next=start,joined=-1;
    while(now_ms()-start<RUN_MS+15000) {
        swat_server_poll(&server);
        bool connected=server.player_mask!=0;
        if(connected && joined<0) joined=now_ms();
        if(!connected && joined>=0) break;                   // the client left
        swat_server_tick(&server,NULL);
        int t=sim.tick;
        if(connected && t>0 && t<MAX_TICKS) {
            truth.ticks=t; truth.tick_ms[t]=now_ms();
            for(int a=0;a<SWAT_MAX_ACTORS;a++) {
                truth.present[t][a]=sim.actors[a].present && sim.actors[a].alive;
                if(sim.actors[a].present) truth.actor[t][a]=b3Body_GetPosition(sim.actors[a].controller.body.body);
            }
            uint32_t ack=server.slots[0].ack;
            if(ack && ack<MAX_SEQ && !truth.exec_ms[ack]) {
                truth.exec_ms[ack]=now_ms(); truth.seq[ack]=truth.actor[t][0];
                if(ack>truth.max_seq) truth.max_seq=ack;
            }
        }
        next+=TICK_MS; sleep_until(next);
        if(now_ms()-next>250) next=now_ms(); // a stalled host does not catch up in a burst
    }
    truth.starved=server.slots[0].starved; truth.merged=server.slots[0].merged;
    FILE* f=fopen(path,"wb"); if(!f || fwrite(&truth,sizeof(truth),1,f)!=1) _exit(3); fclose(f);
    swat_server_close(&server); swat_sim_close(&sim);
    _exit(0);
}

// ---------------------------------------------------------------------------
// Impairment shim: a UDP relay with seeded delay, jitter, loss, duplication
// ---------------------------------------------------------------------------
typedef struct Shim {
    int front,back;                  // client-facing socket, server-facing socket
    struct sockaddr_in client,server;
    bool have_client; volatile bool stop;
    double rtt,jitter,loss,duplicate; uint32_t rng;
    struct { double at; int dir; int size; unsigned char bytes[1500]; } queue[8192]; int count;
    uint64_t dropped,duplicated,forwarded;
} Shim;
static double shim_uniform(Shim* s) { return (double)(swat_random(&s->rng)>>8)/16777216.0; }
static void shim_enqueue(Shim* s,int dir,const unsigned char* bytes,int size,double now) {
    for(int copy=0;copy<2;copy++) {
        if(copy==0 && shim_uniform(s)<s->loss) { s->dropped++; continue; }
        if(copy==1 && shim_uniform(s)>=s->duplicate) break;
        if(copy==1) s->duplicated++;
        if(s->count==8192 || size>1500) return;
        double delay=s->rtt*.5+(shim_uniform(s)*2-1)*s->jitter; if(delay<.5) delay=.5;
        s->queue[s->count].at=now+delay; s->queue[s->count].dir=dir; s->queue[s->count].size=size;
        memcpy(s->queue[s->count].bytes,bytes,(size_t)size); s->count++;
    }
}
static void* shim_run(void* context) {
    Shim* s=context; unsigned char buffer[2048];
    while(!s->stop) {
        double now=now_ms();
        for(int i=0;i<s->count;) {
            if(s->queue[i].at>now) { i++; continue; }
            if(s->queue[i].dir==0) sendto(s->back,s->queue[i].bytes,(size_t)s->queue[i].size,0,(struct sockaddr*)&s->server,sizeof(s->server));
            else if(s->have_client) sendto(s->front,s->queue[i].bytes,(size_t)s->queue[i].size,0,(struct sockaddr*)&s->client,sizeof(s->client));
            s->forwarded++; s->queue[i]=s->queue[--s->count];
        }
        struct pollfd fds[2]={{s->front,POLLIN,0},{s->back,POLLIN,0}};
        if(poll(fds,2,1)<=0) continue;
        if(fds[0].revents&POLLIN) {
            struct sockaddr_in from; socklen_t length=sizeof(from);
            ssize_t n=recvfrom(s->front,buffer,sizeof(buffer),0,(struct sockaddr*)&from,&length);
            if(n>0) { s->client=from; s->have_client=true; shim_enqueue(s,0,buffer,(int)n,now_ms()); }
        }
        if(fds[1].revents&POLLIN) {
            ssize_t n=recv(s->back,buffer,sizeof(buffer),0);
            if(n>0) shim_enqueue(s,1,buffer,(int)n,now_ms());
        }
    }
    return NULL;
}
static int udp_socket(int port) {
    int fd=socket(AF_INET,SOCK_DGRAM,0); assert(fd>=0);
    struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons((uint16_t)port),.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
    if(bind(fd,(struct sockaddr*)&address,sizeof(address))!=0) { close(fd); return -1; }
    return fd;
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------
typedef struct Stat { double sum,sum2,max; int n,cap; float* v; } Stat;
static void add(Stat* s,double x) {
    s->sum+=x; s->sum2+=x*x; if(x>s->max) s->max=x;
    if(s->n==s->cap) { s->cap=s->cap ? s->cap*2 : 4096; s->v=realloc(s->v,sizeof(float)*(size_t)s->cap); }
    s->v[s->n++]=(float)x;
}
static int cmpf(const void* a,const void* b) { float x=*(const float*)a,y=*(const float*)b; return x<y ? -1 : x>y; }
static double mean(const Stat* s) { return s->n ? s->sum/s->n : 0; }
static double rms(const Stat* s) { return s->n ? sqrt(s->sum2/s->n) : 0; }
static double pct(Stat* s,double q) { if(!s->n) return 0; qsort(s->v,(size_t)s->n,sizeof(float),cmpf); return s->v[(int)(q*(s->n-1)+.5)]; }

static b3Pos truth_at(double tick,int actor) {
    if(tick<1) tick=1; if(tick>truth.ticks) tick=truth.ticks;
    int a=(int)floor(tick),b=a+1<=truth.ticks ? a+1 : a; double u=tick-a;
    b3Pos p=truth.actor[a][actor],q=truth.actor[b][actor];
    return (b3Pos){p.x+(q.x-p.x)*u,p.y+(q.y-p.y)*u,p.z+(q.z-p.z)*u};
}

typedef struct Frame { double ms,render_tick; uint32_t seq; b3Pos local; b3Pos remote[SWAT_MAX_ACTORS]; bool valid[SWAT_MAX_ACTORS]; } Frame;
#define MAX_FRAMES 2200 // (RUN_MS+1000)/FRAME_MS plus margin
static Frame frames[MAX_FRAMES];
static double sent_ms[MAX_SEQ];

int main(int argc,char** argv) {
    (void)argc; (void)argv;
    signal(SIGPIPE,SIG_IGN);
    double rtt=120,jitter=20,loss=.02,duplicate=.005;
    // Ports: the server's and the shim's client-facing one, chosen free.
    int server_port=0,front=-1,back=-1,front_port=0;
    for(int attempt=0;attempt<50 && front<0;attempt++) {
        int base=31000+(int)((getpid()*7+attempt*13)%20000);
        int probe=udp_socket(base); if(probe<0) continue; close(probe);
        front=udp_socket(base+1); if(front<0) continue;
        server_port=base; front_port=base+1;
    }
    assert(front>=0);
    back=socket(AF_INET,SOCK_DGRAM,0); assert(back>=0);
    char path[]="/tmp/swat_net_udp_XXXXXX"; int fd=mkstemp(path); assert(fd>=0); close(fd);
    pid_t child=fork(); assert(child>=0);
    if(child==0) { close(front); close(back); server_process(server_port,path); }

    static Shim shim; memset(&shim,0,sizeof(shim));
    shim.front=front; shim.back=back; shim.rtt=rtt; shim.jitter=jitter; shim.loss=loss; shim.duplicate=duplicate; shim.rng=20261115u;
    shim.server=(struct sockaddr_in){.sin_family=AF_INET,.sin_port=htons((uint16_t)server_port),.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
    pthread_t thread; assert(pthread_create(&thread,NULL,shim_run,&shim)==0);

    static SwatSim replica; static SwatNetClient client;
    SwatConfig config=swat_default_config(); config.mission=SWAT_MOTEL; swat_config_human(&config);
    swat_sim_init(&replica,config,7);
    usleep(300000); // let the server build its world and listen
    bool opened=false;
    for(int attempt=0;attempt<5 && !opened;attempt++) {
        opened=swat_client_open(&client,&replica,"127.0.0.1",front_port);
        double wait=now_ms();
        while(opened && client.status!=SWAT_NET_ACTIVE && client.status!=SWAT_NET_FAILED && now_ms()-wait<8000) { swat_client_poll(&client); usleep(2000); }
        if(opened && client.status!=SWAT_NET_ACTIVE) { swat_client_close(&client); opened=false; usleep(500000); }
    }
    if(!opened) { printf("FAIL net-udp: client could not join the server through the shim\n"); kill(child,SIGKILL); return 1; }
    printf("joined: slot %d actor %d, server port %d via shim %d (RTT %.0f ms +-%.0f, loss %.1f%%, duplication %.1f%%)\n",
        client.slot,client.actor,server_port,front_port,rtt,jitter,loss*100,duplicate*100);
    int actor=client.actor;
    double start=now_ms(),next_frame=start,next_command=start; int frame_count=0; uint32_t revision_start=0; uint64_t bytes_start=0;
    double measure_start=start+WARMUP_MS; bool measuring=false; uint32_t revisions_measured=0;
    while(now_ms()-start<RUN_MS && client.status==SWAT_NET_ACTIVE) {
        double now=now_ms();
        swat_client_poll(&client);
        while(next_command<=now) {
            next_command+=TICK_MS;
            SwatInput in=script(client.sequence+1);
            if(swat_client_input(&client,&in) && client.sequence<MAX_SEQ) sent_ms[client.sequence]=now;
        }
        float alpha=(float)(1-(next_command-now)/TICK_MS);
        swat_client_present(&client,(float)(FRAME_MS/1000),alpha);
        if(!measuring && now>=measure_start) { measuring=true; revision_start=client.deltas_applied; bytes_start=client.bytes_received; }
        if(measuring && frame_count<(int)(sizeof(frames)/sizeof(frames[0]))) {
            Frame* f=&frames[frame_count++]; f->ms=now; f->render_tick=client.view.render_tick; f->seq=client.sequence;
            f->local=b3Body_GetPosition(replica.actors[actor].controller.body.body);
            for(int a=0;a<SWAT_MAX_ACTORS;a++) {
                f->valid[a]=a!=actor && replica.actors[a].present && replica.actors[a].alive && client.view.shown_valid[a];
                if(f->valid[a]) f->remote[a]=b3Body_GetPosition(replica.actors[a].controller.body.body);
            }
        }
        next_frame+=FRAME_MS; sleep_until(next_frame);
        if(now_ms()-next_frame>250) next_frame=now_ms();
    }
    double measured_s=(now_ms()-measure_start)/1000;
    revisions_measured=client.deltas_applied-revision_start; uint64_t bytes=client.bytes_received-bytes_start;
    SwatPredictor predictor=client.predictor; bool shared=client.predictor.world!=NULL;
    swat_client_close(&client);
    int status=0; waitpid(child,&status,0);
    shim.stop=true; pthread_join(thread,NULL); close(front); close(back);
    FILE* f=fopen(path,"rb"); bool read=f && fread(&truth,sizeof(truth),1,f)==1; if(f) fclose(f); unlink(path);
    if(!read || !WIFEXITED(status) || WEXITSTATUS(status)!=0) { printf("FAIL net-udp: server process failed (status %d)\n",status); return 1; }

    Stat local_err={0},remote_err={0},remote_jerk={0},local_jerk={0},latency={0},delay={0};
    b3Pos lh[3]; int lf=0; b3Pos rh[SWAT_MAX_ACTORS][3]; int rf[SWAT_MAX_ACTORS]={0};
    for(int i=0;i<frame_count;i++) {
        const Frame* fr=&frames[i];
        if(fr->seq<MAX_SEQ && truth.exec_ms[fr->seq]) add(&local_err,100*b3Distance(fr->local,truth.seq[fr->seq]));
        lh[0]=lh[1]; lh[1]=lh[2]; lh[2]=fr->local;
        if(++lf>=3) add(&local_jerk,1000*b3Length(swat_add(b3SubPos(lh[2],lh[1]),swat_mul(b3SubPos(lh[1],lh[0]),-1))));
        int tick=(int)floor(fr->render_tick);
        if(tick>=1 && tick<truth.ticks && truth.tick_ms[tick]) add(&delay,fr->ms-truth.tick_ms[tick]-(fr->render_tick-tick)*TICK_MS);
        for(int a=0;a<SWAT_MAX_ACTORS;a++) {
            if(!fr->valid[a] || tick<2 || tick>=truth.ticks || !truth.present[tick][a]) { rf[a]=0; continue; }
            if(b3Distance(truth_at(fr->render_tick,a),truth_at(fr->render_tick-1,a))*60<.2f) { rf[a]=0; continue; }
            add(&remote_err,100*b3Distance(fr->remote[a],truth_at(fr->render_tick,a)));
            rh[a][0]=rh[a][1]; rh[a][1]=rh[a][2]; rh[a][2]=fr->remote[a];
            if(++rf[a]>=3) add(&remote_jerk,1000*b3Length(swat_add(b3SubPos(rh[a][2],rh[a][1]),swat_mul(b3SubPos(rh[a][1],rh[a][0]),-1))));
        }
    }
    for(uint32_t q=1;q<MAX_SEQ;q++) if(sent_ms[q]>=measure_start && truth.exec_ms[q]) add(&latency,truth.exec_ms[q]-sent_ms[q]);
    double correction_mean=predictor.corrections ? 100.0*predictor.correction_sum/predictor.corrections : 0;
    printf("| %-38s | %9s | unit\n","metric (real UDP, two processes)","value");
    printf("|----------------------------------------|-----------|-----\n");
    printf("| %-38s | %9.2f | %s\n","remote error vs truth (shown time)",mean(&remote_err),"cm mean");
    double remote_p95=pct(&remote_err,.95);
    printf("| %-38s | %9.2f | %s\n","remote error vs truth p95",remote_p95,"cm");
    printf("| %-38s | %9.2f | %s\n","remote display delay",mean(&delay),"ms mean");
    printf("| %-38s | %9.2f | %s\n","remote jerk RMS",rms(&remote_jerk),"mm/frame^2");
    printf("| %-38s | %9.2f | %s\n","local error vs own latest input",mean(&local_err),"cm mean");
    double local_p95=pct(&local_err,.95);
    printf("| %-38s | %9.2f | %s\n","local error p95",local_p95,"cm");
    printf("| %-38s | %9.2f | %s\n","local jerk RMS",rms(&local_jerk),"mm/frame^2");
    printf("| %-38s | %9.2f | %s\n","local correction mean (per event)",correction_mean,"cm");
    printf("| %-38s | %9.2f | %s\n","local correction max",100.0*predictor.correction_max,"cm");
    printf("| %-38s | %9.2f | %s\n","input send->execute latency",mean(&latency),"ms mean");
    printf("| %-38s | %9.2f | %s\n","input latency p95",pct(&latency,.95),"ms");
    printf("| %-38s | %9u | %s\n","server input starvation",truth.starved,"ticks");
    printf("| %-38s | %9u | %s\n","server merged commands",truth.merged,"count");
    printf("| %-38s | %9.2f | %s\n","snapshot deltas applied",revisions_measured/measured_s,"per s");
    printf("| %-38s | %9.2f | %s\n","downstream (ENet payload)",bytes/measured_s/1000,"kB/s");
    printf("shim: forwarded %llu, dropped %llu, duplicated %llu datagrams; prediction %s, %u replays, %u rebuilds\n",
        (unsigned long long)shim.forwarded,(unsigned long long)shim.dropped,(unsigned long long)shim.duplicated,
        shared ? "shared controller" : "kinematic fallback",predictor.replays,predictor.rebuilds);
    int failures=0;
#define LIMIT(cond,msg) if(!(cond)) { printf("FAIL net-udp: %s\n",msg); failures++; }
    LIMIT(frame_count>500 && local_err.n>500 && remote_err.n>100,"too few measured frames");
    LIMIT(shared,"client did not predict with the shared controller");
    LIMIT(mean(&local_err)<12,"local error mean above 12 cm");
    LIMIT(100.0*predictor.correction_max<40,"local correction max above 40 cm");
    LIMIT(remote_p95<4,"remote error p95 above 4 cm");
    LIMIT(rms(&remote_jerk)<20,"remote jerk RMS above 20 mm/frame^2");
    LIMIT(mean(&latency)>40 && mean(&latency)<260,"input latency mean outside 40-260 ms");
    LIMIT(revisions_measured/measured_s>18,"fewer than 18 snapshots per second");
    LIMIT(bytes/measured_s<40000,"downstream above 40 kB/s");
    LIMIT(shim.dropped>0,"the shim dropped nothing");
#undef LIMIT
    if(failures) return 1;
    puts("PASS net-udp: two processes over real UDP through a seeded delay/jitter/loss shim, same metrics end to end");
    return 0;
}
