# CPack pre-build hook: code-sign the staged executable before it is archived (#419).
#
# CPack strips binaries while staging (CPACK_STRIP_FILES), which would invalidate a
# signature applied to the build tree, so signing happens here, on the stripped copy that
# actually ships. Signing is opt-in through environment variables set by release.yml when
# the signing secrets exist; without them this script only reports an unsigned package.
#
#   macOS:   IKORE_CODESIGN_IDENTITY  - codesign identity in the unlocked build keychain
#   Windows: IKORE_SIGNTOOL           - path to signtool.exe
#            IKORE_SIGN_PFX           - path to the decoded .pfx certificate
#            IKORE_SIGN_PFX_PASSWORD  - its password
#            IKORE_SIGN_TIMESTAMP_URL - optional RFC 3161 server (default DigiCert)

set(_stage "${CPACK_TEMPORARY_DIRECTORY}")
if(NOT _stage)
    set(_stage "${CPACK_TEMPORARY_INSTALL_DIRECTORY}")
endif()
file(GLOB_RECURSE _candidates "${_stage}/*/IKore" "${_stage}/*/IKore.exe")
if(NOT _candidates)
    message(FATAL_ERROR "SignPackage: no staged IKore executable under ${_stage}")
endif()

foreach(_bin IN LISTS _candidates)
    if(_bin MATCHES "\\.exe$" AND DEFINED ENV{IKORE_SIGNTOOL} AND DEFINED ENV{IKORE_SIGN_PFX})
        set(_ts "$ENV{IKORE_SIGN_TIMESTAMP_URL}")
        if(NOT _ts)
            set(_ts "http://timestamp.digicert.com")
        endif()
        message(STATUS "SignPackage: signing ${_bin} with signtool")
        execute_process(
            COMMAND "$ENV{IKORE_SIGNTOOL}" sign /fd SHA256 /f "$ENV{IKORE_SIGN_PFX}"
                    /p "$ENV{IKORE_SIGN_PFX_PASSWORD}" /tr "${_ts}" /td SHA256 "${_bin}"
            RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "SignPackage: signtool sign failed (${_rc})")
        endif()
        execute_process(COMMAND "$ENV{IKORE_SIGNTOOL}" verify /pa /v "${_bin}" RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "SignPackage: signtool verify failed (${_rc})")
        endif()
    elseif(NOT _bin MATCHES "\\.exe$" AND APPLE AND DEFINED ENV{IKORE_CODESIGN_IDENTITY})
        message(STATUS "SignPackage: signing ${_bin} with codesign (hardened runtime)")
        execute_process(
            COMMAND codesign --force --timestamp --options runtime
                    --sign "$ENV{IKORE_CODESIGN_IDENTITY}" "${_bin}"
            RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "SignPackage: codesign failed (${_rc})")
        endif()
        execute_process(COMMAND codesign --verify --strict --verbose=2 "${_bin}" RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "SignPackage: codesign verification failed (${_rc})")
        endif()
    else()
        message(STATUS "SignPackage: no signing credentials for ${_bin}; packaging unsigned")
    endif()
endforeach()
