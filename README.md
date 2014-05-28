CRC32 device driver for Linux 2.6.34.8
======================================

Everything is pretty well explained in sources, sorry for the mess with
separated synchronization -- I've tried to make things more structured but
failed.

Building
--------
Use modified qemu-crcdev [https://aur.archlinux.org/packages/qemu-crcdev/]
configured with at least one crcdev, Linux 2.6.34.8 kernel on board and sparse
checker.

    make
    make test

Copyright and License
---------------------

Copyright (c) 2013-2014 Mateusz Machalica
