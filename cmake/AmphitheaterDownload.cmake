# amphitheater_download(<url> <file>): a URL's bytes into a file, a transient failure tried again. CMake's own
# download, which FetchContent uses, tries a failed connection again but an HTTP error only once; this tries again on an
# HTTP 429 or 5xx, or a connection that failed, timed out, was reset or ended part way (curl's errors 7, 18, 28, 35, 52,
# 55, 56 and 92), six tries in all after the waits in AMPHITHEATER_DOWNLOAD_WAITS (4, 8, 16, 32 and 60 seconds), and says
# so on each. Any other failure, or the last, stops the configure with nothing left behind. It checks no hash: what
# fetches the file holds it to its pin.

if(NOT DEFINED AMPHITHEATER_DOWNLOAD_WAITS)
    set(AMPHITHEATER_DOWNLOAD_WAITS 4 8 16 32 60)
endif()

function(amphitheater_download url file)
    set(transient_curl_errors 7 18 28 35 52 55 56 92)
    foreach(wait IN LISTS AMPHITHEATER_DOWNLOAD_WAITS ITEMS 0)
        file(DOWNLOAD "${url}" "${file}" STATUS status LOG log TIMEOUT 300 INACTIVITY_TIMEOUT 60)
        list(GET status 0 code)
        if(code EQUAL 0)
            return()
        endif()
        set(http "")
        if(log MATCHES "returned error: ([0-9]+)")
            set(http "${CMAKE_MATCH_1}")
        endif()
        if(wait EQUAL 0 OR NOT (http MATCHES "^(5..|429)$" OR code IN_LIST transient_curl_errors))
            break()
        endif()
        message(STATUS "${url}: ${status} ${http}; trying again in ${wait} s")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep ${wait})
    endforeach()
    file(REMOVE "${file}")
    message(FATAL_ERROR "${url} could not be downloaded: ${status} ${http}")
endfunction()
