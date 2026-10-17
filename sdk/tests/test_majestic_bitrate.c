#include <assert.h>
#include <stdio.h>

#include "../../majestic/majestic_bitrate.h"

int main(void)
{
    assert(majestic_auto_bitrate_kbps(1920, 1080, 25) == 3648);
    assert(majestic_auto_bitrate_kbps(704, 576, 25) == 704);
    assert(majestic_auto_bitrate_kbps(160, 120, 1) == 128);
    printf("majestic auto bitrate matches its logged values\n");
    return 0;
}
