STD_FLAGS = -std=c++26 -Wall -Wextra -Isrc -Iexternal -DRGFW_VULKAN
LIBS = -lvulkan -lX11 -lXext -lXcursor -lXrandr -lXdmcp -lXau -ldl
TARGET = huinya_engine
SRCS = src/main.cpp src/rgfw.cpp src/vulkanshit.cpp
RELEASE_FLAGS = -O2 -DNDEBUG
DEV_FLAGS = -O0 -g
OBJDIR = obj
OBJS = $(SRCS:src/%.cpp=$(OBJDIR)/%.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	g++ $(STD_FLAGS) $(RELEASE_FLAGS) $^ -o $@ $(LIBS)

$(OBJDIR)/%.o: src/%.cpp | $(OBJDIR)
	g++ $(STD_FLAGS) $(RELEASE_FLAGS) -c $< -o $@

dev: $(TARGET)

$(TARGET): $(OBJS)
	g++ $(STD_FLAGS) $(DEV_FLAGS) $^ -o $@ $(LIBS)

$(OBJDIR)/%.o: src/%.cpp | $(OBJDIR)
	g++ $(STD_FLAGS) $(DEV_FLAGS) -c $< -o $@

$(OBJDIR):
	mkdir -p $@

clean:
	rm -rf obj $(TARGET)

.PHONY: all dev clean
