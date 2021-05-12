# simple-tftp-server
Basic tftp server that implements the octet mode (transfers raw 8-bit bytes).

Tftp server listens on the start port. Supports multiple clients, and uses fork() to handle them. 

## Usage 
Compile and run with two arguments, the start port and the end port. Make sure the start port isn't a reserved port.

Send RRQs (read requests) and WRQs (write requests) to the server.  
