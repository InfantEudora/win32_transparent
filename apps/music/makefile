#=======================================================================================
# music - a bench for the adaptive music: a score (assets/music/<name>.json) played live, with
# an ImGui panel for key, tempo and suspense and music_* MCP tools for the same, plus an
# offline render to wav so the result can be measured rather than only listened to.
# See readme.md and MusicEngine.h.
#
# Build and run from this folder:
#
#     export PATH="/c/msys64/mingw64/bin:$PATH"      # the toolchain is NOT on the default PATH
#     mingw32-make.exe samples                       # once: the library's samples -> assets/music/sounds/*.wav
#     mingw32-make.exe -j8
#     ./build/music.exe --minimized 2>stderr.log &
#
# `make clean` removes this app's build folder only. The shared core objects under
# ../../build/core belong to every app; `make cleancore` is the one that clears those.
#=======================================================================================

ROOT    := ../..
PROJECT := music

#The score, the engine and the player are core now (core/Music*.cpp, with USE_SOUND), since the
#games play what this designs. What is left here is the designer: the panels and the library.
APP_SRCS += main.cpp
APP_SRCS += ApplicationMusic.cpp
APP_SRCS += MusicLibrary.cpp

USE_SOUND := 1

#Nothing here simulates a body. See the USE_PHYSICS block in engine.mk.
USE_PHYSICS := 0

ASSET_ROOTS := assets $(ROOT)/shared_assets

#publish's knobs (below), which engine.mk would otherwise refuse as unknown build settings.
APP_SETTINGS := GAME SCORE OGG OGGENC OGG_QUALITY OGG_MAX_RATE

include $(ROOT)/engine.mk

#---------------------------------------------------------------------------------------
# samples - the wavs the bench plays, exported from the sample library.
#
# The library (samples/, catalogued by tools/samplescan) is mostly mp3, and the engine loads
# PCM16 wav only, so these are GENERATED and not kept in git (see .gitignore): regenerate them
# with `make samples`. WHICH files become which wav is samples/exports.csv - one row per wav,
# `name,file,trim` - and that list is the Library panel's too: its Export button writes the wav
# and adds the row, so an export made there is one this rebuilds on a fresh checkout. Loops are
# exported whole (trim `no`), because their seam is their first and last sample; everything
# played as a note or a hit is trimmed, because silence in front of a note is latency on every
# note. The `tr` lines take the file as Excel saves it too: CRLF, and ';' in a Dutch locale.
#---------------------------------------------------------------------------------------
SAMPLESCAN := $(ROOT)/tools/samplescan/build/samplescan.exe
EXPORTS    := samples/exports.csv
OUT        := assets/music/sounds

.PHONY: samples
samples:
	$(MAKE) -C $(ROOT)/tools/samplescan
	@mkdir -p $(OUT)
	@tail -n +2 $(EXPORTS) | tr -d '\r' | tr ';' ',' | while IFS=, read -r name file trim; do \
	    [ -n "$$name" ] || continue; \
	    if [ "$$trim" = "yes" ]; then mode=--export-trim; else mode=--export; fi; \
	    $(SAMPLESCAN) $$mode "samples/$$file" "$(OUT)/$$name.wav" || exit 1; \
	done

#---------------------------------------------------------------------------------------
# publish - hands a finished score to a game: `make publish GAME=archer SCORE=jungle`.
#
# This app is the designer and the game keeps its OWN copy, in git: the score to
# <game>/assets/music/<score>.json, and every wav it names (music/sounds/*.wav) beside it. Only
# those: an export that no part of the score plays is an experiment, and stays here. Run it again
# after changing the score; what the game has is overwritten, and a sample the score has stopped
# naming is left for you to delete.
#
# THE GAME GETS OGG VORBIS, NOT WAV. Each wav is encoded to <name>.ogg, the game's copy of the
# score is rewritten to name the .ogg, and a .wav of the same name already in the game is removed -
# the .ogg replaces it. The wavs here stay the source: this bench still plays and exports wav, and
# only the game's copy is compressed. The loaders tell the two apart by content (core/AudioDecode.h).
#
# The settings are the ones listened for on 2026-09-30, headphones in a quiet room, across pads
# AND plucked and struck samples: -q -1, which is libvorbis's floor (-2 is refused, anything
# between rounds up), and 32 kHz. For the archer's 42 samples that is 66 MB of wav -> 2.0 MB.
# Only a file ABOVE OGG_MAX_RATE is resampled; a 24 kHz sample is left at 24 kHz rather than
# padded up. Mono stays mono and stereo stays stereo - a downmix was clearly audible at any
# quality, so there is deliberately no knob for it.
#
# The encoder is foobar2000's copy of oggenc2 (OggEnc 2.88, libvorbis 1.3.7) - the 64-bit
# install's encoders folder, not (x86). Any oggenc2 works: OGGENC=<path>. OGG=0 copies the wavs
# as before.
#---------------------------------------------------------------------------------------
GAME  ?= archer
SCORE ?= jungle
GAME_ASSETS := $(ROOT)/apps/$(GAME)/assets

OGG          ?= 1
OGGENC       ?= C:/Program Files/foobar2000/encoders/oggenc2.exe
OGG_QUALITY  ?= -1
OGG_MAX_RATE ?= 32000

.PHONY: publish
publish:
	@test -d "$(GAME_ASSETS)" || { echo "no game called $(GAME) (looked for $(GAME_ASSETS))"; exit 1; }
	@if [ "$(OGG)" = "1" ] && [ ! -f "$(OGGENC)" ]; then echo "no encoder at $(OGGENC) - pass OGGENC=<path to oggenc2.exe>, or OGG=0 for wavs"; exit 1; fi
	@mkdir -p "$(GAME_ASSETS)/music/sounds"
ifeq ($(OGG),1)
	sed 's#"\(music/sounds/[^"]*\)\.wav"#"\1.ogg"#g' assets/music/$(SCORE).json > "$(GAME_ASSETS)/music/$(SCORE).json"
else
	cp assets/music/$(SCORE).json "$(GAME_ASSETS)/music/$(SCORE).json"
endif
	@n=0; for s in $$(grep -o '"music/sounds/[^"]*\.wav"' assets/music/$(SCORE).json | tr -d '"' | sort -u); do \
	    if [ "$(OGG)" = "1" ]; then \
	        rate=$$(od -An -tu4 -j24 -N4 "assets/$$s" | tr -d ' '); \
	        resample=; [ "$$rate" -gt $(OGG_MAX_RATE) ] && resample="--resample $(OGG_MAX_RATE)"; \
	        "$(OGGENC)" -Q -q $(OGG_QUALITY) $$resample -o "$(GAME_ASSETS)/$${s%.wav}.ogg" "assets/$$s" || exit 1; \
	        rm -f "$(GAME_ASSETS)/$$s"; \
	    else \
	        cp "assets/$$s" "$(GAME_ASSETS)/$$s" || exit 1; \
	    fi; \
	    n=$$((n+1)); \
	done; \
	if [ "$(OGG)" = "1" ]; then kind=ogg; else kind=wav; fi; \
	echo "$(SCORE): $$n samples -> $(GAME_ASSETS)/music/sounds as $$kind"
