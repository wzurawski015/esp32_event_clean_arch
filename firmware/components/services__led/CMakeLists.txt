idf_component_register(
    SRCS "services_led.c"
    INCLUDE_DIRS "include"
    REQUIRES
        core__ev
        ports                          # Dostęp do led_strip_port.h, wdt_port.h, log_port.h
        infrastructure__idf_wdt_port   # Implementacja Watchdoga (pośrednio przez linkowanie, ale warto zaznaczyć)
    PRIV_REQUIRES
        freertos
)

