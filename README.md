jakobmenu
=========

pipemenu for openbox.

Building
--------

You need perl and a C99 compiler.

```sh
./configure.pl
make
make install
```

Configuration
-------------

`jakobmenu` needs some paths that contain `.dekstop` files.

There is an example `/etc/jakobmenu.conf` file installed in `/usr/local/share/jakobmenu/jakobmenu.conf` if you want it.

You can pass paths with the `-p` argument.

In your OpenBox `menu.xml`, you can use this as a pipe menu:

```xml
<?xml version="1.0" encoding="utf-8"?>
<openbox_menu xmlns="http://openbox.org/" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:schemaLocation="http://openbox.org/ file:///usr/share/openbox/menu.xsd">
 ...menu stuff here...
 <menu
     id="ID"
     label="TITLE"
     execute="jakobmenu"
 />
</openbox_menu>
```

You can then include this menu by ID in your other menu structure(s).

If you think `jakobmenu` is too slow, then you can run it once (in a while):

```
jakobmenu > ~/.config/jakobmenu/cache.xml
```

Then tell OpenBox to execute `cat ~/.config/jakobmenu/cache.xml`.
