# ImDisk Virtual Disk Driver 3.0 for Windows 7/10/11

This project is a fork from the last [ImDisk](https://github.com/LTRData/ImDisk) by Olof Lagerkvist,
It ads functionality required on more modern windows versions Windows 11.
The goal of this project is to maintain the lightweight functionality while

ImDisk Virtual Disk driver emulates harddisk partitions and CD/DVD-ROM drives
from disk image files, in virtual memory or by redirecting I/O requests
somewhere else, possibly to another machine, through a co-operating user-mode
service, ImDskSvc.

To install this driver, service and command line tool, right-click on the
imdisk.inf file and select 'Install'. To uninstall, use the Add/Remove
Programs applet in the Control Panel.

You can get syntax help to the command line tool by typing just imdisk
without parameters.

To install/uninstall on ARM or ARM64 architectures a manual setup is needed.
More about that and other frequently asked questions in the wiki:
https://github.com/LTRData/ImDisk/wiki


