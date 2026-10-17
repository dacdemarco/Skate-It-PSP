cmake_minimum_required(VERSION 3.11)

project(skateit)

add_executable(${PROJECT_NAME}
    src/main.c
    src/skel.c
    src/skate.c
    src/rider.c
    src/coll.c
    src/rails.c
    src/mixer.c
    src/sfx.c
    src/sparks.c
    src/audio.c
    src/trick.c
    src/score.c
    src/control.c
    src/nearclip.c
    src/levelobj.c
    src/career.c
    src/goals.c
    src/tutorial.c
    src/contest.c
    src/race.c
    src/ghost.c
    src/stage.c
    src/progress.c
    src/story.c
    src/scene.c
    src/stats.c
    src/logbook.c
    src/custom.c
    src/storyui.c
    src/ui.c
    src/frontend.c
    src/hud.c
    src/settings.c
    src/options.c
    src/vx.c
)

# The build type was empty, i.e. NO optimisation at all (-O0) for everything so far. -O2 (and the code reads structs out of file buffers: no strict aliasing assumptions).
target_compile_options(${PROJECT_NAME} PRIVATE -O2 -fno-strict-aliasing)

target_link_libraries(${PROJECT_NAME} PRIVATE
    pspgum
    pspgu
    pspge
    pspdebug
    pspdisplay
    pspctrl
    psppower
    pspaudio
    m
)

# Optional XMB art lives in pbp/ and is packed INTO EBOOT.PBP at build time.
# Missing files are skipped (NULL), so the build works with an empty pbp/.
function(pbp_file out_var filename)
    if(EXISTS ${CMAKE_SOURCE_DIR}/pbp/${filename})
        set(${out_var} ${CMAKE_SOURCE_DIR}/pbp/${filename} PARENT_SCOPE)
    else()
        set(${out_var} NULL PARENT_SCOPE)
    endif()
endfunction()

pbp_file(PBP_ICON ICON0.png)
pbp_file(PBP_PREVIEW PIC0.png)
pbp_file(PBP_BACKGROUND PIC1.png)
pbp_file(PBP_MUSIC SND0.AT3)

create_pbp_file(
    TARGET ${PROJECT_NAME}
    ICON_PATH ${PBP_ICON}
    BACKGROUND_PATH ${PBP_BACKGROUND}
    PREVIEW_PATH ${PBP_PREVIEW}
    MUSIC_PATH ${PBP_MUSIC}
    TITLE "Skate It PSP"
    VERSION 01.00
    # MEMSIZE 1 unlocks the PSP-2000/3000's extra RAM for homebrew (about 44 MB of heap instead of about 22 MB; the
    # PSP-1000 ignores it). create_pbp_file defaults to MEMSIZE 2 - the standard partition - unless it is passed
    # explicitly (found in the RSPSP project, which measured 47 MB on a PSP-3000).
    MEMSIZE 1
)
