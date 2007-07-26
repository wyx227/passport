
POST-INSTALLATION

1. Windows Vista
Go to the Programs menu and find the PassPort shortcut. Open the properties of the 
shortcut, then open the advanced page and make sure that the shortcut is executed
with Administrator's rights. Otherwise the program will not be able to save 
configuration file in the installation directory. 


RELEASE NOTES

0.1.0 (12/07/2007)

Created the first public version by taking the port forwarding piece of code from
web page of QuantumG. Then added the interface and possibility of easy configuration
plus forwarding of more than one port. All that compiled and released ;-)

The program installs as the service that reads the PassPortConfig.xml containing
list of ports to be forwarded to remote machines. The XML file can be prepared by 
running the PassPort.exe -Control. The application opens the service control
page that can be used to start/stop service and configure the list of forwards.

To install the service it is enough to execute PassPort.exe -Install. To uninstall
PassPort.exe -Uninstall



