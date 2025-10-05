idf_component_register(
    SRCS "app_log_bus.c"
    INCLUDE_DIRS "include"
    REQUIRES
        core__ev
        core__leasepool
        ports
        log
)
