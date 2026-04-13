# Lua API (регистрируемые функции):
-  orbita.set_isd_channel(channel, value) – HTTP команда имитатору (IP из конфига).

-  orbita.set_power_supply(volt, curr) – UDP команда источнику.

- orbita.read_voltmeter() – возвращает напряжение через VISA.

- orbita.get_channel_value(address_string) – получить текущее значение по адресу.

- orbita.sleep_ms(ms) – задержка.

- orbita.log(message) – вывод в лог
