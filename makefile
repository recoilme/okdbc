#gcc -o pudge pudge.c shared.c sophia/sophia.c server.c workqueue.c commands.c -lpthread -levent -I./sophia -I/usr/local/include/ -L/usr/local/lib
# binary name and location
NAME=okdb
OUT_ROOT=.
# temporary folder will be cleared out and deleted between fresh builds
# All object files will be placed in this folder
TMP_ROOT=./tmp
# the .c and .cpp source files root folder - subfolders are automatically included
SRC_ROOT=.
# any allowed subfolders in the src root
SRC_SUB_FOLDERS=sophia
# any librries required (write in full flags)
LINKER_FLAGS=-lpthread -levent -L/usr/local/lib
# any include folders, space seperated list
INCLUDE=/usr/local/include sophia
# optimization level.
OPTIMIZATION= -O3 -march=native -DSERVER_DELAY_IO=1 -DDEBUG
# Warnings... i.e. -Wpedantic -Weverything -Wno-format-pedantic
WARNINGS= -Wall -Wextra -Wno-missing-field-initializers -Wno-unused-function

##############
## OS specific data - compiler, assembler etc.

ifneq ($(OS),Windows_NT)
	OS := $(shell uname)
endif
ifeq ($(OS),Darwin) # Run MacOS commands
	# c compiler
	CC=@gcc
	# c++ compiler
	CPP=@g++
	# debugger
	DB=@lldb
	# disassemble tool. Use stub to disable.
	DISAMS=@otool -tVX
	# documentation commands
	# DOCUMENTATION=cldoc generate $(INCLUDE_STR) -- --output ./html $(foreach dir, $(SRCDIR), $(wildcard $(addsuffix /, $(basename $(dir)))*.h*))


else
	# c compiler
	CC=@gcc
	# c++ compiler
	CPP=@g++
	# debugger
	DB=@gdb
	# disassemble tool, leave undefined.
	# DISAMS=@otool -tVX
	DOCUMENTATION=

endif

#auto computed values
BIN = $(OUT_ROOT)/$(NAME)
SRCDIR = $(SRC_ROOT) $(foreach dir, $(SRC_SUB_FOLDERS), $(addsuffix /,$(basename $(SRC_ROOT)))$(dir))
SRC = $(foreach dir, $(SRCDIR), $(wildcard $(addsuffix /, $(basename $(dir)))*.c*))
BUILDTREE =$(foreach dir, $(SRCDIR), $(addsuffix /, $(basename $(TMP_ROOT)))$(basename $(dir)))
OBJS = $(foreach source, $(SRC), $(addprefix $(TMP_ROOT)/, $(addsuffix .o, $(basename $(source)))))
CCL = $(CC)
INCLUDE_STR = $(foreach dir,$(INCLUDE),$(addprefix -I, $(dir))) $(foreach dir,$(SRCDIR),$(addprefix -I, $(dir)))
# the C flags
CFLAGS= -g -std=c11 $(WARNINGS) $(OPTIMIZATION) $(INCLUDE_STR)
CPPFLAGS= -std=c++11 $(WARNINGS) $(OPTIMIZATION) $(INCLUDE_STR)

$(NAME): clean build run

build: $(OBJS)
	$(CCL) -o $(BIN) $^ $(OPTIMIZATION) $(LINKER_FLAGS)
	$(DOCUMENTATION)

ifdef DISAMS
$(TMP_ROOT)/%.o: %.c
	$(CC) -o $@ -c $^ $(CFLAGS)
	$(DISAMS) $@ > $@.s

$(TMP_ROOT)/%.o: %.cpp
	$(CPP) -o $@ -c $^ $(CPPFLAGS)
	$(eval CCL = $(CPP))
	$(DISAMS) $@ > $@.s

else
$(TMP_ROOT)/%.o: %.c
	$(CC) -o $@ -c $^ $(CFLAGS)

$(TMP_ROOT)/%.o: %.cpp
	$(CPP) -o $@ -c $^ $(CPPFLAGS)
	$(eval CCL = $(CPP))
endif

.PHONY : clean
clean:
	-@rm -rf $(BIN)
	-@rm -rf $(TMP_ROOT)
	-@mkdir -p $(BUILDTREE)
asrv:
	-@rm async
	gcc -o asyncsrv asyncsrv.c -lpthread -levent -I/usr/local/include -L/usr/local/lib

execute:
	@$(BIN)

.PHONY : run
run: | build execute

.PHONY : db
db: | clean build
	$(DB) $(BIN)

vars:
	@echo "BIN: $(BIN)"
	@echo ""
	@echo "SRCDIR: $(SRCDIR)"
	@echo ""
	@echo "SRC: $(SRC)"
	@echo ""
	@echo "BUILDTREE: $(BUILDTREE)"
	@echo ""
	@echo "OBJS: $(OBJS)"
	@echo ""
	@echo "CFLAGS: $(CFLAGS)"
	@echo ""
	@echo "CPPFLAGS: $(CPPFLAGS)"
