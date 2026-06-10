// tobii_gaze — self-contained Tobii Eye Tracker 5 (IS5, 2104:0313) gaze reader.
// Talks to the EyeChip directly over libusb: replays the reverse-engineered init
// sequence, then streams 1724-byte gaze packets and prints normalized gaze x,y.
//
// No Tobii libraries, no daemon. Just libusb-1.0.
// Protocol reversed from Talon captures — see ../PROTOCOL.md.
//
// Build: cc -O2 -o tobii_gaze tobii_gaze.c -lusb-1.0

#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include "init_seq.h"

#define VID 0x2104
#define PID 0x0313
#define IFACE 0
#define EP_OUT 0x05
#define EP_IN  0x83

// Gaze packet layout (reverse-engineered, big-endian int16 fields):
#define PKT_LEN     1724
#define OFF_VALID   154   // byte: nonzero => gaze valid
#define OFF_GAZE_X  1411  // int16 BE, ~ normalized*1000 (left->right)
#define OFF_GAZE_Y  1424  // int16 BE, ~ normalized*1000 (top->bottom)

static volatile int running = 1;
static void on_sigint(int s){ (void)s; running = 0; }

static int16_t be16(const unsigned char *p, int off){
    return (int16_t)((p[off] << 8) | p[off+1]);
}

int main(int argc, char **argv){
    int raw_mode = (argc > 1 && strcmp(argv[1], "--raw") == 0);
    signal(SIGINT, on_sigint);

    libusb_context *ctx = NULL;
    if (libusb_init(&ctx) < 0){ fprintf(stderr, "libusb_init failed\n"); return 1; }

    libusb_device_handle *h = libusb_open_device_with_vid_pid(ctx, VID, PID);
    if (!h){ fprintf(stderr, "Tobii 5 (2104:0313) not found / no permission\n"); return 1; }

    libusb_set_auto_detach_kernel_driver(h, 1);

    // Device is already in config 1 from kernel enumeration; calling
    // SET_CONFIGURATION again resets/re-enumerates it, so we skip it and
    // just send the vendor "enable" control transfer before the bulk init.
    int r = libusb_claim_interface(h, IFACE);
    if (r < 0){ fprintf(stderr, "claim_interface %d: %s\n", IFACE, libusb_error_name(r)); return 1; }

    unsigned char buf[8192];
    int transferred;

    // Vendor enable: bmRequestType=0x41 (host->dev, vendor, interface), bRequest=0x41.
    r = libusb_control_transfer(h, 0x41, 0x41, 0x0000, 0x0000, NULL, 0, 1000);
    if (r < 0) fprintf(stderr, "[!] vendor enable control: %s\n", libusb_error_name(r));

    // 1) Replay the init/config sequence; drain responses between commands.
    fprintf(stderr, "[*] sending %d init commands...\n", init_cmds_count);
    for (int i = 0; i < init_cmds_count; i++){
        r = libusb_bulk_transfer(h, EP_OUT, (unsigned char*)init_cmds[i].data,
                                 (int)init_cmds[i].len, &transferred, 1000);
        if (r < 0){ fprintf(stderr, "init cmd %d OUT: %s\n", i, libusb_error_name(r)); }
        // drain any immediate responses (don't block long)
        for (int k = 0; k < 4; k++){
            r = libusb_bulk_transfer(h, EP_IN, buf, sizeof(buf), &transferred, 30);
            if (r != 0 || transferred == 0) break;
        }
    }
    fprintf(stderr, "[*] streaming (Ctrl-C to stop)...\n");

    // 2) Stream and decode gaze packets.
    while (running){
        r = libusb_bulk_transfer(h, EP_IN, buf, sizeof(buf), &transferred, 1000);
        if (r == LIBUSB_ERROR_TIMEOUT) continue;
        if (r < 0){ fprintf(stderr, "IN: %s\n", libusb_error_name(r)); break; }
        if (transferred < PKT_LEN) continue;       // skip control/response packets

        int valid = buf[OFF_VALID] != 0;
        int16_t rx = be16(buf, OFF_GAZE_X);
        int16_t ry = be16(buf, OFF_GAZE_Y);
        if (raw_mode){
            printf("%d %d %d\n", valid, rx, ry);
        } else if (valid){
            // Normalized [0,1] screen position. Affine fit derived by regressing
            // the raw int16 fields against Talon's gaze on a 797x334mm display;
            // recalibrate per display/user if needed (raw ~ normalized*1000).
            double nx = rx * 0.000957714 + 0.0144512;
            double ny = ry * 0.000970392 + 0.0058576;
            printf("%.4f %.4f\n", nx, ny);
        } else {
            printf("- -\n");
        }
        fflush(stdout);
    }

    libusb_release_interface(h, IFACE);
    libusb_close(h);
    libusb_exit(ctx);
    return 0;
}
