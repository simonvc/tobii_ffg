#include <tobii/tobii.h>
#include <tobii/tobii_streams.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

static int n_gaze_valid=0, n_gaze_inval=0, n_origin=0, n_origin_valid=0, n_presence=0, n_notif=0;
static int last_presence=-99;

void gaze_cb(tobii_gaze_point_t const *gp, void *) {
    if (gp->validity==TOBII_VALIDITY_VALID){ n_gaze_valid++;
        if(n_gaze_valid<=8) printf("[gaze VALID] %.4f %.4f\n", gp->position_xy[0], gp->position_xy[1]); }
    else n_gaze_inval++;
}
void origin_cb(tobii_gaze_origin_t const *o, void *) {
    n_origin++;
    if(o->left_validity==TOBII_VALIDITY_VALID||o->right_validity==TOBII_VALIDITY_VALID){ n_origin_valid++;
        if(n_origin_valid<=8) printf("[orig] L%s(%.0f,%.0f,%.0f) R%s(%.0f,%.0f,%.0f)\n",
            o->left_validity?"+":"-",o->left_xyz[0],o->left_xyz[1],o->left_xyz[2],
            o->right_validity?"+":"-",o->right_xyz[0],o->right_xyz[1],o->right_xyz[2]); }
}
void presence_cb(tobii_user_presence_status_t status, int64_t, void *) {
    n_presence++; if((int)status!=last_presence){ last_presence=(int)status; printf("[presence] -> %d\n",(int)status);} }
void notif_cb(tobii_notification_t const *n, void *) { n_notif++; printf("[notif] type=%d\n",(int)n->type); }
static void url_recv(char const *url, void *u){ char*b=(char*)u; if(!*b&&strlen(url)<256&&strncmp(url,"tobii-",6)==0) strcpy(b,url); }

int main() {
    tobii_api_t *api; tobii_api_create(&api,NULL,NULL);
    char url[256]={0}; tobii_enumerate_local_device_urls(api,url_recv,url);
    tobii_device_t *dev; tobii_error_t e=tobii_device_create(api,url,&dev);
    printf("create(%s): %s\n",url,tobii_error_message(e)); if(e) return 1;
    printf("presence sub: %s\n", tobii_error_message(tobii_user_presence_subscribe(dev,presence_cb,0)));
    printf("origin   sub: %s\n", tobii_error_message(tobii_gaze_origin_subscribe(dev,origin_cb,0)));
    printf("gaze     sub: %s\n", tobii_error_message(tobii_gaze_point_subscribe(dev,gaze_cb,0)));
    printf("notif    sub: %s\n", tobii_error_message(tobii_notifications_subscribe(dev,notif_cb,0)));
    int secs = (getenv("RUN_SECS")) ? atoi(getenv("RUN_SECS")) : 15;
    time_t end = time(NULL) + secs; time_t tick = time(NULL);
    int pg=0,pgv=0,po=0,pov=0;
    while (time(NULL) < end){
        tobii_wait_for_callbacks(1,&dev); tobii_device_process_callbacks(dev);
        if(time(NULL)!=tick){ tick=time(NULL);
            printf("  ...%lds left: gaze +%d/%d  origin +%d/%d  presence=%d\n",
                (long)(end-tick), n_gaze_valid-pgv, n_gaze_valid+n_gaze_inval-pg,
                n_origin_valid-pov, n_origin-po, last_presence);
            pg=n_gaze_valid+n_gaze_inval; pgv=n_gaze_valid; po=n_origin; pov=n_origin_valid; }
    }
    printf("\nCOUNTS: gaze_valid=%d gaze_inval=%d origin=%d origin_valid=%d presence_cbs=%d notif=%d\n",
        n_gaze_valid,n_gaze_inval,n_origin,n_origin_valid,n_presence,n_notif);
    tobii_notifications_unsubscribe(dev);
    tobii_gaze_point_unsubscribe(dev);
    tobii_gaze_origin_unsubscribe(dev);
    tobii_user_presence_unsubscribe(dev);
    tobii_device_process_callbacks(dev);
    tobii_device_destroy(dev); tobii_api_destroy(api); return 0;
}
