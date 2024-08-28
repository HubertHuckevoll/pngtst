// https://github.com/zenmumbler/pngread/blob/master/pngread.cpp
// compile with wcl /3 /d2 /fp5 /mh program.c zlib.lib

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "zlib.h"

// Structures must be packed to ensure correct alignment in 16-bit DOS
#pragma pack(push, 1)

struct ChunkHeader {
    unsigned long dataSize;
    unsigned long chunkType;
};

struct IHDRChunk {
    unsigned long Width;
    unsigned long Height;
    unsigned char BitDepth;
    unsigned char ColorType;
    unsigned char Compression;
    unsigned char Filter;
    unsigned char Interlace;
};

#pragma pack(pop)

#define HEADER_CHUNK 0x52444849  // 'IHDR' in little-endian
#define IMAGE_DATA_CHUNK 0x54414449  // 'IDAT' in little-endian
#define END_CHUNK 0x444E4549  // 'IEND' in little-endian

enum LineFilter {
    LFNone = 0,
    LFSub = 1,
    LFUp = 2,
    LFAverage = 3,
    LFPaeth = 4
};

unsigned char far* loadFile(const char *filename, unsigned long *fileSize) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    *fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);

    unsigned char far* buffer = (unsigned char far*)farmalloc(*fileSize);
    if (!buffer) {
        fclose(file);
        return NULL;
    }

    fread(buffer, 1, *fileSize, file);
    fclose(file);
    return buffer;
}

int inflateBuffer(const unsigned char far* source, unsigned long sourceSize, unsigned char far* dest, unsigned long destSize) {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.avail_in = (unsigned int)sourceSize;
    strm.next_in = (unsigned char*)source;
    strm.avail_out = (unsigned int)destSize;
    strm.next_out = dest;

    int ret = inflateInit(&strm);
    if (ret != Z_OK) {
        return ret;
    }

    ret = inflate(&strm, Z_NO_FLUSH);
    inflateEnd(&strm);
    return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}

void unfilterImage(unsigned char far* imageDataPtr, unsigned long width, unsigned long height, unsigned char bpp) {
    unsigned long rowBytes = width * bpp;
    unsigned long rowPitch = rowBytes + 1;

    for (unsigned long lineIx = 0; lineIx < height; ++lineIx) {
        LineFilter filter = (LineFilter)(*imageDataPtr++);
        unsigned char far* row = imageDataPtr;
        unsigned long bytes = rowBytes;

        if (filter == LFSub) {
            for (unsigned long i = bpp; i < rowBytes; i++) {
                row[i] += row[i - bpp];
            }
        } else if (filter == LFUp && lineIx > 0) {
            for (unsigned long i = 0; i < rowBytes; i++) {
                row[i] += row[i - rowPitch];
            }
        } else if (filter == LFAverage) {
            if (lineIx == 0) {
                for (unsigned long i = bpp; i < rowBytes; i++) {
                    row[i] += row[i - bpp] >> 1;
                }
            } else {
                for (unsigned long i = 0; i < rowBytes; i++) {
                    unsigned char left = (i >= bpp) ? row[i - bpp] : 0;
                    unsigned char above = row[i - rowPitch];
                    row[i] += (left + above) >> 1;
                }
            }
        } else if (filter == LFPaeth) {
            if (lineIx == 0) {
                for (unsigned long i = bpp; i < rowBytes; i++) {
                    row[i] += row[i - bpp];
                }
            } else {
                for (unsigned long i = 0; i < rowBytes; i++) {
                    int a = (i >= bpp) ? row[i - bpp] : 0;
                    int b = row[i - rowPitch];
                    int c = (i >= bpp) ? row[i - bpp - rowPitch] : 0;
                    int p = a + b - c;
                    int pa = abs(p - a);
                    int pb = abs(p - b);
                    int pc = abs(p - c);
                    if (pa <= pb && pa <= pc)
                        row[i] += a;
                    else if (pb <= pc)
                        row[i] += b;
                    else
                        row[i] += c;
                }
            }
        }
        imageDataPtr += rowBytes;
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("No filename specified.\n");
        return 1;
    }

    unsigned long fileSize;
    unsigned char far* fileData = loadFile(argv[1], &fileSize);
    if (!fileData) {
        printf("Error loading file.\n");
        return 1;
    }

    unsigned long offset = 8;  // Skip PNG signature
    unsigned long width = 0, height = 0, bpp = 0;
    unsigned char far* compressedData = NULL;
    unsigned long compressedSize = 0;

    while (offset < fileSize) {
        ChunkHeader chdr;
        memcpy(&chdr, fileData + offset, sizeof(ChunkHeader));
        chdr.dataSize = ntohl(chdr.dataSize);
        offset += sizeof(ChunkHeader);

        if (chdr.chunkType == HEADER_CHUNK) {
            IHDRChunk ihdr;
            memcpy(&ihdr, fileData + offset, sizeof(IHDRChunk));
            width = ntohl(ihdr.Width);
            height = ntohl(ihdr.Height);
            assert(ihdr.BitDepth == 8);
            assert(ihdr.Compression == 0 && ihdr.Filter == 0 && ihdr.Interlace == 0);

            switch (ihdr.ColorType) {
                case 2: bpp = 3; break;  // RGB
                case 4: bpp = 2; break;  // Grayscale + Alpha
                case 6: bpp = 4; break;  // RGBA
                default: bpp = 1; break; // Grayscale
            }

            compressedData = (unsigned char far*)farmalloc(width * height);  // Memory allocation for compressed data
            if (!compressedData) {
                printf("Memory allocation failed.\n");
                farfree(fileData);
                return 1;
            }
        } else if (chdr.chunkType == IMAGE_DATA_CHUNK) {
            if (compressedSize + chdr.dataSize > width * height) {
                printf("Image data too large.\n");
                farfree(compressedData);
                farfree(fileData);
                return 1;
            }
            memcpy(compressedData + compressedSize, fileData + offset, chdr.dataSize);
            compressedSize += chdr.dataSize;
        } else if (chdr.chunkType == END_CHUNK) {
            break;  // Finished processing PNG file
        }

        offset += chdr.dataSize + 4;  // Move to next chunk, skip CRC
    }

    unsigned char far* imageData = (unsigned char far*)farmalloc((width * bpp + 1) * height);
    if (!imageData) {
        printf("Memory allocation failed.\n");
        farfree(compressedData);
        farfree(fileData);
        return 1;
    }

    if (inflateBuffer(compressedData, compressedSize, imageData, (width * bpp + 1) * height) != Z_OK) {
        printf("Error decompressing image data.\n");
        farfree(imageData);
        farfree(compressedData);
        farfree(fileData);
        return 1;
    }

    unfilterImage(imageData, width, height, bpp);

    FILE *out = fopen("out.raw", "wb");
    if (!out) {
        printf("Error opening output file.\n");
        farfree(imageData);
        farfree(compressedData);
        farfree(fileData);
        return 1;
    }
    fwrite(imageData, 1, (width * bpp + 1) * height, out);
    fclose(out);

    farfree(imageData);
    farfree(compressedData);
    farfree(fileData);
    return 0;
}
