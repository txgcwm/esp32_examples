deps_config := \
	/srv/esp/esp-idf/components/app_trace/Kconfig \
	/srv/esp/esp-idf/components/aws_iot/Kconfig \
	/srv/esp/esp-idf/components/bt/Kconfig \
	/srv/esp/esp-idf/components/esp32/Kconfig \
	/srv/esp/esp-idf/components/ethernet/Kconfig \
	/srv/esp/esp-idf/components/fatfs/Kconfig \
	/srv/esp/esp-idf/components/freertos/Kconfig \
	/srv/esp/esp-idf/components/heap/Kconfig \
	/srv/esp/esp-idf/components/libsodium/Kconfig \
	/srv/esp/esp-idf/components/log/Kconfig \
	/srv/esp/esp-idf/components/lwip/Kconfig \
	/srv/esp/esp-idf/components/mbedtls/Kconfig \
	/srv/esp/esp-idf/components/openssl/Kconfig \
	/srv/esp/esp-idf/components/pthread/Kconfig \
	/srv/esp/esp-idf/components/spi_flash/Kconfig \
	/srv/esp/esp-idf/components/spiffs/Kconfig \
	/srv/esp/esp-idf/components/tcpip_adapter/Kconfig \
	/srv/esp/esp-idf/components/wear_levelling/Kconfig \
	/srv/esp/esp-idf/components/bootloader/Kconfig.projbuild \
	/srv/esp/esp-idf/components/esptool_py/Kconfig.projbuild \
	/srv/esp32_examples/ota/main/Kconfig.projbuild \
	/srv/esp/esp-idf/components/partition_table/Kconfig.projbuild \
	/srv/esp/esp-idf/Kconfig

include/config/auto.conf: \
	$(deps_config)


$(deps_config): ;
