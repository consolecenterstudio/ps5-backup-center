#pragma once
#include <stddef.h>
#include <stdint.h>
#define SCE_OK 0
typedef int32_t SceUserServiceUserId;
#define SCE_USER_SERVICE_USER_ID_INVALID (-1)  /* [A] */
typedef uint32_t SceNpServiceLabel;
#define SCE_NP_UNIFIED_ENTITLEMENT_LABEL_SIZE 17  /* [A] */
#define SCE_NP_SERVICE_ENTITLEMENT_LABEL_SIZE 7   /* [A] */
typedef struct SceNpUnifiedEntitlementLabel { char data[SCE_NP_UNIFIED_ENTITLEMENT_LABEL_SIZE]; char padding[3]; } SceNpUnifiedEntitlementLabel;  /* 20 [A] */
typedef struct SceNpServiceEntitlementLabel { char data[SCE_NP_SERVICE_ENTITLEMENT_LABEL_SIZE]; char padding[13]; } SceNpServiceEntitlementLabel;  /* 20 [A] */
typedef struct SceRtcTick { uint64_t tick; } SceRtcTick;
#ifdef __cplusplus
static_assert(sizeof(SceNpUnifiedEntitlementLabel) == 20 && sizeof(SceNpServiceEntitlementLabel) == 20, "étiquettes");
#endif
