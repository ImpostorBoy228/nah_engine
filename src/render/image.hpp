#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// minimal png loader: 8-bit gray/rgb/graya/rgba, non-interlaced, -> rgba8
// no external deps, inflate written by hand. header only.
namespace img {

    struct Image {
        uint32_t width;
        uint32_t height;
        uint8_t* pixels; // rgba8, width*height*4, owned by malloc
    };

    // ---------------------------------------------------------------- inflate

    struct BitReader {
        const uint8_t* data;
        size_t len;
        size_t pos;
        uint32_t bitbuf;
        int bitcnt;
    };

    static int readBit(BitReader* br) {
        if (br->bitcnt == 0) {
            if (br->pos >= br->len)
                return -1;
            br->bitbuf = br->data[br->pos++];
            br->bitcnt = 8;
        }
        int bit = br->bitbuf & 1;
        br->bitbuf >>= 1;
        br->bitcnt -= 1;
        return bit;
    }

    static int readBits(BitReader* br, int n) {
        int val = 0;
        for (int i = 0; i < n; i++) {
            int bit = readBit(br);
            if (bit < 0)
                return -1;
            val |= bit << i;
        }
        return val;
    }

    static void alignByte(BitReader* br) {
        br->bitcnt = 0;
    }

    struct Huffman {
        int counts[16];
        int symbols[288];
    };

    // canonical huffman from code lengths, puff style
    static int buildHuffman(Huffman* h, const uint8_t* lengths, int n) {
        memset(h->counts, 0, sizeof(h->counts));
        for (int i = 0; i < n; i++)
            h->counts[lengths[i]] += 1;
        if (h->counts[0] == n)
            return 0; // no codes, allowed for distance tree

        int left = 1;
        for (int len = 1; len < 16; len++) {
            left <<= 1;
            left -= h->counts[len];
            if (left < 0)
                return -1; // over-subscribed
        }

        int offs[16];
        offs[1] = 0;
        for (int len = 1; len < 15; len++)
            offs[len + 1] = offs[len] + h->counts[len];

        for (int i = 0; i < n; i++) {
            if (lengths[i])
                h->symbols[offs[lengths[i]]++] = i;
        }
        return 0;
    }

