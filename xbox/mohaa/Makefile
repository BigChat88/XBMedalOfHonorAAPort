# OpenMoHAA build for the Xbox (nxdk).
#
# Three modules, as in the original CMake build, each with its own defines:
#   engine : client + server + UI + GL1 renderer + skeletor/tiki + Xbox layer
#   cgame  : CGAME_DLL
#   game   : GAME_DLL + script engine + recast
# cgame and game share code with the engine (q_shared, corepp...) built with
# different defines; they are linked statically after renaming their symbols
# (see tools/prefix-module.sh).
#
# Usage (inside the nxdk container): make -j12 objs      # compile only
#                                    make -k -j12 objs   # error scan
#                                    make -j12 xbe       # the XBE (bin/default.xbe)
#                                    make -j12 iso       # XBE + test ISO

OM    := $(CURDIR)/../../third_party/openmohaa/code
BUILD := $(CURDIR)/build

# ---------------------------------------------------------------------------
# Sources

SCRIPT_SYSTEM := \
	corepp/class.cpp corepp/con_set.cpp corepp/con_timer.cpp corepp/delegate.cpp \
	corepp/lightclass.cpp corepp/listener.cpp corepp/lz77.cpp corepp/mem_blockalloc.cpp \
	corepp/mem_tempalloc.cpp corepp/script.cpp corepp/str.cpp \
	script/scriptexception.cpp script/scriptvariable.cpp

COMMON := \
	qcommon/alias.c qcommon/bg_compat.cpp qcommon/cm_fencemask.c qcommon/cm_load.c \
	qcommon/cm_patch.c qcommon/cm_polylib.c qcommon/cm_terrain.c qcommon/cm_test.c \
	qcommon/cm_trace.c qcommon/cm_trace_lbd.cpp qcommon/cm_trace_obfuscation.cpp \
	qcommon/cmd.c qcommon/common.c qcommon/crc.c qcommon/cvar.c qcommon/files.cpp \
	qcommon/ioapi.c qcommon/huffman.cpp qcommon/md4.c qcommon/md5.c qcommon/memory.c \
	qcommon/msg.cpp qcommon/net_chan.c qcommon/q_math.c \
	qcommon/q_shared.c qcommon/unzip.c gamespy/q_gamespy.c \
	skeletor/bonetable.cpp skeletor/skeletor_imports.cpp skeletor/skeletor_loadanimation.cpp \
	skeletor/skeletor_model_files.cpp skeletor/skeletor_utilities.cpp skeletor/skeletor.cpp \
	skeletor/skeletorbones.cpp skeletor/tokenizer.cpp \
	tiki/tiki_anim.cpp tiki/tiki_cache.cpp tiki/tiki_commands.cpp tiki/tiki_files.cpp \
	tiki/tiki_frame.cpp tiki/tiki_imports.cpp tiki/tiki_parse.cpp tiki/tiki_skel.cpp \
	tiki/tiki_surface.cpp tiki/tiki_tag.cpp tiki/tiki_utility.cpp \
	corepp/tiki_main.cpp corepp/tiki_script.cpp

SERVER := \
	server/sv_client.c server/sv_ccmds.c server/sv_game.c server/sv_init.c server/sv_main.c \
	server/sv_net_chan.c server/sv_snapshot.c server/sv_snd.c server/sv_world.c \
	gamespy/sv_gamespy.c gamespy/sv_gqueryreporting.c

# sys_win32.c, con_passive.c and net_ip.c are replaced by src/*_xbox.c
SYSTEM := \
	sys/con_log.c sys/sys_main.c sys/new/sys_main_new.c sys/win_bounds.cpp \
	sys/win_localization.cpp

