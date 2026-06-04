#!/usr/bin/env bash
set -u

# Keep Vuo's welcome/donation stack out of the way without modifying Vuo.app.
# The donation pane has no persistent "don't show again" setting in Vuo 2.4.6.

while true; do
  /usr/bin/osascript >/dev/null <<'APPLESCRIPT'
tell application "System Events"
  if not (exists process "Vuo") then return

  tell process "Vuo"
    if not (exists window 1) then return

    if exists button "Maybe Later" of window 1 then
      click button "Maybe Later" of window 1
      return
    end if

    if exists button "No, don't ask again" of window 1 then
      click button "No, don't ask again" of window 1
      return
    end if

  end tell
end tell
APPLESCRIPT
  sleep 0.5
done
