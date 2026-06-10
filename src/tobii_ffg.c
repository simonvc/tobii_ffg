// tobii_ffg — Tobii Eye Tracker 5 "focus follows gaze" for Hyprland, in one tool.
//
// Reads gaze straight from the EyeChip over libusb (no Tobii libs, no daemon),
// maps it to the configured monitor, and focuses the window you look at after a
// short dwell — without moving the mouse.
//
// Config: ~/.config/tobii_ffg/config  (created with defaults on first run)
//   monitor=DP-1
//   dwell_ms=100
//   x_scale / x_offset / y_scale / y_offset   (gaze affine, per display/user)
//
// Flags override config: --monitor NAME  --dwell-ms N  --print  --debug
//   --print : also print "x y" gaze to stdout (don't focus). Good for calibration.
//
// Build: cc -O2 -Isrc -o tobii_ffg src/tobii_ffg.c -lusb-1.0

#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <time.h>
#include "init_seq.h"

#define VID 0x2104
#define PID 0x0313
#define IFACE 0
#define EP_OUT 0x05
#define EP_IN  0x83
#define PKT_LEN    1724
#define OFF_VALID  154
#define OFF_GAZE_X 1411
#define OFF_GAZE_Y 1424

// ---- config ----
struct cfg {
    char monitor[64];
    long dwell_ms;
    double xs, xo, ys, yo;
};
static struct cfg C = { "DP-1", 100, 0.000957714, 0.0144512, 0.000970392, 0.0058576 };

static void cfg_path(char *out, size_t n){
    const char *xc = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xc && *xc) snprintf(out, n, "%s/tobii_ffg/config", xc);
    else snprintf(out, n, "%s/.config/tobii_ffg/config", home ? home : ".");
}
static int cfg_save(void){
    char path[512]; cfg_path(path, sizeof path);
    char dir[512]; snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/'); if (slash){ *slash=0; mkdir(dir, 0755); }
    FILE *f = fopen(path, "w");
    if (!f){ fprintf(stderr, "[!] cannot write %s\n", path); return 0; }
    fprintf(f,
        "# tobii_ffg config\n"
        "monitor=%s\n"
        "dwell_ms=%ld\n"
        "# gaze affine: normalized = raw*scale + offset (per display/user)\n"
        "x_scale=%.9g\nx_offset=%.9g\ny_scale=%.9g\ny_offset=%.9g\n",
        C.monitor, C.dwell_ms, C.xs, C.xo, C.ys, C.yo);
    fclose(f);
    fprintf(stderr, "[*] saved config to %s\n", path);
    return 1;
}
static void cfg_load_or_create(void){
    char path[512]; cfg_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f){ cfg_save(); return; }
    char line[256];
    while (fgets(line, sizeof line, f)){
        if (line[0]=='#' || line[0]=='\n') continue;
        char key[64]; char val[128];
        if (sscanf(line, " %63[^= ] = %127s", key, val) != 2) continue;
        if      (!strcmp(key,"monitor"))  strncpy(C.monitor, val, sizeof C.monitor-1);
        else if (!strcmp(key,"dwell_ms")) C.dwell_ms = atol(val);
        else if (!strcmp(key,"x_scale"))  C.xs = atof(val);
        else if (!strcmp(key,"x_offset")) C.xo = atof(val);
        else if (!strcmp(key,"y_scale"))  C.ys = atof(val);
        else if (!strcmp(key,"y_offset")) C.yo = atof(val);
    }
    fclose(f);
}

// ---- Hyprland IPC ----
static char sockpath[512];
static int hypr_req(const char *cmd, char *out, size_t outsz){
    int fd = socket(AF_UNIX, SOCK_STREAM, 0); if (fd<0) return -1;
    struct sockaddr_un a; memset(&a,0,sizeof a); a.sun_family=AF_UNIX;
    strncpy(a.sun_path, sockpath, sizeof(a.sun_path)-1);
    if (connect(fd,(struct sockaddr*)&a,sizeof a)<0){ close(fd); return -1; }
    write(fd, cmd, strlen(cmd));
    size_t n=0; ssize_t r;
    while (out && n<outsz-1 && (r=read(fd,out+n,outsz-1-n))>0) n+=r;
    if (out) out[n]=0;
    close(fd); return (int)n;
}
static const char *field(const char *p, const char *key){
    char pat[64]; snprintf(pat,sizeof pat,"\"%s\"",key);
    const char *q=strstr(p,pat); if(!q) return NULL;
    q=strchr(q+strlen(pat),':'); return q?q+1:NULL;
}
static int two_ints(const char *p, long *a, long *b){
    while(*p && *p!='[') p++;
    if(*p!='[') return 0;
    return sscanf(p,"[ %ld , %ld",a,b)==2;
}
#define MAXW 256
struct win { char addr[24]; long x,y,w,h; long ws,fhid; int floating; };

