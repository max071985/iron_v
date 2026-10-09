# Light (REV-23, REV-25): the DevKitC-1's RGB LED (WS2812 on GPIO8), its page at / (index.html,
# gzip in flash), REST v1 /api/v1/light, the `light` command and the BOOT short press.
MODULE_SRCS_light := src/modules/light/light.c src/modules/light/rgb_led.c src/modules/light/light_module.c
MODULE_REQUIRES_light :=
