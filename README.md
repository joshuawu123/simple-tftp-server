# simple-tftp-server
Basic tftp server that implements the octet mode (transfers raw 8-bit bytes).

Tftp server listens on the start port. Supports multiple clients, as it uses fork() to handle them. 

## Usage 
Compile and run. Takes in two arguments, the start port and the end port. Make sure the start port isn't a reserved port.
