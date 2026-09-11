set shell := ["bash", "-euo", "pipefail", "-c"]

build-type := "Debug"
build-dir := "build"

default:
    @just --list

configure:
    cmake -S . -B {{ build-dir }} -DCMAKE_BUILD_TYPE={{ build-type }} -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

build: configure
    cmake --build {{ build-dir }} --parallel

release:
    cmake -S . -B {{ build-dir }} -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    cmake --build {{ build-dir }} --parallel

format:
    clang-format --verbose -i $(git ls-files --cached --others --exclude-standard '*.cpp' '*.hpp' '*.h')

format-check:
    clang-format --verbose --dry-run --Werror $(git ls-files --cached --others --exclude-standard '*.cpp' '*.hpp' '*.h')

tidy: configure
    clang-tidy -p {{ build-dir }} $(git ls-files --cached --others --exclude-standard '*.cpp' '*.hpp' '*.h')

compatibility-check:
    cmake -S . -B {{ build-dir }} -DCMAKE_BUILD_TYPE=Debug -DHYPRFIELD_NATIVE_OPTIMIZATIONS=OFF -DBUILD_TESTING=OFF
    cmake --build {{ build-dir }} --target hello --parallel

clean:
    if [[ -e "{{ build-dir }}" ]]; then trash "{{ build-dir }}"; fi

plugin-list:
    hyprctl plugin list

load plugin: build
    hyprctl plugin load "$PWD/{{ build-dir }}/{{ plugin }}/{{ plugin }}.so"

unload plugin:
    hyprctl plugin unload "$PWD/{{ build-dir }}/{{ plugin }}/{{ plugin }}.so"

reload plugin: build
    hyprctl plugin unload "$PWD/{{ build-dir }}/{{ plugin }}/{{ plugin }}.so" || true
    hyprctl plugin load "$PWD/{{ build-dir }}/{{ plugin }}/{{ plugin }}.so"

verify-whiteboard: build
    path="$PWD/{{ build-dir }}/whiteboard/whiteboard.so"
    hyprctl plugin load "$path"
    hyprctl dispatch 'function() hl.plugin.whiteboard.proof("current") end'
    hyprctl plugin unload "$path"
