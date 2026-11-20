#pragma once
#include "np/np_common.h"
/* Codes d'erreur [bin] : 0x817D00xx (et non 0x80558xxx, valeur d'AnyPS5 démentie par le module compilé). */
#define SCE_NP_ENTITLEMENT_ACCESS_ERROR_NOT_INITIALIZED  ((int32_t)0x817D0001)
#define SCE_NP_ENTITLEMENT_ACCESS_ERROR_PARAMETER        ((int32_t)0x817D0002)
#define SCE_NP_ENTITLEMENT_ACCESS_ERROR_BUSY             ((int32_t)0x817D0003)
#define SCE_NP_ENTITLEMENT_ACCESS_ERROR_INTERNAL         ((int32_t)0x817D0004)  /* sans effet (np_rpc_invoke, retiré) */
#define SCE_NP_ENTITLEMENT_ACCESS_ERROR_NO_ENTITLEMENT   ((int32_t)0x817D0007)
#define SCE_NP_ENTITLEMENT_ACCESS_ERROR_REQUEST_NOT_FOUND ((int32_t)0x817D0015)
#define SCE_NP_ENTITLEMENT_ACCESS_ERROR_ABORTED          ((int32_t)0x817D0016)
#define SCE_NP_ENTITLEMENT_ACCESS_ERROR_USER_NOT_FOUND   ((int32_t)0x817D0017)
#define SCE_NP_ENTITLEMENT_ACCESS_POLL_RET_FINISHED 0                     /* [bin] */
#define SCE_NP_ENTITLEMENT_ACCESS_INVALID_OFFSET (-1)                     /* [bin] */
#define SCE_NP_ENTITLEMENT_ACCESS_ENTITLEMENT_INFO_LIST_MAX_SIZE 100      /* [bin] */
#define SCE_NP_ENTITLEMENT_ACCESS_ADDCONT_ENTITLEMENT_INFO_LIST_MAX_SIZE 2500  /* [bin] */
#define SCE_NP_ENTITLEMENT_ACCESS_ENTITLEMENT_KEY_SIZE 16                 /* [A] */
#define SCE_NP_ENTITLEMENT_ACCESS_TRANSACTION_ID_MAX_SIZE 65              /* [A] */
#define SCE_NP_ENTITLEMENT_ACCESS_ENTITLEMENT_TYPE_SERVICE 1              /* [bin] */
#define SCE_NP_ENTITLEMENT_ACCESS_ENTITLEMENT_TYPE_UNIFIED 2              /* [bin] */
#define SCE_NP_ENTITLEMENT_ACCESS_SORT_TYPE_NONE 0
#define SCE_NP_ENTITLEMENT_ACCESS_SORT_TYPE_ACTIVE_DATE 1                 /* [bin] */
#define SCE_NP_ENTITLEMENT_ACCESS_DIRECTION_TYPE_NONE 0
#define SCE_NP_ENTITLEMENT_ACCESS_DIRECTION_TYPE_ASC 1
#define SCE_NP_ENTITLEMENT_ACCESS_DIRECTION_TYPE_DESC 2                   /* [bin] */
/* Types de paquet [bin] : ≤ 6 puis 8 accepté, dans l'ordre de la table de noms de dlc_emu. */
#define SCE_NP_ENTITLEMENT_ACCESS_PACKAGE_TYPE_NONE 0
#define SCE_NP_ENTITLEMENT_ACCESS_PACKAGE_TYPE_PSGD 1
#define SCE_NP_ENTITLEMENT_ACCESS_PACKAGE_TYPE_PSAC 2
#define SCE_NP_ENTITLEMENT_ACCESS_PACKAGE_TYPE_PSAL 3
#define SCE_NP_ENTITLEMENT_ACCESS_PACKAGE_TYPE_PSCONS 4
#define SCE_NP_ENTITLEMENT_ACCESS_PACKAGE_TYPE_PSVC 5
#define SCE_NP_ENTITLEMENT_ACCESS_PACKAGE_TYPE_PSSUBS 6
/* États de téléchargement [bin] (installé = 4, comparé dans le montage) [d]. */
#define SCE_NP_ENTITLEMENT_ACCESS_DOWNLOAD_STATUS_NO_EXTRA_DATA 0
#define SCE_NP_ENTITLEMENT_ACCESS_DOWNLOAD_STATUS_NO_IN_QUEUE 1
#define SCE_NP_ENTITLEMENT_ACCESS_DOWNLOAD_STATUS_DOWNLOADING 2
#define SCE_NP_ENTITLEMENT_ACCESS_DOWNLOAD_STATUS_DOWNLOAD_SUSPENDED 3
#define SCE_NP_ENTITLEMENT_ACCESS_DOWNLOAD_STATUS_INSTALLED 4
#define SCE_NP_ENTITLEMENT_ACCESS_SKU_FLAG_FULL 3                         /* [A] [d] */
typedef uint32_t SceNpEntitlementAccessSkuFlag;
typedef uint32_t SceNpEntitlementAccessPackageType;
typedef uint32_t SceNpEntitlementAccessDownloadStatus;
typedef struct SceNpEntitlementAccessInitParam { char reserved[32]; } SceNpEntitlementAccessInitParam;  /* [A] */
typedef struct SceNpEntitlementAccessBootParam { char reserved[32]; } SceNpEntitlementAccessBootParam;  /* [A] */
typedef struct SceNpEntitlementAccessTransactionId { char transactionId[SCE_NP_ENTITLEMENT_ACCESS_TRANSACTION_ID_MAX_SIZE]; char padding[7]; } SceNpEntitlementAccessTransactionId;  /* 72 [A] */
typedef struct SceNpEntitlementAccessEntitlementKey { uint8_t data[SCE_NP_ENTITLEMENT_ACCESS_ENTITLEMENT_KEY_SIZE]; } SceNpEntitlementAccessEntitlementKey;  /* [A] */
typedef struct SceNpEntitlementAccessAddcontEntitlementInfo {
    SceNpUnifiedEntitlementLabel entitlementLabel; uint32_t packageType; uint32_t downloadStatus;
} SceNpEntitlementAccessAddcontEntitlementInfo;  /* 28 [A] */
/* [bin] : relevées dans PollUnifiedEntitlementInfo / PollServiceEntitlementInfo / RequestUnifiedEntitlementInfoList. */
typedef struct SceNpEntitlementAccessUnifiedEntitlementInfo {
    SceNpUnifiedEntitlementLabel entitlementLabel;  /* +0 */
    uint8_t padding0[4];
    SceRtcTick activeDate;                          /* +24 */
    SceRtcTick inactiveDate;                        /* +32 */
    uint32_t entitlementType;                       /* +40 */
    int32_t useCount;                               /* +44 */
    int32_t useLimit;                               /* +48 */
    uint32_t packageType;                           /* +52 */
    bool activeFlag;                                /* +56 */
    uint8_t reserved[7];
} SceNpEntitlementAccessUnifiedEntitlementInfo;
typedef struct SceNpEntitlementAccessServiceEntitlementInfo {
    SceNpServiceEntitlementLabel entitlementLabel;  /* +0 */
    uint8_t padding0[4];
    SceRtcTick activeDate;                          /* +24 */
    SceRtcTick inactiveDate;                        /* +32 */
    uint32_t entitlementType;                       /* +40 */
    int32_t useCount;                               /* +44 */
    int32_t useLimit;                               /* +48 */
    uint8_t reserved0[4];                           /* +52 */
    bool activeFlag;                                /* +56 */
    bool isConsumable;                              /* +57 */
    uint8_t reserved[6];
} SceNpEntitlementAccessServiceEntitlementInfo;
typedef struct SceNpEntitlementAccessRequestEntitlementInfoListParam {
    size_t size;                                    /* +0 (= 32) */
    uint32_t entitlementType;                       /* +8 */
    int32_t offset;                                 /* +12 */
    int32_t limit;                                  /* +16 */
    uint32_t sort;                                  /* +20 */
    uint32_t direction;                             /* +24 */
    uint32_t packageType;                           /* +28 */
} SceNpEntitlementAccessRequestEntitlementInfoListParam;
#ifdef __cplusplus
#include <cstddef>
static_assert(sizeof(SceNpEntitlementAccessUnifiedEntitlementInfo) == 64 && offsetof(SceNpEntitlementAccessUnifiedEntitlementInfo, activeFlag) == 56 && offsetof(SceNpEntitlementAccessUnifiedEntitlementInfo, packageType) == 52, "UnifiedEntitlementInfo [bin]");
static_assert(sizeof(SceNpEntitlementAccessServiceEntitlementInfo) == 64 && offsetof(SceNpEntitlementAccessServiceEntitlementInfo, isConsumable) == 57, "ServiceEntitlementInfo [bin]");
static_assert(sizeof(SceNpEntitlementAccessRequestEntitlementInfoListParam) == 32 && offsetof(SceNpEntitlementAccessRequestEntitlementInfoListParam, packageType) == 28, "ListParam [bin]");
static_assert(sizeof(SceNpEntitlementAccessTransactionId) == 72, "TransactionId [A]");
#endif