static int monitors_get(const char *want, long *mx,long *my,long *mw,long *mh,long *aws){
    static char buf[1<<16];
    if (hypr_req("j/monitors",buf,sizeof buf)<=0) return 0;
    const char *p=buf;
    while ((p=strstr(p,"\"name\""))!=NULL){
        const char *nv=field(p,"name"); char name[64]={0};
        if(nv) sscanf(nv," \"%63[^\"]\"",name);
        const char *xv=field(p,"x"),*yv=field(p,"y"),*wv=field(p,"width"),*hv=field(p,"height"),*av=field(p,"activeWorkspace");
        long x=xv?atol(xv):0,y=yv?atol(yv):0,w=wv?atol(wv):0,h=hv?atol(hv):0,ws=-1;
        if(av){const char*iv=field(av,"id"); if(iv) ws=atol(iv);}
        int match = want&&*want ? !strcmp(name,want) : (x==0&&y==0);
        if(match){*mx=x;*my=y;*mw=w;*mh=h;*aws=ws;return 1;}
        p+=6;
    }
    return 0;
}
static int clients_get(struct win *o,int max){
    static char buf[1<<18];
    if (hypr_req("j/clients",buf,sizeof buf)<=0) return 0;
    int n=0; const char *p=buf;
    while(n<max && (p=strstr(p,"\"address\""))!=NULL){
        struct win *w=&o[n]; const char *av=field(p,"address");
        if(av) sscanf(av," \"%23[^\"]\"",w->addr);
        const char *atv=field(p,"at"),*szv=field(p,"size"); long a,b;
        if(atv&&two_ints(atv,&a,&b)){w->x=a;w->y=b;}
        if(szv&&two_ints(szv,&a,&b)){w->w=a;w->h=b;}
        const char *wsv=field(p,"workspace"); w->ws=-1;
        if(wsv){const char*iv=field(wsv,"id"); if(iv) w->ws=atol(iv);}
        const char *fv=field(p,"focusHistoryID"); w->fhid=fv?atol(fv):1<<30;
        const char *flv=field(p,"floating"); w->floating=flv&&!strncmp(flv," true",5);
        p+=9; n++;
    }
    return n;
}

static long now_ms(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1000L+t.tv_nsec/1000000L; }
static int16_t be16(const unsigned char*p,int o){ return (int16_t)((p[o]<<8)|p[o+1]); }
static volatile int running=1; static void on_sigint(int s){(void)s; running=0;}

// vendor enable + replay init/config sequence so the device streams valid gaze.
static void gaze_init(libusb_device_handle *h){
    unsigned char buf[8192]; int tr;
    libusb_control_transfer(h,0x41,0x41,0,0,NULL,0,1000);   // vendor enable
    fprintf(stderr,"[*] sending %d init commands...\n",init_cmds_count);
    for(int i=0;i<init_cmds_count;i++){
        libusb_bulk_transfer(h,EP_OUT,(unsigned char*)init_cmds[i].data,(int)init_cmds[i].len,&tr,1000);
        for(int k=0;k<4;k++){ if(libusb_bulk_transfer(h,EP_IN,buf,sizeof buf,&tr,30)!=0||tr==0) break; }
    }
}

