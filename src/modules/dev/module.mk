# Developer tools for the test build (PROFILE=dev): do-test and soak (test.c, on-target only),
# LAN speed test, stability soak engine, diagnostic routes /api/telemetry|health|speedtest.
MODULE_SRCS_dev := src/modules/dev/test.c src/modules/dev/speedtest.c src/modules/dev/soak.c src/modules/dev/dev_module.c
MODULE_TARGET_ONLY_dev := src/modules/dev/test.c
MODULE_REQUIRES_dev := light   # do-test checks the light page and REST v1
