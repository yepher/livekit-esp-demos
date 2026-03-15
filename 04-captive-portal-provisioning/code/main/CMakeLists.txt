idf_component_register(SRCS "main.c" "board.c" "media.c" "jwt_generator.c"
                      INCLUDE_DIRS "."
                      REQUIRES captive_portal
                      PRIV_REQUIRES mbedtls json tempotian__media_lib_sal)
