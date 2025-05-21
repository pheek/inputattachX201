# inputattachX201
inputattach for lenovo X201 Tablet on gentoo linux

Because the default lxde on gentoo did not recognize the X201 Tablet digitizer, I wrote this inputattachX201.
when you start it, it reads input from /dev/ttyS4 and transforms the bytes from the digitzer to X11 events.

It has to be started after X11 window manager is loadet.
It has to be started as root.

The input device /dev/ttyS4 has to be attached to the digitizer input (modprobe wacom_w8001 should do this).
You can test your device using
cat /dev/ttyS4
If this brings correlated output bytes (unreadable) while moving the digitizer, /dev/ttyS4 is corretly loaded.
