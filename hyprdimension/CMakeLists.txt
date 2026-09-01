cmake_minimum_required(VERSION 3.27)

hyprfield_add_plugin(hyprdimension
    VERSION 0.1.0
    DESCRIPTION "Monitor-scoped dimensional workspaces"
    SOURCE main.cpp commands.cpp
)