CLIENT := \
	client/cl_avi.cpp client/cl_cgame.cpp client/cl_cin.cpp client/cl_consolecmds.cpp \
	client/cl_input.cpp client/cl_instantAction.cpp client/cl_inv.cpp client/cl_invrender.cpp \
	client/cl_keys.cpp client/cl_main.cpp client/cl_net_chan.cpp client/cl_parse.cpp \
	client/cl_scrn.cpp client/cl_ui.cpp client/cl_uibind.cpp client/cl_uidmbox.cpp \
	client/cl_uifilepicker.cpp client/cl_uigamespy.cpp client/cl_uigmbox.cpp \
	client/cl_uilangame.cpp client/cl_uiloadsave.cpp client/cl_uimaprotationsetup.cpp \
	client/cl_uimaprunner.cpp client/cl_uiminicon.cpp client/cl_uimpmappicker.cpp \
	client/cl_uiplayermodelpicker.cpp client/cl_uiradar.cpp client/cl_uiserverlist.cpp \
	client/cl_uisoundpicker.cpp client/cl_uistd.cpp client/cl_uiview3d.cpp \
	client/snd_codec_mp3.c client/snd_codec_wav.c client/snd_codec.c \
	client/snd_dma_new.cpp client/snd_info.cpp client/snd_mem_new.cpp \
	client/snd_miles_new.cpp client/snd_openal_new.cpp client/qal.c client/usignal.cpp \
	gamespy/cl_gamespy.c \
	sdl/sdl_input.c sdl/sdl_mouse.c

