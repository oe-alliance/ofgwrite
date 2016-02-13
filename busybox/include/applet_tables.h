/* This is a generated file, don't edit */

#define NUM_APPLETS 5

const char applet_names[] ALIGN1 = ""
"fdisk" "\0"
"fuser" "\0"
"ps" "\0"
"rm" "\0"
"tar" "\0"
;

#define APPLET_NO_fdisk 0
#define APPLET_NO_fuser 1
#define APPLET_NO_ps 2
#define APPLET_NO_rm 3
#define APPLET_NO_tar 4

#ifndef SKIP_applet_main
int (*const applet_main[])(int argc, char **argv) = {
fdisk_main,
fuser_main,
ps_main,
rm_main,
tar_main,
};
#endif

const uint16_t applet_nameofs[] ALIGN2 = {
0x0000,
0x0006,
0x000c,
0x000f,
0x0012,
};