    static int decodeSymbol(BitReader* br, const Huffman* h) {
        int code = 0;
        int first = 0;
        int index = 0;
        for (int len = 1; len < 16; len++) {
            int bit = readBit(br);
            if (bit < 0)
                return -1;
            code |= bit;
            int count = h->counts[len];
            if (code - first < count)
                return h->symbols[index + (code - first)];
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        return -1;
    }

    static const uint16_t LENGTH_BASE[29] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258,
    };
    static const uint8_t LENGTH_EXTRA[29] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
    };
    static const uint16_t DIST_BASE[30] = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193,
        12289, 16385, 24577,
    };
    static const uint8_t DIST_EXTRA[30] = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
        7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13,
    };

    // inflate a raw deflate stream into out, returns decompressed size or (size_t)-1
    static size_t inflateRaw(const uint8_t* src, size_t src_len, uint8_t* out, size_t out_cap) {
        BitReader br = { src, src_len, 0, 0, 0 };
        size_t out_pos = 0;
        int last;

        do {
            last = readBits(&br, 1);
            int type = readBits(&br, 2);
            if (last < 0 || type < 0)
                return (size_t)-1;

            if (type == 0) {
                alignByte(&br);
                if (br.pos + 4 > br.len)
                    return (size_t)-1;
                int len = br.data[br.pos] | (br.data[br.pos + 1] << 8);
                br.pos += 4;
                if (br.pos + (size_t)len > br.len)
                    return (size_t)-1;
                if (out_pos + (size_t)len > out_cap)
                    return (size_t)-1;
                memcpy(out + out_pos, br.data + br.pos, (size_t)len);
                br.pos += (size_t)len;
                out_pos += (size_t)len;
            } else {
                Huffman lit;
                Huffman dist;
                if (type == 1) {
                    uint8_t lengths[288];
                    for (int i = 0; i < 144; i++) lengths[i] = 8;
                    for (int i = 144; i < 256; i++) lengths[i] = 9;
                    for (int i = 256; i < 280; i++) lengths[i] = 7;
                    for (int i = 280; i < 288; i++) lengths[i] = 8;
                    buildHuffman(&lit, lengths, 288);
                    uint8_t d[30];
                    for (int i = 0; i < 30; i++) d[i] = 5;
                    buildHuffman(&dist, d, 30);
                } else if (type == 2) {
                    int hlit = readBits(&br, 5) + 257;
                    int hdist = readBits(&br, 5) + 1;
                    int hclen = readBits(&br, 4) + 4;
                    if (hlit < 0 || hdist < 0 || hclen < 0)
                        return (size_t)-1;
                    static const uint8_t ORDER[19] = {
                        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15,
                    };
                    uint8_t clen[19] = {};
                    for (int i = 0; i < hclen; i++)
                        clen[ORDER[i]] = (uint8_t)readBits(&br, 3);
                    Huffman cl;
                    if (buildHuffman(&cl, clen, 19) != 0)
                        return (size_t)-1;
                    uint8_t lengths[288 + 30] = {};
                    int n = 0;
                    while (n < hlit + hdist) {
                        int sym = decodeSymbol(&br, &cl);
                        if (sym < 0)
                            return (size_t)-1;
                        if (sym < 16) {
                            lengths[n++] = (uint8_t)sym;
                        } else if (sym == 16) {
                            if (n == 0)
                                return (size_t)-1;
                            uint8_t prev = lengths[n - 1];
                            int rep = 3 + readBits(&br, 2);
                            if (rep < 0)
                                return (size_t)-1;
                            while (rep-- && n < hlit + hdist) lengths[n++] = prev;
                        } else if (sym == 17) {
                            int rep = 3 + readBits(&br, 3);
                            if (rep < 0)
                                return (size_t)-1;
                            while (rep-- && n < hlit + hdist) lengths[n++] = 0;
                        } else {
                            int rep = 11 + readBits(&br, 7);
                            if (rep < 0)
                                return (size_t)-1;
                            while (rep-- && n < hlit + hdist) lengths[n++] = 0;
                        }
                    }
                    if (buildHuffman(&lit, lengths, hlit) != 0)
                        return (size_t)-1;
                    if (buildHuffman(&dist, lengths + hlit, hdist) != 0)
                        return (size_t)-1;
                } else {
                    return (size_t)-1;
                }

                for (;;) {
                    int sym = decodeSymbol(&br, &lit);
                    if (sym < 0)
                        return (size_t)-1;
                    if (sym < 256) {
                        if (out_pos >= out_cap)
                            return (size_t)-1;
                        out[out_pos++] = (uint8_t)sym;
                    } else if (sym == 256) {
                        break;
                    } else {
                        sym -= 257;
                        if (sym >= 29)
                            return (size_t)-1;
                        int length = LENGTH_BASE[sym] + readBits(&br, LENGTH_EXTRA[sym]);
                        int dsym = decodeSymbol(&br, &dist);
                        if (dsym < 0 || dsym >= 30)
                            return (size_t)-1;
                        size_t distance = (size_t)(DIST_BASE[dsym] + readBits(&br, DIST_EXTRA[dsym]));
                        if (length < 0 || distance == 0 || distance > out_pos)
                            return (size_t)-1;
                        if (out_pos + (size_t)length > out_cap)
                            return (size_t)-1;
                        size_t from = out_pos - distance;
                        for (int i = 0; i < length; i++)
                            out[out_pos + i] = out[from + i];
                        out_pos += (size_t)length;
                    }
                }
            }
        } while (!last);

        return out_pos;
    }

    // zlib wrapper: 2 byte header then deflate, adler32 tail ignored
    static size_t zlibInflate(const uint8_t* src, size_t src_len, uint8_t* out, size_t out_cap) {
        if (src_len < 2)
            return (size_t)-1;
        return inflateRaw(src + 2, src_len - 2, out, out_cap);
    }

    // ------------------------------------------------------------ png decode

    static uint32_t read32(const uint8_t* p) {
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    }

    static int paeth(int a, int b, int c) {
        int p = a + b - c;
        int pa = abs(p - a);
        int pb = abs(p - b);
        int pc = abs(p - c);
        if (pa <= pb && pa <= pc)
            return a;
        if (pb <= pc)
            return b;
        return c;
    }

    // undo png row filters in place, each row is [filter byte][rowbytes]
    static void unfilter(uint8_t* raw, uint32_t rowbytes, uint32_t height, uint32_t bpp) {
        uint8_t* prev = NULL;
        for (uint32_t y = 0; y < height; y++) {
            uint8_t* row = raw + (size_t)y * (rowbytes + 1);
            int filter = row[0];
            uint8_t* cur = row + 1;
            for (uint32_t x = 0; x < rowbytes; x++) {
                int a = x >= bpp ? cur[x - bpp] : 0;
                int b = prev ? prev[x] : 0;
                int c = (prev && x >= bpp) ? prev[x - bpp] : 0;
                int f = cur[x];
                switch (filter) {
                    case 1: cur[x] = (uint8_t)(f + a); break;
                    case 2: cur[x] = (uint8_t)(f + b); break;
                    case 3: cur[x] = (uint8_t)(f + ((a + b) >> 1)); break;
                    case 4: cur[x] = (uint8_t)(f + paeth(a, b, c)); break;
                    default: break; // 0 = none, already literal
                }
            }
            prev = cur;
        }
    }

    // decode a png file into rgba8. supports 8-bit gray/rgb/graya/rgba, non-interlaced.
    static bool loadPng(const char* path, Image* out) {
        out->width = out->height = 0;
        out->pixels = NULL;

        FILE* f = fopen(path, "rb");
        if (!f) {
            fprintf(stderr, "img: cannot open %s\n", path);
            return false;
        }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz < 8) {
            fclose(f);
            return false;
        }
        uint8_t* file = (uint8_t*)malloc((size_t)sz);
        if (!file || fread(file, 1, (size_t)sz, f) != (size_t)sz) {
            free(file);
            fclose(f);
            return false;
        }
        fclose(f);

        static const uint8_t SIG[8] = { 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a };
        if (memcmp(file, SIG, 8) != 0) {
            fprintf(stderr, "img: %s is not a png\n", path);
            free(file);
            return false;
        }

        uint32_t width = 0, height = 0;
        int bit_depth = 0, color_type = 0, interlace = 0;
        uint8_t* idat = NULL;
        size_t idat_len = 0;

        size_t pos = 8;
        bool got_ihdr = false;
        while (pos + 8 <= (size_t)sz) {
            uint32_t clen = read32(file + pos);
            const uint8_t* type = file + pos + 4;
            const uint8_t* data = file + pos + 8;
            if (pos + 12 + (size_t)clen > (size_t)sz)
                break;

            if (memcmp(type, "IHDR", 4) == 0 && clen >= 13) {
                width = read32(data);
                height = read32(data + 4);
                bit_depth = data[8];
                color_type = data[9];
                interlace = data[12];
                got_ihdr = true;
            } else if (memcmp(type, "IDAT", 4) == 0) {
                uint8_t* n = (uint8_t*)realloc(idat, idat_len + clen);
                if (!n)
                    break;
                idat = n;
                memcpy(idat + idat_len, data, clen);
                idat_len += clen;
            } else if (memcmp(type, "IEND", 4) == 0) {
                break;
            }
            pos += 12 + (size_t)clen;
        }

        int channels = 0;
        if (color_type == 0) channels = 1;
        else if (color_type == 2) channels = 3;
        else if (color_type == 4) channels = 2;
        else if (color_type == 6) channels = 4;

        bool ok = got_ihdr && width > 0 && height > 0 && bit_depth == 8 &&
            interlace == 0 && channels > 0 && idat != NULL;
        if (!ok) {
            fprintf(stderr, "img: %s unsupported (depth=%d type=%d interlace=%d)\n",
                path, bit_depth, color_type, interlace);
            free(idat);
            free(file);
            return false;
        }

        uint32_t rowbytes = width * (uint32_t)channels;
        size_t raw_size = ((size_t)rowbytes + 1) * height;
        uint8_t* raw = (uint8_t*)malloc(raw_size);
        size_t got = zlibInflate(idat, idat_len, raw, raw_size);
        free(idat);
        free(file);
        if (got != raw_size) {
            fprintf(stderr, "img: %s inflate mismatch (%zu vs %zu)\n", path, got, raw_size);
            free(raw);
            return false;
        }

        unfilter(raw, rowbytes, height, (uint32_t)channels);

        uint8_t* rgba = (uint8_t*)malloc((size_t)width * height * 4);
        for (uint32_t y = 0; y < height; y++) {
            const uint8_t* row = raw + (size_t)y * (rowbytes + 1) + 1;
            for (uint32_t x = 0; x < width; x++) {
                uint8_t* d = rgba + ((size_t)y * width + x) * 4;
                const uint8_t* s = row + (size_t)x * channels;
                if (color_type == 0) {
                    d[0] = d[1] = d[2] = s[0]; d[3] = 255;
                } else if (color_type == 2) {
                    d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255;
                } else if (color_type == 4) {
                    d[0] = d[1] = d[2] = s[0]; d[3] = s[1];
                } else {
                    d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
                }
            }
        }
        free(raw);

        out->width = width;
        out->height = height;
        out->pixels = rgba;
        return true;
    }

    static void freeImage(Image* img) {
        free(img->pixels);
        img->pixels = NULL;
        img->width = img->height = 0;
    }

} // namespace img
