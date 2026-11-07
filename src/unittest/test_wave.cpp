#include <algorithm>
#include <vector>
#include <stdint.h>
#include <cppunit/extensions/HelperMacros.h>
#include "../wave.h"
#include "../riff.h"
#include "../song.h"
#include "../mml_input.h"
#include "../platform/mdsdrv.h"
#include "../util.h"

class Wave_Bank_Test : public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE(Wave_Bank_Test);
	CPPUNIT_TEST(test_offset_sample_first);
	CPPUNIT_TEST(test_offset_sample_after_full_sample);
	CPPUNIT_TEST(test_offset_sample_added_again);
	CPPUNIT_TEST(test_offset_sample_loop_points);
	CPPUNIT_TEST(test_linker_offset_samples);
	CPPUNIT_TEST(test_rom_data_end);
	CPPUNIT_TEST_SUITE_END();
private:
	static std::vector<uint8_t> make_data(uint8_t first, unsigned int size)
	{
		std::vector<uint8_t> data;
		for(unsigned int i = 0; i < size; i++)
			data.push_back(first + i);
		return data;
	}

	// Header as add_sample(const Tag&) creates it for "offset=<offset>".
	static Wave_Bank::Sample make_header(uint32_t size, uint32_t offset,
		uint32_t loop_start = 0, uint32_t loop_end = 0)
	{
		return {0, offset, size - offset, loop_start, loop_end, 17500, 0, 0};
	}

	// Bytes the drivers play: size bytes from position + start.
	static std::vector<uint8_t> played(Wave_Bank& bank, unsigned int id)
	{
		Wave_Bank::Sample header = bank.get_sample_headers().at(id);
		auto begin = bank.get_rom_data().begin() + header.position + header.start;
		return std::vector<uint8_t>(begin, begin + header.size);
	}

	static uint32_t used_bytes(Wave_Bank& bank)
	{
		return bank.get_rom_data().size();
	}

	static Wave_Bank::Encoded_Sample make_sample(const std::vector<uint8_t>& data)
	{
		return {make_header(data.size(), 0), data};
	}

