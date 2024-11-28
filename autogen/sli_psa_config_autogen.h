// This is an autogenerated config file, any changes to this file will be overwritten

#ifndef SLI_PSA_CONFIG_AUTOGEN_H
#define SLI_PSA_CONFIG_AUTOGEN_H

#define PSA_WANT_ALG_CTR 1
#define PSA_WANT_ALG_ECB_NO_PADDING 1
#define PSA_WANT_KEY_TYPE_AES 1

#define MBEDTLS_PSA_KEY_SLOT_COUNT ( + SL_PSA_KEY_USER_SLOT_COUNT)
#ifndef SL_PSA_ITS_MAX_FILES
#define SL_PSA_ITS_MAX_FILES ( + SL_PSA_ITS_USER_MAX_FILES)
#endif

#endif // SLI_PSA_CONFIG_AUTOGEN_H