// ---- calibration ----
static int cursorpos_get(long *x,long *y){
    char buf[256]; if(hypr_req("j/cursorpos",buf,sizeof buf)<=0) return 0;
    const char *xv=field(buf,"x"),*yv=field(buf,"y");
    if(!xv||!yv) return 0;
    *x=atol(xv); *y=atol(yv); return 1;
}
static int cmp_int(const void*a,const void*b){ int x=*(const int*)a,y=*(const int*)b; return (x>y)-(x<y); }
static double median_int(int *v,int n){ qsort(v,n,sizeof(int),cmp_int); return v[n/2]; }
// least squares: y = a*x + b
static void fit_line(const double *X,const double *Y,int n,double *a,double *b){
    double sx=0,sy=0,sxx=0,sxy=0;
    for(int i=0;i<n;i++){ sx+=X[i]; sy+=Y[i]; sxx+=X[i]*X[i]; sxy+=X[i]*Y[i]; }
    double d=n*sxx-sx*sx;
    if(d==0){ *a=0; *b=sy/n; return; }
    *a=(n*sxy-sx*sy)/d; *b=(sy-*a*sx)/n;
}
static int run_calibration(libusb_device_handle *h){
    long mx,my,mw,mh,ws;
    if(!monitors_get(C.monitor,&mx,&my,&mw,&mh,&ws)){ fprintf(stderr,"monitor '%s' not found\n",C.monitor); return 1; }
    fprintf(stderr,"\n=== tobii_ffg calibration — monitor %s (%ldx%ld) ===\n",C.monitor,mw,mh);
    fprintf(stderr,
        "For each point: move the MOUSE somewhere on this monitor, LOOK AT THE CURSOR,\n"
        "then press Enter. Spread points out (4 corners + centre is ideal).\n"
        "Type q + Enter when done. Need at least 2 points; 5+ recommended.\n\n");
    gaze_init(h);
    double RX[64],NX[64],RY[64],NY[64]; int np=0;
    // Rolling buffer of the most recent valid gaze frames. We read the stream
    // continuously (so the illuminators stay on) and, when you press Enter,
    // use the median of the last ~RING frames — i.e. while you were on the cursor.
    #define RING 24
    int rx[RING], ry[RING], rn=0, rpos=0;
    unsigned char buf[8192]; int tr;
    char line[64];
    fprintf(stderr,"point %d: look at the cursor, press Enter (q=finish) ",np+1); fflush(stderr);
    struct pollfd pfd = { .fd = 0, .events = POLLIN };
    while(np<64){
        // keep the gaze stream flowing (lights on) + fill the rolling buffer
        if(libusb_bulk_transfer(h,EP_IN,buf,sizeof buf,&tr,40)==0 && tr>=PKT_LEN && buf[OFF_VALID]){
            rx[rpos]=be16(buf,OFF_GAZE_X); ry[rpos]=be16(buf,OFF_GAZE_Y);
            rpos=(rpos+1)%RING; if(rn<RING) rn++;
        }
        if(poll(&pfd,1,0)<=0 || !(pfd.revents&POLLIN)) continue;   // Enter pressed?
        if(!fgets(line,sizeof line,stdin)) break;
        if(line[0]=='q'||line[0]=='Q') break;
        long cx,cy;
        if(!cursorpos_get(&cx,&cy)){ fprintf(stderr,"  ! couldn't read cursor position\n"); goto next; }
        double tnx=(double)(cx-mx)/(double)mw, tny=(double)(cy-my)/(double)mh;
        if(tnx<-0.05||tnx>1.05||tny<-0.05||tny>1.05){
            fprintf(stderr,"  ! cursor not on %s (target %.2f,%.2f) — skipped\n",C.monitor,tnx,tny); goto next; }
        if(rn<5){ fprintf(stderr,"  ! no valid gaze (look at the cursor) — skipped\n"); goto next; }
        int tx[RING],ty[RING]; for(int i=0;i<rn;i++){ tx[i]=rx[i]; ty[i]=ry[i]; }
        double gmx=median_int(tx,rn), gmy=median_int(ty,rn);
        RX[np]=gmx; NX[np]=tnx; RY[np]=gmy; NY[np]=tny; np++;
        fprintf(stderr,"  ok: screen (%.2f,%.2f)  raw gaze (%.0f,%.0f)  [%d captured]\n",tnx,tny,gmx,gmy,np);
    next:
        rn=0; rpos=0;   // reset buffer for the next point
        if(np<64){ fprintf(stderr,"point %d: look at the cursor, press Enter (q=finish) ",np+1); fflush(stderr); }
    }
    if(np<2){ fprintf(stderr,"\nneed >= 2 points; config unchanged.\n"); return 1; }
    double ax,bx,ay,by;
    fit_line(RX,NX,np,&ax,&bx); fit_line(RY,NY,np,&ay,&by);
    C.xs=ax; C.xo=bx; C.ys=ay; C.yo=by;
    fprintf(stderr,"\nnew calibration (%d points):\n  x = %.8g*raw + %.5g\n  y = %.8g*raw + %.5g\n",np,ax,bx,ay,by);
    cfg_save();
    return 0;
}

