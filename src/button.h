/*
 * Iron V - Setup Button (REV-15)
 *
 * The BOOT button (GPIO9 on the ESP32-C6 DevKit, active low) held for BUTTON_LONG_PRESS_US
 * asks for the setup SoftAP (review 7.2: SoftAP only by a physical button). Released before
 * BUTTON_SHORT_PRESS_MAX_US it toggles the light (REV-23); a press in between does nothing. Polled from the
 * main loop; the debounce/long-press state machine takes the pin level as an argument so it
 * can be host-tested.
 */
#ifndef IRON_V_BUTTON_H
#define IRON_V_BUTTON_H

#include <stdbool.h>
#include <stdint.h>
#include "config.h"

#define BUTTON_SETUP_GPIO                CONFIG_SETUP_BUTTON_GPIO
#define BUTTON_ACTIVE_LEVEL              0         /* pressed pulls the pin low */
#define BUTTON_DEBOUNCE_US               50000ULL  /* level must hold this long to count */
#define BUTTON_LONG_PRESS_US             3000000ULL
#define BUTTON_SHORT_PRESS_MAX_US        1000000ULL  /* released before this: short press (REV-23) */

typedef enum {
    BUTTON_EVENT_NONE = 0,
    BUTTON_EVENT_LONG_PRESS,             /* once per press, when the hold time is reached */
    BUTTON_EVENT_SHORT_PRESS             /* on release, if held less than BUTTON_SHORT_PRESS_MAX_US */
} button_event_t;

typedef struct {
    bool     raw;                        /* last sampled level (true: pressed) */
    bool     pressed;                    /* debounced state */
    bool     long_fired;                 /* long press already reported for this press */
    uint64_t raw_since_us;               /* when the raw level last changed */
    uint64_t pressed_since_us;           /* start of the current debounced press */
    uint32_t presses;                    /* debounced presses since boot */
    uint32_t long_presses;
    uint32_t short_presses;
} button_t;

void           button_reset(button_t *b);
/* One sample: pressed = pin at its active level. Returns the event this sample caused. */
button_event_t button_update(button_t *b, bool pressed, uint64_t now_us);

/* Setup button on BUTTON_SETUP_GPIO: input with pull-up, then polled each main-loop pass */
void           button_setup_init(void);
button_event_t button_setup_poll(uint64_t now_us);
const button_t *button_setup_state(void);

#endif /* IRON_V_BUTTON_H */
