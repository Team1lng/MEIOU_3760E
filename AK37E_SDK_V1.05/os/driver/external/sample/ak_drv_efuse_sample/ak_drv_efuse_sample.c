
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include "ak_drv_common.h"


int ak_drv_read_globalID(void);

int main (int argc, char **argv)
{
     ak_drv_read_globalID();
    return 0;
}
