file(GLOB THEME_FONTS ${CMAKE_CURRENT_LIST_DIR}/../../ui/fonts/font_*_*.c)
idf_component_register(SRCS main.c ble.c debug.c ../../ui/face.c ../../ui/imu.c ../../ui/pip.c ../../ui/doodle.c ../../ui/pet.c ../../ui/shell.c ../../ui/apps.c ../../ui/voice.c ../../ui/profile.c
                            ../../ui/theme.c ../../ui/phone.c ../../ui/mic.c ../../ui/maze.c ${THEME_FONTS}
                       INCLUDE_DIRS ../../ui)
# the face renderer: its per-pixel loop in IRAM (FACE_HOT); no errno from the little libm it still calls
set_source_files_properties(../../ui/face.c PROPERTIES COMPILE_OPTIONS "-fno-math-errno;-O2"
                            COMPILE_DEFINITIONS "FACE_HOT=__attribute__((section(\".iram1.face\")))")

# the owner's own extras, kept out of the public repo (private/ is gitignored): the AITU mark on the badge
if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/../../private/aitu_mark.c AND NOT DEFINED ENV{KEYPET_PUBLIC}) # the release is built without it
    target_sources(${COMPONENT_LIB} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../private/aitu_mark.c)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE KEYPET_AITU)
endif()
