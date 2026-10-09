/*
 * Iron V - Module registry (REV-33)
 *
 * Iron-V is one core plus modules chosen per application at build time (profiles/<name>.config,
 * MODULES=...). A module lives in src/modules/<name>/ and is only compiled and linked when the
 * profile selects it, so an unselected module costs no RAM and no flash.
 *
 * Modules register themselves: MODULE_DEFINE() places a descriptor in a linker-section table
 * and SHELL_COMMAND_DEFINE() (shell.h) does the same for console commands. The core never names
 * a module; it walks the tables. Hooks are optional (NULL = not used) and run on the main loop.
 *
 *   init         once at boot, after the core is up and before the boot join
 *   routes       registers HTTP routes; called by http_server_init(), so routes survive a re-init
 *   tick         every main-loop iteration (timed as one loop client, "modules")
 *   event        core events, e.g. the BOOT button short press
 *   print_info   lines for the shell `info` command
 *   device_json  fields appended to GET /api/v1/device (",\"key\":..." without the closing brace)
 */
#ifndef IRON_V_MODULE_H
#define IRON_V_MODULE_H

#include <stddef.h>
#include <stdint.h>

/* Section names must be C identifiers: the host linker then defines __start_/__stop_ itself */
#define MODULE_SECTION                   "iron_modules"
#define MODULE_MAX                       16U   /* registry entries walked in order */

typedef enum {
    MODULE_EVENT_BUTTON_SHORT_PRESS = 0,       /* BOOT button released before the long-press time */
    MODULE_EVENT_COUNT
} module_event_t;

typedef struct {
    const char *name;                          /* the module directory name */
    uint32_t    order;                         /* init/tick order, lower first (ties: link order) */
    void (*init)(uint64_t now_us);
    void (*routes)(void);
    void (*tick)(uint64_t now_us);
    void (*event)(module_event_t ev, uint64_t now_us);
    void (*print_info)(void);
    void (*device_json)(char *body, size_t max_len);
} module_t;

/* Init/tick order of the modules shipped with Iron-V (a module that uses another one comes later) */
#define MODULE_ORDER_LIGHT               10U
#define MODULE_ORDER_MQTT                20U   /* publishes the light's state */
#define MODULE_ORDER_IEEE802154          30U
#define MODULE_ORDER_DEV                 90U

#define MODULE_DEFINE(ident, ...) \
    static const module_t ident __attribute__((used, section(MODULE_SECTION), aligned(sizeof(void *)))) = __VA_ARGS__

/* Registry walk in init/tick order */
uint32_t module_count(void);
const module_t *module_at(uint32_t index);
const module_t *module_find(const char *name);

void modules_init(uint64_t now_us);
void modules_routes(void);
void modules_tick(uint64_t now_us);
void modules_event(module_event_t ev, uint64_t now_us);
void modules_print_info(void);
void modules_device_json(char *body, size_t max_len);

#endif /* IRON_V_MODULE_H */
