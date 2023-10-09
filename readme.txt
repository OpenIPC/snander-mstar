SNANDer - Spi Nor/nAND programmER v.1.8.0 by McMCC

  Usage:
 -h             display this message
 -p <name>      select programmer device (mstar, ch341a)
 -s             enable i2c fast speed mode (for mstar programmer)
 -q             query connected i2c devices (for mstar programmer)
 -d             disable internal ECC (use read and write page size + OOB size)
 -o <bytes>     manual set OOB size with disable internal ECC (default 0)
 -I             ECC ignore errors (for read test only)
 -k             Skip BAD pages, try read or write in to next page
 -L             print list support chips
 -i             read the chip ID info
 -e             erase chip (full or use with -a [-l])
 -l <bytes>     manually set length
 -a <address>   manually set address
 -w <filename>  write chip with data from filename
 -r <filename>  read chip and save data to filename
 -v             verify after write on chip
