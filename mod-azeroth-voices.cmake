# Azeroth Voices uses the core's vendored cpp-httplib in HTTPS mode. The final
# mangosd already links OpenSSL, but a dynamically linked module needs the same
# dependency explicitly. Applying this to both possible targets also makes the
# static relationship visible and avoids platform-specific transitive-linking
# surprises.
if(TORTOISE_MODULE_CMAKE_PHASE STREQUAL "POST_TARGETS")
  # Compile against exactly the OpenSSL that mangosd links.
  #
  # The Windows dependency set ships a bundled OpenSSL 1.1 header tree under
  # dep/include-windows and dep/windows/include, and sibling static modules
  # expose the latter as a public include directory. In the generated project it
  # sorts ahead of OPENSSL_INCLUDE_DIR, so <openssl/...> resolved to the 1.1
  # headers while the worldserver linked a vcpkg OpenSSL 3: the vendored
  # cpp-httplib then mapped SSL_get1_peer_certificate back to the 1.1-only
  # SSL_get_peer_certificate name and bin/Release/mangosd.exe failed with
  # LNK2001. Strip only those two roots from this archive's own include path;
  # OPENSSL_INCLUDE_DIR added below is the authoritative header set.
  function(av_strip_bundled_openssl_include_roots target)
    if(TARGET ${target})
      get_target_property(_av_target_includes ${target} INCLUDE_DIRECTORIES)
      if(_av_target_includes)
        list(FILTER _av_target_includes EXCLUDE REGEX
          "[/\\\\]dep[/\\\\](windows[/\\\\]include|include-windows)$")
        set_target_properties(${target} PROPERTIES INCLUDE_DIRECTORIES "${_av_target_includes}")
      endif()
    endif()
  endfunction()

  if(WIN32)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.19)
      # Sibling module CMake files are included after this one and re-add the
      # bundled roots, so defer the strip until the modules directory is fully
      # processed.
      cmake_language(DEFER CALL av_strip_bundled_openssl_include_roots modules)
    else()
      av_strip_bundled_openssl_include_roots(modules)
    endif()
  endif()

  foreach(AV_TARGET modules mod_mod_azeroth_voices)
    if(TARGET ${AV_TARGET})
      target_include_directories(${AV_TARGET} PRIVATE
        ${OPENSSL_INCLUDE_DIR}
        ${CMAKE_SOURCE_DIR}/src/game/MapNodes)
      target_link_libraries(${AV_TARGET} PUBLIC ${OPENSSL_LIBRARIES})
    endif()
  endforeach()

  install(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/data/"
    DESTINATION "modules/mod-azeroth-voices/data")
  # The native Turtle WoW AzerothVoices addon is shipped so an operator can
  # copy client/AzerothVoices into the client's Interface/AddOns directory.
  # The module answers both native .avaddon frames and legacy .llmc requests.
  install(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/client/"
    DESTINATION "modules/mod-azeroth-voices/client")
  # Volume presets are deliberately stored outside conf/ so the module system
  # never auto-loads them. Operators copy the settings they want into the live
  # mod-azeroth-voices.conf.
  install(FILES "${CMAKE_CURRENT_LIST_DIR}/conf/presets/mod-azeroth-voices-quieter.conf.dist"
    DESTINATION "modules/mod-azeroth-voices/conf/presets")
  install(FILES "${CMAKE_CURRENT_LIST_DIR}/README.md"
    DESTINATION "modules/mod-azeroth-voices")

  # Focused, engine-free unit tests. BUILD_TESTING is honoured when the core
  # enables CTest; AZEROTH_VOICES_BUILD_TESTS lets a module-only build opt in
  # without changing the core.
  option(AZEROTH_VOICES_BUILD_TESTS "Build the mod-azeroth-voices unit tests." ON)
  if(BUILD_TESTING OR AZEROTH_VOICES_BUILD_TESTS)
    enable_testing()
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.19)
      # The core calls enable_testing() only when its own test switch is on, so
      # `ctest --test-dir <build>` at the build root would otherwise see no
      # tests. Turning it on for the top-level directory keeps both
      # `ctest --test-dir <build>` and `ctest --test-dir <build>/modules`
      # working without touching the core.
      cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL enable_testing)
    endif()
    add_executable(azeroth_voices_tests
      "${CMAKE_CURRENT_LIST_DIR}/tests/AzerothVoicesTests.cpp"
      "${CMAKE_CURRENT_LIST_DIR}/src/AzerothVoicesAddon.cpp"
      "${CMAKE_CURRENT_LIST_DIR}/src/AzerothVoicesBossDialogue.cpp"
      "${CMAKE_CURRENT_LIST_DIR}/src/AzerothVoicesBossRegistry.cpp"
      "${CMAKE_CURRENT_LIST_DIR}/src/AzerothVoicesInstanceLore.cpp"
      "${CMAKE_CURRENT_LIST_DIR}/src/AzerothVoicesMemory.cpp"
      "${CMAKE_CURRENT_LIST_DIR}/src/AzerothVoicesPacing.cpp"
      "${CMAKE_CURRENT_LIST_DIR}/src/AzerothVoicesPartyGate.cpp"
      "${CMAKE_CURRENT_LIST_DIR}/src/AzerothVoicesPersonality.cpp"
      "${CMAKE_CURRENT_LIST_DIR}/src/AzerothVoicesProximity.cpp"
      "${CMAKE_CURRENT_LIST_DIR}/src/AzerothVoicesReasoning.cpp"
      "${CMAKE_CURRENT_LIST_DIR}/src/AzerothVoicesSocial.cpp")
    target_include_directories(azeroth_voices_tests PRIVATE
      "${CMAKE_CURRENT_LIST_DIR}/src"
      "${CMAKE_SOURCE_DIR}/src/shared")
    set_target_properties(azeroth_voices_tests PROPERTIES FOLDER "modules")
    add_test(NAME azeroth_voices_tests COMMAND azeroth_voices_tests)
    set_property(TARGET azeroth_voices_tests PROPERTY CXX_STANDARD 17)
  endif()
endif()
