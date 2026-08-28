set shell := ["bash", "-euo", "pipefail", "-c"]

build-type := "Debug"
build-dir := "build"
plugin := build-dir / "hello" / "hello.so"

default:
    @just --list

configure:
    cmake -S . -B {{ build-dir }} -DCMAKE_BUILD_TYPE={{ build-type }} -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

build: configure
    cmake --build {{ build-dir }} --parallel

format:
    clang-format -i $(git ls-files --cached --others --exclude-standard '*.cpp' '*.hpp' '*.h')

format-check:
    clang-format --dry-run --Werror $(git ls-files --cached --others --exclude-standard '*.cpp' '*.hpp' '*.h')

tidy: configure
    clang-tidy -p {{ build-dir }} $(git ls-files --cached --others --exclude-standard '*.cpp' '*.hpp' '*.h')

clean:
    rm -rf {{ build-dir }}

load-hello: build
    hyprctl plugin load "$PWD/{{ plugin }}"

unload-hello:
    hyprctl plugin unload "$PWD/{{ plugin }}"
