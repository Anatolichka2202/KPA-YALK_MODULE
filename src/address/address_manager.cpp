#include "address_manager.h"
#include "address_parser.h"
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cstdio>

struct orbita_address_manager {
    int informativnost;
    int group_size;
    int cycle_size;
    std::vector<orbita_channel_desc_t> channels;
};

static int get_group_size_for_inf(int inf) {
    switch (inf) {
    case 16: return 2048;
    case 8:  return 1024;
    case 4:  return 512;
    case 2:  return 256;
    case 1:  return 128;
    default: return 0;
    }
}

orbita_address_manager_t* orbita_address_manager_create(int informativnost) {
    orbita_address_manager_t* mgr = (orbita_address_manager_t*)malloc(sizeof(orbita_address_manager_t));
    if (!mgr) return nullptr;
    mgr->informativnost = informativnost;
    mgr->group_size = get_group_size_for_inf(informativnost);
    mgr->cycle_size = mgr->group_size * 32;
    return mgr;
}

void orbita_address_manager_destroy(orbita_address_manager_t* mgr) {
    if (mgr) {
        for (auto& ch : mgr->channels) {
            orbita_free_channel_desc(&ch);
        }
        free(mgr);
    }
}

int orbita_address_manager_add_channel(orbita_address_manager_t* mgr, const orbita_channel_desc_t* desc) {
    if (!mgr || !desc) return -1;
    orbita_channel_desc_t copy = *desc;
    copy.arrNumGroup = nullptr;
    copy.arrNumCikl = nullptr;
    if (desc->numGroups > 0 && desc->arrNumGroup) {
        copy.arrNumGroup = (uint16_t*)malloc(desc->numGroups * sizeof(uint16_t));
        if (!copy.arrNumGroup) return -2;
        memcpy(copy.arrNumGroup, desc->arrNumGroup, desc->numGroups * sizeof(uint16_t));
    }
    if (desc->numCikls > 0 && desc->arrNumCikl) {
        copy.arrNumCikl = (uint16_t*)malloc(desc->numCikls * sizeof(uint16_t));
        if (!copy.arrNumCikl) {
            free(copy.arrNumGroup);
            return -2;
        }
        memcpy(copy.arrNumCikl, desc->arrNumCikl, desc->numCikls * sizeof(uint16_t));
    }
    mgr->channels.push_back(copy);
    return 0;
}

int orbita_address_manager_load_file(orbita_address_manager_t* mgr, const char* filename) {
    if (!mgr || !filename) return -1;
    orbita_channel_desc_t* channels = nullptr;
    int count = 0;
    int res = orbita_load_address_file(filename, &channels, &count);
    if (res != 0) return res;
    for (int i = 0; i < count; ++i) {
        orbita_address_manager_add_channel(mgr, &channels[i]);
    }
    orbita_free_channels(channels, count);
    return 0;
}

int orbita_address_manager_load_lines(orbita_address_manager_t* mgr, const char** lines, int count) {
    if (!mgr || !lines || count <= 0) return -1;
    for (int i = 0; i < count; ++i) {
        orbita_channel_desc_t desc;
        if (orbita_parse_address_line(lines[i], &desc) != 0) {
            return -2 - i;
        }
        orbita_address_manager_add_channel(mgr, &desc);
        orbita_free_channel_desc(&desc);
    }
    return 0;
}

const orbita_channel_desc_t* orbita_address_manager_get_channels(const orbita_address_manager_t* mgr, int* out_count) {
    if (!mgr || !out_count) return nullptr;
    *out_count = (int)mgr->channels.size();
    return mgr->channels.data();
}

static void fill_group_arr(orbita_channel_desc_t* desc, uint32_t first_elem, uint32_t step, uint32_t group_size) {
    if (step <= group_size) {
        desc->flagGroup = 0;
        return;
    }
    desc->flagGroup = 1;
    uint32_t fElem = first_elem;
    uint32_t fNumGr = (fElem - 1) / group_size + 1;
    uint32_t sNumGr = (step - 1) / group_size;
    std::vector<uint16_t> groups;
    for (uint32_t gr = fNumGr; gr <= 32; gr += sNumGr) {
        groups.push_back((uint16_t)gr);
    }
    desc->numGroups = (uint16_t)groups.size();
    desc->arrNumGroup = (uint16_t*)realloc(desc->arrNumGroup, desc->numGroups * sizeof(uint16_t));
    if (desc->arrNumGroup) {
        for (size_t i = 0; i < groups.size(); ++i) desc->arrNumGroup[i] = groups[i];
    }
}

static void fill_cikl_arr(orbita_channel_desc_t* desc, uint32_t first_elem, uint32_t step, uint32_t group_size, uint32_t cycle_size) {
    if (step <= cycle_size) {
        desc->flagCikl = 0;
        fill_group_arr(desc, first_elem, step, group_size);
        return;
    }
    desc->flagCikl = 1;
    uint32_t fElem = first_elem;
    uint32_t fNumC = (fElem - 1) / cycle_size + 1;
    uint32_t sNumC = (step - 1) / cycle_size;
    std::vector<uint16_t> cikls;
    for (uint32_t cc = fNumC; cc <= 4; cc += sNumC) {
        cikls.push_back((uint16_t)cc);
    }
    desc->numCikls = (uint16_t)cikls.size();
    desc->arrNumCikl = (uint16_t*)realloc(desc->arrNumCikl, desc->numCikls * sizeof(uint16_t));
    if (desc->arrNumCikl) {
        for (size_t i = 0; i < cikls.size(); ++i) desc->arrNumCikl[i] = cikls[i];
    }
    uint32_t fElemGr = first_elem - (fNumC - 1) * cycle_size;
    uint32_t sElemGr = step;
    if (sElemGr > group_size) {
        fill_group_arr(desc, fElemGr, sElemGr, group_size);
    } else {
        desc->flagGroup = 0;
    }
}

int orbita_address_manager_finalize(orbita_address_manager_t* mgr) {
    if (!mgr) return -1;
    for (auto& ch : mgr->channels) {
        if (ch.stepOutG > (uint32_t)mgr->cycle_size) {
            fill_cikl_arr(&ch, ch.numOutElemG, ch.stepOutG, mgr->group_size, mgr->cycle_size);
        } else if (ch.stepOutG > (uint32_t)mgr->group_size) {
            fill_group_arr(&ch, ch.numOutElemG, ch.stepOutG, mgr->group_size);
        }
    }
    return 0;
}

int orbita_address_manager_get_group_size(const orbita_address_manager_t* mgr) {
    return mgr ? mgr->group_size : 0;
}

int orbita_address_manager_get_cycle_size(const orbita_address_manager_t* mgr) {
    return mgr ? mgr->cycle_size : 0;
}
