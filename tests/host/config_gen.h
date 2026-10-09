/* Host tests build with the src/config.h defaults (never with the local .config) and with every
 * module, sized like the dev profile (profiles/dev.config), so all module code is covered (REV-33) */
#ifndef IRON_V_CONFIG_GEN_H
#define IRON_V_CONFIG_GEN_H
#define CONFIG_PROFILE                      "host"
#define CONFIG_MODULES                      "light mqtt ieee802154 dev"
#define CONFIG_MODULE_LIGHT                 1U
#define CONFIG_MODULE_MQTT                  1U
#define CONFIG_MODULE_IEEE802154            1U
#define CONFIG_MODULE_DEV                   1U
#define CONFIG_POOL_SMALL_BLOCKS            32U
#define CONFIG_POOL_MEDIUM_BLOCKS           16U
#endif
