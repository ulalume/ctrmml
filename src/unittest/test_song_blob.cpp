#include <stdexcept>
#include <cppunit/extensions/HelperMacros.h>
#include "../song_blob.h"
#include "../mml_input.h"
#include "../input.h"
#include "../song.h"
#include "../track.h"
#include "../wave.h"

class Song_Blob_Test : public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE(Song_Blob_Test);
	CPPUNIT_TEST(test_tracks);
	CPPUNIT_TEST(test_tags);
	CPPUNIT_TEST(test_platform);
	CPPUNIT_TEST(test_samples);
	CPPUNIT_TEST(test_reserialize);
	CPPUNIT_TEST(test_vgm_output);
	CPPUNIT_TEST(test_missing_sample);
	CPPUNIT_TEST(test_locations);
	CPPUNIT_TEST(test_invalid_data);
	CPPUNIT_TEST_SUITE_END();
private:
	Song *song;
	MML_Input *mml_input;
	Song *copy;

	void round_trip()
	{
		std::vector<uint8_t> blob = serialize_song(*song);
		deserialize_song(blob.data(), blob.size(), *copy);
	}

	static Wave_Bank::Encoded_Sample read_sample(const std::string& filename)
	{
		Wave_Bank bank(0);
		bank.set_include_paths({"sample/"});
		Wave_Bank::Encoded_Sample buffer;
		return bank.get_encoded_sample(filename, buffer);
	}

	// VGM output without the GD3 tag, which holds metadata that is not serialized.
	static std::vector<uint8_t> vgm_without_tags(Song& song)
	{
		std::vector<uint8_t> vgm = song.get_platform()->get_export_data(song, 0);
		uint32_t gd3 = vgm[0x14] | (vgm[0x15] << 8) | (vgm[0x16] << 16) | (vgm[0x17] << 24);
		if(gd3)
			vgm.resize(0x14 + gd3);
		for(int i = 0; i < 4; i++)
			vgm[0x04 + i] = vgm[0x14 + i] = 0;
		return vgm;
	}

