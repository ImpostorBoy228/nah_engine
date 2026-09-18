STD_FLAGS = -std=c++26 -Wall -Wextra -Isrc -Iexternal -DRGFW_VULKAN -fopt-info-vec-all
LIBS = -lvulkan -lX11 -lXext -lXcursor -lXrandr -lXdmcp -lXau -ldl
SRCS = src/main.cpp src/rgfw.cpp src/vulkanshit.cpp

ifeq ($(BUILD_TYPE),dev)
OBJDIR = obj/dev
BUILD_FLAGS = -O0 -g
else
OBJDIR = obj
BUILD_FLAGS = -O2 -DNDEBUG
endif

OBJS = $(SRCS:src/%.cpp=$(OBJDIR)/%.o)

all: shaders huinya_engine

dev:
	$(MAKE) BUILD_TYPE=dev

huinya_engine: $(OBJS)
	g++ $(STD_FLAGS) $(BUILD_FLAGS) $^ -o $@ $(LIBS)

$(OBJDIR)/%.o: src/%.cpp | $(OBJDIR)
	g++ $(STD_FLAGS) $(BUILD_FLAGS) -c $< -o $@

$(OBJDIR):
	mkdir -p $@

shaders: shaders/vert.spv shaders/frag.spv

shaders/vert.spv: shaders/vert.glsl
	glslc -fshader-stage=vert $< -o $@

shaders/frag.spv: shaders/frag.glsl
	glslc -fshader-stage=frag $< -o $@

deps:
	cd depfetch && cargo run

clean:
	rm -rf obj obj/dev huinya_engine shaders/*.spv

.PHONY: all dev shaders clean deps
