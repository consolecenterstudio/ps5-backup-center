#pragma once
#include "np/np_common.h"
/* Codes [bin] : Initialize → 2, Terminate → 1, CreateRequest → 6, Abort/Delete → 5, GetAddcontLatestVersion :
 * étiquette absente → 3 (avant la taille → 4), Check → 7. */
#define SCE_GAME_UPDATE_ERROR_NOT_INITIALIZED     ((int32_t)0x80412801)
#define SCE_GAME_UPDATE_ERROR_ALREADY_INITIALIZED ((int32_t)0x80412802)
#define SCE_GAME_UPDATE_ERROR_INVALID_ARG         ((int32_t)0x80412803)
#define SCE_GAME_UPDATE_ERROR_INVALID_SIZE        ((int32_t)0x80412804)
#define SCE_GAME_UPDATE_ERROR_REQUEST_NOT_FOUND   ((int32_t)0x80412805)
#define SCE_GAME_UPDATE_ERROR_TOO_MANY_REQUESTS   ((int32_t)0x80412806)
#define SCE_GAME_UPDATE_ERROR_ABORTED             ((int32_t)0x80412807)
typedef struct SceGameUpdateCheckParam { size_t size; uint32_t option; uint32_t reserved[9]; } SceGameUpdateCheckParam;  /* [A] */
typedef struct SceGameUpdateCheckResult {
    size_t size; bool found; bool addcontFound; char padding[2]; char contentVersion[11]; char padding2[1]; uint32_t reserved[6];
} SceGameUpdateCheckResult;  /* [A] */
typedef struct SceGameUpdateAddcontVersionInfo { size_t size; bool found; char contentVersion[11]; uint32_t reserved[6]; } SceGameUpdateAddcontVersionInfo;  /* [A] */
