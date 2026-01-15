idf_component_register(
    SRCS "services_ds18b20_ev.c"
    INCLUDE_DIRS "include"
    REQUIRES 
    PRIV_REQUIRES 
        core__ev
        core__leasepool   # Do payloadu LEASE
        infrastructure__gpio_onewire # Zależność od implementacji 1-Wire
)
