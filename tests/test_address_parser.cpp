#include "../include/orbita_address.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <address_file.txt>\n", argv[0]);
        return 1;
    }
    const char* filename = argv[1];
    orbita_channel_desc_t* channels = nullptr;
    int count = 0;
    int res = orbita_load_address_file(filename, &channels, &count);
    if (res != 0) {
        fprintf(stderr, "Error loading file: %d\n", res);
        return 1;
    }
    printf("Loaded %d channels\n", count);
    for (int i = 0; i < count && i < 10; ++i) {
        printf("Channel %d: numOutElemG=%u stepOutG=%u type=%d bit=%d\n",
               i, channels[i].numOutElemG, channels[i].stepOutG,
               channels[i].adressType, channels[i].bitNumber);
    }
    orbita_free_channels(channels, count);
    return 0;
}
