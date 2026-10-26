# ============================================================
# 本仓的 libssh overlay port（源：microsoft/vcpkg 的 ports/libssh@0.12.0，只把版本推到 0.12.2）
# ------------------------------------------------------------
# 上游 vcpkg 注册表截至 2026-10-27 仍只到 0.12.0，而 0.12.1/0.12.2 是带安全修复的两档：
# 0.12.1 修 CVE-2026-15370/59842/59844/59845/59846/59847/59848/59849/59850/59851，
# 0.12.2 补 CVE-2026-59843。其中「parameter size mismatch in mlkem768x25519-sha256」正是
# Windows/x64 腿断在密钥交换的原因（上游 issue libssh#368：`ssh_client_hybrid_mlkem_init`
# 把 int 常量 CURVE25519_PUBKEY_SIZE 递进 'P' 格式的 size_t 可变参槽，MSVC 只写栈槽低 32 位，
# 高 32 位是垃圾 ⇒ `ssh_buffer_pack` 判长度溢出 ⇒ `ssh_connect` 报
# "Failed to construct client init buffer"；GCC 侧同一段侥幸可用，故 POSIX 腿一直没红）。
# 取 overlay 而不是分叉源码：源码仍由 vcpkg 从 libssh.org 取官方发布件并校验 SHA512，
# 本目录只是那份端口的元数据（裁决 7.12「不 vendor 源码进仓」不破）。
# 退场条件：注册表出现 ≥0.12.2 的 libssh 端口后，删本目录并把 vcpkg.json 的
# builtin-baseline 推到含该端口的提交。
# ============================================================

vcpkg_download_distfile(distfile
    URLS https://www.libssh.org/files/0.12/libssh-${VERSION}.tar.xz
    FILENAME libssh-${VERSION}.tar.xz
    SHA512 92b95d64772906fc0fe497fed4dc34a160f2397f71ef3871dc1ea0fe1e8e3c00df699ed0efdc0b754feb23e2140bda17be41d16aee48ff715487384023fdd467
)
vcpkg_extract_source_archive(SOURCE_PATH
    ARCHIVE "${distfile}"
    PATCHES
        0001-export-pkgconfig-file.patch
        0003-no-source-write.patch
        0004-file-permissions-constants.patch
        android-glob-tilde.diff
)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        pcap    WITH_PCAP
        server  WITH_SERVER
        zlib    WITH_ZLIB
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${FEATURE_OPTIONS}
        -DCMAKE_REQUIRE_FIND_PACKAGE_OpenSSL=ON
        -DWITH_EXAMPLES=OFF
        -DWITH_GSSAPI=OFF
        -DWITH_NACL=OFF
        -DWITH_SYMBOL_VERSIONING=OFF
)

vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_fixup_pkgconfig()

if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/include/libssh/libssh.h"
        "#ifdef LIBSSH_STATIC"
        "#if 1"
    )
endif()

vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/libssh)

file(READ "${CURRENT_PACKAGES_DIR}/share/libssh/libssh-config.cmake" cmake_config)
file(WRITE "${CURRENT_PACKAGES_DIR}/share/libssh/libssh-config.cmake" "
include(CMakeFindDependencyMacro)
if(MINGW32)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_dependency(Threads)
endif()
find_dependency(OpenSSL)
if(\"${WITH_ZLIB}\")
    find_dependency(ZLIB)
endif()
${cmake_config}"
)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
