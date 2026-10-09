/*
 * Iron V - Module registry (REV-33). See module.h.
 */
#include "module.h"
#include "string.h"

/* Table bounds: from ld/link.ld on the target, from the host linker for the host tests */
extern const module_t __start_iron_modules[];
extern const module_t __stop_iron_modules[];

/* Registry entries sorted by order once, on first use (the table itself is constant) */
static const module_t *s_sorted[MODULE_MAX];
static uint32_t s_count = 0U;
static uint8_t  s_sorted_ready = 0U;

static void module_sort(void)
{
    if (s_sorted_ready)
    {
        return;
    }
    uint32_t n = 0U;
    for (const module_t *m = __start_iron_modules; m < __stop_iron_modules && n < MODULE_MAX; m++)
    {
        /* Insertion sort, stable: equal orders keep link order */
        uint32_t i = n;
        while (i > 0U && s_sorted[i - 1U]->order > m->order)
        {
            s_sorted[i] = s_sorted[i - 1U];
            i--;
        }
        s_sorted[i] = m;
        n++;
    }
    s_count = n;
    s_sorted_ready = 1U;
}

uint32_t module_count(void)
{
    module_sort();
    return s_count;
}

const module_t *module_at(uint32_t index)
{
    module_sort();
    return (index < s_count) ? s_sorted[index] : NULL;
}

const module_t *module_find(const char *name)
{
    module_sort();
    for (uint32_t i = 0U; name != NULL && i < s_count; i++)
    {
        if (strcmp(s_sorted[i]->name, name) == 0)
        {
            return s_sorted[i];
        }
    }
    return NULL;
}

void modules_init(uint64_t now_us)
{
    module_sort();
    for (uint32_t i = 0U; i < s_count; i++)
    {
        if (s_sorted[i]->init != NULL)
        {
            s_sorted[i]->init(now_us);
        }
    }
}

void modules_routes(void)
{
    module_sort();
    for (uint32_t i = 0U; i < s_count; i++)
    {
        if (s_sorted[i]->routes != NULL)
        {
            s_sorted[i]->routes();
        }
    }
}

void modules_tick(uint64_t now_us)
{
    module_sort();
    for (uint32_t i = 0U; i < s_count; i++)
    {
        if (s_sorted[i]->tick != NULL)
        {
            s_sorted[i]->tick(now_us);
        }
    }
}

void modules_event(module_event_t ev, uint64_t now_us)
{
    module_sort();
    for (uint32_t i = 0U; i < s_count; i++)
    {
        if (s_sorted[i]->event != NULL)
        {
            s_sorted[i]->event(ev, now_us);
        }
    }
}

void modules_print_info(void)
{
    module_sort();
    for (uint32_t i = 0U; i < s_count; i++)
    {
        if (s_sorted[i]->print_info != NULL)
        {
            s_sorted[i]->print_info();
        }
    }
}

void modules_device_json(char *body, size_t max_len)
{
    module_sort();
    for (uint32_t i = 0U; body != NULL && i < s_count; i++)
    {
        if (s_sorted[i]->device_json != NULL)
        {
            s_sorted[i]->device_json(body, max_len);
        }
    }
}
