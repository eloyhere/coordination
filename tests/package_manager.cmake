# End-to-end test for the package manager.
#
# Required cache variables:
#   COORC - path to the coorc executable
#   WORK  - scratch directory
# Optional:
#   BUILD - when set, also build and run a program that uses the installed
#           package (requires a host C++ compiler).

if(NOT DEFINED COORC OR NOT DEFINED WORK)
  message(FATAL_ERROR "package_manager.cmake requires COORC and WORK")
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY
  "${WORK}/pkg/src"
  "${WORK}/registry/index"
  "${WORK}/registry/packages/math"
  "${WORK}/registry/packages/unverified"
  "${WORK}/unverified/src"
  "${WORK}/app/src"
  "${WORK}/trusted/src"
  "${WORK}/insecure/src"
  "${WORK}/cache")

set(REGISTRY "${WORK}/registry")
if(WIN32)
  file(TO_CMAKE_PATH "${REGISTRY}" REGISTRY)
endif()

file(WRITE "${WORK}/pkg/coordination.toml" [=[
[package]
name = "math"
version = "1.2.0"
edition = "2026"
source = "src"
description = "math helpers"
]=])
file(WRITE "${WORK}/pkg/src/math.coo" [=[
export integer twice(integer value) {
    return value * 2;
}
]=])

execute_process(
  COMMAND "${COORC}" pack --manifest "${WORK}/pkg/coordination.toml"
          -o "${WORK}/registry/packages/math/1.2.0.tar.gz"
  RESULT_VARIABLE pack_result
  OUTPUT_VARIABLE pack_out
  ERROR_VARIABLE pack_err)
if(NOT pack_result EQUAL 0)
  message(FATAL_ERROR "coorc pack failed\n${pack_out}\n${pack_err}")
endif()
if(NOT EXISTS "${WORK}/registry/packages/math/1.2.0.tar.gz")
  message(FATAL_ERROR "pack did not create the archive")
endif()
file(SHA256 "${WORK}/registry/packages/math/1.2.0.tar.gz" MATH_HASH)

file(WRITE "${WORK}/registry/index/math.toml" [=[
name = "math"
description = "math helpers"
[[version]]
version = "1.2.0"
checksum = "sha256:@MATH_HASH@"
dependencies = []
]=])

# An index entry without a checksum, used by the require_checksum policy test.
file(WRITE "${WORK}/unverified/coordination.toml" [=[
[package]
name = "unverified"
version = "1.0.0"
edition = "2026"
source = "src"
description = "package without an index checksum"
]=])
file(WRITE "${WORK}/unverified/src/lib.coo" [=[
export integer value() {
    return 1;
}
]=])
execute_process(
  COMMAND "${COORC}" pack --manifest "${WORK}/unverified/coordination.toml"
          -o "${WORK}/registry/packages/unverified/1.0.0.tar.gz"
  RESULT_VARIABLE unverified_pack_result
  ERROR_VARIABLE unverified_pack_err)
if(NOT unverified_pack_result EQUAL 0)
  message(FATAL_ERROR "packing unverified failed\n${unverified_pack_err}")
endif()
file(WRITE "${WORK}/registry/index/unverified.toml" [=[
name = "unverified"
description = "package without an index checksum"
[[version]]
version = "1.0.0"
dependencies = []
]=])

file(WRITE "${WORK}/registry/index.toml" [=[
[[package]]
name = "math"
description = "math helpers"
latest = "1.2.0"

[[package]]
name = "unverified"
description = "package without an index checksum"
latest = "1.0.0"
]=])

file(WRITE "${WORK}/app/coordination.toml" [=[
[package]
name = "app"
version = "0.1.0"
edition = "2026"
source = "src"

[dependencies]
math = "^1.0"

[registries]
official = "file://@REGISTRY@"

[[mirrors]]
name = "broken"
registry = "official"
url = "file://@WORK@/missing-registry"
priority = 100

[[mirrors]]
name = "local"
registry = "official"
url = "file://@REGISTRY@"
priority = 10
]=])
file(WRITE "${WORK}/app/src/main.coo" [=[
integer main() {
    print(twice(21));
    return 0;
}
]=])

