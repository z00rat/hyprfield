#!/usr/bin/env fish

# Temporarily place existing windows on the active workspace and restore them
# before unloading Whiteboard. Existing windows are never closed.

set -l script_dir (dirname (status filename))
set -l root (realpath "$script_dir/..")
set -l script_file (realpath (status filename))
cd "$root"; or exit 1

set -l plugin_source "$PWD/build/whiteboard/whiteboard.so"
set -g load_attempted 0
set -g cleanup_done 0
set -g result 0
set -g original_windows
set -g test_addresses
set -l stamp (date +%s)

if not set -q HYPRFIELD_MANUAL_TEST_LOGGED
    set -l log_dir "$XDG_STATE_HOME/hyprfield"
    if test -z "$XDG_STATE_HOME"
        set log_dir "$HOME/.local/state/hyprfield"
    end
    mkdir -p "$log_dir"
    set -l log_file "$log_dir/manual-whiteboard-test-$stamp.log"
    echo "Logging this run to $log_file"
    env HYPRFIELD_MANUAL_TEST_LOGGED=1 fish "$script_file" $argv 2>&1 | tee "$log_file"
    exit $pipestatus[1]
end

echo "Preflight: unloading any existing whiteboard plugin..."
set -l existing_plugins (hyprctl plugin list 2>&1)
if string match -q '*whiteboard*' -- $existing_plugins
    set -l preflight_unload (just unload whiteboard 2>&1)
    echo $preflight_unload
    sleep 1
    set existing_plugins (hyprctl plugin list 2>&1)
    if string match -q '*whiteboard*' -- $existing_plugins
        echo "ERROR: existing whiteboard plugin is still loaded; stopping."
        exit 1
    end
    sleep 2
else
    echo "No existing whiteboard plugin found."
end

echo "Preflight: building the current plugin..."
if not just build
    echo "ERROR: build failed; stopping before plugin load."
    exit 1
end
set -g plugin "/tmp/hyprfield-whiteboard-$stamp.so"
if not cp "$plugin_source" "$plugin"
    echo "ERROR: could not create isolated plugin image: $plugin"
    exit 1
end
rm -f /tmp/hyprfield-whiteboard-debug.log
set -l plugin_checksum (sha256sum "$plugin" | string split ' ')[1]
echo "Plugin checksum: $plugin_checksum"

set -l monitor (hyprctl -j activeworkspace | jq -r '.monitor')
set -l workspace (hyprctl -j activeworkspace | jq -r '.id')
if test -z "$monitor"; or test -z "$workspace"
    echo "Could not identify the current monitor/workspace."
    exit 1
end

set -l fullscreen_count (hyprctl -j clients | jq --argjson workspace "$workspace" \
    '[.[] | select(.workspace.id == $workspace and ((.fullscreen // 0) != 0))] | length')
if test "$fullscreen_count" -gt 0
    echo "Skipping $fullscreen_count fullscreen window(s); fullscreen clients cannot be placed safely."
end
set original_windows (hyprctl -j clients | jq -r --argjson workspace "$workspace" \
    '.[] | select(.workspace.id == $workspace and ((.fullscreen // 0) == 0)) | [.address, .workspace.id, .monitor, .x, .y, .width, .height, .floating] | @tsv')
if test (count $original_windows) -eq 0
    echo "No non-fullscreen windows found on the active workspace; nothing to test."
    exit 1
end

for window in $original_windows
    set -l fields (string split \t -- $window)
    set -a test_addresses $fields[1]
end
echo "Temporarily testing windows: $test_addresses"

function restore_windows
    echo "Restoring original window placement..."
    for window in $original_windows
        set -l fields (string split \t -- $window)
        if test (count $fields) -ne 8
            continue
        end
        set -l address $fields[1]
        set -l original_workspace $fields[2]
        set -l original_x $fields[4]
        set -l original_y $fields[5]
        set -l original_width $fields[6]
        set -l original_height $fields[7]
        set -l original_floating $fields[8]
        set -l current_floating (hyprctl -j clients | jq -r --arg address "$address" '.[] | select(.address == $address) | .floating' | head -n 1)
        if test "$current_floating" != "$original_floating"
            set -l floating_result (hyprctl dispatch "hl.dsp.window.float({action=\"toggle\",window=\"address:$address\"})" 2>&1)
            if not string match -q 'ok*' -- $floating_result
                echo "WARNING: could not restore floating state for $address: $floating_result"
            end
        end
        set -l workspace_result (hyprctl dispatch "hl.dsp.window.move({workspace=\"$original_workspace\",follow=false,window=\"address:$address\"})" 2>&1)
        if not string match -q 'ok*' -- $workspace_result
            echo "WARNING: could not restore workspace for $address: $workspace_result"
        end
        if test "$original_floating" = "true"
            set -l move_result (hyprctl dispatch "hl.dsp.window.move({x=$original_x,y=$original_y,window=\"address:$address\"})" 2>&1)
            set -l resize_result (hyprctl dispatch "hl.dsp.window.resize({x=$original_width,y=$original_height,window=\"address:$address\"})" 2>&1)
            if not string match -q 'ok*' -- $move_result
                echo "WARNING: could not restore position for $address: $move_result"
            end
            if not string match -q 'ok*' -- $resize_result
                echo "WARNING: could not restore size for $address: $resize_result"
            end
        end
    end
