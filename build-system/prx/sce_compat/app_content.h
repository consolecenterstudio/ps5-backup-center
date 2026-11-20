#pragma once
#include "np/np_common.h"
/* Codes d'erreur [bin] : le filtre de dlc_emu accepte d'un bloc 0x80D90001–0x80D90012 (ses 18 premiers noms, dans
 * l'ordre) puis 0x80D90014–0x80D90018 (les 5 suivants) ; 0x13 = NETWORK, traité à part. Confirmés un par un : 1, 2, 3,
 * 5, 7 [S] ; 4, 6, 0x0A, 0x12 [bin]. */
#define SCE_APP_CONTENT_ERROR_NOT_INITIALIZED   ((int32_t)0x80D90001)
#define SCE_APP_CONTENT_ERROR_PARAMETER         ((int32_t)0x80D90002)
#define SCE_APP_CONTENT_ERROR_BUSY              ((int32_t)0x80D90003)
#define SCE_APP_CONTENT_ERROR_NOT_MOUNTED       ((int32_t)0x80D90004)
#define SCE_APP_CONTENT_ERROR_NOT_FOUND         ((int32_t)0x80D90005)
#define SCE_APP_CONTENT_ERROR_MOUNT_FULL        ((int32_t)0x80D90006)
#define SCE_APP_CONTENT_ERROR_DRM_NO_ENTITLEMENT ((int32_t)0x80D90007)
#define SCE_APP_CONTENT_ERROR_NO_SPACE          ((int32_t)0x80D90008)
#define SCE_APP_CONTENT_ERROR_NOT_SUPPORTED     ((int32_t)0x80D90009)
#define SCE_APP_CONTENT_ERROR_INTERNAL          ((int32_t)0x80D9000A)
#define SCE_APP_CONTENT_ERROR_DOWNLOAD_ENTRY_FULL ((int32_t)0x80D9000B)
#define SCE_APP_CONTENT_ERROR_INVALID_PKG       ((int32_t)0x80D9000C)
#define SCE_APP_CONTENT_ERROR_OTHER_APPLICATION_PKG ((int32_t)0x80D9000D)
#define SCE_APP_CONTENT_ERROR_CREATE_FULL       ((int32_t)0x80D9000E)
#define SCE_APP_CONTENT_ERROR_MOUNT_OTHER_APP   ((int32_t)0x80D9000F)
#define SCE_APP_CONTENT_ERROR_OF_MEMORY         ((int32_t)0x80D90010)
#define SCE_APP_CONTENT_ERROR_ADDCONT_SHRANK    ((int32_t)0x80D90011)
#define SCE_APP_CONTENT_ERROR_ADDCONT_NO_IN_QUEUE ((int32_t)0x80D90012)
#define SCE_APP_CONTENT_ERROR_NETWORK           ((int32_t)0x80D90013)
#define SCE_APP_CONTENT_ERROR_SIGNED_OUT        ((int32_t)0x80D90014)
#define SCE_APP_CONTENT_ERROR_UNSUPPORTED_COMPRESSION_FORMAT ((int32_t)0x80D90015)
#define SCE_APP_CONTENT_ERROR_BROKEN            ((int32_t)0x80D90016)
#define SCE_APP_CONTENT_ERROR_ADDCONT_NO_SPACE  ((int32_t)0x80D90017)
#define SCE_APP_CONTENT_ERROR_ADDCONT_ENFILE    ((int32_t)0x80D90018)
#define SCE_APP_CONTENT_ADDCONT_MOUNT_MAXNUM 64                 /* [bin] (> 63 → MOUNT_FULL) */
#define SCE_APP_CONTENT_TEMPORARY_DATA_OPTION_NONE 0
#define SCE_APP_CONTENT_TEMPORARY_DATA_OPTION_FORMAT (1 << 0)    /* [S] */
typedef uint32_t SceAppContentAppParamId;
typedef uint32_t SceAppContentPftFlag;
typedef uint32_t SceAppContentTemporaryDataOption;
typedef uint32_t SceAppContentAddcontDownloadStatus;
typedef struct SceAppContentInitParam { char reserved[32]; } SceAppContentInitParam;                    /* [A] */
typedef struct SceAppContentBootParam { char reserved1[4]; uint32_t attr; char reserved2[32]; } SceAppContentBootParam;  /* [A] */
typedef struct SceAppContentMountPoint { char data[16]; } SceAppContentMountPoint;                     /* [A] */
typedef struct SceAppContentAddcontDownloadProgress { uint64_t dataSize; uint64_t downloadedSize; } SceAppContentAddcontDownloadProgress;
