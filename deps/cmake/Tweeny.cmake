set(WINDOWS_FLAGS "")

if(MSVC)
    set(WINDOWS_FLAGS "-DCMAKE_GENERATOR_PLATFORM=x64")
endif()

ExternalProject_Add(
  Tweeny

  URL ${TWEENY_URL}
  URL_HASH SHA256=${TWEENY_HASH}

  BUILD_IN_SOURCE 1
  SOURCE_DIR ${DEPS_BUILD_DIR}/tweeny
  CONFIGURE_COMMAND ${CMAKE_COMMAND}
        -DCMAKE_INSTALL_PREFIX=${DEPS_INSTALL_DIR}
        -DTWEENY_BUILD_EXAMPLES=OFF
        -DTWEENY_BUILD_DOCUMENTATION=OFF
        ${DEPS_BUILD_DIR}/tweeny
        ${WINDOWS_FLAGS})

list(APPEND THIRD_PARTY_DEPS Tweeny)
