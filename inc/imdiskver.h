#define IMDISK_MAJOR_VERSION        3
#define IMDISK_MINOR_VERSION        0
#define IMDISK_MINOR_LOW_VERSION    0

#define STR_EXPAND(tok) #tok
#define STR(tok) STR_EXPAND(tok)

#define IMDISK_RC_VERSION_FLD       IMDISK_MAJOR_VERSION,IMDISK_MINOR_VERSION,IMDISK_MINOR_LOW_VERSION
#define IMDISK_RC_VERSION_STR       STR(IMDISK_MAJOR_VERSION) "." STR(IMDISK_MINOR_VERSION) "." STR(IMDISK_MINOR_LOW_VERSION)