function(fix_manifest path)
  file(READ "${path}" content)
  string(REPLACE "@MATH_HASH@" "${MATH_HASH}" content "${content}")
  string(REPLACE "@REGISTRY@" "${REGISTRY}" content "${content}")
  string(REPLACE "@WORK@" "${WORK}" content "${content}")
  file(WRITE "${path}" "${content}")
endfunction()
fix_manifest("${WORK}/registry/index/math.toml")
fix_manifest("${WORK}/app/coordination.toml")

set(ENV_RUN "${CMAKE_COMMAND}" -E env
    "COORD_CACHE_DIR=${WORK}/cache"
    "COORD_CONFIG=${WORK}/config.toml")

execute_process(
  COMMAND ${ENV_RUN} "${COORC}" install --manifest "${WORK}/app/coordination.toml"
  RESULT_VARIABLE install_result
  OUTPUT_VARIABLE install_out
  ERROR_VARIABLE install_err)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "install failed\n${install_out}\n${install_err}")
endif()
if(NOT install_out MATCHES "installed 1 package")
  message(FATAL_ERROR "unexpected install output:\n${install_out}")
endif()
foreach(required
    "${WORK}/app/coord.lock"
    "${WORK}/app/coord_modules/math/coordination.toml"
    "${WORK}/app/coord_modules/math/.coord-package.toml"
    "${WORK}/app/coord_modules/math/src/math.coo")
  if(NOT EXISTS "${required}")
    message(FATAL_ERROR "install did not create ${required}")
  endif()
endforeach()

execute_process(
  COMMAND ${ENV_RUN} "${COORC}" list --manifest "${WORK}/app/coordination.toml"
  RESULT_VARIABLE list_result
  OUTPUT_VARIABLE list_out
  ERROR_VARIABLE list_err)
if(NOT list_result EQUAL 0 OR NOT list_out MATCHES "math[ \t]+1.2.0")
  message(FATAL_ERROR "coorc list failed\n${list_out}\n${list_err}")
endif()

execute_process(
  COMMAND ${ENV_RUN} "${COORC}" tree --manifest "${WORK}/app/coordination.toml"
  RESULT_VARIABLE tree_result
  OUTPUT_VARIABLE tree_out
  ERROR_VARIABLE tree_err)
if(NOT tree_result EQUAL 0 OR NOT tree_out MATCHES "math 1.2.0")
  message(FATAL_ERROR "coorc tree failed\n${tree_out}\n${tree_err}")
endif()

execute_process(
  COMMAND ${ENV_RUN} "${COORC}" install --locked
          --manifest "${WORK}/app/coordination.toml"
  RESULT_VARIABLE locked_result
  OUTPUT_VARIABLE locked_out
  ERROR_VARIABLE locked_err)
if(NOT locked_result EQUAL 0)
  message(FATAL_ERROR "--locked install failed\n${locked_out}\n${locked_err}")
endif()

# Offline install must work from the cache after the vendor tree disappears.
file(REMOVE_RECURSE "${WORK}/app/coord_modules")
execute_process(
  COMMAND ${ENV_RUN} "${COORC}" install --offline
          --manifest "${WORK}/app/coordination.toml"
  RESULT_VARIABLE offline_result
  OUTPUT_VARIABLE offline_out
  ERROR_VARIABLE offline_err)
if(NOT offline_result EQUAL 0)
  message(FATAL_ERROR "offline install failed\n${offline_out}\n${offline_err}")
endif()
if(NOT EXISTS "${WORK}/app/coord_modules/math/coordination.toml")
  message(FATAL_ERROR "offline install did not restore the vendor tree")
endif()

# Missing packages are reported as resolution failures.
file(MAKE_DIRECTORY "${WORK}/missing/src")
file(WRITE "${WORK}/missing/coordination.toml" [=[
[package]
name = "missing"
version = "0.1.0"
source = "src"

[dependencies]
not-a-real-package = "^1.0"

[registries]
official = "file://@REGISTRY@"
]=])
fix_manifest("${WORK}/missing/coordination.toml")
file(WRITE "${WORK}/missing/src/main.coo" "integer main() { return 0; }\n")
execute_process(
  COMMAND ${ENV_RUN} "${COORC}" install --offline
          --manifest "${WORK}/missing/coordination.toml"
  RESULT_VARIABLE missing_result
  OUTPUT_VARIABLE missing_out
  ERROR_VARIABLE missing_err)
