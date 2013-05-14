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

Copyright Mateusz Machalica 2013.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
