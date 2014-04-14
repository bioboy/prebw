prebw
=====

glFTPd PreBW by biohazard
=========================

Installation steps:

1. Edit GLFTPD_PATH at top of Makefile
2. Edit settings in config.hpp
3. Run 'make' to build prebw utility
4. Run 'make install' as root to copy binaries to glftpd's bin directory
5. Copy prebw.tcl to your eggdrop's scripts directory
6. Add source scripts/prebw.tcl to bottom of eggdrop conf
7. Rehash eggdrop

