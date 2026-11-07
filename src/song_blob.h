//! \file song_blob.h
//! Song serialization.
#ifndef SONG_BLOB_H
#define SONG_BLOB_H
#include <cstddef>
#include "core.h"

std::vector<uint8_t> serialize_song(const Song& song);
void deserialize_song(const uint8_t* data, size_t size, Song& song);

#endif
