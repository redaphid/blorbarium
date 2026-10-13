"""NVS images that ESP-IDF refuses to open, made from the generator's own output.

    nvs_variants.py newer <in> <out>   every written page claims a newer NVS format
                                       (version byte below NVS_VERSION 0xfe):
                                       nvs_flash_init gives ESP_ERR_NVS_NEW_VERSION_FOUND
    nvs_variants.py full <in> <out>    every blank page gets a valid FULL header, so
                                       none is free: ESP_ERR_NVS_NO_FREE_PAGES

A page header is state u32, seq u32, version u8, 19 reserved, crc32 u32 over
bytes 4..28, the CRC being zlib's with 0xFFFFFFFF as the start (the generator's).
"""
import struct
import sys
import zlib

PAGE = 4096
ACTIVE, FULL, BLANK = b"\xfe\xff\xff\xff", b"\xfc\xff\xff\xff", b"\xff" * 4


def seal(page: bytearray) -> None:
    page[28:32] = struct.pack("<I", zlib.crc32(bytes(page[4:28]), 0xFFFFFFFF))


def main() -> None:
    how, src, dst = sys.argv[1:4]
    data = bytearray(open(src, "rb").read())
    pages = [memoryview(data)[p:p + PAGE] for p in range(0, len(data), PAGE)]
    seq = max(struct.unpack("<I", p[4:8])[0] for p in pages if p[:4] != BLANK) + 1
    for p in pages:
        h = bytearray(p[:32])
        if how == "newer" and h[:4] != BLANK:
            h[8] = 0xFD
        elif how == "full" and h[:4] == BLANK:
            h[:4], h[4:8], h[8] = FULL, struct.pack("<I", seq), 0xFE
            seq += 1
        elif how == "full" and h[:4] == ACTIVE:
            h[:4] = FULL
        else:
            continue
        seal(h)
        p[:32] = h
    open(dst, "wb").write(data)


if __name__ == "__main__":
    main()
