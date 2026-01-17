idf_component_register(
    SRCS "services_ds18b20_ev.c"
    INCLUDE_DIRS "include"
    REQUIRES 
    PRIV_REQUIRES 
        core__ev
        core__leasepool               # Payload LEASE
        infrastructure__gpio_onewire  # 1-Wire bit-banging
        ports                         # Interfejsy
        esp_timer                     # Wymagane przez esp_timer_create
        infrastructure__idf_wdt_port  # <--- NOWOŚĆ: Watchdog
)

