ofgwrite
========

ofgwrite from Betacentauri: Based upon: mtd-utils-native-1.4.9

Usage: ofgwrite &lt;parameter&gt; &lt;image_directory&gt;  
Options:  
-k --kernel flash kernel (default)  
-r --rootfs flash root (default)  
-n --nowrite show only found image and mtd partitions (no write)  
-h --help show help  

Warning:  
Run the program once with -n parameter and check whether mtd partitions   
are recognized properly. If not, don't use this tool!!!  
On VU+ boxes there is a risk of bricking the box which is afaik not  
possible with Xtrend boxes.  
