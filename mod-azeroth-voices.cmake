# Azeroth Voices uses the core's vendored cpp-httplib in HTTPS mode. The final
# mangosd already links OpenSSL, but a dynamically linked module needs the same
# dependency explicitly. Applying this to both possible targets also makes the
# static relationship visible and avoids platform-specific transitive-linking
# surprises.
if(TORTOISE_MODULE_CMAKE_PHASE STREQUAL "POST_TARGETS")
  foreach(AV_TARGET modules mod_mod_azeroth_voices)
    if(TARGET ${AV_TARGET})
      target_include_directories(${AV_TARGET} PRIVATE
        ${OPENSSL_INCLUDE_DIR}
        ${CMAKE_SOURCE_DIR}/src/game/MapNodes)
      if(WIN32)
        # VMangos' bundled OpenSSL headers live below this include root and
        # cpp-httplib includes them as <openssl/...>.
        target_include_directories(${AV_TARGET} PRIVATE
          ${CMAKE_SOURCE_DIR}/dep/include-windows)
      endif()
      target_link_libraries(${AV_TARGET} PUBLIC ${OPENSSL_LIBRARIES})
    endif()
  endforeach()
endif()
