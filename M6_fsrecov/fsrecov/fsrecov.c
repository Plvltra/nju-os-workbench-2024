#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <math.h>
#include <stdbool.h>
#include "fat32.h"

#define DEBUG
#ifdef DEBUG
    FILE *logfile;
    #define LOG(...) fprintf(logfile, __VA_ARGS__)
#else
    #define LOG(...) {}
#endif

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) < (b) ? (b) : (a))
#define MAX_PROC = 3

struct fat32hdr *hdr;
clus_info_t *clus_infos;

u32 RES_CLUS = 2;       // Reserved clusters count
u32 CLUS_CNT;           // Reserved clusters exclusive
u32 TOT_CLUS_CNT;       // Reserved clusters inclusive
u32 MAX_VALID_CLUSID;
u32 CLUS_SIZE;
u32 NDENTS;             // Number of dents for a cluster

bool is_zero(const void *ptr, size_t size) {
    const unsigned char *byte_ptr = (const unsigned char *)ptr;
    for (size_t i = 0; i < size; i++) {
        if (byte_ptr[i] != 0) {
            return false;
        }
    }
    return true;
}

unsigned char ChkSum(unsigned char *pFcbName) {
    // See: Sec 7.2
    short FcbNameLen;
    unsigned char Sum;
    Sum = 0;
    for (FcbNameLen=11; FcbNameLen!=0; FcbNameLen--) {
        // NOTE: The operation is an unsigned char rotate right
        Sum = ((Sum & 1) ? 0x80 : 0) + (Sum >> 1) + *pFcbName++;
    }
    return (Sum);
}

bool is_longnm_dent(struct longnm_dent *ldent) {
    if (ldent->LDIR_Ord == 0)
        return false;

    u8 ord_begin;
    if (ldent->LDIR_Ord & LAST_LONG_ENTRY) {
        ord_begin = ldent->LDIR_Ord - LAST_LONG_ENTRY;
    } else {
        ord_begin = ldent->LDIR_Ord;
    }
    if (ord_begin > 8) {
        return false;
    }

    struct fat32dent *sdent = (struct fat32dent *)ldent + ord_begin;
    u8 chksum = ChkSum(sdent->DIR_Name);
    if (ldent->LDIR_Chksum != chksum)
        return false;
    for (int ord = ord_begin - 1; ord > 0; ord--) {
        struct longnm_dent *dent = ldent + (ord_begin - ord);
        if (dent->LDIR_Attr != ATTR_LONG_NAME || dent->LDIR_Ord != ord || dent->LDIR_Chksum != chksum) {
            return false;
        }
    }
    return true;
}

int count_of_clusters() {
    // See: Sec 3.5
    u32 DataSec = MAX(hdr->BPB_TotSec16, hdr->BPB_TotSec32) - (hdr->BPB_RsvdSecCnt + (hdr->BPB_NumFATs * hdr->BPB_FATSz32)); // BPB_TotSec32 doesn't work
    int CountOfClusters = DataSec / hdr->BPB_SecPerClus;
    return CountOfClusters;
}

void *clus_to_addr(int n) {
    // See: Sec 6.7
    u32 DataSec = hdr->BPB_RsvdSecCnt + hdr->BPB_NumFATs * hdr->BPB_FATSz32;
    u32 FirstSectorofCluster = (n - 2) * hdr->BPB_SecPerClus + DataSec;
    return ((char*)hdr) + FirstSectorofCluster * hdr->BPB_BytsPerSec;
}

enum ClusType clus_to_type(int n) {
    if (clus_infos[n].type != UNDETERMINED) {
        return clus_infos[n].type;
    }
    
    void *cluster = clus_to_addr(n);

    // Is root cluster?
    bool is_root_clus = (n == hdr->BPB_RootClus);
    if (is_root_clus) {
        return DIR;
    }
    // Is directory entry?
    struct fat32dent *dent = (struct fat32dent *)cluster;
    if (dent->DIR_Attr == (dent->DIR_Attr & ATTR_DENT_MASK)) {
        if (dent->DIR_Attr == ATTR_LONG_NAME) { // LDIR_Attr and DIR_Attr are in the same position
            // Is long name directory entry?
            struct longnm_dent *ldent = (struct longnm_dent *)cluster;
            if (is_longnm_dent(ldent)) {
                return DIR;
            }
        } else {
            int data_clusId = dent->DIR_FstClusLO | (dent->DIR_FstClusHI << 16);
            bool is_valid_dent = dent->DIR_NTRes == 0 
                && (2 < data_clusId && data_clusId <= MAX_VALID_CLUSID)
                && (0 <= dent->DIR_CrtTimeTenth && dent->DIR_CrtTimeTenth <= 199);
            if (is_valid_dent) {
                if (dent->DIR_Attr == ATTR_DIRECTORY) {
                    const char dot[] = {0x2E, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20};
                    const char dotdot[] = {0x2E, 0x2E, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20};
                    bool is_dir_dent = memcmp(dent, dot, sizeof(dot)) == 0
                        && memcmp(dent + 1, dotdot, sizeof(dotdot)) == 0;
                    if (is_dir_dent) {
                        return DIR;
                    }
                    if (dent->DIR_FileSize == 0 && clus_to_type(data_clusId) == DIR) {
                        return DIR;
                    }
                } else if (dent->DIR_Attr == ATTR_ARCHIVE) { // possible optimization: check bmp size
                    if (clus_to_type(data_clusId) == BMP_H) {
                        return DIR;
                    }
                }
            }
        }
    }

