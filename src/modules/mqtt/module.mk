# MQTT (REV-23): MQTT 3.1.1 client to the bridge's broker with Home Assistant discovery for the
# light, REST v1 /api/v1/mqtt, the `mqtt` command. Optional, off by default: see README.md here.
MODULE_SRCS_mqtt := src/modules/mqtt/mqtt.c src/modules/mqtt/mqtt_module.c
MODULE_REQUIRES_mqtt := light
