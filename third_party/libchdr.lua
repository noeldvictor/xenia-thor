group("third_party")
project("libchdr")
  uuid("eeab1aa7-d5fa-4c3a-a1b2-8bcef8f1ab36")
  kind("StaticLib")
  language("C")

  -- libchdr reads CHD (MAME compressed hunks of data) images: the whole library
  -- and its bundled decoders (LZMA, miniz zlib, zstd, dr_flac) in one unity
  -- translation unit, as its own unity.c does.
  defines({
    "_LIB",
  })
  includedirs({
    "libchdr/include",
    "libchdr/deps/lzma-26.02/include",
    "libchdr/deps/miniz-3.1.2",
    "libchdr/deps/zstd-1.5.7",
  })
  files({
    "libchdr/unity.c",
    -- Referenced by libchdr_chd.c, not in unity.c.
    "libchdr/src/libchdr_codec_avhuff.c",
  })
  warnings("Off")