    struct bmphdr *bmp_hdr = (struct bmphdr *)cluster;
    if (bmp_hdr->magic == 0x4d42) {
        u32 bits_imgdata = bmp_hdr->BitsPerPx * bmp_hdr->width;
        bits_imgdata = (bits_imgdata + 31) & ~31; // 4 bytes aligned
        bits_imgdata *= bmp_hdr->height;
        bool is_size_valid = (bmp_hdr->file_size == bmp_hdr->offset_of_imgdata + bits_imgdata / 8);
        if (is_size_valid) {
            return BMP_H;
        }
    }

    if (is_zero(cluster, CLUS_SIZE)) {
        return UNUSED;
    }

    return BMP_D;
    // return UNDETERMINED;
}

void *mmap_file(const char *fname) {
    int fd = open(fname, O_RDONLY);
    if (fd < 0) {
        goto release;
    }

    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        goto release;
    }

    size_t length = sb.st_size;
    void *addr = mmap(NULL, length, PROT_READ, MAP_PRIVATE, fd, 0);
    if (addr == MAP_FAILED) {
        goto release;
    }
    return addr;

release:
    if (fd > 0) {
        close(fd);
    }
    perror("map error");
    exit(EXIT_FAILURE);
}

bool get_filename(struct fat32dent *dent, char *buf) {
    u8 chksum = ChkSum(dent->DIR_Name);
    int len = 0;
    int ord = 0;
    while (++ord) {
        struct longnm_dent *ldent = (struct longnm_dent *)dent - ord;
        int diff = ldent->LDIR_Ord - ord;
        if (diff != 0 && diff != LAST_LONG_ENTRY) {
            // long int offset = (char *)dent - (char *)hdr;
            // LOG("[Error] long name corrupt! short entry name: %s, address: 0x%lx, diff : %d.", dent->DIR_Name, offset, diff);
            return false; // TODO: handle long name entry cross cluster(concat it with other cluster end)
        }
        assert(ldent->LDIR_Attr == ATTR_LONG_NAME);
        assert(ldent->LDIR_Chksum == chksum);
        u16 LDIR_Name[13];
        memcpy(LDIR_Name, ldent->LDIR_Name1, sizeof(ldent->LDIR_Name1));
        memcpy(LDIR_Name + 5, ldent->LDIR_Name2, sizeof(ldent->LDIR_Name2));
        memcpy(LDIR_Name + 11, ldent->LDIR_Name3, sizeof(ldent->LDIR_Name3));
        for (int i = 0; i < 13; i++) {
            buf[len++] = (u8)LDIR_Name[i];
            if (LDIR_Name[i] == 0)
                return true;
        }
        if (diff == LAST_LONG_ENTRY) {
            buf[len++] = '\0';
            return true;
        }
    }
    assert(false);
    // int len = 0;
    // for (int i = 0; i < sizeof(dent->DIR_Name); i++) {
    //     if (dent->DIR_Name[i] != ' ') {
    //         if (i == 8)
    //             buf[len++] = '.';
    //         buf[len++] = dent->DIR_Name[i];
    //     }
    // }
    // buf[len] = '\0';
}

void dump_bmp(const char *fpath, int clusIds[], int byts_to_write) {
    int fd = open(fpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("Failed to open file");
        exit(EXIT_FAILURE);
    }

    int length = (byts_to_write + CLUS_SIZE - 1) / CLUS_SIZE;
    assert(length >= 1);
    for (int i = 0; i < length; i++) {
        int byts_num;
        if (i != length - 1) {
            byts_num = CLUS_SIZE;
        } else {
            byts_num = byts_to_write - (length - 1) * CLUS_SIZE;
        }
        
        int clusId = clusIds[i];
        assert(clus_infos[clusId].type == BMP_D || clus_infos[clusId].type == BMP_H);
        void *cluster = clus_to_addr(clusId);
        write(fd, cluster, byts_num);
    }

    close(fd);
}

