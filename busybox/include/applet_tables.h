/* This is a generated file, don't edit */

#define NUM_APPLETS 4

const char applet_names[] ALIGN1 = ""
"fuser" "\0"
"ps" "\0"
"rm" "\0"
"tar" "\0"
;

#define APPLET_NO_fuser 0
#define APPLET_NO_ps 1
#define APPLET_NO_rm 2
#define APPLET_NO_tar 3

#ifndef SKIP_applet_main
int (*const applet_main[])(int argc, char **argv) = {
fuser_main,
ps_main,
rm_main,
tar_main,
};
#endif

const uint16_t applet_nameofs[] ALIGN2 = {
0x0000,
0x0006,
0x0009,
0x000c,
};

