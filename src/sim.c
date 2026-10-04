#include "sim.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static Uint32 g_type;
static pthread_t g_thr;
static int g_started;

/* 每个事件独立堆分配；主循环处理完后由 SIM_RELEASE 事件释放。 */
static void post(SimKind kind, int a, int b, int c, const char *text) {
    SimEvent *d = calloc(1, sizeof(*d));
    d->kind = kind; d->a = a; d->b = b; d->c = c;
    if (text) snprintf(d->text, sizeof(d->text), "%s", text);
    SDL_Event e;
    memset(&e, 0, sizeof(e));
    e.type = g_type;
    e.user.code = (int)kind;
    e.user.data1 = d;
    SDL_PushEvent(&e);
}

static void msleep(int ms) {
    usleep((useconds_t)ms * 1000);
}

static void *run(void *arg) {
    FILE *f = fopen((const char *)arg, "r");
    if (!f) { perror("sim script"); return NULL; }
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char cmd[32];
        if (sscanf(line, "%31s", cmd) != 1) continue;
        if (cmd[0] == '#') continue;
        if (!strcmp(cmd, "wait")) { int ms=0; sscanf(line,"%*s %d",&ms); msleep(ms); }
        else if (!strcmp(cmd, "size")) {
            int w=0,h=0,dd=0; sscanf(line,"%*s %d %d %d",&w,&h,&dd);
            post(SIM_SIZE, w, h, dd, NULL);
        }
        else if (!strcmp(cmd, "burst")) {
            int n=0,ms=0,w0=0,h0=0,w1=0,h1=0;
            sscanf(line,"%*s %d %d %d %d %d %d",&n,&ms,&w0,&h0,&w1,&h1);
            for (int i=0;i<n;i++) {
                int w = w0 + (w1-w0)*(i+1)/n;
                int h = h0 + (h1-h0)*(i+1)/n;
                post(SIM_SIZE, w, h, 0, NULL);
                if (ms > 0) usleep((useconds_t)((long)ms*1000/n));
            }
        }
        else if (!strcmp(cmd, "rotate")) { int rr=0; sscanf(line,"%*s %d",&rr); post(SIM_ROTATE,rr,0,0,NULL); }
        else if (!strcmp(cmd, "move")) { int xx=0,yy=0; sscanf(line,"%*s %d %d",&xx,&yy); post(SIM_MOVE,xx,yy,0,NULL); }
        else if (!strcmp(cmd, "tap")) { int xx=0,yy=0; sscanf(line,"%*s %d %d",&xx,&yy); post(SIM_TAP,xx,yy,0,NULL); }
        else if (!strcmp(cmd, "shot")) { char pp[256]={0}; sscanf(line,"%*s %255s",pp); post(SIM_SHOT,0,0,0,pp); }
        else if (!strcmp(cmd, "publish")) { post(SIM_PUBLISH,0,0,0,NULL); }
        else if (!strcmp(cmd, "quit")) { post(SIM_QUIT,0,0,0,NULL); }
    }
    fclose(f);
    return NULL;
}

bool sim_start(SimBridge *b, const char *script_path) {
    g_type = b->event_type;
    if (g_started) return false;
    if (pthread_create(&g_thr, NULL, run, (void *)script_path) != 0) return false;
    g_started = 1;
    return true;
}

void sim_stop(void) {
    if (g_started) { pthread_join(g_thr, NULL); g_started = 0; }
}
