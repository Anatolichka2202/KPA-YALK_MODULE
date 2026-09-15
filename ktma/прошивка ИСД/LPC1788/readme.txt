s_ob_easyweb
=====================
This project contains NXP's easyweb example demonstrating
the use of the Ethernet interface. A simple webserver has been
implemented allowing a webpage to be downloaded from the board.

The IP address is specified in the tcpip.h file. The MAC address
is specified in the ethmac.h file

Use a web browser to access the webserver. 


The project makes use of code from the following library projects:
- Lib_CMSIS          : for CMSIS files relevant to LPC178x
- Lib_OemBoard       : for Embedded Artists OEM Board drivers


These library projects must exist in the same workspace in order
for the project to successfully build.

