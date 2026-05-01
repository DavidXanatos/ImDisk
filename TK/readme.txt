These sources are compiled with MinGW 14.2.1, available here: https://gcc-mcf.lhmouse.com/
64-bit:
https://gcc-mcf.lhmouse.com/mingw-w64-gcc-mcf_20250120_14.2.1_x64-msvcrt_faed7859111100585739fa8e33d5ff9d658f14f0.7z
32-bit:
https://gcc-mcf.lhmouse.com/mingw-w64-gcc-mcf_20250120_14.2.1_x86-msvcrt_965e3868dc088ad49c562b602bd744acaecbede7.7z

Source files without copyright notice can be used without any restriction and
are provided without any warranty.

The sources of the Imdisk Virtual Disk Driver and the DiscUtils library are
available separately on the website of their respective authors.


Building a release requires the following steps:
- MinGW must be extracted in the root of the same drive of the sources. Otherwise, batch files must be adapted.
- comp_all.bat can be used to compile both the 32 and 64-bit executables.
- The driver (https://ltr-data.se/opencode.html/#ImDisk) must be extracted in the "files\driver" folder.
- The DiscUtils library and related tools (https://ltr-data.se/opencode.html/#ImDisk) must be extracted in the "MountImg\DiscUtils" folder.
- Finally, make_releases.bat can be used to produce the final install packages. It assumes that the x64 version of 7-Zip is installed: https://www.7-zip.org/