# The libraries, each on the one before: the rules of chess, UCI engines, games between them. The command line on top,
# and the window, Reckless UCI Host, on the toolkit.
add_subdirectory(version)
add_subdirectory(chess)
add_subdirectory(uci)
add_subdirectory(match)
add_subdirectory(cli)
if(UCI_HOST_GUI)
    add_subdirectory(app)
endif()
