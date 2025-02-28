# ImDisk Virtual Disk Driver 3.0 for Windows 7/10/11

This project is a fork of the last [ImDisk](https://github.com/LTRData/ImDisk) release by Olof Lagerkvist, 
adding functionality required for compatibility with modern Windows versions, 
including Windows 11 24H2. The goal is to maintain the lightweight nature of ImDisk 
while ensuring full compatibility with the latest Windows releases. 
Please note that 32-bit Windows (x86 and arm) is no longer supported, 
and this project focuses exclusively on 64-bit systems (x64 and ARM64).

ImDisk Virtual Disk driver emulates harddisk partitions and CD/DVD-ROM drives
from disk image files, in virtual memory or by redirecting I/O requests
somewhere else, possibly to another machine, through a co-operating user-mode
service, ImDskSvc.

To install this driver, service and command line tool, right-click on the
imdisk.inf file and select 'Install'. To uninstall, use the Add/Remove
Programs applet in the Control Panel.

You can get syntax help to the command line tool by typing just imdisk
without parameters.

To install/uninstall on ARM64 architectures a manual setup is needed.
More about that and other frequently asked questions in the wiki:
https://github.com/LTRData/ImDisk/wiki