UI := $(patsubst $(OM)/%,%,$(wildcard $(OM)/uilib/*.c $(OM)/uilib/*.cpp))

RENDERER := \
	renderercommon/tr_font.c renderercommon/tr_image_bmp.c renderercommon/tr_image_jpg.c \
	renderercommon/tr_image_pcx.c renderercommon/tr_image_png.c renderercommon/tr_image_pvr.c \
	renderercommon/tr_image_tga.c renderercommon/tr_noise.c renderercommon/puff.c \
	tiki/tiki_mesh.cpp \
	renderergl1/tr_backend.c renderergl1/tr_bsp.c renderergl1/tr_cmds.c renderergl1/tr_curve.c \
	renderergl1/tr_draw.c renderergl1/tr_flares.c renderergl1/tr_font.cpp renderergl1/tr_ghost.cpp \
	renderergl1/tr_image.c renderergl1/tr_init.c renderergl1/tr_light.c renderergl1/tr_main.c \
	renderergl1/tr_marks_permanent.c renderergl1/tr_marks.c renderergl1/tr_model.cpp \
	renderergl1/tr_scene.c renderergl1/tr_shade_calc.c renderergl1/tr_shade.c \
	renderergl1/tr_shader.c renderergl1/tr_shadows.c renderergl1/tr_sky_portal.cpp \
	renderergl1/tr_sky.c renderergl1/tr_sphere_shade.cpp renderergl1/tr_sprite.c \
	renderergl1/tr_staticmodels.cpp renderergl1/tr_sun_flare.cpp renderergl1/tr_surface.c \
	renderergl1/tr_swipe.cpp renderergl1/tr_terrain.c renderergl1/tr_util.cpp \
	renderergl1/tr_vis.cpp renderergl1/tr_world.c

MAD := $(addprefix thirdparty/libmad/,bit.c decoder.c fixed.c frame.c huffman.c \
	layer12.c layer3.c stream.c synth.c timer.c version.c)

# The port's own sources (xbox/mohaa/src), built with the engine flags
XBOX_SRCS := $(patsubst $(CURDIR)/src/%,%,$(wildcard $(CURDIR)/src/*.c $(CURDIR)/src/*.cpp))

ENGINE_SRCS := $(SERVER) $(CLIENT) $(UI) $(COMMON) $(SCRIPT_SYSTEM) $(SYSTEM) $(RENDERER) $(MAD)

BG := fgame/bg_misc.cpp fgame/bg_pmove.cpp fgame/bg_slidemove.cpp fgame/bg_voteoptions.cpp
MODULE_SHARED := qcommon/q_math.c qcommon/q_shared.c $(SCRIPT_SYSTEM)

CGAME_SRCS := $(patsubst $(OM)/%,%,$(wildcard $(OM)/cgame/*.c $(OM)/cgame/*.cpp)) \
	$(MODULE_SHARED) $(BG)

RECAST := $(patsubst $(OM)/%,%,$(wildcard \
	$(OM)/thirdparty/recastnavigation/Recast/Source/*.cpp \
	$(OM)/thirdparty/recastnavigation/Detour/Source/*.cpp \
	$(OM)/thirdparty/recastnavigation/DetourCrowd/Source/*.cpp))

GAME_SRCS := $(patsubst $(OM)/%,%,$(wildcard $(OM)/fgame/*.c $(OM)/fgame/*.cpp \
	$(OM)/script/*.c $(OM)/script/*.cpp)) \
	parser/parsetree.cpp parser/generated/yyParser.cpp parser/generated/yyLexer.cpp \
	$(MODULE_SHARED) $(RECAST)
GAME_SRCS := $(sort $(GAME_SRCS))

# ---------------------------------------------------------------------------
# Flags

CC  := nxdk-cc
CXX := nxdk-cxx

INCLUDES := -I$(CURDIR)/generated -I$(CURDIR)/compat -I$(OM)/qcommon -I$(OM)/script -I$(OM) \
	-I$(NXDK_DIR)/lib/sdl/SDL2/include -I$(NXDK_DIR)/lib/zlib/zlib \
	-I$(NXDK_DIR)/lib/libjpeg/libjpeg-turbo -I$(NXDK_DIR)/lib/libjpeg \
	-I$(NXDK_DIR)/lib/net/lwip/src/include -I$(NXDK_DIR)/lib/net/nforceif/include \
	-I$(OM)/thirdparty/openal-soft-1.24.3/include/AL -I$(CURDIR)/../../third_party/pbgl/include
CPPFLAGS := -DNDEBUG -DNXDK_PORT -DXBOX -DFPM_INTEL -include $(CURDIR)/compat/xbox_compat.h $(INCLUDES)
DEPFLAGS  = -MD -MP -MF $(@:.obj=.d)
WARN     := -Wno-everything
# SSE for float math (the Pentium III has it, x87 is slower: 80 -> 76 ms per
# frame in m1l1); double stays on x87 (SSE2 is not available)
FPFLAGS  ?= -msse -mfpmath=sse
# -fno-math-errno: sqrt and friends inline instead of calling the libc
CFLAGS   := -std=gnu17 -O2 -fno-strict-aliasing -fno-math-errno $(FPFLAGS) $(WARN)
CXXFLAGS := -std=gnu++17 -O2 -fno-strict-aliasing -fno-math-errno -fexceptions $(FPFLAGS) $(WARN)

# USE_OPENAL without DLOPEN: qal* calls straight into our OpenAL (src/xbox_al.c)
ENGINE_DEFS := -DBOTLIB -DAPP_MODULE -DUSE_OPENAL -DAL_LIBTYPE_STATIC -DUSE_CODEC_MAD=1 -I$(OM)/client
CGAME_DEFS  := -DCGAME_DLL
GAME_DEFS   := -DGAME_DLL -DWITH_SCRIPT_ENGINE -DARCHIVE_SUPPORTED \
	$(addprefix -I$(OM)/thirdparty/recastnavigation/,Recast/Include Detour/Include DetourCrowd/Include)

objs_of = $(addprefix $(BUILD)/$(1)/,$(addsuffix .obj,$(basename $(2))))
ENGINE_OBJS := $(call objs_of,engine,$(ENGINE_SRCS)) $(call objs_of,xbox,$(XBOX_SRCS))
CGAME_OBJS  := $(call objs_of,cgame,$(CGAME_SRCS))
GAME_OBJS   := $(call objs_of,game,$(GAME_SRCS))

.PHONY: objs engine cgame game clean
objs: engine cgame game
engine: $(ENGINE_OBJS)
cgame: $(CGAME_OBJS)
game: $(GAME_OBJS)

define module_rules
$(BUILD)/$(1)/%.obj: $(OM)/%.c
	@mkdir -p $$(dir $$@)
	@echo "[ CC $(1) ] $$*.c"
	@$(CC) $(CPPFLAGS) $(2) $(CFLAGS) $$(DEPFLAGS) -c -o $$@ $$<
$(BUILD)/$(1)/%.obj: $(OM)/%.cpp
	@mkdir -p $$(dir $$@)
	@echo "[ CXX $(1) ] $$*.cpp"
	@$(CXX) $(CPPFLAGS) $(2) $(CXXFLAGS) $$(DEPFLAGS) -c -o $$@ $$<
endef
$(eval $(call module_rules,engine,$(ENGINE_DEFS)))
$(eval $(call module_rules,cgame,$(CGAME_DEFS)))
$(eval $(call module_rules,game,$(GAME_DEFS)))

$(BUILD)/xbox/%.obj: $(CURDIR)/src/%.c
	@mkdir -p $(dir $@)
	@echo "[ CC xbox ] $*.c"
	@$(CC) $(CPPFLAGS) $(ENGINE_DEFS) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<
$(BUILD)/xbox/%.obj: $(CURDIR)/src/%.cpp
	@mkdir -p $(dir $@)
	@echo "[ CXX xbox ] $*.cpp"
	@$(CXX) $(CPPFLAGS) $(ENGINE_DEFS) $(CXXFLAGS) $(DEPFLAGS) -c -o $@ $<

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)

clean:
	rm -rf $(BUILD)

# ---------------------------------------------------------------------------
# Linking

COMMON_DIR := $(CURDIR)/../common
EH_OBJ := $(BUILD)/xbox/cxxeh.obj
$(EH_OBJ): $(COMMON_DIR)/cxxeh.cpp
	@mkdir -p $(dir $@)
	@echo "[ CXX xbox ] cxxeh.cpp"
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -c -o $@ $<

# pbgl (OpenGL 1.x on top of pbkit). PBGL_EXTRA=-DPBGL_DEBUG logs every GL
# error with its function and line through DbgPrint (shows up in the serial log).
PBGL_DIR  := $(CURDIR)/../../third_party/pbgl
PBGL_OBJS := $(patsubst $(PBGL_DIR)/src/%.c,$(BUILD)/pbgl/%.obj,$(wildcard $(PBGL_DIR)/src/*.c))
$(BUILD)/pbgl/%.obj: $(PBGL_DIR)/src/%.c
	@mkdir -p $(dir $@)
	@echo "[ CC pbgl ] $*.c"
	@$(CC) -I$(PBGL_DIR)/include -DXBOX -DPBGL_VERSION=0.1 -DPBGL_GL_VERSION=1.2 -O2 $(FPFLAGS) -Wno-everything \
		$(PBGL_EXTRA) $(DEPFLAGS) -c -o $@ $<

# eyalroz/printf (MIT): printf family with floating point support. Built
# without xbox_compat.h, which redirects the standard names to it.
PRINTF_DIR := $(CURDIR)/../../third_party/printf/src/printf
PRINTF_FLAGS := -DPRINTF_SUPPORT_LONG_LONG=1 -DPRINTF_MAX_INTEGRAL_DIGITS_FOR_DECIMAL=18 -DPRINTF_DECIMAL_BUFFER_SIZE=64 -DPRINTF_LOG10_TAYLOR_TERMS=8
PRINTF_OBJ := $(BUILD)/printf/printf.obj
$(PRINTF_OBJ): $(PRINTF_DIR)/printf.c
	@mkdir -p $(dir $@)
	@echo "[ CC printf ] printf.c"
	@$(CC) -I$(PRINTF_DIR)/.. $(PRINTF_FLAGS) -O2 -Wno-everything $(DEPFLAGS) -c -o $@ $<

# Modules with renamed symbols (see tools/prefix-module.sh)
$(BUILD)/cgame_p/.stamp: $(CGAME_OBJS) $(CURDIR)/tools/prefix-module.sh
	@rm -rf $(dir $@)
	@sh $(CURDIR)/tools/prefix-module.sh cg_ $(dir $@) _GetCGameAPI $(CGAME_OBJS)
	@touch $@
$(BUILD)/game_p/.stamp: $(GAME_OBJS) $(CURDIR)/tools/prefix-module.sh
	@rm -rf $(dir $@)
	@sh $(CURDIR)/tools/prefix-module.sh g_ $(dir $@) _GetGameAPI $(GAME_OBJS)
	@touch $@

NXDK_LIBS := $(addprefix $(NXDK_DIR)/lib/,libpdclib.lib libwinapi.lib libnxdk_hal.lib \
	libnxdk.lib libnxdk_automount_d.lib libpbkit.lib nxdk_usb.lib libxboxrt.lib libzlib.lib \
	libpng.lib libjpeg.lib libc++.lib libSDL2.lib libnxdk_net.lib winmm.lib) \
	$(NXDK_DIR)/lib/xboxkrnl/libxboxkrnl.lib

# Same as nxdk's lib/nxdk/Makefile: nothing references the D: automount, so it
# must be pulled in explicitly or D:\ never gets mounted.
# nxdk-link defaults to a 64 KB main thread stack; the engine needs far more
# (4 KB print buffers, large renderer temporaries). The last -stack wins.
NXDK_LINK_FLAGS := -include:_automount_d_drive -stack:1048576
# Linker map for tools/addr2sym.py (crash addresses), rewritten on every link
NXDK_LINK_FLAGS += -map:$(BUILD)/main.map

XBE_TITLE := MOHAA
OUT := $(CURDIR)/bin

.PHONY: link xbe
link: $(BUILD)/main.exe
$(BUILD)/main.exe: $(ENGINE_OBJS) $(EH_OBJ) $(PBGL_OBJS) $(PRINTF_OBJ) $(BUILD)/cgame_p/.stamp $(BUILD)/game_p/.stamp
	@echo "[ LD ] $@"
	@nxdk-link -out:$@ $(ENGINE_OBJS) $(EH_OBJ) $(PBGL_OBJS) $(PRINTF_OBJ) $(BUILD)/cgame_p/*.obj $(BUILD)/game_p/*.obj \
		$(NXDK_LIBS) $(NXDK_LINK_FLAGS) $(LDFLAGS)

xbe: $(OUT)/default.xbe
$(OUT)/default.xbe: $(BUILD)/main.exe
	@mkdir -p $(OUT)
	@echo "[ CXBE ] $@"
	@$(NXDK_DIR)/tools/cxbe/cxbe -OUT:$@ -TITLE:"$(XBE_TITLE)" $< > /dev/null
	@python3 $(CURDIR)/tools/xbe-128mb.py $@ > /dev/null

# For trying changes: a new XBE on the game that tools/pack_iso.py left in output/work/stage.
# AUTOEXEC picks the autoexec.cfg, e.g. make iso AUTOEXEC=data/autoexec_weapons.cfg
# A release is built with tools/pack_iso.py, which writes the image and the folder.
AUTOEXEC ?= data/autoexec.cfg
ISO_DIR := $(abspath $(CURDIR)/../../output)
STAGE   := $(ISO_DIR)/work/stage
.PHONY: iso folder stage
stage: $(OUT)/default.xbe
	@test -d $(STAGE)/main || { echo "No $(STAGE): run python tools/pack_iso.py first"; exit 1; }
	@rm -f $(STAGE)/default.xbe $(STAGE)/main/autoexec.cfg $(STAGE)/main/xbox_default.cfg
	@cp $(OUT)/default.xbe $(STAGE)/default.xbe
	@cp $(CURDIR)/$(AUTOEXEC) $(STAGE)/main/autoexec.cfg
	@cp $(CURDIR)/data/xbox_default.cfg $(STAGE)/main/xbox_default.cfg

iso: stage
	@echo "[ XISO ] $(ISO_DIR)/mohaa.iso"
	@$(NXDK_DIR)/tools/extract-xiso/build/extract-xiso -c $(STAGE) $(ISO_DIR)/mohaa.iso > /dev/null

# The same files as a folder (output/mohaa/), to copy to the console's hard
# disk and start default.xbe from there: the game reads its data from D:\,
# which nxdk maps to the XBE's folder
folder: stage
	@echo "[ FOLDER ] $(ISO_DIR)/mohaa"
	@rm -rf $(ISO_DIR)/mohaa
	@cp -rL $(STAGE) $(ISO_DIR)/mohaa