void sha1sum(const char fpath[], char out_hash[]) {
    char cmd[110];
    snprintf(cmd, sizeof(cmd), "sha1sum %s", fpath);
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        perror("popen");
        exit(EXIT_FAILURE);
    }
    fscanf(fp, "%s", out_hash);
}

double diff(u8 *line1, u8 *line2, int byts_line) {
    double ret = 0;
    for (int i = 0; i < byts_line; i++) {
        u8 c1 = line1[i];
        u8 c2 = line2[i];
        ret += c1 > c2 ? c1 - c2 : c2 - c1;
    }
    ret /= byts_line;
    return ret;
}

int padding_offset(int clus_idx, int byts_line_c, int byts_bmphdr) {
    assert(clus_idx >= 1);
    int byts_line = (byts_line_c + 3) & ~3; // 4 bytes align
    int padcnt = byts_line - byts_line_c;

    int rem = (clus_idx * CLUS_SIZE + byts_line - byts_bmphdr) % byts_line;
    int pad_end = byts_line - rem;
    int pad_begin = pad_end - padcnt;
    if (pad_begin < 0)
        pad_begin += byts_line;
    return pad_begin;
}

bool check_line(u8 *line, int size, int offset, int pad_cnt) {
    bool is_valid = true;
    for (int pad_idx = 0; pad_idx < pad_cnt; pad_idx++) {
        int pad_off = (offset + pad_idx) % size;
        u8 pad = line[pad_off];
        if (pad != 0) {
            is_valid = false;
        }
    }
    return is_valid;
}