int main(int argc,char**argv){
    int do_print=0, debug=0, calibrate=0;
    const char *ov_mon=NULL; long ov_dwell=-1;
    cfg_load_or_create();
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--monitor")&&i+1<argc) ov_mon=argv[++i];
        else if(!strcmp(argv[i],"--dwell-ms")&&i+1<argc) ov_dwell=atol(argv[++i]);
        else if(!strcmp(argv[i],"--print")) do_print=1;
        else if(!strcmp(argv[i],"--calibrate")) calibrate=1;
        else if(!strcmp(argv[i],"--debug")) debug=1;
        else if(!strcmp(argv[i],"--help")||!strcmp(argv[i],"-h")){
            printf("usage: tobii_ffg [--calibrate] [--print] [--monitor NAME] [--dwell-ms N] [--debug]\n"
                   "  (no flags)   focus follows gaze in Hyprland\n"
                   "  --calibrate  interactively recalibrate gaze->screen and save to config\n"
                   "  --print      print normalized gaze 'x y' instead of focusing\n"
                   "  --monitor N  monitor the tracker sits under (default from config)\n"
                   "  --dwell-ms N gaze dwell before focusing (default from config)\n");
            return 0;
        }
    }
    if(ov_mon) strncpy(C.monitor,ov_mon,sizeof C.monitor-1);
    if(ov_dwell>=0) C.dwell_ms=ov_dwell;

    const char *xrd=getenv("XDG_RUNTIME_DIR"),*his=getenv("HYPRLAND_INSTANCE_SIGNATURE");
    int need_hypr = !do_print;   // focusing and calibration both need the Hyprland socket
    if(need_hypr && (!xrd||!his)){ fprintf(stderr,"need XDG_RUNTIME_DIR + HYPRLAND_INSTANCE_SIGNATURE (or use --print)\n"); return 1; }
    if(xrd&&his) snprintf(sockpath,sizeof sockpath,"%s/hypr/%s/.socket.sock",xrd,his);

    long mx=0,my=0,mw=3440,mh=1440,mon_ws=-1;
    if(need_hypr && !calibrate){
        if(!monitors_get(C.monitor,&mx,&my,&mw,&mh,&mon_ws)){ fprintf(stderr,"monitor '%s' not found\n",C.monitor); return 1; }
        fprintf(stderr,"[*] monitor %s (%ld,%ld) %ldx%ld; dwell %ldms\n",C.monitor,mx,my,mw,mh,C.dwell_ms);
    }

    signal(SIGINT,on_sigint);
    libusb_context *ctx=NULL;
    if(libusb_init(&ctx)<0){ fprintf(stderr,"libusb_init failed\n"); return 1; }
    libusb_device_handle *h=libusb_open_device_with_vid_pid(ctx,VID,PID);
    if(!h){ fprintf(stderr,"Tobii 5 (2104:0313) not found / no permission\n"); return 1; }
    libusb_set_auto_detach_kernel_driver(h,1);
    int r=libusb_claim_interface(h,IFACE);
    if(r<0){ fprintf(stderr,"claim_interface: %s\n",libusb_error_name(r)); return 1; }

    if(calibrate){
        int rc=run_calibration(h);
        libusb_release_interface(h,IFACE); libusb_close(h); libusb_exit(ctx);
        return rc;
    }

    unsigned char buf[8192]; int tr;
    gaze_init(h);
    fprintf(stderr,"[*] %s (Ctrl-C to stop)\n", do_print?"printing gaze":"focus follows gaze");

    struct win wins[MAXW]; int nwin=0; long last_refresh=0;
    char cur[24]={0}, focused[24]={0}; long since=0;

    while(running){
        r=libusb_bulk_transfer(h,EP_IN,buf,sizeof buf,&tr,1000);
        if(r==LIBUSB_ERROR_TIMEOUT) continue;
        if(r<0){ fprintf(stderr,"IN: %s\n",libusb_error_name(r)); break; }
        if(tr<PKT_LEN) continue;
        int valid=buf[OFF_VALID]!=0;
        if(!valid){ if(do_print){printf("- -\n");fflush(stdout);} cur[0]=0; continue; }
        double nx=be16(buf,OFF_GAZE_X)*C.xs+C.xo;
        double ny=be16(buf,OFF_GAZE_Y)*C.ys+C.yo;
        if(do_print){ printf("%.4f %.4f\n",nx,ny); fflush(stdout); continue; }

        long t=now_ms();
        if(t-last_refresh>400){ nwin=clients_get(wins,MAXW); long a,b,c,d; monitors_get(C.monitor,&a,&b,&c,&d,&mon_ws); last_refresh=t; }
        long gx=mx+(long)(nx*mw), gy=my+(long)(ny*mh);
        const char *hit=NULL; long bf=1L<<60; int bfl=-1;
        for(int i=0;i<nwin;i++){
            struct win*w=&wins[i];
            if(w->ws!=mon_ws) continue;
            if(gx<w->x||gx>=w->x+w->w||gy<w->y||gy>=w->y+w->h) continue;
            if(w->floating>bfl||(w->floating==bfl&&w->fhid<bf)){ bfl=w->floating; bf=w->fhid; hit=w->addr; }
        }
        if(!hit||!hit[0]){ cur[0]=0; continue; }
        if(strcmp(hit,cur)){ strncpy(cur,hit,sizeof cur-1); since=t; continue; }
        if(t-since>=C.dwell_ms && strcmp(cur,focused)){
            char cmd[64],resp[64]; snprintf(cmd,sizeof cmd,"dispatch focuswindow address:%s",cur);
            hypr_req(cmd,resp,sizeof resp); strncpy(focused,cur,sizeof focused-1);
            if(debug) fprintf(stderr,"[focus] %.3f,%.3f -> (%ld,%ld) %s\n",nx,ny,gx,gy,cur);
        }
    }
    libusb_release_interface(h,IFACE); libusb_close(h); libusb_exit(ctx);
    return 0;
}
