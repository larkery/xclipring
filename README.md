# A simpler X clipboard manager

`xclipring` is a program which keeps a ringbuffer of clipboard contents somewhere in the filesystem.

To use, checkout, run `make` (depends on `libxcb-dev` and `libxcb-xfixes-0-dev`), and then

```
xclipring [-s CLIPBOARD|PRIMARY|SECONDARY|WHATEVER] [-d ~/.cache/xclipring/CLIPBOARD] [-c 100]
```

The default behaviour is to store the 100 most recent contents for the CLIPBOARD selection in ~/.cache/xclipring/CLIPBOARD.

These are stored in files named `0`, `1`, `2` and so on to `99`. The front of the ringbuffer is whichever was most recently modified, and xclipring will start at that position + 1 if you stop it and restart it.

At the moment, this only records clipped items that can be requested as UTF8 strings (i.e. text selections).

Included is a script `yank` which will use `rofi` (like `dmenu`) to select an item from the ringbuffer and copy it to the front.

If you want a temporary clipboard ring, use -d to point to a ramdisk.
