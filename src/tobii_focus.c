// tobii_focus — Hyprland "focus follows eyes", independent of the mouse.
//
// Reads normalized gaze "x y" lines on stdin (from tobii_gaze), maps them to
// global screen coordinates on the tracker's monitor, hit-tests the window
// under the gaze using Hyprland's IPC, and after a short dwell focuses that
// window via `dispatch focuswindow address:0x...` — without moving the cursor.
//
// Usage:  tobii_gaze | tobii_focus [--monitor NAME] [--dwell-ms N] [--debug]
// Build:  cc -O2 -o tobii_focus tobii_focus.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>

static char sockpath[512];

// One request/response round trip on Hyprland's command socket (.socket.sock).
static int hypr_req(const char *cmd, char *out, size_t outsz){
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un a; memset(&a,0,sizeof a); a.sun_family = AF_UNIX;
    strncpy(a.sun_path, sockpath, sizeof(a.sun_path)-1);
    if (connect(fd, (struct sockaddr*)&a, sizeof a) < 0){ close(fd); return -1; }
    write(fd, cmd, strlen(cmd));
    size_t n = 0; ssize_t r;
    while (out && n < outsz-1 && (r = read(fd, out+n, outsz-1-n)) > 0) n += r;
    if (out) out[n] = 0;
    close(fd);
    return (int)n;
}

static long now_ms(void){
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec*1000L + ts.tv_nsec/1000000L;
}

// --- tiny helpers to pull fields out of Hyprland's compact JSON ---
// Find the next occurrence of "key" starting at p, return pointer just after the colon.
static const char *field(const char *p, const char *key){
    char pat[64]; snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *q = strstr(p, pat);
    if (!q) return NULL;
    q = strchr(q + strlen(pat), ':');
    return q ? q+1 : NULL;
}
static int two_ints(const char *p, long *a, long *b){     // parse  [x, y]
    while (*p && *p!='[') p++;
    if (*p!='[') return 0;
    return sscanf(p, "[ %ld , %ld", a, b) == 2;
}

#define MAXW 256
struct win { char addr[24]; long x,y,w,h; long ws, mon, fhid; int floating; };

static int monitors_get(const char *want, long *mx,long *my,long *mw,long *mh,long *active_ws){
    static char buf[1<<16];
    if (hypr_req("j/monitors", buf, sizeof buf) <= 0) return 0;
    const char *p = buf; int found = 0;
    while ((p = strstr(p, "\"name\"")) != NULL){
        const char *namev = field(p, "name");      // points after colon -> "DP-1"
        char name[64]={0};
        if (namev) sscanf(namev, " \"%63[^\"]\"", name);
        const char *xv=field(p,"x"), *yv=field(p,"y"),
                   *wv=field(p,"width"), *hv=field(p,"height");
        const char *aw=field(p,"activeWorkspace");
        long x=0,y=0,w=0,h=0,ws=-1;
        if (xv) x=atol(xv); if (yv) y=atol(yv);
        if (wv) w=atol(wv); if (hv) h=atol(hv);
        if (aw){ const char *idv=field(aw,"id"); if (idv) ws=atol(idv); }
        int match = want ? (strcmp(name,want)==0) : (x==0 && y==0);
        if (match){ *mx=x;*my=y;*mw=w;*mh=h;*active_ws=ws; found=1; break; }
        p += 6;
    }
    return found;
}

static int clients_get(struct win *ws_out, int max){
    static char buf[1<<18];
    if (hypr_req("j/clients", buf, sizeof buf) <= 0) return 0;
    int n=0; const char *p=buf;
    while (n<max && (p = strstr(p,"\"address\"")) != NULL){
        struct win *w = &ws_out[n];
        const char *av=field(p,"address");
        if (av) sscanf(av, " \"%23[^\"]\"", w->addr);
        const char *atv=field(p,"at"), *szv=field(p,"size");
        long a,b; if (atv && two_ints(atv,&a,&b)){ w->x=a; w->y=b; }
        if (szv && two_ints(szv,&a,&b)){ w->w=a; w->h=b; }
        const char *wsv=field(p,"workspace");
        w->ws = -1;
        if (wsv){ const char *idv=field(wsv,"id"); if (idv) w->ws=atol(idv); }
        const char *mv=field(p,"monitor"); w->mon = mv?atol(mv):-1;
        const char *fv=field(p,"focusHistoryID"); w->fhid = fv?atol(fv):1<<30;
        const char *flv=field(p,"floating"); w->floating = flv && strncmp(flv," true",5)==0;
        // skip to end of this client object before next search
        p += 9;
        n++;
    }
    return n;
}

