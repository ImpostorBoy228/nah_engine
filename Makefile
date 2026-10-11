DEPFETCH  := depfetch/target/release/depfetch
RGFW_H    := shit/external/RGFW.h

all: $(DEPFETCH) $(RGFW_H)
	./$(DEPFETCH) make

$(RGFW_H): $(DEPFETCH)
	./$(DEPFETCH) depfetch

$(DEPFETCH):
	cargo build --release --manifest-path depfetch/Cargo.toml

fetch: $(DEPFETCH)
	./$(DEPFETCH) depfetch

protocols: $(DEPFETCH)
	./$(DEPFETCH) protocols

shaders: $(DEPFETCH)
	./$(DEPFETCH) shadermake

run:
	./shit/build/bin/nah_engine

clean: $(DEPFETCH)
	./$(DEPFETCH) clean

.PHONY: all fetch protocols shaders clean
.NOTPARALLEL:
