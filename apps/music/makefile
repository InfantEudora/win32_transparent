#=======================================================================================
# music - a bench for the adaptive music: a score (assets/music/<name>.json) played live, with
# an ImGui panel for key, tempo and suspense and music_* MCP tools for the same, plus an
# offline render to wav so the result can be measured rather than only listened to.
# See readme.md and MusicEngine.h.
#
# Build and run from this folder:
#
#     export PATH="/c/msys64/mingw64/bin:$PATH"      # the toolchain is NOT on the default PATH
#     mingw32-make.exe samples                       # once: the library's samples -> assets/sound/*.wav
#     mingw32-make.exe -j8
#     ./build/music.exe --minimized 2>stderr.log &
#
# `make clean` removes this app's build folder only. The shared core objects under
# ../../build/core belong to every app; `make cleancore` is the one that clears those.
#=======================================================================================

ROOT    := ../..
PROJECT := music

APP_SRCS += main.cpp
APP_SRCS += ApplicationMusic.cpp
APP_SRCS += MusicEngine.cpp
APP_SRCS += MusicScore.cpp
APP_SRCS += MusicPlayer.cpp
APP_SRCS += MusicLibrary.cpp

USE_SOUND := 1

#Nothing here simulates a body. See the USE_PHYSICS block in engine.mk.
USE_PHYSICS := 0

ASSET_ROOTS := assets $(ROOT)/shared_assets

include $(ROOT)/engine.mk

#---------------------------------------------------------------------------------------
# samples - the wavs the bench plays, exported from the sample library.
#
# The library (samples/, catalogued by tools/samplescan) is mostly mp3, and the engine loads
# PCM16 wav only, so these are GENERATED and not kept in git (see .gitignore): regenerate them
# with `make samples`. Loops are exported whole, because their seam is their first and last
# sample; everything played as a note or a hit is trimmed, because silence in front of a note
# is latency on every note.
#---------------------------------------------------------------------------------------
SAMPLESCAN := $(ROOT)/tools/samplescan/build/samplescan.exe
LIB        := samples/unsorted
OUT        := assets/sound

EXPORT_WHOLE := \
    desert_wind:tanweraman-desert-wind-1-350398.mp3 \
    jungle:freesound_community-jungle-6432.mp3 \
    suspense_strings:freesound_community-suspense_strings_001wav-14805.mp3 \
    flute_rain_loop:musiqo-flute-rain-flute-loop-ambient-short-loop-340800.mp3 \
    low_bass_wind:freesound_community-low-bass-wind-sound-101190.mp3 \
    cemetery_wind:dragon-studio-cemetery-wind-467488.mp3 \
    atmos_drums:dragon-studio-atmospheric-drums-443147.mp3 \
    creepy_whistles:freesound_community-creepy-whistles-66703.mp3

EXPORT_TRIM := \
    eery_E4:freesound_community-eery-ambience-78055.mp3 \
    violin_B3:freesound_community-el-violin-long-note-flanger-27946.mp3 \
    kalimba_Fs3:freesound_community-kalimba-76210.wav \
    kalimba_G3:freesound_community-kalimba_g-46244.mp3 \
    panflute_Gs3:floraphonic-pan-flute-transition-b-1-188138.mp3 \
    flute_Fs5:soundreality-flute-132530.mp3 \
    scare_1:kalsstockmedia-horror-jump-scare-effect-2-250464.mp3 \
    scare_2:kalsstockmedia-sudden-scary-jump-scare-effect-3-294235.mp3 \
    hang_F3:freesound_community-068320_hang-drum-2wav-80568.mp3 \
    hang_G4:freesound_community-hang-drum-9-80579.mp3 \
    bass_C2:freesound_community-2_c_mf-102718.mp3 \
    string_B1:freesound_community-b-string-b0-39527.mp3 \
    metallophone_E6:freesound_community-metallophone-e-note-105649.mp3 \
    zanka_D3:freesound_community-zanka-note-d-44992.mp3 \
    bamboo_flute_C5:floraphonic-fluttering-bamboo-flute-transition-b-2-209651.mp3 \
    harp_fall:freesound_community-harp-buzzing-80812.mp3 \
    low_hit:universfield-cinematic-low-hit-291095.mp3 \
    reverse_cymbal:11325622-reverse-cymbal-sound-effect-239951.mp3

.PHONY: samples
samples:
	$(MAKE) -C $(ROOT)/tools/samplescan
	@mkdir -p $(OUT)
	@for pair in $(EXPORT_WHOLE); do \
	    name=$${pair%%:*}; src=$${pair#*:}; \
	    $(SAMPLESCAN) --export "$(LIB)/$$src" "$(OUT)/$$name.wav" || exit 1; \
	done
	@for pair in $(EXPORT_TRIM); do \
	    name=$${pair%%:*}; src=$${pair#*:}; \
	    $(SAMPLESCAN) --export-trim "$(LIB)/$$src" "$(OUT)/$$name.wav" || exit 1; \
	done
