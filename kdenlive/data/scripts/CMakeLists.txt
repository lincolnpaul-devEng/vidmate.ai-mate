# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: Jean-Baptiste Mardelle <jb@kdenlive.org>

add_subdirectory(automask)
add_subdirectory(vosk)
add_subdirectory(whisper)
add_subdirectory(scenedetect)
add_subdirectory(audio)
add_subdirectory(vfx)
add_subdirectory(agent)

INSTALL(FILES 
    checkpackages.py
    otiointerface.py
    checkgpu.py
    generate_transition_previews.py

DESTINATION ${KDE_INSTALL_DATADIR}${DATA_INSTALL_PREFIX}/scripts)

file(COPY 
    checkpackages.py
    otiointerface.py
    checkgpu.py
    generate_transition_previews.py
    DESTINATION ${CMAKE_BINARY_DIR}/bin/scripts
)
