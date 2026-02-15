#-------------------------------------------------------------------------------
.SUFFIXES:
#-------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)

#-------------------------------------------------------------------------------
# APP_NAME sets the long name of the application
# APP_SHORTNAME sets the short name of the application
# APP_AUTHOR sets the author of the application
#-------------------------------------------------------------------------------
APP_NAME		:= UTheme
APP_SHORTNAME	:= UTheme
APP_AUTHOR		:= Xziip

include $(DEVKITPRO)/wut/share/wut_rules

#-------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
# CONTENT is the path to the bundled folder that will be mounted as /vol/content/
# ICON is the game icon, leave blank to use default rule
# TV_SPLASH is the image displayed during bootup on the TV, leave blank to use default rule
# DRC_SPLASH is the image displayed during bootup on the DRC, leave blank to use default rule
#-------------------------------------------------------------------------------
TARGET		:=	UTheme
BUILD		:=	build
SOURCES		:=	source source/screens source/input source/utils source/utils/minizip \
			source/utils/src/dec source/utils/src/dsp source/utils/src/utils
DATA		:=	data
INCLUDES	:=	source include source/utils
CONTENT		:=
ICON		:=	res/icon.png
TV_SPLASH	:=	res/banner.png
DRC_SPLASH	:=	res/banner.png

#-------------------------------------------------------------------------------
# options for code generation
#-------------------------------------------------------------------------------
CURL_CFLAGS := $(shell $(PREFIX)pkg-config --cflags libcurl)
CURL_LIBS := $(shell $(PREFIX)pkg-config --libs libcurl)

SDL2_CFLAGS := $(shell $(PREFIX)pkg-config --cflags sdl2 SDL2_mixer SDL2_image SDL2_ttf)
SDL2_LIBS := $(shell $(PREFIX)pkg-config --libs sdl2 SDL2_mixer SDL2_image SDL2_ttf)

# Use the libmocha submodule.
EXTERNAL_LIBMOCHA_DIR := $(TOPDIR)/external/libmocha

CXX += -std=gnu++20

CFLAGS	:=	-Wall -O2 -ffunction-sections \
		$(MACHDEP)

CFLAGS	+=	$(INCLUDE) -D__WIIU__ -D__WUT__ \
		-DWEBP_DISABLE_STATS \
		-DWEBP_REDUCE_SIZE -DWEBP_REDUCE_CSP \
		$(CURL_CFLAGS) \
		$(SDL2_CFLAGS)

CXXFLAGS	:= $(CFLAGS)

ASFLAGS	:=	$(ARCH)
LDFLAGS	=	$(ARCH) $(RPXSPECS) -Wl,-Map,$(notdir $*.map) -Wl,--allow-multiple-definition

LIBS	:=	-lmocha $(SDL2_LIBS) $(CURL_LIBS) -lharfbuzz -lwut

#-------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level
# containing include and lib
#-------------------------------------------------------------------------------
LIBDIRS	:= \
	$(EXTERNAL_LIBMOCHA_DIR) \
	$(PORTLIBS) \
	$(WUT_ROOT)

#-------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#-------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#-------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(filter-out BGM.mp3,$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*))))

#-------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#-------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#-------------------------------------------------------------------------------
	export LD	:=	$(CC)
#-------------------------------------------------------------------------------
else
#-------------------------------------------------------------------------------
	export LD	:=	$(CXX)
#-------------------------------------------------------------------------------
endif
#-------------------------------------------------------------------------------

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES 	:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD) -I$(DEVKITPRO)/portlibs/wiiu/include/SDL2

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifneq (,$(strip $(CONTENT)))
	export APP_CONTENT := $(TOPDIR)/$(CONTENT)
endif

ifneq (,$(strip $(ICON)))
	export APP_ICON := $(TOPDIR)/$(ICON)
else ifneq (,$(wildcard $(TOPDIR)/$(TARGET).png))
	export APP_ICON := $(TOPDIR)/$(TARGET).png
else ifneq (,$(wildcard $(TOPDIR)/icon.png))
	export APP_ICON := $(TOPDIR)/icon.png
endif

ifneq (,$(strip $(TV_SPLASH)))
	export APP_TV_SPLASH := $(TOPDIR)/$(TV_SPLASH)
else ifneq (,$(wildcard $(TOPDIR)/tv-splash.png))
	export APP_TV_SPLASH := $(TOPDIR)/tv-splash.png
else ifneq (,$(wildcard $(TOPDIR)/splash.png))
	export APP_TV_SPLASH := $(TOPDIR)/splash.png
endif

ifneq (,$(strip $(DRC_SPLASH)))
	export APP_DRC_SPLASH := $(TOPDIR)/$(DRC_SPLASH)
else ifneq (,$(wildcard $(TOPDIR)/drc-splash.png))
	export APP_DRC_SPLASH := $(TOPDIR)/drc-splash.png
else ifneq (,$(wildcard $(TOPDIR)/splash.png))
	export APP_DRC_SPLASH := $(TOPDIR)/splash.png
endif

.PHONY: $(BUILD) clean all

#-------------------------------------------------------------------------------
all: $(BUILD)

$(BUILD):
	@$(MAKE) --no-print-directory -C $(EXTERNAL_LIBMOCHA_DIR)
	@mkdir -p $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#-------------------------------------------------------------------------------
clean:
	@echo clean ...
	@$(RM) -r $(BUILD) $(TARGET).wuhb $(TARGET).rpx $(TARGET).elf
	@$(MAKE) --no-print-directory -C $(EXTERNAL_LIBMOCHA_DIR) clean

#-------------------------------------------------------------------------------
else
.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

#-------------------------------------------------------------------------------
# main targets
#-------------------------------------------------------------------------------
all	:	$(OUTPUT).wuhb

$(OUTPUT).wuhb : $(OUTPUT).rpx
$(OUTPUT).rpx	:	$(OUTPUT).elf
$(OUTPUT).elf	:	$(OFILES)

$(OFILES_SRC)	: $(HFILES_BIN)

#-------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#-------------------------------------------------------------------------------
%.bin.o	%_bin.h :	%.bin
#-------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#-------------------------------------------------------------------------------
%.ttf.o	%_ttf.h :	%.ttf
#-------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#-------------------------------------------------------------------------------
%.bdf.o	%_bdf.h :	%.bdf
#-------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#-------------------------------------------------------------------------------
%.json.o	%_json.h :	%.json
#-------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#-------------------------------------------------------------------------------
%.ogg.o	%_ogg.h :	%.ogg
#-------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#-------------------------------------------------------------------------------
%.pem.o	%_pem.h :	%.pem
#-------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#-------------------------------------------------------------------------------
endif
#-------------------------------------------------------------------------------