public:
	void setUp()
	{
		song = new Song();
		mml_input = new MML_Input(song);
		copy = new Song();
	}
	void tearDown()
	{
		delete copy;
		delete mml_input;
		delete song;
	}
	void test_tracks()
	{
		mml_input->read_line("A t140 o4 l8 [c d]3 L e4^8 r16 k-5 'mode 1' D30 c");
		mml_input->read_line("*30 o3c");
		round_trip();
		CPPUNIT_ASSERT_EQUAL((size_t)2, copy->get_track_map().size());
		for(auto&& track : song->get_track_map())
		{
			const std::vector<Event>& expected = track.second.get_events();
			const std::vector<Event>& actual = copy->get_track(track.first).get_events();
			CPPUNIT_ASSERT_EQUAL(expected.size(), actual.size());
			for(unsigned int i = 0; i < expected.size(); i++)
			{
				CPPUNIT_ASSERT_EQUAL(expected[i].type, actual[i].type);
				CPPUNIT_ASSERT_EQUAL(expected[i].param, actual[i].param);
				CPPUNIT_ASSERT_EQUAL(expected[i].on_time, actual[i].on_time);
				CPPUNIT_ASSERT_EQUAL(expected[i].off_time, actual[i].off_time);
				CPPUNIT_ASSERT(actual[i].reference == nullptr);
			}
		}
		CPPUNIT_ASSERT_EQUAL(song->get_ppqn(), copy->get_ppqn());
	}
	void test_tags()
	{
		mml_input->read_line("#title Song title");
		mml_input->read_line("#option noextpitch");
		mml_input->read_line("@1 psg 15 /");
		mml_input->read_line("@M2 0 1");
		mml_input->read_line("A @1 'lfo 1 2' c");
		song->add_tag("include_path", "somewhere/");
		round_trip();
		CPPUNIT_ASSERT(!copy->check_tag("#title"));
		CPPUNIT_ASSERT(copy->get_tag("#option") == song->get_tag("#option"));
		CPPUNIT_ASSERT(copy->get_tag("@1") == song->get_tag("@1"));
		CPPUNIT_ASSERT(copy->get_tag("@m2") == song->get_tag("@m2"));
		CPPUNIT_ASSERT(copy->get_tag("cmd_-32768") == song->get_tag("cmd_-32768"));
		CPPUNIT_ASSERT(copy->get_tag_order_list() == Tag({"#option", "@1", "@m2", "cmd_-32768"}));
		CPPUNIT_ASSERT_EQUAL((size_t)0, copy->get_tag("include_path").size());
	}
	void test_platform()
	{
		mml_input->read_line("#platform mdsdrv");
		round_trip();
		CPPUNIT_ASSERT_EQUAL(std::string("mdsdrv"), copy->get_platform_key());
		song->set_platform("megadrive");
		round_trip();
		CPPUNIT_ASSERT_EQUAL(std::string("megadrive"), copy->get_platform_key());
	}
	void test_samples()
	{
		mml_input->open_file("sample/idk.mml");
		round_trip();
		const Wave_Bank::Encoded_Sample_Map& samples = copy->get_sample_map();
		CPPUNIT_ASSERT_EQUAL((size_t)4, samples.size());
		for(auto&& name : {"pcm/bd_17k5.wav", "pcm/tom2l_17k5.wav"})
		{
			Wave_Bank::Encoded_Sample expected = read_sample(name);
			const Wave_Bank::Encoded_Sample& actual = samples.at(name);
			CPPUNIT_ASSERT(expected.header.to_bytes() == actual.header.to_bytes());
			CPPUNIT_ASSERT(expected.data == actual.data);
		}
	}
	void test_reserialize()
	{
		mml_input->open_file("sample/idk.mml");
		round_trip();
		std::vector<uint8_t> blob = serialize_song(*copy);
		CPPUNIT_ASSERT(serialize_song(*song) == blob);
	}
	void test_vgm_output()
	{
		mml_input->open_file("sample/idk.mml");
		round_trip();
		CPPUNIT_ASSERT(vgm_without_tags(*song) == vgm_without_tags(*copy));
	}
	void test_missing_sample()
	{
		mml_input->read_line("@30 pcm \"pcm/missing.wav\"");
		mml_input->read_line("A @30 c");
		song->add_tag("include_path", "sample/");
		round_trip();
		CPPUNIT_ASSERT_EQUAL((size_t)0, copy->get_sample_map().size());
		CPPUNIT_ASSERT_THROW(copy->get_platform()->get_export_data(*copy, 0), InputError);
	}
	void test_locations()
	{
		mml_input->open_file("sample/idk.mml");
		round_trip();
		for(auto&& track : copy->get_track_map())
			for(auto&& event : track.second.get_events())
				CPPUNIT_ASSERT(event.reference == nullptr);

		std::vector<uint8_t> blob = serialize_song(*song, true);
		deserialize_song(blob.data(), blob.size(), *copy);
		const std::string* filename = nullptr;
		for(auto&& track : song->get_track_map())
		{
			const std::vector<Event>& expected = track.second.get_events();
			const std::vector<Event>& actual = copy->get_track(track.first).get_events();
			CPPUNIT_ASSERT_EQUAL(expected.size(), actual.size());
			for(unsigned int i = 0; i < expected.size(); i++)
			{
				if(!expected[i].reference)
				{
					CPPUNIT_ASSERT(actual[i].reference == nullptr);
					continue;
				}
				CPPUNIT_ASSERT(actual[i].reference != nullptr);
				CPPUNIT_ASSERT_EQUAL(expected[i].reference->get_filename(), actual[i].reference->get_filename());
				CPPUNIT_ASSERT_EQUAL(expected[i].reference->get_line(), actual[i].reference->get_line());
				CPPUNIT_ASSERT_EQUAL(expected[i].reference->get_column(), actual[i].reference->get_column());
				// One file name for every reference
				if(filename)
					CPPUNIT_ASSERT_EQUAL(filename, &actual[i].reference->get_filename());
				filename = &actual[i].reference->get_filename();
			}
		}
		CPPUNIT_ASSERT(filename != nullptr);
		CPPUNIT_ASSERT(serialize_song(*copy, true) == blob);
		CPPUNIT_ASSERT_THROW(deserialize_song(blob.data(), blob.size() - 1, *copy), std::invalid_argument);
	}
	void test_invalid_data()
	{
		mml_input->read_line("A c");
		std::vector<uint8_t> blob = serialize_song(*song);
		std::vector<uint8_t> bad_magic = blob;
		bad_magic[0] = 'X';
		CPPUNIT_ASSERT_THROW(deserialize_song(bad_magic.data(), bad_magic.size(), *copy), std::invalid_argument);
		CPPUNIT_ASSERT_THROW(deserialize_song(blob.data(), blob.size() - 1, *copy), std::invalid_argument);
		CPPUNIT_ASSERT_THROW(deserialize_song(blob.data(), 2, *copy), std::invalid_argument);
	}
};

CPPUNIT_TEST_SUITE_REGISTRATION(Song_Blob_Test);
