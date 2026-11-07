#include <vector>
#include <cppunit/extensions/HelperMacros.h>
#include "../mml_input.h"
#include "../song.h"
#include "../vgm.h"
#include "../platform/md.h"

//! Keeps the data blocks and collects the sample data of each DAC stream start.
class DAC_Recorder : public VGM_Interface
{
	public:
		void write(uint8_t command, uint16_t port, uint16_t reg, uint16_t data) override {}
		void dac_setup(uint8_t sid, uint8_t chip_id, uint32_t port, uint32_t reg, uint8_t db_id) override {}
		void dac_start(uint8_t sid, uint32_t start, uint32_t length, uint32_t freq) override
		{
			streams.emplace_back(rom.begin() + start, rom.begin() + start + length);
		}
		void dac_stop(uint8_t sid) override {}
		void poke32(uint32_t offset, uint32_t data) override {}
		void poke16(uint32_t offset, uint16_t data) override {}
		void poke8(uint32_t offset, uint8_t data) override {}
		void datablock(uint8_t dbtype, uint32_t dbsize, const uint8_t* db, uint32_t maxsize,
			uint32_t mask, uint32_t flags, uint32_t offset) override
		{
			rom.resize(maxsize);
			std::copy_n(db, dbsize, rom.begin() + offset);
		}

		std::vector<uint8_t> rom;
		std::vector<std::vector<uint8_t>> streams;
};

class MD_Driver_Test : public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE(MD_Driver_Test);
	CPPUNIT_TEST(test_relink_new_sample);
	CPPUNIT_TEST_SUITE_END();
private:
	static void read_song(Song& song, const char* sample)
	{
		MML_Input input(&song);
		song.add_tag("include_path", "sample/");
		input.read_line(std::string("@30 pcm \"") + sample + "\"");
		input.read_line("F @30 L c4");
	}

	static void play_until(MD_Driver& driver, uint32_t ticks)
	{
		while(driver.get_player_ticks() < ticks)
			driver.play_step();
	}

public:
	// After a relink to a song with a new sample, DAC streams play the new
	// sample data, as they do when the new song is started at that tick.
	void test_relink_new_sample()
	{
		Song song1, song2;
		read_song(song1, "pcm/bd_17k5.wav");
		read_song(song2, "pcm/sd_17k5.wav");

		DAC_Recorder relinked;
		MD_Driver relinked_driver(44100, &relinked);
		relinked_driver.play_song(song1);
		play_until(relinked_driver, 30);
		uint32_t relink_tick = relinked_driver.get_player_ticks();
		relinked_driver.relink_song(song2, relink_tick);
		relinked.streams.clear();
		play_until(relinked_driver, 200);

		DAC_Recorder fresh;
		MD_Driver fresh_driver(44100, &fresh);
		fresh_driver.play_song(song2);
		fresh_driver.skip_ticks(relink_tick);
		play_until(fresh_driver, 200);

		CPPUNIT_ASSERT(fresh.streams.size() > 1);
		CPPUNIT_ASSERT_EQUAL(fresh.streams.size(), relinked.streams.size());
		for(unsigned int i = 0; i < fresh.streams.size(); i++)
			CPPUNIT_ASSERT(fresh.streams[i] == relinked.streams[i]);
	}
};

CPPUNIT_TEST_SUITE_REGISTRATION(MD_Driver_Test);
