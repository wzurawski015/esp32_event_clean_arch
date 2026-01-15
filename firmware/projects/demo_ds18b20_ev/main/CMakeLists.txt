idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        core__leasepool
        core__ev
        ports
        services__timer
        services__ds18b20_ev
        infrastructure__gpio_onewire
)
