/* This is a generated file, don't edit */

#define NUM_APPLETS 3

const char applet_names[] ALIGN1 = ""
"fuser" "\0"
"ps" "\0"
"tar" "\0"
;

#define APPLET_NO_fuser 0
#define APPLET_NO_ps 1
#define APPLET_NO_tar 2

#ifndef SKIP_applet_main
int (*const applet_main[])(int argc, char **argv) = {
fuser_main,
ps_main,
tar_main,
};
#endif

const uint16_t applet_nameofs[] ALIGN2 = {
0x0000,
0x0006,
0x0009,
};

