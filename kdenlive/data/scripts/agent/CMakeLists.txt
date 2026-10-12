# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: 2026 VidMate AI-Mate Team

INSTALL(FILES 
    __init__.py
    config.py
    providers.py
    schemas.py
    agent.py
    service.py
    requirements.txt
DESTINATION ${KDE_INSTALL_DATADIR}${DATA_INSTALL_PREFIX}/scripts/agent)

file(COPY 
    __init__.py
    config.py
    providers.py
    schemas.py
    agent.py
    service.py
    requirements.txt
    DESTINATION ${CMAKE_BINARY_DIR}/bin/scripts/agent
)
