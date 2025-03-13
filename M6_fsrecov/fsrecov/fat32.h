#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

struct fat32hdr {
    u8  BS_jmpBoot[3];
    u8  BS_OEMName[8];
    u16 BPB_BytsPerSec;
    u8  BPB_SecPerClus;
    u16 BPB_RsvdSecCnt;     // Number of reserved sectors in the "Reserved Region"
    u8  BPB_NumFATs;
    u16 BPB_RootEntCnt;     // 0 for fat32, fat32 has no "Root Directory Region"
    u16 BPB_TotSec16;
    u8  BPB_Media;
    u16 BPB_FATSz16;
    u16 BPB_SecPerTrk;
    u16 BPB_NumHeads;
    u32 BPB_HiddSec;
    u32 BPB_TotSec32;
    u32 BPB_FATSz32;
    u16 BPB_ExtFlags;
    u16 BPB_FSVer;
    u32 BPB_RootClus;
    u16 BPB_FSInfo;
    u16 BPB_BkBootSec;
    u8  BPB_Reserved[12];
    u8  BS_DrvNum;
    u8  BS_Reserved1;
    u8  BS_BootSig;
    u32 BS_VolID;
    u8  BS_VolLab[11];
    u8  BS_FilSysType[8];
    u8  __padding_1[420];
    u16 Signature_word;
} __attribute__((packed));

struct fat32dent {
    u8  DIR_Name[11];
    u8  DIR_Attr;
    u8  DIR_NTRes;
    u8  DIR_CrtTimeTenth;
    u16 DIR_CrtTime;
    u16 DIR_CrtDate;
    u16 DIR_LastAccDate;
    u16 DIR_FstClusHI;
    u16 DIR_WrtTime;
    u16 DIR_WrtDate;
    u16 DIR_FstClusLO;
    u32 DIR_FileSize;
} __attribute__((packed));

#define CLUS_INVALID   0xffffff7

#define ATTR_READ_ONLY 0x01
#define ATTR_HIDDEN    0x02
#define ATTR_SYSTEM    0x04
#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE   0x20
#define ATTR_LONG_NAME (ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID)
#define ATTR_DENT_MASK (ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID | ATTR_DIRECTORY | ATTR_ARCHIVE)

struct longnm_dent {
    u8  LDIR_Ord;
    u16 LDIR_Name1[5];
    u8  LDIR_Attr;
    u8  LDIR_Type;
    u8  LDIR_Chksum;
    u16 LDIR_Name2[6];
    u16 LDIR_FstClusLO;
    u16 LDIR_Name3[2];
} __attribute__((packed));

struct bmphdr {
    u16 magic;
    u32 file_size;          // (bytes)
    u16 reserved1;
    u16 reserved2;
    u32 offset_of_imgdata;
    u32 DIB_hdr_size;
    u32 width;              // (pixels)
    u32 height;             // (pixels)
    u16 planes;
    u16 BitsPerPx;
    u32 compression;
    u32 img_size;           // (bytes)
    u32 x_resolution;       // (px/m)
    u32 y_resolution;       // (px/m)
    u32 colors_in_table;
    u32 important_color;
} __attribute__((packed));

typedef struct {
    u8 r;
    u8 g;
    u8 b;
} __attribute__((packed)) Color;

enum ClusType { UNDETERMINED=0, DIR, BMP_H, BMP_D, UNUSED };
const char* to_string(enum ClusType type) {
    switch (type) {
        case UNDETERMINED:  return "UNDETERMINED";
        case DIR:           return "DIR";
        case BMP_H:         return "BMP_H";
        case BMP_D:         return "BMP_D";
        case UNUSED:        return "UNUSED";
        default:            return "UNKNOWN";
    }
}

typedef struct {
    enum ClusType type;
} clus_info_t;

#define LAST_LONG_ENTRY ((u8)0x40)

#define ELONGNAME   0x1
#define ECLUSTER    0x2     // cluster exist error
#define ENEXT       0x4     // cannot find next