int main(int argc, char **argv){
    const char *want_mon = NULL;
    long dwell_ms = 250;
    int debug = 0;
    for (int i=1;i<argc;i++){
        if (!strcmp(argv[i],"--monitor") && i+1<argc) want_mon = argv[++i];
        else if (!strcmp(argv[i],"--dwell-ms") && i+1<argc) dwell_ms = atol(argv[++i]);
        else if (!strcmp(argv[i],"--debug")) debug = 1;
    }

    const char *xrd = getenv("XDG_RUNTIME_DIR");
    const char *his = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!xrd || !his){ fprintf(stderr,"need XDG_RUNTIME_DIR + HYPRLAND_INSTANCE_SIGNATURE\n"); return 1; }
    snprintf(sockpath, sizeof sockpath, "%s/hypr/%s/.socket.sock", xrd, his);

    long mx=0,my=0,mw=3440,mh=1440,mon_ws=-1;
    if (!monitors_get(want_mon,&mx,&my,&mw,&mh,&mon_ws)){
        fprintf(stderr,"could not read monitors\n"); return 1;
    }
    fprintf(stderr,"[*] tracker monitor rect (%ld,%ld) %ldx%ld active ws %ld\n",mx,my,mw,mh,mon_ws);

    struct win wins[MAXW]; int nwin=0;
    long last_refresh=0;
    char cur_target[24]={0};         // window the gaze currently rests on
    long target_since=0;
    char focused[24]={0};            // last window we asked to focus

    char line[128];
    while (fgets(line, sizeof line, stdin)){
        double nx, ny;
        if (sscanf(line, "%lf %lf", &nx, &ny) != 2) continue;   // "- -" => invalid

        long t = now_ms();
        if (t - last_refresh > 400){    // refresh window list a few times/sec
            nwin = clients_get(wins, MAXW);
            // re-read active workspace of the monitor (it can change)
            long a,b,c,d; monitors_get(want_mon,&a,&b,&c,&d,&mon_ws);
            last_refresh = t;
        }

        long gx = mx + (long)(nx * mw);
        long gy = my + (long)(ny * mh);

        // hit-test: visible windows on this monitor's active workspace; prefer
        // floating, then most-recently-focused (lowest focusHistoryID).
        const char *hit=NULL; long best_fhid=1L<<60; int best_float=-1;
        for (int i=0;i<nwin;i++){
            struct win *w=&wins[i];
            if (w->ws != mon_ws) continue;
            if (gx < w->x || gx >= w->x + w->w) continue;
            if (gy < w->y || gy >= w->y + w->h) continue;
            int better = (w->floating > best_float) ||
                         (w->floating == best_float && w->fhid < best_fhid);
            if (better){ best_float=w->floating; best_fhid=w->fhid; hit=w->addr; }
        }

        if (!hit || hit[0]==0){ cur_target[0]=0; continue; }

        if (strcmp(hit, cur_target) != 0){          // gaze moved to a new window
            strncpy(cur_target, hit, sizeof cur_target-1);
            target_since = t;
            continue;
        }
        // same window — has the gaze dwelled long enough, and is it not already focused?
        if (t - target_since >= dwell_ms && strcmp(cur_target, focused) != 0){
            char cmd[64], resp[64];
            snprintf(cmd, sizeof cmd, "dispatch focuswindow address:%s", cur_target);
            hypr_req(cmd, resp, sizeof resp);
            strncpy(focused, cur_target, sizeof focused-1);
            if (debug) fprintf(stderr,"[focus] %.3f,%.3f -> (%ld,%ld) %s\n",nx,ny,gx,gy,cur_target);
        }
    }
    return 0;
}