int main(int argc, char *argv[]) {
#ifdef DEBUG
    logfile = fopen("log.txt", "w");
#endif

    if (argc != 2) {
        exit(EXIT_FAILURE);
    }

    // Init fat32 parameters
    const char *fname = argv[1];
    hdr = mmap_file(fname);
    assert(hdr->Signature_word == 0xaa55);
    LOG("%s: DOS/MBR boot sector, ", fname);
    LOG("OEM-ID \"%s\", ", hdr->BS_OEMName);
    LOG("sectors/cluster %d, ", hdr->BPB_SecPerClus);
    LOG("sectors size %d, ", hdr->BPB_BytsPerSec);
    LOG("sectors count %d, ", hdr->BPB_TotSec16);
    LOG("sectors count %d, ", hdr->BPB_TotSec32);
    LOG("sectors/FAT %d, ", hdr->BPB_FATSz32);
    LOG("serial number 0x%x\n", hdr->BS_VolID);

    CLUS_CNT = count_of_clusters();
    TOT_CLUS_CNT = CLUS_CNT + 2;
    MAX_VALID_CLUSID = CLUS_CNT + 1;
    CLUS_SIZE = hdr->BPB_SecPerClus * hdr->BPB_BytsPerSec;
    NDENTS = hdr->BPB_BytsPerSec * hdr->BPB_SecPerClus / sizeof(struct fat32dent);

    int size = TOT_CLUS_CNT * sizeof(clus_info_t);
    clus_infos = (clus_info_t *)malloc(size);
    memset(clus_infos, 0, size);

    // First pass: classify clusters
    int total_dir = 0;
    int total_bmp = 0;
    int bmpd_clusId = 0;
    int dir_clusIds[30] = {0};
    for (int clusId = RES_CLUS; clusId < TOT_CLUS_CNT; clusId++) {
        enum ClusType type = clus_to_type(clusId);
        clus_infos[clusId].type = type;

        if (type == BMP_D) {
            if (!bmpd_clusId) {
                bmpd_clusId = clusId;
            }
        } else {
            // flush buffer
            if (bmpd_clusId) {
                LOG("[cluster %3d - %3d] type: %s\n", bmpd_clusId, clusId - 1, to_string(BMP_D));
                bmpd_clusId = 0;
            }
            if (type != UNUSED && type != UNDETERMINED) {
                LOG("[cluster %9d] type: %s\n", clusId, to_string(type));
                if (type == DIR) {
                    dir_clusIds[total_dir++] = clusId;
                } else if (type == BMP_H) {
                    total_bmp++;
                }
            }
        }
    }

    // Next pass: traverse all dents & recover bmp
    int recovered = 0;
    int longname_err = 0;
    int cluster_err = 0;
    int next_err = 0;
    for (int dir_idx = 0; dir_idx < total_dir; dir_idx++) {
        int clusId = dir_clusIds[dir_idx];
        struct fat32dent *dent0 = (struct fat32dent *)clus_to_addr(clusId);
        for (int d = 0; d < NDENTS; d++) {
            struct fat32dent *dent = dent0 + d;
            if (dent->DIR_Name[0] == 0x00) // 0x00 indicate that all directory entries following the current free entry are free
                break;
            if (dent->DIR_Name[0] == 0xE5)
                continue;
            if (dent->DIR_Attr != ATTR_DIRECTORY
                && dent->DIR_Attr != ATTR_ARCHIVE)
                continue;

            int data_clusId = dent->DIR_FstClusLO | (dent->DIR_FstClusHI << 16);
            bool is_bmphdr = (dent->DIR_Attr & ATTR_ARCHIVE) && clus_infos[data_clusId].type == BMP_H;
            if (is_bmphdr) {
                u32 file_size = dent->DIR_FileSize; // possible optimization: check bmp header size is equal
                assert(file_size > 0);
                struct bmphdr *bmp_hdr = (struct bmphdr *)clus_to_addr(data_clusId);
                int byts_line_c = sizeof(Color) * bmp_hdr->width;
                int byts_line = (byts_line_c + 3) & ~3; // 4 bytes align
                int byts_pad = byts_line - byts_line_c;
                int byts_bmphdr = bmp_hdr->offset_of_imgdata;
                assert(CLUS_SIZE - byts_bmphdr > byts_line); // assume cluster contains at least a line of pixels

                int length = (file_size + CLUS_SIZE - 1) / CLUS_SIZE;
                int *clusIds = malloc(length * sizeof(int));
                int err = 0;

                int pre_clusId = data_clusId;
                clusIds[0] = data_clusId;
                for (int idx = 1; idx < length && !err; idx++) {
                    int clusId;
                    // assume that bmp is contiguous
                    if (clus_infos[pre_clusId + 1].type == BMP_D) {
                        clusId = pre_clusId + 1;
                    } else {
                        u8 *clus = (u8 *)clus_to_addr(pre_clusId);
                        u8 *line_begin = clus + (CLUS_SIZE - byts_line);
                        u8 *line_end = clus + CLUS_SIZE;
                        int pad_off = padding_offset(idx, byts_line_c, byts_bmphdr);
                        if (!check_line(line_begin, byts_line, pad_off, byts_pad)) {
                            err |= ECLUSTER;
                        }

                        double min_diff = 0xff;
                        for (int id = RES_CLUS; id < TOT_CLUS_CNT && !err; id++) {
                            if (clus_infos[id].type != BMP_D || id == pre_clusId)
                                continue;
                            u8 *next_clus = clus_to_addr(id);
                            bool valid = check_line(next_clus, byts_line, pad_off, byts_pad);
                            if (valid) {
                                double ave_diff = diff(line_begin, next_clus, byts_line);
                                if (ave_diff < min_diff) {
                                    clusId = id;
                                    min_diff = ave_diff;
                                }
                            }
                        }
                        if (clusId == -1) {
                            err |= ENEXT;
                        }
                    }
                    clusIds[idx] = clusId;
                    pre_clusId = clusId;
                }

                char fname[90];
                if (!get_filename(dent, fname)) {
                    err |= ELONGNAME;
                }
                if (!err) {
                    LOG("[%-25s] %6.1f KiB    ", fname, dent->DIR_FileSize / 1024.0);
                    for (int i = 0; i < length; i++) {
                        LOG("#%d ", clusIds[i]);
                    }
                    LOG("\n");

                    char fpath[100];
                    snprintf(fpath, sizeof(fpath), "./output/%s", fname);
                    dump_bmp(fpath, clusIds, file_size);
                    char hash[50];
                    sha1sum(fpath, hash);
                    printf("%s  %s\n", hash, fname);
                }

                if (!err) {
                    recovered++;
                } else if (err & ELONGNAME) {
                    longname_err++;
                } else if (err & ECLUSTER) {
                    cluster_err++;
                } else if (err & ENEXT) {
                    next_err++;
                }

                free(clusIds);
            }
        }
    }
    LOG("total: %d, recovered: %d, long name error: %d, cluster error: %d, next error: %d\n", 
        total_bmp, recovered, longname_err, cluster_err, next_err);
    LOG("recovered ratio: %.2f", (float)recovered / total_bmp);
}


// Cmd:
// find resource/testdata -name "*.bmp" -exec sha1sum {} \; | awk '{print $1, $2}' > sha1sums.txt
// comm -12 <(awk '{print $1, $2}' result.txt | sort) <(awk '{print $1, $2}' sha1sum_expect.txt | sort) | wc -l