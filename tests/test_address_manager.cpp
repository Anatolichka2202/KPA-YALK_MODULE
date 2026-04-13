#include "orbita_address.h"
#include "../src/address/address_manager.h"
#include <stdio.h>

int main() {
    orbita_address_manager_t* mgr = orbita_address_manager_create(16);
    if (!mgr) {
        fprintf(stderr, "Failed to create manager\n");
        return 1;
    }
    const char* filename = "address_file.txt";
    int res = orbita_address_manager_load_file(mgr, filename);
    if (res != 0) {
        fprintf(stderr, "Failed to load file: %d\n", res);
        orbita_address_manager_destroy(mgr);
        return 1;
    }
    res = orbita_address_manager_finalize(mgr);
    if (res != 0) {
        fprintf(stderr, "Finalize failed: %d\n", res);
        orbita_address_manager_destroy(mgr);
        return 1;
    }
    int count = 0;
    const orbita_channel_desc_t* channels = orbita_address_manager_get_channels(mgr, &count);
    printf("Loaded %d channels\n", count);
    for (int i = 0; i < count && i < 10; ++i) {
        printf("Ch%d: elem=%u step=%u type=%d bit=%d flags: gr=%d ck=%d\n",
               i, channels[i].numOutElemG, channels[i].stepOutG,
               channels[i].adressType, channels[i].bitNumber,
               channels[i].flagGroup, channels[i].flagCikl);
        if (channels[i].flagGroup) {
            printf("  Groups: ");
            for (int g = 0; g < channels[i].numGroups; ++g)
                printf("%d ", channels[i].arrNumGroup[g]);
            printf("\n");
        }
        if (channels[i].flagCikl) {
            printf("  Cycles: ");
            for (int c = 0; c < channels[i].numCikls; ++c)
                printf("%d ", channels[i].arrNumCikl[c]);
            printf("\n");
        }
    }
    orbita_address_manager_destroy(mgr);
    return 0;
}
