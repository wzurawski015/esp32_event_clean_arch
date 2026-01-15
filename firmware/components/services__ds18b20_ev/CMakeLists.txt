idf_component_register(
    SRCS "services_ds18b20_ev.c"
    INCLUDE_DIRS "include"
    
    # Publiczne API jest niezależne od IDF (czyste C), więc REQUIRES jest puste.
    REQUIRES 

    # Prywatne detale implementacji (ukryte przed resztą systemu):
    PRIV_REQUIRES 
        core__ev          # Do publikowania zdarzeń
        esp_driver_gpio   # Do sterowania pinem 1-Wire
        esp_timer         # Do odmierzania czasu
        esp_rom           # Do precyzyjnych opóźnień (esp_rom_delay_us)
)

