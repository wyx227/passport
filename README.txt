RELEASE NOTES
==============
==============

Release 1.0.1 - 2008-Nov-08
===========================
Made sure that debug logs don't appear in the release code.
Included fix not to drop uploads when WOULDBLOCK error is returned from the channel. 
Corrected minor GUI glitches.


Release 1.0.0 - 2007-Nov-06
===========================
Added udp procotol support
Logfile support
Bugfixes
Uses winsock2 api

Dependencies:
=============
Needs Visual Studio 2005 runtime, if you dont' have it. you can download it form here:
http://www.microsoft.com/downloads/details.aspx?familyid=32bc1bee-a3f9-4c13-9c99-220b62a191ee&displaylang=en


Upgrade from an earlier version:
================================
To upgrade simply uninstall the old version and install this release
Note: Config file syntax has changed so old config files are not compatible any more, please recreate them

POST-INSTALLATION
=================

1. Windows Vista
Go to the Programs menu and find the PassPort shortcut. Open the properties of the 
shortcut, then open the advanced page and make sure that the shortcut is executed
with Administrator's rights. Otherwise the program will not be able to save 
configuration file in the installation directory. 

Command Line Usage:
===================
PassPort.exe -Control
	The application opens the service control
	page that can be used to start/stop service and configure the list of forwards.
	
PassPort.exe -Install
	Install the service
	
PassPort.exe -Uninstall
	Uninstall the service

Thanx, PassPort Team

