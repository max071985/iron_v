/*
 * Iron V - Setup Button (REV-15). See button.h.
 */
#include "button.h"
#include "gpio.h"
#include "string.h"

static button_t s_setup_button;

void button_reset(button_t *b)
{
    if (b != NULL)
    {
        memset(b, 0, sizeof(*b));
    }
}

button_event_t button_update(button_t *b, bool pressed, uint64_t now_us)
{
    if (b == NULL)
    {
        return BUTTON_EVENT_NONE;
    }
    if (pressed != b->raw)
    {
        b->raw = pressed;
        b->raw_since_us = now_us;
        return BUTTON_EVENT_NONE;
    }
    if (b->pressed != b->raw && (now_us - b->raw_since_us) >= BUTTON_DEBOUNCE_US)
    {
        b->pressed = b->raw;
        if (b->pressed)
        {
            b->pressed_since_us = b->raw_since_us;
            b->long_fired = false;
            b->presses++;
        }
    }
    if (b->pressed && !b->long_fired && (now_us - b->pressed_since_us) >= BUTTON_LONG_PRESS_US)
    {
        b->long_fired = true;
        b->long_presses++;
        return BUTTON_EVENT_LONG_PRESS;
    }
    return BUTTON_EVENT_NONE;
}

void button_setup_init(void)
{
    button_reset(&s_setup_button);
    (void)gpio_set_direction(BUTTON_SETUP_GPIO, GPIO_DIR_INPUT);
    (void)gpio_set_pull(BUTTON_SETUP_GPIO, GPIO_PULL_UP);
}

button_event_t button_setup_poll(uint64_t now_us)
{
    bool pressed = (gpio_get_level(BUTTON_SETUP_GPIO) == BUTTON_ACTIVE_LEVEL);
    return button_update(&s_setup_button, pressed, now_us);
}

const button_t *button_setup_state(void)
{
    return &s_setup_button;
}
