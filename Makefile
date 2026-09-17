STD_FLAGS = -std=c++26 -Wall -Wextra -Isrc -Iexternal -DRGFW_VULKAN
LIBS = -lvulkan -lX11 -lXext -lXcursor -lXrandr -lXdmcp -lXau -ldl
SRCS = src/main.cpp src/rgfw.cpp src/vulkanshit.cpp
OBJS = $(SRCS:src/%.cpp=obj/%.o)

all: shaders huinya_engine

huinya_engine: $(OBJS)
	g++ $(STD_FLAGS) -O2 -DNDEBUG $^ -o $@ $(LIBS)

obj/%.o: src/%.cpp | obj
	g++ $(STD_FLAGS) -O2 -DNDEBUG -c $< -o $@

dev: shaders huinya_engine

huinya_engine: $(OBJS)
	g++ $(STD_FLAGS) -O0 -g $^ -o $@ $(LIBS)

obj/%.o: src/%.cpp | obj
	g++ $(STD_FLAGS) -O0 -g -c $< -o $@

shaders: shaders/vert.spv shaders/frag.spv

shaders/vert.spv: shaders/vert.glsl
	glslc -fshader-stage=vert $< -o $@

shaders/frag.spv: shaders/frag.glsl
	glslc -fshader-stage=frag $< -o $@

obj:
	mkdir -p $@

clean:
	rm -rf obj huinya_engine shaders/*.spv

.PHONY: all dev shaders clean
