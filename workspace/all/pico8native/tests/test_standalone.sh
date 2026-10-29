#!/bin/sh
# pico8shim.so in standalone mode (Splore): fake_splore runs under it against
# fake_sdl_standalone.c, whose pad follows a script. Checks:
# - SDL goes straight through: the sensor flag is dropped from SDL_Init and
#   SDL_InitSubSystem, vsync and the clock, audio and pad are the real SDL's,
#   and cart data still goes to P8NATIVE_CDATA
# - a tap of Menu opens the quit dialog, which freezes the game (presents with no
#   game frame between), pauses its audio and keeps the time from its clock
# - B, or Menu again, closes it and resumes audio; A, or an SDL_QUIT (SIGTERM), quits
#   (A and B as labelled on the pad: SDL's B and A in Splore's mapping):
#   the game gets SDL_QUIT
# - the game never sees Menu, nor a button that closed the dialog, held or let go
# - Menu with another button opens nothing, and that button is the game's
#
#   sh test_standalone.sh <pico8shim.so> <fake_splore>
set -u
SHIM=$1
GAME=$2
T=${TMPDIR:-/tmp}/nx_test_standalone.$$
trap 'rm -rf "$T"' EXIT INT TERM
mkdir -p "$T/home" "$T/cdata"

FAILS=0
PASSES=0
check() { # <description> <command...>
	d=$1
	shift
	if "$@"; then PASSES=$((PASSES + 1)); else
		FAILS=$((FAILS + 1))
		echo "FAIL: $NAME: $d"
	fi
}
has() { grep -qF -- "$1" "$T/log"; }
lacks() { ! grep -qF -- "$1" "$T/log"; }
lacks_re() { ! grep -qE -- "$1" "$T/log"; }
run() { # <name> <FAKE_PAD> [FAKE_QUIT_AT]; the log in $T/log, exit status in $STATUS
	NAME=$1
	env P8SHIM_STANDALONE=1 P8NATIVE_HOME="$T/home" P8NATIVE_CDATA="$T/cdata" FAKE_FRAMES=40 FAKE_PAD="$2" \
		${3:+FAKE_QUIT_AT=$3} SDCARD_PATH="$T/nosd" LD_PRELOAD="$SHIM" "$GAME" >"$T/log" 2>&1
	STATUS=$?
}
# no game frame between the dialog opening and closing, and it drew and presented
frozen() { # <closing line>
	awk -v want="$1" '
		/pico8shim: quit dialog open/ { open = 1; next }
		open && index($0, want) { done = 1; exit }
		open && /^game:/ { game++ }
		open && /^sdl: present/ && $5 > 0 { drawn++ }
		END { exit !(done && !game && drawn >= 3) }' "$T/log"
}
# the game's clock moved by less than 60 ms over the frames around the dialog
ticks_kept() {
	awk '/^game: frame/ { if (seen && $5 - last > 60) bad = 1; last = $5; seen = 1 } END { exit bad }' "$T/log"
}

# pass-through, no Menu
run "pass-through" ""
check "exits 0" test "$STATUS" -eq 0
check "runs all frames" has "game: done"
check "SDL_Init without the sensor flag" has "sdl: init 0x7231"
check "SDL_InitSubSystem without the sensor flag" has "sdl: initsubsystem 0x0"
check "vsync kept" has "sdl: renderer flags 0x4"
check "real audio" has "sdl: open audio"
check "real audio unpaused" has "sdl: pause audio 0"
check "real audio lock" has "sdl: lock audio"
check "real controller" has "sdl: controller open 0"
check "the clock is SDL's" lacks "game: frame 39 ticks 0"
check "no dialog" lacks "opening the quit dialog"
check "cart data in CDATA" test -f "$T/cdata/splore.p8d.txt"
check "no cart data in home" test ! -e "$T/home/cdata/splore.p8d.txt"
check "no ipc needed" lacks "IPC"

# Menu tap, then B: back to the game
run "menu then B" "5:0x20,8:0,14:0x1,18:0"
check "exits 0" test "$STATUS" -eq 0
check "runs all frames" has "game: done"
check "opens" has "pico8shim: Menu: opening the quit dialog"
check "pauses audio" has "sdl: pause device 1 1"
check "B cancels" has "pico8shim: quit dialog: Cancel"
check "resumes audio" has "sdl: pause device 1 0"
check "reads the frame back" has "sdl: read pixels"
check "freezes the game while drawn" frozen "pico8shim: quit dialog: Cancel"
check "puts the viewport back" has "sdl: viewport 128,0 768x768"
check "keeps the dialog's time from the clock" ticks_kept
check "game never sees Menu" lacks_re "game: (event (down|up)|button) 5( |$)"
check "game never sees B" lacks_re "game: (event (down|up)|button) 0( |$)"
check "no quit" lacks "game: quit"

# Menu tap, then Menu again
run "menu then menu" "5:0x20,8:0,14:0x20,17:0"
check "opens" has "pico8shim: Menu: opening the quit dialog"
check "Menu cancels" has "pico8shim: quit dialog: Cancel"
check "freezes the game" frozen "pico8shim: quit dialog: Cancel"
check "resumes audio" has "sdl: pause device 1 0"
check "runs all frames" has "game: done"
check "game never sees Menu" lacks_re "game: (event (down|up)|button) 5( |$)"
check "opens once" test "$(grep -c 'opening the quit dialog' "$T/log")" -eq 1

# Menu tap, then A: PICO-8 gets SDL_QUIT
run "menu then A" "5:0x20,8:0,14:0x2,30:0"
check "exits 0" test "$STATUS" -eq 0
check "A quits" has "pico8shim: quit dialog: Quit"
check "freezes the game" frozen "pico8shim: quit dialog: Quit"
check "game gets SDL_QUIT" grep -qE "game: quit at frame (9|10)$" "$T/log"
check "audio stays paused" lacks "sdl: pause device 1 0"
check "game never sees A" lacks_re "game: (event (down|up)|button) 1( |$)"

# Menu tap, then power-off's SIGTERM (an SDL_QUIT) while it is open
run "menu then SIGTERM" "5:0x20,8:0" 13
check "quits" has "pico8shim: quit dialog: Quit"
check "game gets SDL_QUIT" has "game: quit at frame"

# Menu with Start: a chord, no dialog, Start is the game's
run "menu chord" "5:0x20,7:0x60,9:0x20,11:0"
check "no dialog" lacks "opening the quit dialog"
check "game sees Start" has "game: button 6 held"
check "game sees Start's events" has "game: event down 6"
check "game never sees Menu" lacks_re "game: (event (down|up)|button) 5( |$)"
check "runs all frames" has "game: done"

# with no font, it still draws text (the built-in font): many rects a present
run "bitmap font" "5:0x20,8:0,14:0x1,18:0"
check "says there is no SDL2_ttf or font" grep -qE "pico8shim: (no SDL2_ttf|cannot open .*font1.ttf)" "$T/log"
check "draws text" awk '/^sdl: present/ && $5 > 100 { ok = 1 } END { exit !ok }' "$T/log"

echo "test_standalone: $PASSES passed, $FAILS failed"
[ "$FAILS" -eq 0 ]
