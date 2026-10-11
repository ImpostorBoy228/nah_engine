CITRUS    := citrus/target/release/citrus
RGFW_H    := shit/external/RGFW.h
LUAJIT_A  := shit/external/luajit/src/libluajit.a

all: $(CITRUS) $(RGFW_H) $(LUAJIT_A)
	./$(CITRUS) make

$(RGFW_H): $(CITRUS)
	./$(CITRUS) depfetch

$(LUAJIT_A): $(CITRUS)
	./$(CITRUS) depmake

$(CITRUS):
	cargo build --release --manifest-path citrus/Cargo.toml

fetch: $(CITRUS)
	./$(CITRUS) depfetch

depmake: $(LUAJIT_A)

protocols: $(CITRUS)
	./$(CITRUS) protocols

shaders: $(CITRUS)
	./$(CITRUS) shadermake

run:
	./shit/build/bin/nah_engine

clean: $(CITRUS)
	./$(CITRUS) clean

.PHONY: all fetch depmake protocols shaders clean
.NOTPARALLEL: