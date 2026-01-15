idf_component_register(
    SRCS "test_ev_post_lease.c"
         "test_ev_qos_replace_last.c"
    PRIV_REQUIRES unity core__ev core__leasepool
)