end

function cleanup
    if test "$cleanup_done" -eq 1
        return
    end
    set cleanup_done 1
    restore_windows
    if test "$load_attempted" -eq 1
        set -l unload_output ""
        set -l unload_ok 0
        for attempt in 1 2 3 4 5
            set unload_output (hyprctl plugin unload "$plugin" 2>&1)
            sleep 1
            set -l plugin_list (hyprctl plugin list 2>&1)
            if not string match -q '*whiteboard*' -- $plugin_list
                set unload_ok 1
                break
            end
        end
        if test "$unload_ok" -eq 1
            echo "Whiteboard unloaded; original window placement restored."
        else
            echo "ERROR: Whiteboard is still loaded after cleanup: $unload_output"
            set result 1
        end
    end
    rm -f "$plugin"
end

function cleanup_on_int --on-signal INT
    cleanup
    exit 130
end

function cleanup_on_term --on-signal TERM
    cleanup
    exit 130
end

function whiteboard_dispatch
    set -l response (hyprctl dispatch "$argv[1]" 2>&1)
    echo $response
    string match -q 'ok*' -- $response
end

if not test -f "$plugin"
    echo "Missing plugin: $plugin"
    exit 1
end

set load_attempted 1
if not hyprctl plugin load "$plugin"
    echo "Whiteboard plugin load failed."
    cleanup
    exit 1
end

if not whiteboard_dispatch "function() hl.plugin.whiteboard.proof('$monitor') end"
    echo "Whiteboard proof API failed."
    set result 1
end

if test $result -eq 0; and not whiteboard_dispatch "function() hl.plugin.whiteboard.activate('$monitor', $workspace) end"
    echo "Whiteboard activation failed."
    set result 1
end

if test $result -eq 0
    for address in $test_addresses
        if not whiteboard_dispatch "function() hl.plugin.whiteboard.registerClient('$monitor', $workspace, '$address') end"
            echo "Could not register window $address."
            set result 1
            break
        end
    end
end

set -l window_count (count $test_addresses)
set -l grid_rows (math "ceil($window_count / 4)")
if test "$grid_rows" -lt 1
    set grid_rows 1
end
    if test $result -eq 0; and not whiteboard_dispatch "function() hl.plugin.whiteboard.configureGrid('$monitor', $workspace, $grid_rows, 4, 2, 2, 'grid') end"
    echo "Could not configure the Whiteboard grid."
    set result 1
end

if test $result -eq 0
    set -l index 0
    for address in $test_addresses
        set -l row (math "floor($index / 4)")
        set -l column (math "$index % 4")
        if not whiteboard_dispatch "function() hl.plugin.whiteboard.placeGrid('$monitor', $workspace, '$address', $row, $column, 1, 1) end"
            echo "Could not place window $address in the grid."
            set result 1
            break
        end
        set index (math "$index + 1")
    end
end

if test $result -eq 0
    echo
    echo "SUCCESS: Whiteboard placed all active windows."
    echo "Holding the ordinary grid for 5 seconds..."
    sleep 5
    echo "Entering bounded camera management at 0.8x (zoom-only test)..."
    if not whiteboard_dispatch "function() hl.plugin.whiteboard.setCamera('$monitor', $workspace, 0.8, 0, 0) end"
        echo "Could not enter camera management mode."
        set result 1
    end
end

if test $result -eq 0
    echo "Inspect the transformed layout for 6 seconds..."
    echo
    sleep 6
    echo "Restoring ordinary 1x presentation..."
    if not whiteboard_dispatch "function() hl.plugin.whiteboard.setCamera('$monitor', $workspace, 1.0, 0, 0) end"
        echo "Could not restore ordinary camera presentation."
        set result 1
    end
    sleep 2
end

cleanup
exit $result
