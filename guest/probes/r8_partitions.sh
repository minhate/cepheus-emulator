#!/system/bin/linker64 /system/bin/sh
/system/bin/linker64 /system/bin/toybox mount -t proc proc /proc
/system/bin/sleep 3
while IFS= read -r line; do
    print -r -- "$line" > /dev/kmsg
done < /proc/partitions
/system/bin/sleep 5
