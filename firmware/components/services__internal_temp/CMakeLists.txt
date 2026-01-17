idf_component_register(
    SRCS "services_internal_temp.c"
    INCLUDE_DIRS "include"
    REQUIRES 
        core__ev 
        ports 
        infrastructure__idf_wdt_port # Safety
        esp_timer                    # Timing source
)

