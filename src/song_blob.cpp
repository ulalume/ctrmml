/*! \file song_blob.cpp
 *  \brief Song serialization.
 *
 *  A serialized Song holds what the sound driver reads when playing it,
 *  so that it can be played without the input files.
 *
 *  All integers are little-endian. A string is a u32 length followed by
 *  that many bytes.
 *
 *      char[4]  magic "CSNG"
 *      u32      version (1)
 *      string   platform key, see Song::set_platform()
 *      u16      ppqn
 *      u32      tag count, then for each tag:
 *                   string key, u32 value count, string values...
 *      u32      track count, then for each track:
 *                   u16 id, u32 event count, then for each event:
 *                       u8 type, s16 param, u16 on_time, u16 off_time
 *      u32      file count, then for each file:
 *                   string name, u32 size, data
 *
 *  Tags are limited to those read by the driver: instruments and envelopes
 *  ('@' prefix), platform commands ('cmd_' prefix), "#option" and the tag
 *  order list. The include paths are replaced by the files that the PCM
 *  instruments read, so a deserialized song does not read any files.
 *  Input references are not stored.
 */
#include <climits>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include "song_blob.h"
#include "song.h"
#include "track.h"
#include "wave.h"
#include "stringf.h"

namespace
{

const char magic[4] = {'C', 'S', 'N', 'G'};
const uint32_t version = 1;

bool is_driver_tag(const std::string& key)
{
	return key[0] == '@'
		|| key.compare(0, 4, "cmd_") == 0
		|| key == "#option";
}

//! Add the sample files read by the PCM instruments of a song.
/*!
 *  The files are found the same way as MDSDRV_Data::read_song() does.
 *  Files that cannot be found are left out.
 */
void add_sample_files(const Song& song, File_Map& files)
{
	const Tag_Map& tags = song.get_tag_map();
	Tag include_paths = {""};
	if(tags.count("include_path"))
		include_paths = tags.at("include_path");
	if(!tags.count("tag_order"))
		return;
	for(auto&& key : tags.at("tag_order"))
	{
		uint16_t id;
		if(std::sscanf(key.c_str(), "@%hu", &id) != 1 || !tags.count(key))
			continue;
		const Tag& tag = tags.at(key);
		if(tag.size() < 2 || !iequal(tag[0], "pcm") || files.count(tag[1]))
			continue;
		Wave_File wave;
		std::vector<uint8_t> contents;
		if(Wave_Bank::read_sample_file(tag[1], &song.get_file_map(), include_paths, wave, &contents) == 0)
			files[tag[1]] = std::move(contents);
	}
}

class Writer
{
	public:
		void u8(uint8_t value)
		{
			out.push_back(value);
		}
		void u16(uint16_t value)
		{
			out.push_back(value);
			out.push_back(value >> 8);
		}
		void u32(uint32_t value)
		{
			u16(value);
			u16(value >> 16);
		}
		void bytes(const uint8_t* data, size_t size)
		{
			out.insert(out.end(), data, data + size);
		}
		void str(const std::string& value)
		{
			u32(value.size());
			bytes((const uint8_t*)value.data(), value.size());
		}

		std::vector<uint8_t> out;
};

class Reader
{
	public:
		Reader(const uint8_t* data, size_t size)
			: pos(data)
			, end(data + size)
		{
		}
		uint8_t u8()
		{
			return *take(1);
		}
		uint16_t u16()
		{
			const uint8_t* p = take(2);
			return p[0] | (p[1] << 8);
		}
		uint32_t u32()
		{
			const uint8_t* p = take(4);
			return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
		}
		const uint8_t* bytes(size_t size)
		{
			return take(size);
		}
		std::string str()
		{
			uint32_t size = u32();
			return std::string((const char*)take(size), size);
		}
		//! Read an element count, each element using at least \p min_size bytes.
		uint32_t count(size_t min_size)
		{
			uint32_t value = u32();
			if(value > (size_t)(end - pos) / min_size)
				throw std::invalid_argument("song data is truncated");
			return value;
		}
		bool at_end() const
		{
			return pos == end;
		}

	private:
		const uint8_t* take(size_t size)
		{
			if(size > (size_t)(end - pos))
				throw std::invalid_argument("song data is truncated");
			const uint8_t* p = pos;
			pos += size;
			return p;
		}

