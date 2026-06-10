#include <tobii/tobii.h>
#include <tobii/tobii_streams.h>
#include <stdio.h>
#include <string.h>

void gaze_cb(tobii_gaze_point_t const *gp, void *) {
    if (gp->validity == TOBII_VALIDITY_VALID)
        printf("gaze %.4f %.4f ts=%lld\n", gp->position_xy[0], gp->position_xy[1], (long long)gp->timestamp_us);
    else
        printf("gaze INVALID ts=%lld\n", (long long)gp->timestamp_us);
}

static void url_receiver(char const *url, void *user_data) {
    printf("found url: %s\n", url);
    char *buffer = (char *)user_data;
    if (*buffer != '\0') return;
    if (strlen(url) < 256) strcpy(buffer, url);
}

int main(int argc, char **argv) {
    tobii_api_t *api;
    tobii_error_t err = tobii_api_create(&api, NULL, NULL);
    printf("api_create: %s\n", tobii_error_message(err));

    tobii_version_t v;
    tobii_get_api_version(&v);
    printf("api version %d.%d.%d.%d\n", v.major, v.minor, v.revision, v.build);

    char url[256] = {0};
    err = tobii_enumerate_local_device_urls(api, url_receiver, url);
    printf("enumerate: %s, url='%s'\n", tobii_error_message(err), url);
    if (!*url) return 1;

    if (argc > 1) strcpy(url, argv[1]);

    tobii_device_t *device;
    err = tobii_device_create(api, url, &device);
    printf("device_create('%s'): %s\n", url, tobii_error_message(err));
    if (err != TOBII_ERROR_NO_ERROR) return 1;

    tobii_device_info_t info;
    if (tobii_get_device_info(device, &info) == TOBII_ERROR_NO_ERROR)
        printf("device: serial=%s model=%s gen=%s fw=%s\n", info.serial_number, info.model, info.generation, info.firmware_version);

    err = tobii_gaze_point_subscribe(device, gaze_cb, 0);
    printf("subscribe: %s\n", tobii_error_message(err));
    if (err != TOBII_ERROR_NO_ERROR) return 1;

    for (int i = 0; i < 600; i++) {
        err = tobii_wait_for_callbacks(1, &device);
        if (err != TOBII_ERROR_NO_ERROR && err != TOBII_ERROR_TIMED_OUT)
            { printf("wait: %s\n", tobii_error_message(err)); break; }
        err = tobii_device_process_callbacks(device);
        if (err != TOBII_ERROR_NO_ERROR)
            { printf("process: %s\n", tobii_error_message(err)); break; }
    }
    tobii_device_destroy(device);
    tobii_api_destroy(api);
    return 0;
}