if(missing_result EQUAL 0)
  message(FATAL_ERROR "install of an unknown package should fail")
endif()
if(NOT missing_err MATCHES "not-a-real-package")
  message(FATAL_ERROR "missing package error is unclear:\n${missing_err}")
endif()

# A checksum mismatch must abort the install.
file(COPY "${WORK}/registry/" DESTINATION "${WORK}/registry-bad")
file(WRITE "${WORK}/registry-bad/index/math.toml" [=[
name = "math"
[[version]]
version = "1.2.0"
checksum = "sha256:0000000000000000000000000000000000000000000000000000000000000000"
dependencies = []
]=])
file(WRITE "${WORK}/badapp/coordination.toml" [=[
[package]
name = "badapp"
version = "0.1.0"
source = "src"

[dependencies]
math = "^1.0"

[registries]
official = "file://@WORK@/registry-bad"

[[mirrors]]
name = "local"
registry = "official"
url = "file://@WORK@/registry-bad"
priority = 10
]=])
fix_manifest("${WORK}/badapp/coordination.toml")
file(WRITE "${WORK}/badapp/src/main.coo" "integer main() { return 0; }\n")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "COORD_CACHE_DIR=${WORK}/cache-bad"
          "COORD_CONFIG=${WORK}/config.toml"
          "${COORC}" install --manifest "${WORK}/badapp/coordination.toml"
  RESULT_VARIABLE bad_result
  OUTPUT_VARIABLE bad_out
  ERROR_VARIABLE bad_err)
if(bad_result EQUAL 0)
  message(FATAL_ERROR "checksum mismatch should fail the install")
endif()
if(NOT bad_err MATCHES "checksum mismatch")
  message(FATAL_ERROR "checksum error is unclear:\n${bad_out}\n${bad_err}")
endif()

# Mirror configuration commands.
execute_process(
  COMMAND ${ENV_RUN} "${COORC}" mirror add local
          "file://${REGISTRY}" --registry official --priority 20
  RESULT_VARIABLE mirror_add_result
  OUTPUT_VARIABLE mirror_add_out
  ERROR_VARIABLE mirror_add_err)
if(NOT mirror_add_result EQUAL 0)
  message(FATAL_ERROR "mirror add failed\n${mirror_add_out}\n${mirror_add_err}")
endif()
execute_process(
  COMMAND ${ENV_RUN} "${COORC}" mirror use local
  RESULT_VARIABLE mirror_use_result
  ERROR_VARIABLE mirror_use_err)
if(NOT mirror_use_result EQUAL 0)
  message(FATAL_ERROR "mirror use failed\n${mirror_use_err}")
endif()
execute_process(
  COMMAND ${ENV_RUN} "${COORC}" mirror list
  RESULT_VARIABLE mirror_list_result
  OUTPUT_VARIABLE mirror_list_out
  ERROR_VARIABLE mirror_list_err)
if(NOT mirror_list_result EQUAL 0 OR NOT mirror_list_out MATCHES "\\* local")
  message(FATAL_ERROR "mirror list failed\n${mirror_list_out}\n${mirror_list_err}")
endif()
execute_process(
  COMMAND ${ENV_RUN} "${COORC}" mirror test local
  RESULT_VARIABLE mirror_test_result
  OUTPUT_VARIABLE mirror_test_out
  ERROR_VARIABLE mirror_test_err)
if(NOT mirror_test_result EQUAL 0 OR NOT mirror_test_out MATCHES "ok")
  message(FATAL_ERROR "mirror test failed\n${mirror_test_out}\n${mirror_test_err}")
endif()
execute_process(
  COMMAND ${ENV_RUN} "${COORC}" config set mirror.timeout-ms 5000
  RESULT_VARIABLE config_set_result
  ERROR_VARIABLE config_set_err)
if(NOT config_set_result EQUAL 0)
  message(FATAL_ERROR "config set failed\n${config_set_err}")
