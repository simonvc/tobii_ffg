#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/usbdevice_fs.h>
int main(int c, char **v){ if(c<2){return 2;} int f=open(v[1],O_WRONLY); int r=ioctl(f,USBDEVFS_RESET,0); printf("reset %s = %d\n",v[1],r); return r; }
