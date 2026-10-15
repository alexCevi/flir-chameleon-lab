/* SPDX-License-Identifier: MIT */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arv.h>

static int is_flea3(unsigned int index)
{
    const char *protocol = arv_get_device_protocol(index);
    const char *model = arv_get_device_model(index);

    if (protocol != NULL && strcmp(protocol, "USB3") == 0)
        return 1;

    if (model != NULL &&
        (strstr(model, "Flea") != NULL || strstr(model, "FL3-") != NULL))
        return 1;

    return 0;
}

int main(void)
{
    unsigned int n;
    unsigned int i;

    arv_update_device_list();
    n = arv_get_n_devices();

    for (i = 0; i < n; i++) {
        if (!is_flea3(i))
            continue;

        printf("Opening Flea3 viewer\n");
        fflush(stdout);
        arv_shutdown();
        execl("./flea3-live", "flea3-live", (char *)NULL);
        perror("flea3-live");
        return 1;
    }

    arv_shutdown();
    printf("Opening Chameleon viewer\n");
    fflush(stdout);
    execl("./chameleon-live", "chameleon-live", (char *)NULL);
    perror("chameleon-live");
    return 1;
}