		const uint8_t* pos;
		const uint8_t* end;
};

} // namespace

//! Serialize a Song.
/*!
 *  Sample files of PCM instruments are read from the song's file map or
 *  include paths and stored with the song.
 */
std::vector<uint8_t> serialize_song(const Song& song)
{
	Writer w;
	w.bytes((const uint8_t*)magic, 4);
	w.u32(version);
	w.str(song.get_platform_key());
	w.u16(song.get_ppqn());

	const Tag_Map& tags = song.get_tag_map();
	Tag tag_order;
	if(tags.count("tag_order"))
	{
		for(auto&& key : tags.at("tag_order"))
			if(is_driver_tag(key) && tags.count(key))
				tag_order.push_back(key);
	}
	uint32_t tag_count = 1;
	for(auto&& tag : tags)
		if(is_driver_tag(tag.first))
			tag_count++;
	w.u32(tag_count);
	w.str("tag_order");
	w.u32(tag_order.size());
	for(auto&& value : tag_order)
		w.str(value);
	for(auto&& tag : tags)
	{
		if(!is_driver_tag(tag.first))
			continue;
		w.str(tag.first);
		w.u32(tag.second.size());
		for(auto&& value : tag.second)
			w.str(value);
	}

	const Track_Map& tracks = song.get_track_map();
	w.u32(tracks.size());
	for(auto&& track : tracks)
	{
		const std::vector<Event>& events = track.second.get_events();
		w.u16(track.first);
		w.u32(events.size());
		for(auto&& event : events)
		{
			w.u8(event.type);
			w.u16(event.param);
			w.u16(event.on_time);
			w.u16(event.off_time);
		}
	}

	File_Map files;
	add_sample_files(song, files);
	w.u32(files.size());
	for(auto&& file : files)
	{
		w.str(file.first);
		w.u32(file.second.size());
		w.bytes(file.second.data(), file.second.size());
	}
	return w.out;
}

//! Deserialize a Song.
/*!
 *  Replaces the tags, tracks and files of \p song. The song gets an empty
 *  include path list, so samples are only read from its file map.
 *
 *  \exception std::invalid_argument if the data is not a valid serialized song.
 */
void deserialize_song(const uint8_t* data, size_t size, Song& song)
{
	Reader r(data, size);
	if(size < 4 || std::memcmp(r.bytes(4), magic, 4))
		throw std::invalid_argument("not a serialized song");
	if(r.u32() != version)
		throw std::invalid_argument("unsupported serialized song version");
	song.set_platform(r.str());
	song.set_ppqn(r.u16());

	Tag_Map& tags = song.get_tag_map();
	tags.clear();
	uint32_t tag_count = r.count(8);
	for(uint32_t i = 0; i < tag_count; i++)
	{
		std::string key = r.str();
		Tag& tag = tags[key];
		uint32_t value_count = r.count(4);
		tag.reserve(value_count);
		for(uint32_t j = 0; j < value_count; j++)
			tag.push_back(r.str());
	}
	tags["include_path"].clear();

	Track_Map& tracks = song.get_track_map();
	tracks.clear();
	uint32_t track_count = r.count(6);
	for(uint32_t i = 0; i < track_count; i++)
	{
		uint16_t id = r.u16();
		std::vector<Event>& events = song.make_track(id).get_events();
		uint32_t event_count = r.count(7);
		events.reserve(event_count);
		for(uint32_t j = 0; j < event_count; j++)
		{
			uint8_t type = r.u8();
			if(type >= Event::CMD_COUNT)
				throw std::invalid_argument("invalid event type in serialized song");
			int16_t param = r.u16();
			uint16_t on_time = r.u16();
			uint16_t off_time = r.u16();
			events.push_back({(Event::Type)type, param, on_time, off_time, UINT_MAX, nullptr});
		}
	}

	File_Map& files = song.get_file_map();
	files.clear();
	uint32_t file_count = r.count(8);
	for(uint32_t i = 0; i < file_count; i++)
	{
		std::string name = r.str();
		uint32_t file_size = r.u32();
		const uint8_t* file_data = r.bytes(file_size);
		files[name].assign(file_data, file_data + file_size);
	}
	if(!r.at_end())
		throw std::invalid_argument("unexpected data after serialized song");
}
