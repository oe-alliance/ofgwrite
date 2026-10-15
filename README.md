ofgwrite
========

ofgwrite from Betacentauri: Based upon: mtd-utils-native-1.5.1 and busybox 1.24.1

Usage: ofgwrite &lt;parameter&gt; &lt;image_directory&gt;  
Options:  
-k --kernel flash kernel (default)  
-r --rootfs flash root (default)  
-n --nowrite show only found image and mtd partitions (no write)  
--lang=xx language of the window, else the one of Enigma2 (de, pl, ar, English without them)  
--dim darken the screen around the window while Enigma2 or another program runs  
-h --help show help  

The window is drawn with LVGL, the submodule lib/lvgl, configured by include/lv_conf.h. Its fonts are
fonts/*.c from tools/mkfonts.sh, its texts po/*.po, which tools/mkstrings.py turns into i18n_strings.c,
so ofgwrite needs no files after pivot_root. `make fbdemo` builds ofgwrite_fbdemo, which shows the
window without flashing: `ofgwrite_fbdemo [--lang=xx] [--dim] progress|check|error|success [seconds]`.

Warning:  
Run the program once with -n parameter and check whether mtd partitions   
are recognized properly. If not, don't use this tool!!!