endif()
execute_process(
  COMMAND ${ENV_RUN} "${COORC}" config get mirror.timeout-ms
  RESULT_VARIABLE config_get_result
  OUTPUT_VARIABLE config_get_out
  ERROR_VARIABLE config_get_err)
string(STRIP "${config_get_out}" config_get_out)
if(NOT config_get_result EQUAL 0 OR NOT config_get_out STREQUAL "5000")
  message(FATAL_ERROR "config get failed: '${config_get_out}' ${config_get_err}")
endif()

# Project scaffolding.
execute_process(
  COMMAND ${ENV_RUN} "${COORC}" init "${WORK}/initpkg" --name initdemo
  RESULT_VARIABLE init_result
  OUTPUT_VARIABLE init_out
  ERROR_VARIABLE init_err)
if(NOT init_result EQUAL 0)
  message(FATAL_ERROR "coorc init failed\n${init_out}\n${init_err}")
endif()
foreach(required
    "${WORK}/initpkg/coordination.toml"
    "${WORK}/initpkg/src/main.coo"
    "${WORK}/initpkg/.gitignore")
  if(NOT EXISTS "${required}")
    message(FATAL_ERROR "coorc init did not create ${required}")
  endif()
endforeach()

# Trust policy: an index entry without a checksum is refused when the manifest
# requires checksums.
file(WRITE "${WORK}/trusted/coordination.toml" [=[
[package]
name = "trusted"
version = "0.1.0"
source = "src"

[dependencies]
unverified = "1.0"

[registries]
official = "file://@REGISTRY@"

[trust]
require_checksum = true
]=])
fix_manifest("${WORK}/trusted/coordination.toml")
file(WRITE "${WORK}/trusted/src/main.coo" "integer main() { return 0; }\n")
execute_process(
  COMMAND ${ENV_RUN} "${COORC}" install
          --manifest "${WORK}/trusted/coordination.toml"
  RESULT_VARIABLE trust_result
  OUTPUT_VARIABLE trust_out
  ERROR_VARIABLE trust_err)
if(trust_result EQUAL 0)
  message(FATAL_ERROR "install accepted an unverified package\n${trust_out}")
endif()
if(NOT "${trust_out}${trust_err}" MATCHES "require_checksum")
  message(FATAL_ERROR "trust policy error not reported\n${trust_out}\n${trust_err}")
endif()

# Insecure HTTP registries are refused before any download.
file(WRITE "${WORK}/insecure/coordination.toml" [=[
[package]
name = "insecure"
version = "0.1.0"
source = "src"

[dependencies]
math = "^1.0"

[registries]
official = "http://example.invalid/registry"
]=])
file(WRITE "${WORK}/insecure/src/main.coo" "integer main() { return 0; }\n")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "COORD_CACHE_DIR=${WORK}/insecure-cache"
          "COORD_CONFIG=${WORK}/insecure-config.toml"
          "${COORC}" install
          --manifest "${WORK}/insecure/coordination.toml"
  RESULT_VARIABLE insecure_result
  OUTPUT_VARIABLE insecure_out
  ERROR_VARIABLE insecure_err)
if(insecure_result EQUAL 0)
  message(FATAL_ERROR "install accepted an insecure HTTP registry\n${insecure_out}")
endif()
if(NOT "${insecure_out}${insecure_err}" MATCHES "insecure HTTP")
  message(FATAL_ERROR "insecure HTTP policy error not reported\n${insecure_out}\n${insecure_err}")
endif()

if(DEFINED BUILD)
  execute_process(
    COMMAND ${ENV_RUN} "${COORC}" build "${WORK}/app"
            -o "${WORK}/app.exe"    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_out
    ERROR_VARIABLE build_err)
  if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "build with installed packages failed\n${build_out}\n${build_err}")
  endif()
  execute_process(
    COMMAND "${WORK}/app.exe"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_out
    ERROR_VARIABLE run_err)
  string(STRIP "${run_out}" run_out)
  if(NOT run_result EQUAL 0 OR NOT run_out STREQUAL "42")
    message(FATAL_ERROR "installed package program failed: '${run_out}' ${run_err}")
  endif()
endif()
