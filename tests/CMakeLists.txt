qt_add_executable(tst_fileoperations tst_fileoperations.cpp)
target_link_libraries(tst_fileoperations PRIVATE argonaut_core Qt6::Test)

add_test(NAME fileoperations COMMAND tst_fileoperations)
# Runs headless; the test sandboxes HOME and the trash itself.
set_tests_properties(fileoperations PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;ARGONAUT_TRASH_HOME_ONLY=1"
    TIMEOUT 300
)

qt_add_executable(tst_directorymodel tst_directorymodel.cpp)
target_link_libraries(tst_directorymodel PRIVATE argonaut_core Qt6::Test)
add_test(NAME directorymodel COMMAND tst_directorymodel)
set_tests_properties(directorymodel PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;ARGONAUT_TRASH_HOME_ONLY=1"
    TIMEOUT 300
)

qt_add_executable(tst_bookmarks tst_bookmarks.cpp)
target_link_libraries(tst_bookmarks PRIVATE argonaut_core Qt6::Test)
add_test(NAME bookmarks COMMAND tst_bookmarks)
set_tests_properties(bookmarks PROPERTIES TIMEOUT 60)

qt_add_executable(tst_openwith tst_openwith.cpp)
target_link_libraries(tst_openwith PRIVATE argonaut_core Qt6::Test)
add_test(NAME openwith COMMAND tst_openwith)
set_tests_properties(openwith PROPERTIES TIMEOUT 60)

qt_add_executable(tst_contenttype tst_contenttype.cpp)
target_link_libraries(tst_contenttype PRIVATE argonaut_core Qt6::Test)
add_test(NAME contenttype COMMAND tst_contenttype)
set_tests_properties(contenttype PROPERTIES TIMEOUT 60)

if(LIBARCHIVE_FOUND)
    qt_add_executable(tst_archives tst_archives.cpp)
    target_link_libraries(tst_archives PRIVATE argonaut_core Qt6::Test)
    add_test(NAME archives COMMAND tst_archives)
    set_tests_properties(archives PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;ARGONAUT_TRASH_HOME_ONLY=1"
        TIMEOUT 300
    )
endif()

qt_add_executable(tst_diskusage tst_diskusage.cpp)
target_link_libraries(tst_diskusage PRIVATE argonaut_core Qt6::Test)
add_test(NAME diskusage COMMAND tst_diskusage)
set_tests_properties(diskusage PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)

qt_add_executable(tst_thumbnailer tst_thumbnailer.cpp)
target_link_libraries(tst_thumbnailer PRIVATE argonaut_core Qt6::Test)
add_test(NAME thumbnailer COMMAND tst_thumbnailer)
set_tests_properties(thumbnailer PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)

qt_add_executable(tst_places tst_places.cpp)
target_link_libraries(tst_places PRIVATE argonaut_core Qt6::Test)
add_test(NAME places COMMAND tst_places)
set_tests_properties(places PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)

qt_add_executable(tst_defaultfm tst_defaultfm.cpp)
target_link_libraries(tst_defaultfm PRIVATE argonaut_core Qt6::Test)
add_test(NAME defaultfm COMMAND tst_defaultfm)
set_tests_properties(defaultfm PROPERTIES TIMEOUT 60)

# D-Bus: only ever on a private bus.
find_program(DBUS_RUN_SESSION dbus-run-session)
if(TARGET Qt6::DBus AND DBUS_RUN_SESSION)
    qt_add_executable(tst_fmservice tst_fmservice.cpp)
    target_link_libraries(tst_fmservice PRIVATE argonaut_core Qt6::Test)
    add_test(NAME fmservice COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:tst_fmservice>)
    set_tests_properties(fmservice PROPERTIES ENVIRONMENT "ARGONAUT_PRIVATE_BUS=1" TIMEOUT 60)

    qt_add_executable(tst_filechooser tst_filechooser.cpp)
    target_link_libraries(tst_filechooser PRIVATE argonaut_core Qt6::Test)
    add_test(NAME filechooser COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:tst_filechooser>)
    set_tests_properties(filechooser PROPERTIES ENVIRONMENT "ARGONAUT_PRIVATE_BUS=1" TIMEOUT 60)
endif()

qt_add_executable(tst_preview tst_preview.cpp)
target_link_libraries(tst_preview PRIVATE argonaut_core Qt6::Test)
add_test(NAME preview COMMAND tst_preview)
set_tests_properties(preview PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)

qt_add_executable(tst_naming tst_naming.cpp)
target_link_libraries(tst_naming PRIVATE argonaut_core Qt6::Test)
add_test(NAME naming COMMAND tst_naming)
set_tests_properties(naming PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;ARGONAUT_TRASH_HOME_ONLY=1"
    TIMEOUT 60
)

qt_add_executable(tst_git tst_git.cpp)
target_link_libraries(tst_git PRIVATE argonaut_core Qt6::Test)
add_test(NAME git COMMAND tst_git)
set_tests_properties(git PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;ARGONAUT_TRASH_HOME_ONLY=1;GIT_CONFIG_NOSYSTEM=1"
    TIMEOUT 120
)

qt_add_executable(tst_contentsearch tst_contentsearch.cpp)
target_link_libraries(tst_contentsearch PRIVATE argonaut_core Qt6::Test)
add_test(NAME contentsearch COMMAND tst_contentsearch)
set_tests_properties(contentsearch PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;ARGONAUT_TRASH_HOME_ONLY=1"
    TIMEOUT 120
)

qt_add_executable(tst_remote tst_remote.cpp)
target_link_libraries(tst_remote PRIVATE argonaut_core Qt6::Test)
add_test(NAME remote COMMAND tst_remote)
set_tests_properties(remote PROPERTIES TIMEOUT 60)

qt_add_executable(tst_claude tst_claude.cpp)
target_link_libraries(tst_claude PRIVATE argonaut_core Qt6::Test)
add_test(NAME claude COMMAND tst_claude)
set_tests_properties(claude PROPERTIES ENVIRONMENT "ARGONAUT_KEYSTORE=file" TIMEOUT 60)

qt_add_executable(tst_organizer tst_organizer.cpp FakeApi.h)
target_link_libraries(tst_organizer PRIVATE argonaut_core Qt6::Test)
add_test(NAME organizer COMMAND tst_organizer)
set_tests_properties(organizer PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;ARGONAUT_KEYSTORE=file" TIMEOUT 120)

qt_add_executable(tst_mediainfo tst_mediainfo.cpp)
target_link_libraries(tst_mediainfo PRIVATE argonaut_core Qt6::Test)
add_test(NAME mediainfo COMMAND tst_mediainfo)
set_tests_properties(mediainfo PROPERTIES TIMEOUT 60)
