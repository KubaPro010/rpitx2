#include <stdio.h>

extern "C"
{
#include "rds.h"
#include "fm_mpx.h"
#include "control_pipe.h"
}

int main() {
    set_rds_pi(0xff);
    set_rds_ps("TEST");
    set_rds_ecc(0xFF);
    int af_array[] = {3, 2, 1, 0};
    set_rds_af(af_array);
    for(int i = 0; i < 10; i++) {
        int buffer[BITS_PER_GROUP];
        get_rds_group(buffer, 0, 0);
        for(int j = 0; j < BITS_PER_GROUP; j++) {
            printf("%d", buffer[j]);
        }
    }
    printf("\n");
    return 0;
}