public:
	void test_offset_sample_first()
	{
		Wave_Bank bank(0x100);
		auto data = make_data(0x10, 16);
		unsigned int id = bank.add_sample(make_header(16, 4), data);
		bank.add_sample(make_header(16, 0), make_data(0xa0, 16));

		CPPUNIT_ASSERT_EQUAL((uint32_t)12, bank.get_sample_headers().at(id).size);
		CPPUNIT_ASSERT(played(bank, id) == std::vector<uint8_t>(data.begin() + 4, data.end()));
	}

	void test_offset_sample_after_full_sample()
	{
		Wave_Bank bank(0x100);
		auto data = make_data(0x10, 16);
		unsigned int full_id = bank.add_sample(make_header(16, 0), data);
		unsigned int id = bank.add_sample(make_header(16, 4), data);

		Wave_Bank::Sample full = bank.get_sample_headers().at(full_id);
		Wave_Bank::Sample header = bank.get_sample_headers().at(id);
		CPPUNIT_ASSERT(full_id != id);
		CPPUNIT_ASSERT_EQUAL(full.position, header.position);
		CPPUNIT_ASSERT_EQUAL((uint32_t)4, header.start);
		CPPUNIT_ASSERT_EQUAL((uint32_t)16, used_bytes(bank));
		CPPUNIT_ASSERT(played(bank, id) == std::vector<uint8_t>(data.begin() + 4, data.end()));
		CPPUNIT_ASSERT(played(bank, full_id) == data);
	}

	void test_offset_sample_added_again()
	{
		Wave_Bank bank(0x100);
		auto data = make_data(0x10, 16);
		unsigned int id = bank.add_sample(make_header(16, 4), data);
		uint32_t used = used_bytes(bank);

		CPPUNIT_ASSERT_EQUAL(id, bank.add_sample(make_header(16, 4), data));
		CPPUNIT_ASSERT_EQUAL(used, used_bytes(bank));
	}

	void test_offset_sample_loop_points()
	{
		auto data = make_data(0x10, 16);
		{
			Wave_Bank bank(0x100);
			unsigned int id = bank.add_sample(make_header(16, 4, 6, 16), data);
			Wave_Bank::Sample header = bank.get_sample_headers().at(id);
			CPPUNIT_ASSERT_EQUAL((uint8_t)0x16, bank.get_rom_data().at(header.position + header.loop_start));
			CPPUNIT_ASSERT_EQUAL(header.start + header.size, header.loop_end);
		}
		{
			// Offset past the loop start: the loop starts where playback does.
			Wave_Bank bank(0x100);
			unsigned int id = bank.add_sample(make_header(16, 8, 6, 16), data);
			Wave_Bank::Sample header = bank.get_sample_headers().at(id);
			CPPUNIT_ASSERT_EQUAL(header.start, header.loop_start);
			CPPUNIT_ASSERT_EQUAL(header.start + header.size, header.loop_end);
		}
		{
			Wave_Bank bank(0x100);
			bank.add_sample(make_header(16, 0, 6, 16), data);
			unsigned int id = bank.add_sample(make_header(16, 4, 6, 16), data);
			Wave_Bank::Sample header = bank.get_sample_headers().at(id);
			CPPUNIT_ASSERT_EQUAL((uint8_t)0x16, bank.get_rom_data().at(header.position + header.loop_start));
			CPPUNIT_ASSERT_EQUAL(header.start + header.size, header.loop_end);
		}
	}

	// The ROM data ends with the last sample, and reads as zero after it.
	void test_rom_data_end()
	{
		Wave_Bank bank(0x100);
		auto data = make_data(0x10, 8);
		unsigned int id = bank.add_sample(make_header(8, 0), data);
		CPPUNIT_ASSERT_EQUAL((size_t)8, bank.get_rom_data().size());
		CPPUNIT_ASSERT_EQUAL(0x100u, bank.get_max_size());

		auto padded = data;
		padded.resize(12, 0);
		unsigned int padded_id = bank.add_sample(make_header(12, 0), padded);
		CPPUNIT_ASSERT(id != padded_id);
		CPPUNIT_ASSERT_EQUAL(bank.get_sample_headers().at(id).position, bank.get_sample_headers().at(padded_id).position);
		CPPUNIT_ASSERT_EQUAL((size_t)8, bank.get_rom_data().size());

		bank.add_sample(make_header(4, 0), make_data(0xa0, 4));
		CPPUNIT_ASSERT_EQUAL((size_t)12, bank.get_rom_data().size());
	}

	// Samples linked from an .MDS play the same bytes as in the song.
	// @1 is unused, so @2 is the only header pointing at its data.
	void test_linker_offset_samples()
	{
		auto a = make_data(0x10, 16);
		auto b = make_data(0xa0, 16);
		Song song;
		song.get_sample_map()["a.wav"] = make_sample(a);
		song.get_sample_map()["b.wav"] = make_sample(b);
		MML_Input input(&song);
		input.read_line("@1 pcm \"a.wav\"");
		input.read_line("@2 pcm \"a.wav\" offset=4");
		input.read_line("@3 pcm \"b.wav\" offset=4");
		input.read_line("F @2 c4 @3 c4");

		MDSDRV_Converter converter(song);
		RIFF mds = converter.get_mds();
		MDSDRV_Linker linker;
		linker.add_song(mds);
		auto seq = linker.get_seq_data();
		auto pcm = linker.get_pcm_data();

		// Wave table: count, then offsets of 8-byte headers (address, size).
		uint32_t table = 8 + read_be32(seq, 8);
		unsigned int count = (seq.at(table) << 8) | seq.at(table + 1);
		std::vector<std::vector<uint8_t>> streams;
		for(unsigned int i = 0; i < count; i++)
		{
			uint32_t header = 8 + ((seq.at(table + 2 + i * 2) << 8) | seq.at(table + 3 + i * 2));
			uint32_t address = read_be32(seq, header) & 0xffffff;
			uint32_t size = read_be32(seq, header + 4);
			CPPUNIT_ASSERT(address + size <= pcm.size());
			streams.emplace_back(pcm.begin() + address, pcm.begin() + address + size);
		}
		std::vector<std::vector<uint8_t>> expected = {
			std::vector<uint8_t>(a.begin() + 4, a.end()),
			std::vector<uint8_t>(b.begin() + 4, b.end())};
		std::sort(streams.begin(), streams.end());
		std::sort(expected.begin(), expected.end());
		CPPUNIT_ASSERT(streams == expected);
	}
};

CPPUNIT_TEST_SUITE_REGISTRATION(Wave_Bank_Test);
